// kcdx.declare(module, name, [versions_kv])
// kcdx.declared(name)
//
// Author-declared track of the unified named-target table.
//
//   kcdx.declare("WHGame.dll", "combatResolver", {
//       ["1.5.1164953"] = { pattern = "48 8B 05 ?? ?? ?? ?? 8B", signature = "i32 (ptr)" },
//       ["1.6.*"]       = { pattern = "48 8B 0D ?? ?? ?? ?? 8B", signature = "i32 (ptr)" },
//   })
//
//   kcdx.declare("WHGame.dll", "combatStateMask", {
//       ["1.5.1164953"] = 0x0F,
//       ["1.6.*"]       = 0x1F,
//   })
//
//   kcdx.declare("WHGame.dll", "combatResolver",
//       { pattern = "48 8B 05 ?? ?? ??", signature = "i32 (ptr)" })
//
// THREE valid third-arg shapes:
//
//   per-version map shape — keys look like version strings ("1.5.1164953"
//   or "1.5.*"); each value is a literal (integer/string) OR a sub-table
//   with one or more of {pattern, signature, kind}.
//
//   flat single-entry shape — keys are the recognized option names
//   (pattern, signature, kind) directly; synthesized as a single
//   versionKey = "*" VersionEntry.
//
//   omitted / empty table — REJECTED (a declared name with no payload
//   is an author bug; the rejection includes a teaching error pointing
//   at the three valid shapes).
//
// Per-version map vs flat single-entry is disambiguated by INSPECTING
// THE KEYS: a key matching ^\d+(\.\d+|\.\*)*$ is version-shaped; a key
// in {pattern, signature, kind} is option-shaped. Any other string key,
// or a mix of the two, is a rejection.
//
// Validation that needs declared-store knowledge (name charset, version
// key syntax, pattern-without-signature) lives in declared_targets::
// Register — that path writes its own structured KV reject line under
// category "DECLARED_TARGET" and returns false. This binder propagates
// the boolean. Binder-layer rejects (bad arg type, unknown key, mixed-
// key-shape, omitted third arg with no payload) write under category
// "DECLARED_TARGET_BIND" so the two layers are greppable separately,
// then return false.
//
// kcdx.declared(name) reads a VALUE entry's payload. PATTERN
// declarations resolve through hook/bytes/code verbs; this accessor
// returns nil for them, for VersionMismatch / NoEntry, and for any
// 3-segment cross-plugin reference where the foreign plugin's
// declaration is a pattern or has no version match.
//
// Lua precision: integer values use lua_pushinteger. LUA_NUMBER=float in
// the CryEngine build → values >= 2^24 round through Lua's number
// representation. The declared-value examples in the spec are small
// bitmasks (0x0F, 0x1F); pointer-magnitude declared values resolve
// through the address verbs, not through this accessor.
//
// Lua bridge: raw Lua C API only; no kcdx-side static-const sentinel.

#include "lua_bind_declare.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "declared_targets.h"  // DeclaredEntry, VersionEntry, Register, LookupForCaller, ResolvedDeclared
#include "log.h"               // LOG_*_KV, ::kcdx::log::KV
#include "lua_registry.h"      // OwningPluginForCurrentCall
#include "plugin_loader.h"     // kcdx::plugins::g_runtimeGameVersionString

