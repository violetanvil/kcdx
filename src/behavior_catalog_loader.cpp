// kcdx::behavior_catalog_loader — see header for the contract.

#include "behavior_catalog_loader.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

extern "C" {
#include "lauxlib.h"
#include "lua.h"  // lua_storedebuginfo / lua_isstoredebuginfo (line-info toggle)
}

#include "crash_guard.h"
#include "log.h"
#include "lua_bind_behavior.h"  // EngineCatalogScope — routes declares to the engine root
#include "paths.h"              // EngineDataDirPath — locates the catalog dir at runtime

namespace fs = std::filesystem;

namespace kcdx::behavior_catalog_loader {

namespace {

constexpr const char* kCat = "BEHAVIOR_CATALOG";

// The catalog dir, relative to <kcdx-engine>/. Engine-owned data, shipped in
// the release zip + live-deploy under kcdx-engine/ (loader-architecture.md).
constexpr const wchar_t* kCatalogSubdir = L"behavior-catalog";

std::atomic<bool> g_ranOnce{false};

// Context for one guarded catalog-file load — luaL_loadfile + lua_pcall run
// inside guard::Call, status/err flow back out for the caller to log.
struct LoadCtx {
    lua_State*  L       = nullptr;
    const char* absPath = nullptr;  // UTF-8 absolute path for luaL_loadfile
    int         status  = -1;       // luaL_loadfile / lua_pcall status
    std::string err;                // captured inside the guard
    bool        ran     = false;    // true if we reached lua_pcall
};

// Message handler installed as the lua_pcall errfunc so a RUNTIME error carries
// the call chain (a catalog author sees file:line + traceback exactly as a
// plugin author does). Stock Lua 5.1 has no luaL_traceback, so use the canonical
// 5.1 idiom: fetch global debug.traceback and let it append. Plain
// lua_CFunction via lua_pushcfunction — no kcdx-side static-const sentinel.
// (Mirrors lua_plugin_loader's TracebackHandler — the proven shape.)
int TracebackHandler(lua_State* L) {
    lua_getfield(L, LUA_GLOBALSINDEX, "debug");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 1;  // leave original errmsg at index 1
    }
    lua_getfield(L, -1, "traceback");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        return 1;
    }
    lua_remove(L, -2);      // drop the debug table; stack: [errmsg][traceback]
    lua_pushvalue(L, 1);    // arg 1: the error message
    lua_pushinteger(L, 1);  // arg 2: skip THIS handler frame
    lua_call(L, 2, 1);
    return 1;
}

// Guarded body: luaL_loadfile + lua_pcall, leaving the Lua stack balanced. Any
// Lua error is captured into ctx->err; any hard fault is trapped by guard::Call.
void LoadOneCatalogFileGuarded(void* userdata) {
    auto* ctx = static_cast<LoadCtx*>(userdata);
    lua_State* L = ctx->L;
    const int top = lua_gettop(L);

    // Store debug line-info across this load so a catalog author's error carries
    // file:line + detail (the engine clears storedebug as a CryEngine memory
    // save; line info is baked at PARSE time, so set it BEFORE luaL_loadfile).
    // Capture + restore the engine's prior value on every exit path.
    const int storedebugWas = lua_isstoredebuginfo(L);
    lua_storedebuginfo(L, 1);

    ctx->status = luaL_loadfile(L, ctx->absPath);
    if (ctx->status != 0) {
        const char* msg = lua_tostring(L, -1);
        ctx->err = msg ? msg : "<load error: no message>";
        lua_storedebuginfo(L, storedebugWas);
        lua_settop(L, top);
        return;
    }

    // Install the traceback handler BELOW the chunk for a runtime traceback.
    lua_pushcfunction(L, &TracebackHandler);  // [chunk][errfunc]
    const int errfuncIdx = top + 1;
    lua_insert(L, errfuncIdx);                 // [errfunc][chunk]

    ctx->ran    = true;
    ctx->status = lua_pcall(L, 0, 0, errfuncIdx);
    if (ctx->status != 0) {
        const char* msg = lua_tostring(L, -1);
        ctx->err = msg ? msg : "<runtime error: no message>";
        lua_storedebuginfo(L, storedebugWas);
        lua_settop(L, top);  // also drops the errfunc still on the stack
        return;
    }

    lua_storedebuginfo(L, storedebugWas);
    lua_settop(L, top);
}

