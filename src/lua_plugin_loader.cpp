// kcdx::lua_plugin_loader — see header for the contract.

#include "lua_plugin_loader.h"

#include <atomic>
#include <filesystem>
#include <string>

extern "C" {
#include "lauxlib.h"
}

#include "crash_guard.h"
#include "load_order.h"
#include "log.h"
#include "lua_registry.h"
#include "plugin_loader.h"

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

// Guarded body: luaL_loadfile + lua_pcall. Any Lua error is captured
// into ctx->err; any hard fault is trapped by the surrounding
// guard::Call. Leaves the Lua stack balanced (pops the loaded chunk or
// the error message).
void LoadOneFileGuarded(void* userdata) {
    auto* ctx = static_cast<LoadCtx*>(userdata);
    lua_State* L = ctx->L;

    const int top = lua_gettop(L);

    // luaL_loadfile pushes the compiled chunk as a function, or an
    // error string on failure.
    ctx->status = luaL_loadfile(L, ctx->absPath);
    if (ctx->status != 0) {
        const char* msg = lua_tostring(L, -1);
        ctx->err = msg ? msg : "<load error: no message>";
        lua_settop(L, top);
        return;
    }

    // Execute the chunk. plugin.lua runs as a parameterless function;
    // its kcdx.hook/.bytes/... calls queue intent into lua_registry.
    ctx->ran = true;
    ctx->status = lua_pcall(L, 0, 0, 0);
    if (ctx->status != 0) {
        const char* msg = lua_tostring(L, -1);
        ctx->err = msg ? msg : "<runtime error: no message>";
        lua_settop(L, top);
        return;
    }

    lua_settop(L, top);
}

std::atomic<bool> g_ranOnce{false};

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
                log::ErrorF("Plugin '%s': lua entrypoint '%s' %s: %s",
                            m.name.c_str(), rel.c_str(),
                            ctx.ran ? "runtime error" : "load error",
                            ctx.err.c_str());
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
