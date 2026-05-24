// kcdx.scan{...} — Lua-side diagnostic AOB scan (address-discovery workbench).
//
// A core authoring verb per .claude/rules/lua-api-surface.md: top-level
// (like kcdx.hook / kcdx.code / kcdx.bytes), configuring -> {named table}.
// A thin Lua binder over the EXISTING, proven scan_engine resolve path
// (scan_engine::ResolveScan — the same locator pipeline [[scan]] /
// [[patch]] use). The engine is NOT touched; this brings the diagnostic
// scan to the Lua surface.
//
//   local r = kcdx.scan{
//       name    = "find_outfit_swap",        -- required (logs + diagnostics)
//       pattern = "48 8B 88 ?? ?? ?? ?? 48", -- required AOB (expert hatch)
//       module  = "WHGame.dll",              -- optional (default WHGame.dll)
//       offset  = 0,                         -- optional (default 0)
//       context = "75 ?? 48",                -- optional uniqueness AOB
//       anchor_string = "SomeLiteral",       -- optional (one anchor only)
//       max_anchor_distance = 4096,          -- optional (default 4096)
//   }
//   -- r.count : number of pattern matches (integer)
//   -- r.matches : { { addr = <pointer>, module = <string>, offset = <int> }, ... }
//   -- r.addr  : r.matches[1].addr, or nil when r.count == 0
//
// kcdx.scan resolves the pattern, LOGS a concise diagnostic (matches +
// per-match module/addr), and RETURNS the structured result the author
// branches on. It is the dev-time AOB-validation workbench.
//
// DESIGN NOTES:
//   * The hand-written `pattern` (and `context`) is the LABELED EXPERT
//     AOB hatch — by-design input here, NOT an AP12 hex-burden defect.
//     kcdx.scan IS the tool an expert uses to discover an address they
//     will then NAME (the disassembler test's "name it once" path).
//   * Module-not-loaded is a count=0 result (NOT a (nil, err)) — a
//     not-loaded module is a real diagnostic outcome the author may
//     branch on, consistent with "0 matches" being a normal scan
//     outcome. The log line distinguishes the two cases ("module not
//     loaded" vs "pattern matches: 0").
//   * CONCISE log only: the binder emits a short summary (match count +
//     per-match module/addr) and does NOT duplicate scan_engine's
//     file-static FormatBytesAt byte-dump — that richness lives in the
//     [[scan]] TOML path (scan_engine::RunOne). The Lua verb's job is
//     resolve + return + a concise log.
//
// Lua precision (lua-precision.md): every returned VA is a POINTER — it
// goes back as a kcdx.memory.pointer userdata via PushPointer, NEVER
// lua_pushinteger (a VA must not round-trip through lua_Number=float).
// count / offset are integers (not pointers) and use lua_pushinteger.
//
// Lua bridge (lua-bridge.md, AP5): raw Lua C API only; no kcdx-side
// static-const sentinel. The pointer userdata is a raw lua_newuserdata
// via PushPointer. PROBE Q stays zero.

#include "lua_bind_scan.h"

#include <cstdint>
#include <exception>
#include <optional>
#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "log.h"
#include "lua_bind_helpers.h"  // PushPointer
#include "lua_memory.h"        // kcdx::lua_memory::pointer
#include "lua_registry.h"      // OwningPluginForCurrentCall
#include "patch_engine.h"      // patch::ParsePattern, patch::Anchor variants
#include "scan_engine.h"       // ScanEntry, ScanResult, ResolveScan

namespace kcdx::lua_bind_scan {

namespace {

// kcdx.scan{ name=, pattern=, module?, offset?, context?, anchor_*?,
//            max_anchor_distance? }
//
//   name    (string, required)  : name for logs + diagnostics.
//   pattern (string, required)  : AOB byte pattern (expert hatch).
//   module  (string, optional)  : module to scan; default "WHGame.dll".
//   offset  (integer, optional) : added to each hit -> apply addr; default 0.
//   context (string, optional)  : second AOB for uniqueness disambiguation.
//   anchor_string / anchor_function_by_export / anchor_symbol (string,
//                                 optional, MUTUALLY EXCLUSIVE — at most one).
//   max_anchor_distance (integer, optional) : default 4096.
//
// Returns { count, matches = {...}, addr } on a resolved scan (count may
// be 0, including module-not-loaded); (nil, teaching error) on bad input.
int Lua_Scan(lua_State* L) {
    // --- arg 1 must be a table ---
    if (lua_type(L, 1) != LUA_TTABLE) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.scan{...}: expects a single table argument with field "
            "`name` (string) and `pattern` (an AOB byte string), plus "
            "optional `module`, `offset`, `context`, one anchor "
            "(anchor_string / anchor_function_by_export / anchor_symbol), "
            "and `max_anchor_distance`. Call shape: kcdx.scan{ name = "
            "\"find_it\", pattern = \"48 8B 88 ?? ?? ?? ?? 48\" }");
        return 2;
    }

