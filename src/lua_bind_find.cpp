// kcdx.find(criteria_table) — discover a game function from what the author
// already knows. The Lua binder over step 0's refdb::FindFunctions dev-DB search
// surface (refdb.cpp owns the connection + the per-criterion query strategy;
// this file is the surface that exposes it to authors).
//
//   local r = kcdx.find{ string = "test_marker" }
//   -- r is ALWAYS a table:
//   --   no matches / dev tool unavailable -> {}  (idiomatic `if #r == 0`)
//   --   matches -> array of records, each:
//   --     { function, module, rva, decompile_quality,
//   --       statements = { {idx, kind, pseudo_text, callee, string_ref,
//   --                       captures, applicable_ops}, ... } }
//   --   over-500 -> first 500 + r._truncated=true + r._total_matches=N
//
// kcdx.find is a DEV TOOL. It searches the dev reference DB
// (reference-dev.sqlite — the full game corpus), opened by step 0 ONLY when dev
// mode is on AND that file is present. When the dev gate fails (dev mode off OR
// the dev DB absent), FindFunctions returns an empty result with `unavailable`
// set; this binder LOGS the teaching message and returns `{}` — the SAME empty
// contract as a genuine no-match, so a shipped mod's `if #r == 0` path runs
// harmlessly in a player's non-dev install. NEVER a Lua error, NEVER a crash.
//
// Discovery is an AUTHORING-TIME activity: the author finds a function here, then
// writes kcdx.statement.* / kcdx.locator.* code against it. The shipped product
// does not carry the dev DB.
//
// Fail-loud (AP14): an unrecognized criteria key is rejected with a teaching
// (nil, err) — never a silent drop; a no-criteria call is rejected (the
// at-least-one-of-N parse-time validation). The dev-gate-off / no-match paths
// are `{}` + a logged teaching line, never a silent nil with no signal.
//
// Lua precision (lua-precision.md): the record's `rva` is a module-relative
// address whose magnitude (~2^25 for WHGame.dll sites) exceeds Lua 5.1's
// float exactness threshold (2^24), so it goes back as a kcdx.memory.pointer
// userdata via PushPointer — NEVER lua_pushinteger (which would silently round
// it to a 16 MB grid). idx / decompile_quality / size_bytes are small integers
// and use lua_pushinteger.
//
// Lua bridge (lua-bridge.md): raw Lua C API only; the pointer userdata is a raw
// PushPointer (no kcdx-side static-const sentinel — PROBE Q stays zero).

#include "lua_bind_find.h"

#include <cstdint>
#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "log.h"               // LOG_INFO (the teaching-message log line)
#include "lua_bind_helpers.h"  // PushPointer, FindUnknownKey
#include "lua_memory.h"        // kcdx::lua_memory::pointer
#include "refdb.h"             // refdb::FindCriteria / FindResult / FindFunctions

