// kcdx::lua_plugin_loader — see header for the contract.

#include "lua_plugin_loader.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

extern "C" {
#include "lauxlib.h"
#include "lua.h"   // lua_storedebuginfo / lua_isstoredebuginfo (line-info toggle)
}

#include "crash_guard.h"
#include "load_order.h"
#include "log.h"
#include "lua_registry.h"
#include "lua_require_searcher.h"  // Install() + OwnerScope (plugin-scoped require)
#include "plugin_loader.h"
#include "test.h"  // kcdx::test::ReportResult (dev-mode-gated, name-keyed)
#include "zone_gate.h"  // RejectReason() — distinguish engine-reject from user-disabled in skip-logs

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

    // Set the kcdx environment as this ENTRYPOINT chunk's fenv BEFORE the
    // pcall (mechanism a — see lua_require_searcher.h). The kcdx env's
    // `require` is the kcdx closure (plugin-scoped resolution + namespaced
    // cache that bypasses _LOADED), so the author's bare require("helper")
    // from this entrypoint routes to kcdx and resolves against THIS
    // plugin's folder + cache slot. require'd helpers get their own kcdx
    // fenv set inside the closure, so the whole load (entrypoint + nested
    // helpers) runs under kcdx's require. lua_setfenv routes through the
    // chunk's env at OP_GETGLOBAL time (vendor/lua/lvm.c:431). Net stack
    // height unchanged — the chunk stays on top. Must be done before the
    // errfunc reorder below (the chunk must still be the function at -1).
    // Stack here: [chunk] (chunk at top == top+1).
    kcdx::lua_require_searcher::SetKcdxEnvOnChunkAtTop(L);

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
std::atomic<bool> g_ranAfterOnce{false};

// THE single entrypoint run-order: (load_order priority asc, name asc).
//
// Both Lua slots — the before/default slot (RunAll, plugin.lua) and the
// after-game slot (RunAfterEntrypoints, lua_after) — run their bodies
// observably (they can call game functions, publish events, observe
// before-work). So both RUN in load-order priority, symmetric with each
// other and with the C++ PostGameLoad mirror (plugin_loader.cpp). Zone is
// irrelevant to the tiebreak: each slot is single-zone by construction
// (RunAll is the before/default slot; lua_after is after_game). Ties broken
// by name for determinism.
//
// Dependency (topo) order is NOT a factor here, and that is correct: topo
// order constrains DLL Preload/Load (which consume g_plugins directly,
// earlier, on the worker thread); plugin.lua / lua_after only QUEUE kcdx.*
// intent that is applied later at lua_registry::ApplyZone in load-order — no
// entrypoint's body depends on ANOTHER plugin's entrypoint having run first.
// load_order::priority is itself dependency-agnostic (load_order.cpp Resolve:
// author hint + user override only), so a pure-priority sort is safe.
//
// Returns true if `a` must run before `b`. Strict-weak-ordering for std::sort.
bool EntrypointRunsBefore(const kcdx::plugins::LoadedPlugin* a,
                          const kcdx::plugins::LoadedPlugin* b) {
    const auto& ea = kcdx::load_order::Of(a->manifest.name);
    const auto& eb = kcdx::load_order::Of(b->manifest.name);
    if (ea.priority != eb.priority) {
        return ea.priority < eb.priority;
    }
    return a->manifest.name < b->manifest.name;
}

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

// Outcome of loading one entrypoint file. The caller (per-plugin loop)
// uses this to keep its counters and to decide whether to keep running
// this plugin's later files.
enum class FileResult {
    Ok,       // chunk loaded + ran clean
    Failed,   // missing / compile error / runtime error — skip this file, keep going
    Faulted,  // hard SEH fault inside the chunk — abandon this plugin's remaining files
};

