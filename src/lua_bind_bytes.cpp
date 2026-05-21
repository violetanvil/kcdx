// kcdx.bytes — Lua-facing byte-rewrite registration.
//
// Phase 2a of the manifest-only restructure (see
// docs/outstanding-work/restructure-plan.md). Succeeds the v0.1
// [[patch]] TOML schema. Authors call:
//
//   local h, err = kcdx.bytes{
//       name        = "...",
//       description = "...",
//       pattern     = "48 ?? 89 ...",   -- OR address_id=N, target_symbol="..."
//       module      = "WHGame.dll",
//       offset      = 13,
//       original    = "44 8A F0",       -- optional verify
//       replacement = "45 31 F6",
//       idempotent  = true,             -- default true
//       priority    = 100,              -- intra-plugin sort key
//       context     = "...",            -- optional context pattern
//       anchor_string = "...",          -- optional string anchor
//   }
//
// Returns: handle table { name=string, applied=bool, reason=string|nil }
// on the success path. On argument-parse failure returns (nil, err).
// `applied` reflects the actual apply outcome — false means the engine
// rejected the patch (locator failed, original-byte mismatch, etc.) with
// `reason` set to the engine's diagnostic.
//
// Phase-2 scoping: this routes straight into patch::ApplyPatch (the
// runtime-apply path that bypasses pre-flight conflict detection).
// Deferred-apply + coroutine wait_applied + conflict_engine pre-flight
// across queued registrations are Phase 9+ work per the restructure
// plan §"API calls register intent; engine applies them in one pass".
// Today's behavior matches the legacy KCDX.ScanAndWrite Lua-runtime
// path so we don't regress what worked.

#include "lua_bind_bytes.h"

#include <stdexcept>
#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "log.h"
#include "patch_engine.h"

namespace kcdx::lua_bind_bytes {

namespace {

// --- Lua-table helpers (local copies — same shape as lua_bind.cpp's,
// but kept self-contained so this TU doesn't depend on lua_bind's
// internals).

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

uint64_t LuaTableU64(lua_State* L, int tableIdx, const char* key,
                     uint64_t fallback) {
    lua_getfield(L, tableIdx, key);
    uint64_t out = fallback;
    if (lua_isnumber(L, -1)) {
        out = static_cast<uint64_t>(lua_tointeger(L, -1));
    }
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

// Push the handle table { name, applied, reason } and return 1 (the
// number of Lua-stack values the C function leaves for the caller).
int PushHandle(lua_State* L, const std::string& name, bool applied,
               const std::string& reason) {
    lua_createtable(L, 0, 3);
    lua_pushlstring(L, name.data(), name.size());
    lua_setfield(L, -2, "name");
    lua_pushboolean(L, applied ? 1 : 0);
    lua_setfield(L, -2, "applied");
    if (!reason.empty()) {
        lua_pushlstring(L, reason.data(), reason.size());
        lua_setfield(L, -2, "reason");
    }
    return 1;
}

int Lua_Bytes(lua_State* L) {
    if (!lua_istable(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "kcdx.bytes: expected a single table argument");
        return 2;
    }

    kcdx::patch::PatchEntry e;
    e.sourceFile = "<lua>";
    e.name = LuaTableString(L, 1, "name", "lua_bytes");
    e.description = LuaTableString(L, 1, "description");
    e.priority = LuaTableInt(L, 1, "priority", 100);
    e.module = LuaTableString(L, 1, "module", "WHGame.dll");
    e.offset = LuaTableInt(L, 1, "offset", 0);
    e.idempotent = LuaTableBool(L, 1, "idempotent", true);
    e.addressId = LuaTableU64(L, 1, "address_id", 0);
    e.targetSymbol = LuaTableString(L, 1, "target_symbol");

    const std::string patternStr     = LuaTableString(L, 1, "pattern");
    const std::string originalStr    = LuaTableString(L, 1, "original");
    const std::string replacementStr = LuaTableString(L, 1, "replacement");
    const std::string contextStr     = LuaTableString(L, 1, "context");
    const std::string anchorStr      = LuaTableString(L, 1, "anchor_string");

    // Exactly-one-locator rule. Mirror the TOML loader's invariant so
    // authors get the same diagnostic regardless of which surface they
    // came in via.
    const int locatorCount =
        (!patternStr.empty() ? 1 : 0) +
        (e.addressId != 0    ? 1 : 0) +
        (!e.targetSymbol.empty() ? 1 : 0);
    if (locatorCount == 0) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.bytes '%s': must specify exactly one locator "
            "(pattern, address_id, or target_symbol)", e.name.c_str());
        return 2;
    }
    if (locatorCount > 1) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.bytes '%s': locators are mutually exclusive "
            "(set exactly one of pattern, address_id, target_symbol)",
            e.name.c_str());
        return 2;
    }
    if (replacementStr.empty()) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.bytes '%s': missing required field 'replacement'",
            e.name.c_str());
        return 2;
    }

    try {
        if (!patternStr.empty()) e.pattern = kcdx::patch::ParsePattern(patternStr);
        e.replacement = kcdx::patch::ParseBytes(replacementStr);
        if (!originalStr.empty()) e.original = kcdx::patch::ParseBytes(originalStr);
        if (!contextStr.empty()) {
            e.context = kcdx::patch::ParsePattern(contextStr);
        }
        if (!anchorStr.empty()) {
            e.anchor = kcdx::patch::AnchorString{anchorStr};
        }
    } catch (const std::exception& ex) {
        lua_pushnil(L);
        lua_pushfstring(L, "kcdx.bytes '%s': %s", e.name.c_str(), ex.what());
        return 2;
    }

    // Length-match check matches the [[patch]] TOML schema invariant.
    if (!e.original.empty() && e.original.size() != e.replacement.size()) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.bytes '%s': original length (%d) != replacement length (%d)",
            e.name.c_str(),
            static_cast<int>(e.original.size()),
            static_cast<int>(e.replacement.size()));
        return 2;
    }

    // Apply immediately. patch::ApplyPatch logs its own outcome line;
    // we mirror the boolean + a short reason for the handle.
    log::InfoF("kcdx.bytes: registering '%s' (module=%s, priority=%d)",
               e.name.c_str(), e.module.c_str(), e.priority);

    bool ok = false;
    std::string reason;
    try {
        ok = kcdx::patch::ApplyPatch(e);
        if (!ok) {
            reason = "apply rejected (see engine log for diagnostic)";
        }
    } catch (const std::exception& ex) {
        ok = false;
        reason = ex.what();
        log::ErrorF("kcdx.bytes '%s' threw during apply: %s",
                    e.name.c_str(), ex.what());
    }

    return PushHandle(L, e.name, ok, reason);
}

}  // namespace

void bind(lua_State* L) {
    // Caller's stack discipline: kcdx table is at top. We push a
    // closure and setfield onto it.
    lua_pushcfunction(L, Lua_Bytes);
    lua_setfield(L, -2, "bytes");
}

}  // namespace kcdx::lua_bind_bytes
