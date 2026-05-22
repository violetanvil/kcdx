// kcdx::lua_plugin_loader — see header for the contract.

#include "lua_plugin_loader.h"

#include <atomic>
#include <filesystem>
#include <string>

extern "C" {
#include "lauxlib.h"
#include "lua.h"   // lua_storedebuginfo / lua_isstoredebuginfo (line-info toggle)
}

#include "crash_guard.h"
#include "load_order.h"
#include "log.h"
#include "lua_registry.h"
#include "plugin_loader.h"
#include "test.h"  // kcdx::test::ReportResult (dev-mode-gated, name-keyed)

namespace fs = std::filesystem;

namespace kcdx::lua_plugin_loader {

namespace {

// Context for one guarded file-load. The guard runs LoadOneFileGuarded
// which loads + executes the chunk; status/err flow back out for the
// caller to log outside the guard.
struct LoadCtx {
    lua_State*  L          = nullptr;
    const char* pluginName = nullptr;
    const char* absPath    = nullptr;   // UTF-8 absolute path for luaL_loadfile
    int         status     = -1;        // luaL_loadfile / lua_pcall status
    std::string err;                    // error message captured inside the guard
    bool        ran        = false;     // true if we reached lua_pcall
};

// Message handler installed as the lua_pcall errfunc so a runtime error
// carries the CALL CHAIN, not just the faulting line. Runs inside the
// VM at the moment of the error, while the failing stack is still live —
// the only point a traceback can be captured.
//
// Stock Lua 5.1 has no luaL_traceback (verified against vendor/lua —
// it first appears in 5.2), so we use the canonical 5.1 idiom: fetch the
// global debug.traceback and let it append the stack to the message.
// We do NOT reference db_errorfb directly (it's static in ldblib.c). If
// debug.traceback is unavailable (debug lib stripped / nilled by a
// plugin), we leave the original message untouched — the file:line+detail
// from piece 1 still survives. This is a plain lua_CFunction pushed via
// lua_pushcfunction (raw C API), so it introduces no kcdx-side
// static-const Lua sentinel (lua-bridge.md / AP5).
int TracebackHandler(lua_State* L) {
    // Stack: [errmsg] (the value the chunk error'd with) at index 1.
    // debug.traceback handles a non-string message itself (it only prepends
    // a newline when the message IS a string), so no pre-coercion is needed.
    lua_getfield(L, LUA_GLOBALSINDEX, "debug");  // push _G.debug (or nil)
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);     // drop the non-table
        return 1;          // leave original errmsg at index 1 as the result
    }
    lua_getfield(L, -1, "traceback");            // push debug.traceback
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);     // drop traceback + debug
        return 1;          // leave original errmsg
    }
    lua_remove(L, -2);     // drop the debug table; stack: [errmsg][traceback]
    lua_pushvalue(L, 1);   // arg 1: the error message
    lua_pushinteger(L, 1); // arg 2: skip THIS handler frame in the trace
    lua_call(L, 2, 1);     // debug.traceback(msg, 1) -> msg .. "\nstack traceback:..."
    return 1;              // single result: message + traceback
}