// Run ONE catalog .lua file against `L` under an EngineCatalogScope (so its
// kcdx.behavior.declare calls stamp the reserved kcdx.behavior.<bare> root).
// Returns true on a clean run. A malformed file (compile error, runtime error,
// or hard fault) is the BUILTIN-PACK BOOT ERROR — logged LOUD here, never a
// silent skip — and returns false; the pack keeps loading the rest.
bool RunOneCatalogFile(lua_State* L, const fs::path& abs) {
    const std::string absUtf8 = kcdx::paths::ToUtf8(abs);
    const std::string nameUtf8 = kcdx::paths::ToUtf8(abs.filename());

    LoadCtx ctx;
    ctx.L       = L;
    ctx.absPath = absUtf8.c_str();

    LOG_INFO_KV(kCat, "file_load",
        ::kcdx::log::KV("file", nameUtf8));

    bool clean;
    {
        // Declares from this file stamp the engine root; the scope is restored
        // on every exit path (clean, Lua error captured in ctx, or hard fault —
        // guard::Call returns rather than unwinding, so the scope dtor runs).
        kcdx::lua_bind_behavior::EngineCatalogScope catalogScope;
        clean = kcdx::guard::Call("behavior.catalog.run", "kcdx",
                                  &LoadOneCatalogFileGuarded, &ctx);
    }

    if (!clean) {
        // Hard fault — guard already logged a FAULTED line with the site.
        LOG_ERROR_KV(kCat, "file_faulted",
            ::kcdx::log::KV("file", nameUtf8),
            ::kcdx::log::KV("detail",
                "builtin behavior-catalog file faulted (see GUARD line) — "
                "this catalog entry did not register; the pack continues"));
        return false;
    }
    if (ctx.status != 0) {
        const char* kind = ctx.ran ? "runtime error" : "load error";
        // Builtin-pack boot error — LOUD, never a silent skip. A malformed
        // catalog file is an engine-side defect (a catalog QA miss), surfaced
        // like any builtin failure so it is caught at the build/test pass.
        LOG_ERROR_KV(kCat, "file_error",
            ::kcdx::log::KV("file",   nameUtf8),
            ::kcdx::log::KV("kind",   kind),
            ::kcdx::log::KV("detail", ctx.err));
        return false;
    }

    LOG_INFO_KV(kCat, "file_ok",
        ::kcdx::log::KV("file", nameUtf8));
    return true;
}

}  // namespace

void RunCatalog(lua_State* L) {
    bool expected = false;
    if (!g_ranOnce.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel)) {
        return;  // already ran this session
    }
    if (!L) {
        LOG_WARN(kCat, "RunCatalog called with null lua_State; "
                       "no behavior catalog will load this session");
        return;
    }

    const fs::path catalogDir =
        kcdx::paths::EngineDataDirPath() / kCatalogSubdir;

    std::error_code ec;
    if (!fs::exists(catalogDir, ec) || !fs::is_directory(catalogDir, ec)) {
        // No catalog dir is a valid state (a dev tree without the asset, or a
        // fresh install before deploy) — info, not error; nothing to load.
        LOG_INFO_KV(kCat, "no_catalog_dir",
            ::kcdx::log::KV("dir", kcdx::paths::ToUtf8(catalogDir)));
        return;
    }

    // Discover *.lua files, sorted by filename for a DETERMINISTIC load order
    // (the registration order is the pin; within the pack, filename order is
    // the stable tiebreak). README.md is index prose, not a catalog file —
    // only *.lua is run.
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(catalogDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const fs::path& p = entry.path();
        if (p.extension() == L".lua") files.push_back(p);
    }
    if (ec) {
        LOG_ERROR_KV(kCat, "dir_scan_failed",
            ::kcdx::log::KV("dir",    kcdx::paths::ToUtf8(catalogDir)),
            ::kcdx::log::KV("detail", ec.message()));
        return;
    }
    std::sort(files.begin(), files.end());

    size_t ran = 0, failed = 0;
    for (const fs::path& f : files) {
        if (RunOneCatalogFile(L, f)) ++ran; else ++failed;
    }

    // One lifecycle info line — applied/failed counts (logging.md: a subsystem
    // load is one info line).
    LOG_INFO_KV(kCat, "catalog_loaded",
        ::kcdx::log::KV("files_ok",     ran),
        ::kcdx::log::KV("files_failed", failed),
        ::kcdx::log::KV("total",        files.size()));
}

}  // namespace kcdx::behavior_catalog_loader