namespace kcdx::lua_bind_find {

namespace {

// The dev-tool-unavailable teaching message (design step-1 §What — verbatim).
// Logged on the gated-off path; the SAME text the console kcdx_find prints.
const char* kUnavailableTeaching =
    "[kcdx.find] dev tool unavailable. kcdx.find / kcdx_dev_inspect need dev mode\n"
    "AND the dev reference DB:\n"
    "  1. set dev_mode = true in <game-bin>/kcdx-engine/engine.toml\n"
    "  2. place reference-dev.sqlite (a separate download, NOT in the release zip)\n"
    "     at <game-bin>/kcdx-engine/data/reference-dev.sqlite\n"
    "These are authoring tools — discover a function here, then write your\n"
    "kcdx.statement.* / kcdx.locator.* code against it.";

// The recognized criteria-key set for kcdx.find. A typo'd `strng=` /
// `callers=` would otherwise be silently ignored — fail loud, never a silent
// drop. The iteration is the shared FindUnknownKey; this list stays local
// because the key set belongs to this binder.
static const char* kKnown[] = {
    "string", "cvar", "callers_of", "callee", "name_contains",
    "callee_in_subsystem",
};

// Read one optional string criterion off the criteria table at index 1: if the
// key is present and a string, set the has_-flag + value and bump *setCount.
// A present-but-non-string value is a fail-loud teaching error (returns false +
// leaves (nil, err) on the stack). Stack-balanced: pops the fetched field.
bool ReadCriterion(lua_State* L, const char* key, bool& hasFlag,
                   std::string& out, int& setCount) {
    lua_getfield(L, 1, key);
    if (lua_type(L, -1) == LUA_TSTRING) {
        out = lua_tostring(L, -1);
        hasFlag = true;
        ++setCount;
    } else if (!lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.find{...}: `%s`, if present, must be a string (a name / "
            "string-literal the engine matches against the dev reference DB).",
            key);
        return false;
    }
    lua_pop(L, 1);
    return true;
}

// Push one FindStatement as a Lua sub-table:
//   { idx, kind, pseudo_text, callee, string_ref, captures, applicable_ops }
// `captures` mirrors the conventionalized kcdx.locator :resolve(...).captures
// shape (docs/lua/locator.md): an array of
// { name, storage_kind, storage_detail, data_type, size_bytes }. Stack +1.
void PushStatement(lua_State* L, const refdb::FindStatement& s) {
    lua_newtable(L);  // statement sub-table

    lua_pushinteger(L, static_cast<lua_Integer>(s.idx));  // small int, not a ptr
    lua_setfield(L, -2, "idx");
    lua_pushstring(L, s.kind.c_str());
    lua_setfield(L, -2, "kind");
    lua_pushstring(L, s.pseudo_text.c_str());
    lua_setfield(L, -2, "pseudo_text");
    lua_pushstring(L, s.callee.c_str());
    lua_setfield(L, -2, "callee");
    lua_pushstring(L, s.string_ref.c_str());
    lua_setfield(L, -2, "string_ref");

    // captures — the per-statement variables (empty table when none). Mirrors
    // the locator :resolve captures shape so the two surfaces read identically.
    lua_newtable(L);
    for (size_t i = 0; i < s.captures.size(); ++i) {
        const refdb::FindCapture& c = s.captures[i];
        lua_newtable(L);  // one capture sub-table

        lua_pushstring(L, c.var_name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushstring(L, c.storage_kind.c_str());
        lua_setfield(L, -2, "storage_kind");
        lua_pushstring(L, c.storage_detail.c_str());
        lua_setfield(L, -2, "storage_detail");
        lua_pushstring(L, c.data_type.c_str());
        lua_setfield(L, -2, "data_type");
        if (c.has_size_bytes) {
            lua_pushinteger(L, static_cast<lua_Integer>(c.size_bytes));
        } else {
            lua_pushnil(L);
        }
        lua_setfield(L, -2, "size_bytes");

        lua_rawseti(L, -2, static_cast<int>(i + 1));  // captures[i+1] = sub
    }
    lua_setfield(L, -2, "captures");

    // applicable_ops — the kcdx.op.* op NAMES that fit this statement
    // (an array of strings; the author uses them verbatim in
    // kcdx.statement.replace_with). Empty array when the kind emits none.
    lua_newtable(L);
    for (size_t i = 0; i < s.applicable_ops.size(); ++i) {
        lua_pushstring(L, s.applicable_ops[i].c_str());
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    lua_setfield(L, -2, "applicable_ops");
}

// Push one FindRecord as a Lua sub-table:
//   { function, module, rva, decompile_quality, statements }
// `rva` is a kcdx.memory.pointer (lua-precision.md — VA-magnitude, exact). Stack +1.
void PushRecord(lua_State* L, const refdb::FindRecord& r) {
    lua_newtable(L);  // record sub-table

    lua_pushstring(L, r.function.c_str());
    lua_setfield(L, -2, "function");
    lua_pushstring(L, r.module.c_str());
    lua_setfield(L, -2, "module");

    // rva — a module-relative address, magnitude-exact via PushPointer (NOT
    // lua_pushinteger — Lua 5.1 LUA_NUMBER=float would round it; lua-precision.md).
    kcdx::lua_bind_helpers::PushPointer(
        L, kcdx::lua_memory::pointer(static_cast<uintptr_t>(r.rva)));
    lua_setfield(L, -2, "rva");

    lua_pushinteger(L, static_cast<lua_Integer>(r.decompile_quality));  // small int
    lua_setfield(L, -2, "decompile_quality");

    // statements = { {idx, kind, ...}, ... } (idx-ordered).
    lua_newtable(L);
    for (size_t i = 0; i < r.statements.size(); ++i) {
        PushStatement(L, r.statements[i]);
        lua_rawseti(L, -2, static_cast<int>(i + 1));  // statements[i+1] = sub
    }
    lua_setfield(L, -2, "statements");
}

// kcdx.find(criteria_table)
//
// criteria_table (REQUIRED, ≥1 criterion) — at least one of: string, cvar,
// callers_of, callee, name_contains, callee_in_subsystem. Validated at
// parse-time (the at-least-one-of-N case).
//
// Returns the result table ALWAYS — `{}` on no-match OR dev-tool-unavailable
// (the teaching line is logged on the latter), a record array on matches (with
// _truncated / _total_matches on an over-500 search). Returns (nil, teaching
// error) ONLY on bad input (not a table, an unrecognized key, a non-string
// criterion value, or no criterion set).
int Lua_Find(lua_State* L) {
    // --- arg 1 must be a table ---
    if (lua_type(L, 1) != LUA_TTABLE) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.find{...}: expects a single criteria table with at least one "
            "of `string`, `cvar`, `callers_of`, `callee`, `name_contains`, "
            "`callee_in_subsystem` (all strings). Call shape: "
            "kcdx.find{ string = \"test_marker\" }. These are DEV-mode "
            "discovery tools — see docs/lua/find.md.");
        return 2;
    }

    // Reject an unrecognized criteria key before reading anything — a typo'd
    // key would otherwise vanish silently (fail loud, never a silent drop).
    {
        std::string bad = kcdx::lua_bind_helpers::FindUnknownKey(
            L, 1, kKnown, sizeof(kKnown) / sizeof(kKnown[0]));
        if (!bad.empty()) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.find: unrecognized criteria key '%s' — not a recognized "
                "kcdx.find criterion (check for a typo). Valid criteria: "
                "string, cvar, callers_of, callee, name_contains, "
                "callee_in_subsystem.",
                bad.c_str());
            return 2;
        }
    }