// Guarded body: luaL_loadfile + lua_pcall. Any Lua error is captured
// into ctx->err; any hard fault is trapped by the surrounding
// guard::Call. Leaves the Lua stack balanced (pops the loaded chunk or
// the error message).
void LoadOneFileGuarded(void* userdata) {
    auto* ctx = static_cast<LoadCtx*>(userdata);
    lua_State* L = ctx->L;

    const int top = lua_gettop(L);

    // Toggle Lua debug-info storage ON across this plugin.lua load so an
    // author's error carries file:line + detail. The engine's
    // CScriptSystem::Init clears G(L)->storedebug (a CryEngine memory-save);
    // with it off, luaG_runerror drops the line/detail and emits only the
    // bare "plugin.lua:0: [Error] Lua error..." for any plugin.lua error.
    // Line info is baked at PARSE time, so this must be set BEFORE
    // luaL_loadfile, not merely before the pcall. We capture the engine's
    // prior value and restore it on every exit path so steady-state stays
    // exactly as the engine left it (a future plugin load must never inherit
    // storedebug=on — that would defeat the memory-save the engine intends).
    // See docs/known-issues/plugin-lua-errors-have-no-line-number.md.
    const int storedebugWas = lua_isstoredebuginfo(L);
    lua_storedebuginfo(L, 1);  // set BEFORE loadfile (parse-time line info)

    // luaL_loadfile pushes the compiled chunk as a function, or an
    // error string on failure. A load error is a COMPILE error — there is
    // no runtime stack to trace, so no errfunc/traceback applies here; the
    // file:line+detail (piece 1) is the whole story. Leave this path as-is.
    ctx->status = luaL_loadfile(L, ctx->absPath);
    if (ctx->status != 0) {
        const char* msg = lua_tostring(L, -1);
        ctx->err = msg ? msg : "<load error: no message>";
        lua_storedebuginfo(L, storedebugWas);  // restore engine steady-state
        lua_settop(L, top);
        return;
    }

    // Install the traceback handler as the pcall errfunc so a RUNTIME
    // error carries the call chain. It must sit BELOW the chunk function
    // on the stack: push it first (index errfuncIdx), then move the chunk
    // back above it. lua_pcall pops the chunk + its args on error but
    // leaves the errfunc in place, so we pop it ourselves afterwards.
    // Stack on entry here: [chunk]  (chunk at top == top+1).
    lua_pushcfunction(L, &TracebackHandler);  // [chunk][errfunc]
    const int errfuncIdx = top + 1;           // 1-based index of the errfunc
    lua_insert(L, errfuncIdx);                 // [errfunc][chunk]

    // Execute the chunk. plugin.lua runs as a parameterless function;
    // its kcdx.hook/.bytes/... calls queue intent into lua_registry.
    ctx->ran = true;
    ctx->status = lua_pcall(L, 0, 0, errfuncIdx);
    if (ctx->status != 0) {
        // On error the errfunc already ran; the stack top is its result
        // (message + "\nstack traceback:" + frames).
        const char* msg = lua_tostring(L, -1);
        ctx->err = msg ? msg : "<runtime error: no message>";
        lua_storedebuginfo(L, storedebugWas);  // restore engine steady-state
        lua_settop(L, top);  // also drops the errfunc still on the stack
        return;
    }

    lua_storedebuginfo(L, storedebugWas);  // restore engine steady-state
    lua_settop(L, top);
}

std::atomic<bool> g_ranOnce{false};

// True iff `s` contains a ':<one-or-more-digits>:' run anywhere — the
// file:line marker (e.g. "plugin.lua:13:") that a runtime error carries
// once storedebug is on (piece 1). Plain scan, no regex: find a ':',
// require at least one digit, require a closing ':'.
bool HasLineMarker(const std::string& s) {
    for (size_t i = 0; i + 1 < s.size(); ++i) {
        if (s[i] != ':') continue;
        size_t j = i + 1;
        while (j < s.size() && s[j] >= '0' && s[j] <= '9') ++j;
        if (j > i + 1 && j < s.size() && s[j] == ':') return true;  // ':' digits ':'
    }
    return false;
}

}  // namespace