// Load + run ONE plugin Lua entrypoint file against `L`. Shared by both
// the before/default slot (RunAll) and the after-game slot
// (RunAfterEntrypoints) so the load discipline — existence check,
// per-plugin OwnerScope (plugin-scoped require + attribution),
// RegisterScriptOwner, crash_guard, storedebug toggle (inside
// LoadOneFileGuarded), dual error-routing (engine log + the plugin's own
// log), and the cap-23 line-info regression read — lives in exactly ONE
// place. `slotLabel` only flavors the log lines (e.g. "lua entrypoint" vs
// "lua_after entrypoint"); behavior is identical.
//
// Pre: caller has already gated on enabled + non-empty file list. Main
// thread only (loads are main-thread; lua-callback-threading.md / AP6).
FileResult RunOneEntrypointFile(lua_State* L,
                                const kcdx::plugins::LoadedPlugin& p,
                                const std::string& rel,
                                const char* slotLabel) {
    const auto& m = p.manifest;

    fs::path abs = m.folderPath / rel;
    std::error_code ec;
    if (!fs::exists(abs, ec)) {
        log::ErrorF("Plugin '%s': %s '%s' not found at %s — skipping this file",
                    m.name.c_str(), slotLabel, rel.c_str(),
                    abs.string().c_str());
        return FileResult::Failed;
    }

    const std::string absUtf8 = abs.string();

    // Attribute kcdx.* calls from this file (and anything it require()s
    // synchronously) to this plugin BEFORE running it. We register the
    // absolute path (what luaL_loadfile is handed, and thus what
    // debug.getinfo reports) so the lookup matches.
    kcdx::lua_registry::RegisterScriptOwner(absUtf8, m.name);

    LoadCtx ctx;
    ctx.L          = L;
    ctx.pluginName = m.name.c_str();
    ctx.absPath    = absUtf8.c_str();

    log::InfoF("Plugin '%s': running %s '%s'",
               m.name.c_str(), slotLabel, rel.c_str());

    // Scope the "current owning plugin" across the SYNCHRONOUS load below.
    // Because guard::Call runs LoadOneFileGuarded (luaL_loadfile +
    // lua_pcall) inline and returns rather than unwinding (it swallows any
    // SEH fault and returns false), the OwnerScope destructor runs on
    // EVERY exit path — clean return, Lua compile/runtime error (captured
    // in ctx), or hard fault. A require(...) executed inside the chunk runs
    // while this owner is set, so the kcdx searcher resolves against THIS
    // plugin's folder. See lua_require_searcher.h.
    bool clean;
    {
        kcdx::lua_require_searcher::OwnerScope ownerScope(m.name, m.folderPath);
        clean = kcdx::guard::Call("plugin.lua.run", m.name.c_str(),
                                  &LoadOneFileGuarded, &ctx);
    }

    if (!clean) {
        // Hard fault inside the chunk — guard already logged a FAULTED line
        // with the site + plugin name.
        log::ErrorF("Plugin '%s': %s '%s' faulted (see GUARD line) — skipping "
                    "remaining files for this plugin",
                    m.name.c_str(), slotLabel, rel.c_str());
        return FileResult::Faulted;
    }
    if (ctx.status != 0) {
        const char* kind = ctx.ran ? "runtime error" : "load error";
        // Engine log (kcdx-dev.log) — the developer / bug-report channel.
        log::ErrorF("Plugin '%s': %s '%s' %s: %s",
                    m.name.c_str(), slotLabel, rel.c_str(), kind,
                    ctx.err.c_str());
        // ALSO route to the offending plugin's OWN log so the author sees
        // the file:line:detail (+ traceback, for runtime errors) where they
        // naturally look. p.handle is the plugin's log stream; PluginError
        // routes by handle (log.h).
        log::PluginError(
            p.handle,
            std::string(slotLabel) + " '" + rel + "' " + kind + ": " + ctx.err);

        // Regression assertion for AP12 #3 (plugin.lua error line-info
        // quality). PURE READ of ctx.err — it does not touch
        // status/err/control flow. FIXTURE-AGNOSTIC: never checks the
        // plugin's name; reports on the line-info quality of WHATEVER
        // runtime error we just captured. Only RUNTIME errors qualify
        // (ctx.ran): they have a live stack, so the errfunc appended a
        // traceback. A load (compile) error has no stack, so it isn't a
        // datapoint. ReportResult's OWN dev-mode early-return is the
        // production-quiet backstop. See cap-23-lua-error.
        if (ctx.ran) {
            const bool pass =
                HasLineMarker(ctx.err) &&
                ctx.err.find("stack traceback:") != std::string::npos;
            kcdx::test::ReportResult("cap-23-lua-error-lineinfo", pass, ctx.err);
        }

        return FileResult::Failed;
    }

    log::InfoF("Plugin '%s': %s '%s' OK", m.name.c_str(), slotLabel, rel.c_str());
    return FileResult::Ok;
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

    // Set up the kcdx plugin-scoped `require` ONCE before any plugin.lua
    // runs: create the namespaced module cache + the shared kcdx env table
    // whose `require` is the kcdx closure. Entrypoint chunks get this env
    // as their fenv (SetKcdxEnvOnChunkAtTop, in LoadOneFileGuarded) so a
    // plain require("helper") resolves against the plugin's OWN folder AND
    // OWN cache slot (cross-plugin collision impossible — _LOADED bypassed)
    // and stamps attribution at the helper's compile point. Idempotent.
    // See lua_require_searcher.h.
    kcdx::lua_require_searcher::Install(L);

    // Run plugin.lua entrypoints in LOAD-ORDER PRIORITY (priority asc, name
    // asc), symmetric with the lua_after slot (RunAfterEntrypoints) and the
    // C++ PostGameLoad mirror. g_plugins is in dependency topo-sort order; the
    // before phase's body runs observably (it can call game functions, publish
    // events), so its RUN order must be predictable load-order priority — not
    // the topo/folder-alphabetical coincidence g_plugins happens to carry.
    // Dependency order does NOT constrain this: plugin.lua only QUEUES kcdx.*
    // intent applied later at ApplyZone in load-order (see EntrypointRunsBefore
    // above for the full rationale). Ties broken by name for determinism.
    std::vector<const kcdx::plugins::LoadedPlugin*> ordered;
    ordered.reserve(kcdx::plugins::g_plugins.size());
    for (const auto& p : kcdx::plugins::g_plugins) {
        if (p.manifest.luaEntrypointsRel.empty()) continue;
        ordered.push_back(&p);
    }
    std::sort(ordered.begin(), ordered.end(), &EntrypointRunsBefore);

    size_t pluginsWithLua = 0, filesRun = 0, filesFailed = 0;

    for (const kcdx::plugins::LoadedPlugin* pp : ordered) {
        const kcdx::plugins::LoadedPlugin& p = *pp;
        const auto& m = p.manifest;

        // Honor the load_order.toml enabled gate — a disabled plugin's
        // Lua doesn't run, same as its DLL kcdxPlugin_Load is skipped.
        // Skip-log distinguishes engine-reject (zone_gate) from
        // user-disabled (load_order.toml) so the author knows which
        // surface to edit. PLUGIN_REJECTED was already emitted loudly
        // by zone_gate; here we stay at Info to match the existing
        // "disabled via load_order.toml" cadence.
        if (!kcdx::load_order::IsPluginEnabled(m.name)) {
            // zone_gate keys g_rejected on the 2-dot <author>.<plugin>
            // form (matches kcdx.plugin.is_rejected lookup shape per
            // naming-namespaces.md). Pass the composed key here.
            const std::string& rejectReason =
                kcdx::zone_gate::RejectReason(m.author + "." + m.name);
            if (!rejectReason.empty()) {
                log::InfoF("Plugin '%s': %zu lua entrypoint(s) skipped "
                           "(rejected by zone_gate: %s)",
                           m.name.c_str(), m.luaEntrypointsRel.size(),
                           rejectReason.c_str());
            } else {
                log::InfoF("Plugin '%s': %zu lua entrypoint(s) skipped "
                           "(plugin disabled via load_order.toml)",
                           m.name.c_str(), m.luaEntrypointsRel.size());
            }
            continue;
        }

        ++pluginsWithLua;
        for (const std::string& rel : m.luaEntrypointsRel) {
            const FileResult r =
                RunOneEntrypointFile(L, p, rel, "lua entrypoint");
            if (r == FileResult::Ok) {
                ++filesRun;
            } else {
                ++filesFailed;
                if (r == FileResult::Faulted) {
                    break;  // don't run this plugin's later files after a fault
                }
            }
        }
    }

    if (pluginsWithLua > 0 || filesFailed > 0) {
        log::InfoF("Lua plugin loader: %zu file(s) run, %zu failed, across "
                   "%zu plugin(s) with lua entrypoints",
                   filesRun, filesFailed, pluginsWithLua);
    }
}