    // --- name (string, required) ---
    lua_getfield(L, 1, "name");
    if (lua_type(L, -1) != LUA_TSTRING) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.scan{...}: `name` (string) is required — the name used "
            "in the diagnostic log (e.g. name = \"find_outfit_swap\").");
        return 2;
    }
    std::string name = lua_tostring(L, -1);
    lua_pop(L, 1);

    // --- pattern (string, required) ---
    lua_getfield(L, 1, "pattern");
    if (lua_type(L, -1) != LUA_TSTRING) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.scan{ name = \"%s\" }: `pattern` (string) is required — "
            "an AOB byte pattern with optional `??` wildcards (e.g. "
            "pattern = \"48 8B 88 ?? ?? ?? ?? 48\"). This is the expert "
            "address-discovery form; once you find the site, name it.",
            name.c_str());
        return 2;
    }
    std::string patternStr = lua_tostring(L, -1);
    lua_pop(L, 1);

    scan_engine::ScanEntry entry;
    entry.sourceFile = "<lua>";
    entry.name = name;

    // Parse the required pattern. ParsePattern throws on a bad string.
    try {
        entry.pattern = kcdx::patch::ParsePattern(patternStr);
    } catch (const std::exception& e) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.scan{ name = \"%s\" }: parse error in `pattern`: %s — "
            "`pattern` is an AOB byte string with optional `??` wildcards "
            "(e.g. \"48 8B 88 ?? ?? ?? ?? 48\").",
            name.c_str(), e.what());
        return 2;
    }

    // --- module (string, optional; default "WHGame.dll") ---
    lua_getfield(L, 1, "module");
    if (lua_type(L, -1) == LUA_TSTRING) {
        entry.module = lua_tostring(L, -1);
    } else if (!lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.scan{ name = \"%s\" }: `module`, if present, must be a "
            "string (e.g. module = \"WHGame.dll\").",
            name.c_str());
        return 2;
    }
    lua_pop(L, 1);

    // --- offset (integer, optional; default 0) ---
    lua_getfield(L, 1, "offset");
    if (lua_type(L, -1) == LUA_TNUMBER) {
        entry.offset = static_cast<int>(lua_tointeger(L, -1));
    } else if (!lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.scan{ name = \"%s\" }: `offset`, if present, must be an "
            "integer added to each hit to compute the apply address.",
            name.c_str());
        return 2;
    }
    lua_pop(L, 1);

    // --- context (string, optional) ---
    lua_getfield(L, 1, "context");
    if (lua_type(L, -1) == LUA_TSTRING) {
        std::string contextStr = lua_tostring(L, -1);
        lua_pop(L, 1);
        try {
            entry.context = kcdx::patch::ParsePattern(contextStr);
        } catch (const std::exception& e) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.scan{ name = \"%s\" }: parse error in `context`: %s "
                "— `context` is an AOB byte string (a uniqueness signal).",
                name.c_str(), e.what());
            return 2;
        }
    } else if (!lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.scan{ name = \"%s\" }: `context`, if present, must be an "
            "AOB byte string used to disambiguate a non-unique pattern.",
            name.c_str());
        return 2;
    } else {
        lua_pop(L, 1);
    }

    // --- anchors (mutually exclusive — at most one; mirror config.cpp
    //     ParseOneScan) ---
    int anchorCount = 0;
    lua_getfield(L, 1, "anchor_string");
    if (lua_type(L, -1) == LUA_TSTRING) {
        entry.anchor = kcdx::patch::AnchorString{lua_tostring(L, -1)};
        ++anchorCount;
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "anchor_function_by_export");
    if (lua_type(L, -1) == LUA_TSTRING) {
        entry.anchor = kcdx::patch::AnchorFunctionByExport{lua_tostring(L, -1)};
        ++anchorCount;
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "anchor_symbol");
    if (lua_type(L, -1) == LUA_TSTRING) {
        entry.anchor = kcdx::patch::AnchorSymbol{lua_tostring(L, -1)};
        ++anchorCount;
    }
    lua_pop(L, 1);
    if (anchorCount > 1) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.scan{ name = \"%s\" }: only one of `anchor_string` / "
            "`anchor_function_by_export` / `anchor_symbol` may be declared "
            "(they are mutually exclusive).",
            name.c_str());
        return 2;
    }

    // --- max_anchor_distance (integer, optional; default 4096) ---
    lua_getfield(L, 1, "max_anchor_distance");
    if (lua_type(L, -1) == LUA_TNUMBER) {
        entry.maxAnchorDistance =
            static_cast<uint32_t>(lua_tointeger(L, -1));
    } else if (!lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.scan{ name = \"%s\" }: `max_anchor_distance`, if "
            "present, must be an integer (the anchor search radius; "
            "default 4096).",
            name.c_str());
        return 2;
    }
    lua_pop(L, 1);

    // Stamp the owning plugin (the kcdx.command/publish/on/hook mechanism)
    // so a future enabled = false honors it; harmless if "" (anonymous).
    std::string callSiteFile;
    int callSiteLine = 0;
    // [[scan]] is a diagnostic-only entry — no resolver downstream
    // consults the author component, so read only `.plugin` from the
    // owner struct.
    entry.pluginName = kcdx::lua_registry::OwningPluginForCurrentCall(
        L, callSiteFile, callSiteLine).plugin;

    // --- Resolve (no logging happens inside ResolveScan) ---
    scan_engine::ScanResult result = scan_engine::ResolveScan(entry);

    // --- Concise diagnostic log (the workbench feedback). Does NOT
    //     duplicate scan_engine::FormatBytesAt — the full byte-dump lives
    //     in the [[scan]] TOML path. ---
    if (!result.moduleLoaded) {
        log::ErrorF("[scan '%s'] module '%s' not loaded (0 matches)",
                    name.c_str(), entry.module.c_str());
    } else {
        log::InfoF("[scan '%s'] pattern matches: %zu",
                   name.c_str(), result.patternMatches);
        if (result.contextMatches) {
            log::InfoF("[scan '%s'] context matches: %zu",
                       name.c_str(), *result.contextMatches);
        }
        for (size_t i = 0; i < result.matches.size(); ++i) {
            const scan_engine::ScanMatch& m = result.matches[i];
            log::InfoF("[scan '%s'] match %zu: %s+0x%llX -> apply addr 0x%p",
                       name.c_str(), i + 1, m.module.c_str(),
                       (unsigned long long)m.relOffset,
                       reinterpret_cast<void*>(m.applyAddr));
        }
    }

    // --- Build the result table: { count, matches = {...}, addr } ---
    lua_newtable(L);  // result table (return value)
    int resultIdx = lua_gettop(L);

    // count = patternMatches (integer — NOT a pointer)
    lua_pushinteger(L, static_cast<lua_Integer>(result.patternMatches));
    lua_setfield(L, resultIdx, "count");

    // matches = { { addr = <pointer>, module = <string>, offset = <int> }, ... }
    lua_newtable(L);  // matches array
    int matchesIdx = lua_gettop(L);
    for (size_t i = 0; i < result.matches.size(); ++i) {
        const scan_engine::ScanMatch& m = result.matches[i];
        lua_newtable(L);  // one match sub-table

        // addr — a VA, MUST go through PushPointer (lua-precision.md).
        kcdx::lua_bind_helpers::PushPointer(
            L, kcdx::lua_memory::pointer(m.applyAddr));
        lua_setfield(L, -2, "addr");

        lua_pushstring(L, m.module.c_str());
        lua_setfield(L, -2, "module");

        // offset — module-relative offset, an integer (NOT a pointer).
        lua_pushinteger(L, static_cast<lua_Integer>(m.relOffset));
        lua_setfield(L, -2, "offset");

        lua_rawseti(L, matchesIdx, static_cast<int>(i + 1));  // matches[i+1] = sub
    }
    lua_setfield(L, resultIdx, "matches");

    // addr = matches[1].addr (first hit), or nil when count == 0.
    if (!result.matches.empty()) {
        kcdx::lua_bind_helpers::PushPointer(
            L, kcdx::lua_memory::pointer(result.matches[0].applyAddr));
    } else {
        lua_pushnil(L);
    }
    lua_setfield(L, resultIdx, "addr");

    // The result table is on top (resultIdx). Return it.
    return 1;
}

}  // namespace

void bind(lua_State* L) {
    // kcdx.scan — a TOP-LEVEL core authoring verb (a bare function on the
    // kcdx table, NOT a sub-table — like kcdx.hook / kcdx.code). The kcdx
    // table is at the top of the stack on entry.
    int kcdx_idx = lua_gettop(L);
    lua_pushcfunction(L, Lua_Scan);
    lua_setfield(L, kcdx_idx, "scan");
}

}  // namespace kcdx::lua_bind_scan