void RunAll(lua_State* L) {
    bool expected = false;
    if (!g_ranOnce.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel)) {
        return;  // already ran this session
    }
    if (!L) {
        log::Warn("lua_plugin_loader::RunAll called with null lua_State; "
                  "no plugin.lua will execute this session");
        return;
    }

    size_t pluginsWithLua = 0, filesRun = 0, filesFailed = 0;

    for (const auto& p : kcdx::plugins::g_plugins) {
        const auto& m = p.manifest;
        if (m.luaEntrypointsRel.empty()) continue;

        // Honor the load_order.toml enabled gate — a disabled plugin's
        // Lua doesn't run, same as its DLL kcdxPlugin_Load is skipped.
        if (!kcdx::load_order::IsPluginEnabled(m.name)) {
            log::InfoF("Plugin '%s': %zu lua entrypoint(s) skipped (plugin "
                       "disabled via load_order.toml)",
                       m.name.c_str(), m.luaEntrypointsRel.size());
            continue;
        }

        ++pluginsWithLua;
        for (const std::string& rel : m.luaEntrypointsRel) {
            fs::path abs = m.folderPath / rel;
            std::error_code ec;
            if (!fs::exists(abs, ec)) {
                log::ErrorF("Plugin '%s': [entrypoints].lua '%s' not found at "
                            "%s — skipping this file",
                            m.name.c_str(), rel.c_str(),
                            abs.string().c_str());
                ++filesFailed;
                continue;
            }

            const std::string absUtf8 = abs.string();

            // Attribute kcdx.* calls from this file (and anything it
            // require()s synchronously) to this plugin BEFORE running it.
            // We register both the absolute path (what luaL_loadfile is
            // handed, and thus what debug.getinfo reports) so the lookup
            // matches.
            kcdx::lua_registry::RegisterScriptOwner(absUtf8, m.name);

            LoadCtx ctx;
            ctx.L          = L;
            ctx.pluginName = m.name.c_str();
            ctx.absPath    = absUtf8.c_str();

            log::InfoF("Plugin '%s': running lua entrypoint '%s'",
                       m.name.c_str(), rel.c_str());

            bool clean = kcdx::guard::Call("plugin.lua.run", m.name.c_str(),
                                           &LoadOneFileGuarded, &ctx);

            if (!clean) {
                // Hard fault inside the chunk — guard already logged a
                // FAULTED line with the site + plugin name.
                log::ErrorF("Plugin '%s': lua entrypoint '%s' faulted "
                            "(see GUARD line) — skipping remaining files "
                            "for this plugin",
                            m.name.c_str(), rel.c_str());
                ++filesFailed;
                break;  // don't run this plugin's later files after a fault
            }
            if (ctx.status != 0) {
                const char* kind = ctx.ran ? "runtime error" : "load error";
                // Engine log (kcdx-dev.log) — the developer / bug-report
                // channel; keep it.
                log::ErrorF("Plugin '%s': lua entrypoint '%s' %s: %s",
                            m.name.c_str(), rel.c_str(), kind,
                            ctx.err.c_str());
                // ALSO route to the offending plugin's OWN log so the author
                // sees the file:line:detail (+ traceback, for runtime errors)
                // where they naturally look — they shouldn't have to know to
                // grep the engine log. p.handle is the plugin's log stream
                // (plugin_loader.h); PluginError routes by handle (log.h).
                log::PluginError(
                    p.handle,
                    std::string("lua entrypoint '") + rel + "' " + kind +
                        ": " + ctx.err);

                // Regression assertion for AP12 #3 (plugin.lua error
                // line-info quality). PURE READ of ctx.err — it does not
                // touch status/err/control flow; the plugin loads/errors
                // exactly as it would without this block. FIXTURE-AGNOSTIC:
                // we never check the plugin's name — we report on the
                // line-info quality of WHATEVER plugin.lua runtime error we
                // just captured. Only RUNTIME errors qualify (ctx.ran):
                // they have a live stack, so the errfunc (piece 2) appended
                // a traceback. A load (compile) error has no stack/traceback,
                // so it isn't a datapoint for this assertion.
                //
                // The production-quiet backstop is ReportResult's OWN
                // dev-mode early-return (test.cpp) — NOT a loader-side
                // fixture check. So in production this read+call is a cheap
                // no-op; in dev mode every plugin.lua runtime error feeds
                // cap-23. The deliberate-error fixture (cap-23-lua-error)
                // reliably triggers it each boot; a real plugin erroring is
                // also a valid datapoint (it too should carry line info).
                if (ctx.ran) {
                    const bool pass =
                        HasLineMarker(ctx.err) &&
                        ctx.err.find("stack traceback:") != std::string::npos;
                    kcdx::test::ReportResult("cap-23-lua-error-lineinfo", pass,
                                             ctx.err);
                }

                ++filesFailed;
                continue;
            }

            ++filesRun;
            log::InfoF("Plugin '%s': lua entrypoint '%s' OK",
                       m.name.c_str(), rel.c_str());
        }
    }

    if (pluginsWithLua > 0 || filesFailed > 0) {
        log::InfoF("Lua plugin loader: %zu file(s) run, %zu failed, across "
                   "%zu plugin(s) with lua entrypoints",
                   filesRun, filesFailed, pluginsWithLua);
    }
}

}  // namespace kcdx::lua_plugin_loader
