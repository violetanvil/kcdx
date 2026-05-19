#include "lua_bind.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

// sol2 is intentionally NOT included here. Per kcdx/CLAUDE.md hard
// rule #15, the Lua-bound surface uses raw Lua C API only — sol2's
// new_usertype<> breaks KCD2 save-load. lua_memory.h is included for
// the pure-C++ types it defines (pointer, value_wrapper_t, to_lua);
// the sol2 bindings inside it are unused from this translation unit.

#include "log.h"
#include "lua_memory.h"
#include "patch_engine.h"
#include "pe_helpers.h"

// kcdx.lua.* helpers live in lua_bind_lua.cpp. Forward-decl bind()
// here so RegisterKcdxTable can call it. (No header needed for a
// single function with this signature.)
namespace kcdx::lua_bind_lua {
    void bind(lua_State* L);
}

namespace kcdx::lua_bind {

namespace {

// Pull an optional string field from a Lua table at index `tableIdx`.
std::string LuaTableString(lua_State* L, int tableIdx, const char* key,
                           const char* fallback = "") {
    lua_getfield(L, tableIdx, key);
    std::string out = fallback;
    if (lua_isstring(L, -1)) out = lua_tostring(L, -1);
    lua_pop(L, 1);
    return out;
}

int LuaTableInt(lua_State* L, int tableIdx, const char* key, int fallback) {
    lua_getfield(L, tableIdx, key);
    int out = fallback;
    if (lua_isnumber(L, -1)) out = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    return out;
}

bool LuaTableBool(lua_State* L, int tableIdx, const char* key, bool fallback) {
    lua_getfield(L, tableIdx, key);
    bool out = fallback;
    if (lua_isboolean(L, -1)) out = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return out;
}

int Lua_ScanAndWrite(lua_State* L) {
    if (!lua_istable(L, 1)) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "KCDX.ScanAndWrite: expected a table argument");
        return 2;
    }

    kcdx::patch::PatchEntry e;
    e.sourceFile = "<lua>";
    e.name = LuaTableString(L, 1, "name", "lua_runtime");
    e.description = LuaTableString(L, 1, "description");
    e.priority = LuaTableInt(L, 1, "priority", 100);
    e.module = LuaTableString(L, 1, "module", "WHGame.dll");
    e.offset = LuaTableInt(L, 1, "offset", 0);
    e.idempotent = LuaTableBool(L, 1, "idempotent", true);

    std::string patternStr = LuaTableString(L, 1, "pattern");
    std::string originalStr = LuaTableString(L, 1, "original");
    std::string replacementStr = LuaTableString(L, 1, "replacement");
    std::string contextStr = LuaTableString(L, 1, "context");
    std::string anchorString = LuaTableString(L, 1, "anchor_string");

    try {
        if (patternStr.empty() || originalStr.empty() || replacementStr.empty()) {
            throw std::runtime_error("missing required field pattern / original / replacement");
        }
        e.pattern = kcdx::patch::ParsePattern(patternStr);
        e.original = kcdx::patch::ParseBytes(originalStr);
        e.replacement = kcdx::patch::ParseBytes(replacementStr);
        if (!contextStr.empty()) e.context = kcdx::patch::ParsePattern(contextStr);
        if (!anchorString.empty()) {
            e.anchor = kcdx::patch::AnchorString{anchorString};
        }
        e.maxAnchorDistance = static_cast<uint32_t>(
            LuaTableInt(L, 1, "max_anchor_distance", 4096));
    } catch (const std::exception& ex) {
        lua_pushboolean(L, 0);
        lua_pushfstring(L, "KCDX.ScanAndWrite: %s", ex.what());
        return 2;
    }

    bool ok = kcdx::patch::ApplyPatch(e);
    lua_pushboolean(L, ok ? 1 : 0);
    lua_pushstring(L, ok ? "ok" : "see kcdx.log");
    return 2;
}

int Lua_ReadBytes(lua_State* L) {
    lua_Integer addr = luaL_checkinteger(L, 1);
    lua_Integer n = luaL_checkinteger(L, 2);
    if (addr <= 0 || n <= 0 || n > 4096) {
        lua_pushnil(L);
        lua_pushstring(L, "KCDX.ReadBytes: invalid address or length");
        return 2;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == 0 ||
        mbi.State != MEM_COMMIT ||
        (mbi.Protect & PAGE_NOACCESS) ||
        (mbi.Protect & PAGE_GUARD)) {
        lua_pushnil(L);
        lua_pushstring(L, "KCDX.ReadBytes: address not readable");
        return 2;
    }

    auto* bytes = reinterpret_cast<const uint8_t*>(addr);
    std::string out;
    out.reserve(static_cast<size_t>(n) * 3);
    for (lua_Integer i = 0; i < n; ++i) {
        char buf[4];
        snprintf(buf, sizeof(buf), i ? " %02X" : "%02X", bytes[i]);
        out += buf;
    }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

int Lua_GetWHGameBase(lua_State* L) {
    kcdx::pe::ModuleView mv;
    if (!kcdx::pe::OpenModule(L"WHGame.dll", mv)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(
        reinterpret_cast<uintptr_t>(mv.baseBytes)));
    return 1;
}

}  // namespace

void RegisterKcdxTable(lua_State* L) {
    if (!L) {
        log::Warn("RegisterKcdxTable called with null L");
        return;
    }
    lua_newtable(L);
    lua_pushcfunction(L, Lua_ScanAndWrite);
    lua_setfield(L, -2, "ScanAndWrite");
    lua_pushcfunction(L, Lua_ReadBytes);
    lua_setfield(L, -2, "ReadBytes");
    lua_pushcfunction(L, Lua_GetWHGameBase);
    lua_setfield(L, -2, "GetWHGameBase");
    lua_setglobal(L, "KCDX");
    log::Info("KCDX Lua API registered (ScanAndWrite, ReadBytes, GetWHGameBase)");

    // Phase 5c.7a-rev2: build the lowercase `kcdx.*` surface via raw
    // Lua C API. sol2's new_usertype<>() machinery is forbidden — see
    // hard rule #15 in kcdx/CLAUDE.md, the "Implementation constraint"
    // subsection in docs/design.md, and the bisect record in
    // workspace memory `project-kcd2-sol2-incompatibility`.
    //
    // Stack discipline: kcdx::lua_memory::bind() expects the kcdx
    // table at stack top. We create it, push it to top, populate via
    // bind(), then pop it after lua_setglobal.
    lua_newtable(L);
    int kcdx_idx = lua_gettop(L);
    lua_pushliteral(L, "0.1.0-phase5c");
    lua_setfield(L, kcdx_idx, "version");
    kcdx::lua_memory::bind(L);
    kcdx::lua_bind_lua::bind(L);
    lua_setglobal(L, "kcdx");
    log::Info("kcdx.* global registered (raw Lua C API; no sol2)");
}

}  // namespace kcdx::lua_bind