    // --- read each optional string criterion (a present non-string is loud) ---
    refdb::FindCriteria criteria;
    int setCount = 0;
    if (!ReadCriterion(L, "string", criteria.has_string,
                       criteria.string, setCount)) return 2;
    if (!ReadCriterion(L, "cvar", criteria.has_cvar,
                       criteria.cvar, setCount)) return 2;
    if (!ReadCriterion(L, "callers_of", criteria.has_callers_of,
                       criteria.callers_of, setCount)) return 2;
    if (!ReadCriterion(L, "callee", criteria.has_callee,
                       criteria.callee, setCount)) return 2;
    if (!ReadCriterion(L, "name_contains", criteria.has_name_contains,
                       criteria.name_contains, setCount)) return 2;
    if (!ReadCriterion(L, "callee_in_subsystem",
                       criteria.has_callee_in_subsystem,
                       criteria.callee_in_subsystem, setCount)) return 2;

    // --- the at-least-one-of-N validation (parse-time, the binder's job) ---
    if (setCount == 0) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.find{...}: at least one criterion is required — pass one of "
            "`string`, `cvar`, `callers_of`, `callee`, `name_contains`, "
            "`callee_in_subsystem` (e.g. kcdx.find{ string = \"test_marker\" }). "
            "An empty criteria table finds nothing.");
        return 2;
    }

    // --- run the dev-DB search (step 0 lazy-opens the dev DB, gated on dev
    //     mode + file presence; on gate failure it returns unavailable=true
    //     + a logged dev_db_unavailable reason). ---
    refdb::FindResult result = refdb::FindFunctions(criteria);

    // --- dev gate failed (dev mode off OR dev DB absent): LOG the teaching
    //     message and return `{}` — the SAME empty contract as a no-match, so
    //     a shipped mod's `if #r == 0` runs harmlessly. NEVER an error/crash. ---
    if (result.unavailable) {
        LOG_INFO("FIND", "%s", kUnavailableTeaching);
        lua_newtable(L);  // {}
        return 1;
    }

    // --- build the result record array (may be empty -> idiomatic `if #r==0`) ---
    lua_newtable(L);  // result table (return value)
    int resultIdx = lua_gettop(L);
    for (size_t i = 0; i < result.records.size(); ++i) {
        PushRecord(L, result.records[i]);
        lua_rawseti(L, resultIdx, static_cast<int>(i + 1));  // r[i+1] = record
    }

    // --- loud truncation: over-500 search carries _truncated + _total_matches
    //     (not silent — the author sees the result is a capped prefix). ---
    if (result.truncated) {
        lua_pushboolean(L, 1);
        lua_setfield(L, resultIdx, "_truncated");
        // _total_matches — a match COUNT, a small integer (not a VA), so
        // lua_pushinteger is correct (a corpus has <2^24 functions).
        lua_pushinteger(L, static_cast<lua_Integer>(result.total_matches));
        lua_setfield(L, resultIdx, "_total_matches");
    }

    return 1;  // the result table
}

}  // namespace

void bind(lua_State* L) {
    // kcdx.find — registered directly on the kcdx table (the kcdx table is at
    // the top of the stack on entry), like kcdx.scan. It is the at-least-one-of-N
    // criteria-table verb (lua-api-surface.md rule 4 "required is at least one
    // of N").
    int kcdx_idx = lua_gettop(L);
    lua_pushcfunction(L, Lua_Find);
    lua_setfield(L, kcdx_idx, "find");
}

}  // namespace kcdx::lua_bind_find