namespace kcdx::lua_bind_declare {

namespace {

// Recognized OPTION keys in the flat single-entry shape AND inside a
// per-version map's sub-table value. Kept identical between the two
// branches because the per-version sub-table is "the flat shape, scoped
// to one version" — the keys an author writes inside one version's
// table are the same names they'd write at the flat top level.
static const char* kKnownOptionKeys[] = {
    "pattern", "signature", "kind",
};

constexpr size_t kKnownOptionKeyCount =
    sizeof(kKnownOptionKeys) / sizeof(kKnownOptionKeys[0]);

bool IsKnownOptionKey(const char* k) {
    if (!k) return false;
    for (size_t i = 0; i < kKnownOptionKeyCount; ++i) {
        if (std::string(k) == kKnownOptionKeys[i]) return true;
    }
    return false;
}

// Recognise a key as version-shaped if it matches one of:
//   "1"     "1.5"     "1.5.1164953"     "1.5.*"     "1.*.*"
// Formally: ^\d+(\.\d+|\.\*)*$. We accept '*' only as a whole
// dot-separated component (matching declared_targets::ValidateVersionKey's
// "wildcard keys must trail, and '*' is trailing-only" contract — exact-
// vs-wildcard syntactic acceptance is shared with the Register validator,
// which is the authoritative gate on key shape).
bool IsVersionShapedKey(const char* k) {
    if (!k || !k[0]) return false;
    const size_t n = std::string(k).size();
    if (!std::isdigit(static_cast<unsigned char>(k[0]))) return false;
    bool prevWasDot = false;
    for (size_t i = 0; i < n; ++i) {
        const char c = k[i];
        if (c == '.') {
            if (i == 0 || i == n - 1) return false;
            if (prevWasDot)            return false;
            prevWasDot = true;
            continue;
        }
        if (c == '*') {
            // '*' is legal only as a whole component (immediately after a
            // dot, immediately before a dot OR end-of-string).
            if (!prevWasDot)                                  return false;
            if (i + 1 != n && k[i + 1] != '.')                return false;
            prevWasDot = false;
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        prevWasDot = false;
    }
    return !prevWasDot;
}

// Emit a binder-layer reject line. Mirrors the shape declared_targets::
// Register uses (author/plugin/name + reason + detail) so the two layers
// grep alike; uses category "DECLARED_TARGET_BIND" so the two are
// separable in the dev log.
void LogBinderReject(const std::string& author,
                     const std::string& plugin,
                     const std::string& name,
                     const char*        reason,
                     const std::string& detail) {
    LOG_ERROR_KV("DECLARED_TARGET_BIND", "register_rejected",
        ::kcdx::log::KV("author", author),
        ::kcdx::log::KV("plugin", plugin),
        ::kcdx::log::KV("name",   name),
        ::kcdx::log::KV("reason", reason),
        ::kcdx::log::KV("detail", detail));
}

// Pull pattern / signature / kind from the table at `optsIdx` into
// `out`. Caller has already validated that every string key in the
// table is one of kKnownOptionKeys; this function does the typed
// extraction and the per-key type checks.
//
// Returns true on success; false on a type error (e.g. pattern is a
// number) with the teaching error in `errOut`.
bool ExtractOptionFields(lua_State* L, int optsIdx,
                         const std::string& callDesc,
                         std::string& patternOut,
                         bool&        patternPresent,
                         std::string& signatureOut,
                         std::string& kindOut,
                         std::string& errOut) {
    patternPresent = false;

    lua_getfield(L, optsIdx, "pattern");
    if (lua_type(L, -1) == LUA_TSTRING) {
        patternOut = lua_tostring(L, -1);
        patternPresent = true;
    } else if (!lua_isnil(L, -1)) {
        lua_pop(L, 1);
        errOut = callDesc +
            ": `pattern`, if present, must be a string (an AOB byte "
            "pattern with optional `??` wildcards, e.g. \"48 8B 05 ?? "
            "?? ?? ?? 8B\").";
        return false;
    }
    lua_pop(L, 1);

    lua_getfield(L, optsIdx, "signature");
    if (lua_type(L, -1) == LUA_TSTRING) {
        signatureOut = lua_tostring(L, -1);
    } else if (!lua_isnil(L, -1)) {
        lua_pop(L, 1);
        errOut = callDesc +
            ": `signature`, if present, must be a string (the function "
            "ABI signature, e.g. \"i32 (ptr)\").";
        return false;
    }
    lua_pop(L, 1);

    lua_getfield(L, optsIdx, "kind");
    if (lua_type(L, -1) == LUA_TSTRING) {
        kindOut = lua_tostring(L, -1);
    } else if (!lua_isnil(L, -1)) {
        lua_pop(L, 1);
        errOut = callDesc +
            ": `kind`, if present, must be a string (the entry-kind "
            "tag, e.g. \"function\" or \"data_slot\").";
        return false;
    }
    lua_pop(L, 1);
    return true;
}

// Walk the keys of the table at `tableIdx`. Classify each STRING key as
// version-shaped, option-shaped, or other. Returns true on a clean walk;
// false (with `errOut` populated) on an unrecognised string key. Sets
// the three counters from the walk. Integer/array keys are ignored
// (per kcdx convention; FindUnknownKey does the same).
bool ClassifyKeys(lua_State* L, int tableIdx,
                  const std::string& callDesc,
                  size_t& versionShapedCount,
                  size_t& optionShapedCount,
                  std::string& errOut) {
    versionShapedCount = 0;
    optionShapedCount  = 0;
    const int abs = (tableIdx < 0)
        ? (lua_gettop(L) + tableIdx + 1) : tableIdx;
    lua_pushnil(L);
    while (lua_next(L, abs) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING) {
            const char* k = lua_tostring(L, -2);
            if (IsVersionShapedKey(k)) {
                ++versionShapedCount;
            } else if (IsKnownOptionKey(k)) {
                ++optionShapedCount;
            } else {
                std::string offending = k;
                lua_pop(L, 2);  // pop value + key (abort iteration)
                errOut = callDesc +
                    ": unrecognised key '" + offending + "' in the third "
                    "argument. The third argument is either a per-version "
                    "map (keys like \"1.5.1164953\" or \"1.5.*\") OR a "
                    "flat single-entry table (keys `pattern` / "
                    "`signature` / `kind`). Mixed shapes are not allowed.";
                return false;
            }
        }
        lua_pop(L, 1);  // pop value, keep key for the next lua_next
    }
    return true;
}

// Parse the per-version map shape. The table at `versionsIdx` has been
// classified — every string key is version-shaped. For each entry,
// construct one VersionEntry on `out`. Returns true on success; false
// (with `errOut` populated) on a sub-table that has an unknown key or
// a value of an unsupported type.
bool ParsePerVersionMap(lua_State* L, int versionsIdx,
                        const std::string& callDesc,
                        std::vector<declared_targets::VersionEntry>& out,
                        std::string& errOut) {
    const int abs = (versionsIdx < 0)
        ? (lua_gettop(L) + versionsIdx + 1) : versionsIdx;
    lua_pushnil(L);
    while (lua_next(L, abs) != 0) {
        // The key is on -2, the value on -1. Skip non-string keys
        // silently — they were already ignored by ClassifyKeys, so the
        // only string keys reaching here are version-shaped (validated
        // above).
        if (lua_type(L, -2) != LUA_TSTRING) {
            lua_pop(L, 1);
            continue;
        }
        const std::string vkey = lua_tostring(L, -2);

        declared_targets::VersionEntry ve;
        ve.versionKey = vkey;

        const int vtype = lua_type(L, -1);
        if (vtype == LUA_TNUMBER) {
            ve.isPattern = false;
            ve.valueIsString = false;
            ve.valueInt = static_cast<int64_t>(lua_tointeger(L, -1));
            out.push_back(std::move(ve));
        } else if (vtype == LUA_TSTRING) {
            ve.isPattern = false;
            ve.valueIsString = true;
            ve.valueStr = lua_tostring(L, -1);
            out.push_back(std::move(ve));
        } else if (vtype == LUA_TTABLE) {
            // Sub-table — classify its keys first, then extract.
            size_t versionShapedInSub = 0;
            size_t optionShapedInSub  = 0;
            const std::string subDesc = callDesc +
                ": version entry '" + vkey + "'";
            if (!ClassifyKeys(L, -1, subDesc,
                              versionShapedInSub, optionShapedInSub,
                              errOut)) {
                // ClassifyKeys aborted iteration on the sub-table; the
                // outer iteration's value+key are still on the stack.
                lua_pop(L, 2);
                return false;
            }
            if (versionShapedInSub > 0) {
                lua_pop(L, 2);
                errOut = subDesc +
                    ": a per-version sub-table may carry only the option "
                    "keys `pattern` / `signature` / `kind` — version-"
                    "shaped keys are only meaningful at the OUTER level.";
                return false;
            }

            std::string pat, sig, kind;
            bool patternPresent = false;
            if (!ExtractOptionFields(L, -1, subDesc,
                                     pat, patternPresent,
                                     sig, kind, errOut)) {
                lua_pop(L, 2);
                return false;
            }

            if (!patternPresent && sig.empty() && kind.empty()) {
                lua_pop(L, 2);
                errOut = subDesc +
                    ": empty sub-table — declare at least one of "
                    "`pattern` / `signature` / `kind` for this version, "
                    "or write the value literally (e.g. [\"" + vkey +
                    "\"] = 0x0F) for a value entry.";
                return false;
            }

            ve.isPattern = patternPresent;
            ve.patternStr = pat;
            ve.signatureStr = sig;
            ve.kindTag = kind;
            out.push_back(std::move(ve));
        } else {
            lua_pop(L, 2);
            errOut = callDesc +
                ": version entry '" + vkey +
                "' has an unsupported value type. A version entry's "
                "value is either an integer (a value entry), a string (a "
                "value entry), or a sub-table {pattern=..., "
                "signature=..., kind=...} (a pattern entry).";
            return false;
        }
        lua_pop(L, 1);  // pop value, keep key for next lua_next
    }
    return true;
}

// Build one synthesized "*" VersionEntry from the flat single-entry
// shape (the third arg's TOP-LEVEL keys are option-shaped). The flat
// shape REQUIRES a pattern — a flat `{kind = "data_slot"}` with no
// pattern is nonsense in this shape (use the per-version map form for a
// value entry).
bool BuildFlatVersionEntry(lua_State* L, int optsIdx,
                           const std::string& callDesc,
                           declared_targets::VersionEntry& out,
                           std::string& errOut) {
    std::string pat, sig, kind;
    bool patternPresent = false;
    if (!ExtractOptionFields(L, optsIdx, callDesc,
                             pat, patternPresent, sig, kind, errOut)) {
        return false;
    }
    if (!patternPresent) {
        errOut = callDesc +
            ": the flat third-argument shape requires `pattern`. To "
            "declare a value-only entry (e.g. a constant), use the "
            "per-version map form: { [\"1.5.1164953\"] = 0x0F }.";
        return false;
    }
    out.versionKey = "*";
    out.isPattern  = true;
    out.patternStr = pat;
    out.signatureStr = sig;
    out.kindTag = kind;
    return true;
}

// kcdx.declare(module, name, [versions_kv])
//
// Always returns a single boolean (true accept / false reject). The
// reject path emits a structured KV reject log line (here for binder-
// layer rejects, in declared_targets::Register for store-layer rejects)
// so the author sees the cause in the dev log; the return value lets
// authors short-circuit on failure: `if not kcdx.declare(...) then return end`.
int Lua_Declare(lua_State* L) {
    // --- Owner identity (the calling plugin's <author>.<plugin>) ---
    std::string callSiteFile;
    int callSiteLine = 0;
    kcdx::lua_registry::OwningPlugin owner =
        kcdx::lua_registry::OwningPluginForCurrentCall(
            L, callSiteFile, callSiteLine);

    // --- arg 1: module (string, REQUIRED, NO default) ---
    if (lua_type(L, 1) != LUA_TSTRING) {
        LogBinderReject(owner.author, owner.plugin, "",
            "bad_arg_module",
            "kcdx.declare(module, name, [versions_kv]): `module` (arg "
            "1) must be a string. No default — kcdx exists to enable "
            "cross-module plugins, and a defaulted module silently "
            "misroutes when secondaries get involved. Call shape: "
            "kcdx.declare(\"WHGame.dll\", \"combatResolver\", { [...] }).");
        lua_pushboolean(L, 0);
        return 1;
    }
    const std::string moduleName = lua_tostring(L, 1);

    // --- arg 2: name (string, REQUIRED) ---
    if (lua_type(L, 2) != LUA_TSTRING) {
        LogBinderReject(owner.author, owner.plugin, "",
            "bad_arg_name",
            "kcdx.declare(module, name, [versions_kv]): `name` (arg 2) "
            "must be a string — the bare name you are declaring. The "
            "engine stamps it as <author>.<plugin>.<name> from your "
            "[plugin] manifest. Call shape: kcdx.declare(\"WHGame.dll\", "
            "\"combatResolver\", { [...] }).");
        lua_pushboolean(L, 0);
        return 1;
    }
    const std::string bareName = lua_tostring(L, 2);

    const std::string callDesc =
        "kcdx.declare('" + moduleName + "', '" + bareName + "')";

    // --- arg 3: versions_kv (table, REQUIRED).
    //
    // Per the spec: omitted-entirely AND empty-table both reject. A
    // declared name with no payload is genuinely an author bug, and the
    // matcher's safety-net warn on an empty per-version list becomes a
    // programmer-error invariant once the binder enforces this. The
    // teaching error names the three valid shapes.
    const int arg3Type = lua_type(L, 3);
    if (arg3Type == LUA_TNONE || arg3Type == LUA_TNIL) {
        LogBinderReject(owner.author, owner.plugin, bareName,
            "missing_versions_kv",
            callDesc +
            ": no payload supplied — provide either a per-version map "
            "`{[\"1.5.1164953\"] = {pattern=..., signature=...}}`, a "
            "flat `{pattern=..., signature=...}` table, or a per-version "
            "value map `{[\"1.5.1164953\"] = 0x0F}`. A declared name "
            "with no payload is a no-op the engine cannot resolve.");
        lua_pushboolean(L, 0);
        return 1;
    }
    if (arg3Type != LUA_TTABLE) {
        LogBinderReject(owner.author, owner.plugin, bareName,
            "bad_arg_versions_kv",
            callDesc +
            ": the third argument, when present, must be a table — "
            "either a per-version map (keys like \"1.5.1164953\" or "
            "\"1.5.*\"), a flat `{pattern=..., signature=...}` table, "
            "or a per-version value map (keys → integers/strings).");
        lua_pushboolean(L, 0);
        return 1;
    }

    // Classify the third-arg's top-level keys.
    size_t versionShapedCount = 0;
    size_t optionShapedCount  = 0;
    std::string keyErr;
    if (!ClassifyKeys(L, 3, callDesc,
                      versionShapedCount, optionShapedCount, keyErr)) {
        LogBinderReject(owner.author, owner.plugin, bareName,
            "unknown_key", keyErr);
        lua_pushboolean(L, 0);
        return 1;
    }
    if (versionShapedCount == 0 && optionShapedCount == 0) {
        // Empty table — covers `kcdx.declare("WHGame.dll", "x", {})`.
        LogBinderReject(owner.author, owner.plugin, bareName,
            "empty_versions_kv",
            callDesc +
            ": the third argument is an empty table — provide a "
            "per-version map `{[\"1.5.1164953\"] = {pattern=...}}`, a "
            "flat `{pattern=...}` table, or a per-version value map "
            "`{[\"1.5.1164953\"] = 0x0F}`. An empty table is the same "
            "as omitting the third argument: no payload to resolve.");
        lua_pushboolean(L, 0);
        return 1;
    }
    if (versionShapedCount > 0 && optionShapedCount > 0) {
        LogBinderReject(owner.author, owner.plugin, bareName,
            "mixed_key_shape",
            callDesc +
            ": the third argument mixes version-shaped keys (like "
            "\"1.5.1164953\" or \"1.5.*\") with option-shaped keys "
            "(`pattern` / `signature` / `kind`). Pick one shape: a "
            "per-version map { [\"1.5.1164953\"] = {pattern=...}, ... } "
            "OR a flat { pattern=..., signature=... } table that "
            "synthesizes to versionKey \"*\".");
        lua_pushboolean(L, 0);
        return 1;
    }

    // Build the per-version vector per the chosen shape.
    std::vector<declared_targets::VersionEntry> versions;
    std::string parseErr;
    if (versionShapedCount > 0) {
        // Per-version map shape.
        if (!ParsePerVersionMap(L, 3, callDesc, versions, parseErr)) {
            LogBinderReject(owner.author, owner.plugin, bareName,
                "bad_version_entry", parseErr);
            lua_pushboolean(L, 0);
            return 1;
        }
    } else {
        // Flat single-entry shape — synthesise versionKey = "*". The
        // matcher's longest-wildcard pass picks this up as the lowest-
        // priority fallback that matches every game version.
        declared_targets::VersionEntry flat;
        if (!BuildFlatVersionEntry(L, 3, callDesc, flat, parseErr)) {
            LogBinderReject(owner.author, owner.plugin, bareName,
                "bad_flat_entry", parseErr);
            lua_pushboolean(L, 0);
            return 1;
        }
        versions.push_back(std::move(flat));
    }

    // Hand off to the declared-store. Register runs name-charset,
    // version-key syntax, and pattern-without-signature validation; on
    // reject it writes its own KV line (category DECLARED_TARGET) and
    // returns false. The binder propagates the boolean unchanged.
    declared_targets::DeclaredEntry entry;
    entry.declaringAuthor = owner.author;
    entry.declaringPlugin = owner.plugin;
    entry.name            = bareName;
    entry.module          = moduleName;
    entry.versions        = std::move(versions);

    const bool accepted = declared_targets::Register(entry);
    lua_pushboolean(L, accepted ? 1 : 0);
    return 1;
}

// kcdx.declared(name) — read a declared VALUE entry's payload.
//
// `name` is either a bare 1-segment name (resolves against the calling
// plugin's own declarations — the SELF tier) or a 3-segment
// "<author>.<plugin>.<bare>" explicit form (mirrors
// address_library::ResolveByName's 3-segment handling for cross-plugin
// reads).
//
// Returns the value (integer or string) on a Kind::Value hit; nil for
// Kind::Pattern (those resolve through hook/bytes/code verbs, not this
// accessor), Kind::VersionMismatch, and Kind::NoEntry.
int Lua_Declared(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        return 1;
    }
    const std::string nameArg = lua_tostring(L, 1);
    if (nameArg.empty()) {
        lua_pushnil(L);
        return 1;
    }