void RunAfterEntrypoints(lua_State* L) {
    bool expected = false;
    if (!g_ranAfterOnce.compare_exchange_strong(expected, true,
                                                 std::memory_order_acq_rel)) {
        return;  // already ran this session
    }
    if (!L) {
        log::Warn("lua_plugin_loader::RunAfterEntrypoints called with null "
                  "lua_State; no lua_after entrypoint will execute this session");
        return;
    }

    // The kcdx require setup is done once by RunAll (which always runs
    // before this in the first-update-tick sequence). Re-call Install —
    // it's idempotent (internal latch) — so RunAfterEntrypoints is
    // self-contained and correct even if the call order ever changes. The
    // session-lived cache means a lua_after entrypoint's require("state")
    // hits the SAME slot a plugin.lua require("state") populated earlier
    // (cap-27 relies on this cross-window within-plugin cache hit).
    kcdx::lua_require_searcher::Install(L);

    // The lua_after slot runs in LOAD-ORDER PRIORITY, not g_plugins
    // (topo/discovery) order — symmetric with the before/default slot (RunAll)
    // and the C++ PostGameLoad mirror, all of which sort by the SAME
    // EntrypointRunsBefore comparator (priority asc, name asc). A lua_after
    // entrypoint's CODE runs here (it may call game functions, observe
    // before-work, etc.), so its RUN order is observable and MUST follow
    // load-order priority. Build an index over plugins with a lua_after file
    // and sort it; ties broken by name for determinism.
    std::vector<const kcdx::plugins::LoadedPlugin*> ordered;
    ordered.reserve(kcdx::plugins::g_plugins.size());
    for (const auto& p : kcdx::plugins::g_plugins) {
        if (p.manifest.luaAfterEntrypointsRel.empty()) continue;
        ordered.push_back(&p);
    }
    std::sort(ordered.begin(), ordered.end(), &EntrypointRunsBefore);

    size_t pluginsWithAfter = 0, filesRun = 0, filesFailed = 0;

    for (const kcdx::plugins::LoadedPlugin* pp : ordered) {
        const kcdx::plugins::LoadedPlugin& p = *pp;
        const auto& m = p.manifest;

        // Honor the load_order.toml enabled gate — same as the before slot.
        // Skip-log distinguishes engine-reject (zone_gate) from
        // user-disabled cause; PLUGIN_REJECTED was already emitted loudly.
        if (!kcdx::load_order::IsPluginEnabled(m.name)) {
            // zone_gate keys g_rejected on the 2-dot <author>.<plugin>
            // form (matches kcdx.plugin.is_rejected lookup shape per
            // naming-namespaces.md). Pass the composed key here.
            const std::string& rejectReason =
                kcdx::zone_gate::RejectReason(m.author + "." + m.name);
            if (!rejectReason.empty()) {
                log::InfoF("Plugin '%s': %zu lua_after entrypoint(s) skipped "
                           "(rejected by zone_gate: %s)",
                           m.name.c_str(), m.luaAfterEntrypointsRel.size(),
                           rejectReason.c_str());
            } else {
                log::InfoF("Plugin '%s': %zu lua_after entrypoint(s) skipped "
                           "(plugin disabled via load_order.toml)",
                           m.name.c_str(), m.luaAfterEntrypointsRel.size());
            }
            continue;
        }

        ++pluginsWithAfter;
        for (const std::string& rel : m.luaAfterEntrypointsRel) {
            const FileResult r =
                RunOneEntrypointFile(L, p, rel, "lua_after entrypoint");
            if (r == FileResult::Ok) {
                ++filesRun;
            } else {
                ++filesFailed;
                if (r == FileResult::Faulted) {
                    break;  // don't run this plugin's later files after a fault
                }
            }
        }
    }

    if (pluginsWithAfter > 0 || filesFailed > 0) {
        log::InfoF("Lua plugin loader (after_game): %zu file(s) run, %zu "
                   "failed, across %zu plugin(s) with lua_after entrypoints",
                   filesRun, filesFailed, pluginsWithAfter);
    }
}

}  // namespace kcdx::lua_plugin_loader