    // Walk the calling-plugin owner context.
    std::string callSiteFile;
    int callSiteLine = 0;
    kcdx::lua_registry::OwningPlugin owner =
        kcdx::lua_registry::OwningPluginForCurrentCall(
            L, callSiteFile, callSiteLine);

    // Parse the 3-segment explicit form `<author>.<plugin>.<bare>` if
    // present, mirroring address_library::ResolveByName's split. Only
    // the 1-segment SELF form (bare name, resolved under (owner.author,
    // owner.plugin, nameArg)) and the 3-segment explicit form are
    // meaningful for declared-value reads — the legacy 1-dot
    // <plugin>.<name> form lives in the address resolver, not here.
    // Any other dot count (0-segment, 2-segment, 4+-segment, or any
    // segment empty) returns nil — no SELF/explicit interpretation
    // exists for it.
    std::string lookupAuthor;
    std::string lookupPlugin;
    std::string lookupBare;

    // Count dots and split into up to 3 segments.
    std::vector<std::string> segs;
    {
        std::string cur;
        for (char c : nameArg) {
            if (c == '.') {
                segs.push_back(std::move(cur));
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        segs.push_back(std::move(cur));
    }
    if (segs.size() == 3 &&
        !segs[0].empty() && !segs[1].empty() && !segs[2].empty()) {
        lookupAuthor = segs[0];
        lookupPlugin = segs[1];
        lookupBare   = segs[2];
    } else if (segs.size() == 1 && !segs[0].empty()) {
        lookupAuthor = owner.author;
        lookupPlugin = owner.plugin;
        lookupBare   = segs[0];
    } else {
        // 2-segment or >3-segment or empty-segment form — no SELF/
        // explicit interpretation exists for declared-value reads.
        lua_pushnil(L);
        return 1;
    }

    // Anonymous caller on the SELF path (console / pak Lua) — no
    // declarations to look up. Mirror address_library's "no owner = no
    // self tier" semantics by returning nil.
    if (lookupAuthor.empty() || lookupPlugin.empty()) {
        lua_pushnil(L);
        return 1;
    }

    const declared_targets::ResolvedDeclared rd =
        declared_targets::LookupForCaller(
            lookupAuthor, lookupPlugin, lookupBare,
            kcdx::plugins::g_runtimeGameVersionString);

    if (rd.kind == declared_targets::ResolvedDeclared::Kind::Value) {
        if (rd.valueIsString) {
            lua_pushstring(L, rd.valueStr.c_str());
        } else {
            // LUA_NUMBER=float in this build: values >= 2^24 lose
            // precision on the way through Lua. The spec's declared-
            // value examples (0x0F, 0x1F) are small bitmasks well
            // under that threshold; pointer-magnitude declared values
            // resolve through the address verbs, not through this
            // accessor (the binder header documents the threshold).
            lua_pushinteger(L,
                static_cast<lua_Integer>(rd.valueInt));
        }
        return 1;
    }
    // Pattern, VersionMismatch, NoEntry → nil.
    lua_pushnil(L);
    return 1;
}

}  // namespace

void bind(lua_State* L) {
    // kcdx.declare + kcdx.declared are TOP-LEVEL "doing" verbs
    // (positional args) on the kcdx table that is at the top of the
    // stack on entry, matching the registration shape used by
    // kcdx.alias / kcdx.scan / kcdx.on.
    const int kcdx_idx = lua_gettop(L);
    lua_pushcfunction(L, Lua_Declare);
    lua_setfield(L, kcdx_idx, "declare");
    lua_pushcfunction(L, Lua_Declared);
    lua_setfield(L, kcdx_idx, "declared");
}

}  // namespace kcdx::lua_bind_declare
