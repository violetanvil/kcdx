// kcdx.locator.* — the §9.3 locator value namespace.
//
// A locator value says WHERE in a curated function a hook / statement op
// applies. This is an ADDITIVE namespace: the hook (step 4) and statement
// (step 5) verbs consume these values, but the namespace stands alone and
// SELF-VERIFIES here via the `:resolve(module, target)` introspection accessor
// (the settled fork — see DESIGN NOTES). Each `kcdx.locator.*` call returns a
// LOCATOR VALUE (a userdata carrying a refdb::StatementLocator descriptor) the
// consuming verbs accept; when omitted on a verb that accepts a default, the
// verb uses `kcdx.locator.function_entry()`.
//
//   -- function-level
//   kcdx.locator.function_entry()      -- first statement
//   kcdx.locator.function_exit()       -- last statement
//   -- statement-content shortcuts (the documented common path)
//   kcdx.locator.first_call_to(fn)     -- first statement, callee == fn
//   kcdx.locator.last_call_to(fn)      -- last  statement, callee == fn
//   kcdx.locator.call_to(fn)           -- the UNIQUE call to fn (errors if many)
//   kcdx.locator.first_return()        -- first return-kind statement
//   kcdx.locator.last_return()         -- last  return-kind statement
//   kcdx.locator.return_value(v)       -- first return whose text references v
//   kcdx.locator.references_string(s)  -- first statement, string_ref == s
//   kcdx.locator.first_read_of_cvar(n) -- first statement reading cvar n
//   -- general matcher
//   kcdx.locator.matching{ kind=, callee=, condition_contains=, reads_cvar=,
//                          references_string= }   -- any SUBSET, ANDed
//   -- LABELED EXPERT HATCH (raw-AOB; NOT a statement-metadata locator)
//   kcdx.locator.matching_pattern("48 8B C1 ...")
//
// THE :resolve(module, target) ACCESSOR (the settled fork). Every locator value
// carries a Lua method that resolves it against a named curated function and
// returns a table — the seam the test asserts against (the locator self-checks
// with NO hook / statement consumer), and a useful author introspection surface:
//
//   local stmt = kcdx.locator.first_return():resolve("WHGame.dll", "SaveGame")
//   -- stmt.found          : bool — true on a resolved statement
//   -- stmt.statement_idx  : int  — the resolved statement's idx
//   -- stmt.kind           : string — decoded kind ("call"/"return"/"assign"/…)
//   -- stmt.byte_range_len  : int | nil — the statement's byte span (nil if absent)
//   -- stmt.callee         : string — call target (empty when not a call)
//   -- stmt.captures        : { { name=, storage_kind=, data_type=, size_bytes=,
//   --                            storage_detail= }, ... }  -- the per-statement vars
//   -- stmt.reason          : string — on found==false, the refdb reason token
//   --   (matching_pattern → "matching_pattern_not_statement_locator")
//
// The accessor CALLS refdb::ResolveStatementByName(target, locator, ctx) — the
// already-built statement-resolution surface (src/refdb.{h,cpp}); it does NOT
// reimplement resolution. `module` is the surface's required first positional
// (the §9.3 module-not-defaulted rule) but the refdb curated set is keyed by
// canonical NAME (names are unique across the curated set), so resolution
// dispatches on `target`; `module` is validated and accepted for surface
// consistency with the consuming verbs.
//
// DESIGN NOTES:
//   * matching_pattern is the LABELED expert raw-AOB hatch (cornerstones.md /
//     AP12) — the common-path locators name what the author already understands
//     (a call to a function, a return), no hex. matching_pattern carries the
//     pattern for the BYTE path and is NOT a statement-metadata locator; handed
//     to :resolve it returns found=false with reason
//     "matching_pattern_not_statement_locator" (refdb's contract), never a
//     silent mis-resolve.
//   * Lua bridge (lua-bridge.md): raw Lua C API only. The locator value is a
//     raw lua_newuserdata + a metatable registered with luaL_newmetatable. NO
//     kcdx-side static-const sentinel in any GCObject; the frealloc canary
//     (PROBE Q) stays zero.
//   * Lua precision (lua-precision.md): statement_idx / byte_range_len /
//     size_bytes are small integers (statement indices, byte spans), NOT
//     pointers — pushed via lua_pushinteger. No VA ever crosses this surface,
//     so the pointer-precision hazard does not apply.
//   * Fail loud (AP14): a bad-arg call returns (nil, teaching error); a valid
//     resolve that misses returns a table with found=false + the reason token,
//     never a silent empty.

#include "lua_bind_locator.h"

#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "refdb.h"  // refdb::StatementLocator{,Kind}, ResolveStatementByName

namespace kcdx::lua_bind_locator {

namespace {

// The locator-value userdata metatable name (LUA_REGISTRYINDEX key). Stable
// identifier, same convention as kcdx.memory.pointer.
constexpr const char* kLocatorMetatable = "kcdx.locator.value";

// ---- locator-value userdata lifecycle ------------------------------------

// The userdata payload IS a refdb::StatementLocator (carries std::string
// members, so it needs placement-new on push + an explicit dtor at __gc).
refdb::StatementLocator* CheckLocator(lua_State* L, int idx) {
    return static_cast<refdb::StatementLocator*>(
        luaL_checkudata(L, idx, kLocatorMetatable));
}

int Lua_LocatorGc(lua_State* L) {
    auto* loc = CheckLocator(L, 1);
    loc->~StatementLocator();
    return 0;
}

// :resolve(module, target) -> table
//
// Resolves the locator against the curated function named `target` (module is
// the §9.3 required first positional; the curated set is name-keyed) and
// returns a result table. On a bad arg → (nil, teaching error). On a resolved
// statement → { found=true, statement_idx, kind, byte_range_len|nil, callee,
// captures }. On a miss → { found=false, reason }.
int Lua_LocatorResolve(lua_State* L) {
    auto* loc = CheckLocator(L, 1);

    if (lua_type(L, 2) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "locator:resolve(module, target): `module` (string) is required — "
            "the module the function lives in (e.g. \"WHGame.dll\"). Call "
            "shape: kcdx.locator.first_return():resolve(\"WHGame.dll\", "
            "\"SaveGame\").");
        return 2;
    }
    if (lua_type(L, 3) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "locator:resolve(module, target): `target` (string) is required — "
            "the curated function name to resolve the locator within (e.g. "
            "\"SaveGame\").");
        return 2;
    }
    // module is accepted for surface consistency; refdb resolves by name.
    (void)lua_tostring(L, 2);
    std::string target = lua_tostring(L, 3);

    refdb::CallerContext ctx;
    ctx.callType = "kcdx.locator";  // attribution tag for the resolve logs.
    refdb::StatementResolution res =
        refdb::ResolveStatementByName(target, *loc, ctx);

    lua_newtable(L);  // result table
    int t = lua_gettop(L);

    lua_pushboolean(L, res.found ? 1 : 0);
    lua_setfield(L, t, "found");

    if (!res.found) {
        // Fail-loud: surface the refdb reason token so a caller (and the test)
        // can assert WHICH miss happened (e.g.
        // matching_pattern_not_statement_locator). refdb logged the detail.
        lua_pushstring(L, res.reason.c_str());
        lua_setfield(L, t, "reason");
        return 1;
    }

    // statement_idx — a small integer, not a pointer.
    lua_pushinteger(L, static_cast<lua_Integer>(res.statement_idx));
    lua_setfield(L, t, "statement_idx");

    lua_pushstring(L, res.kind.c_str());
    lua_setfield(L, t, "kind");

    lua_pushstring(L, res.callee.c_str());
    lua_setfield(L, t, "callee");

    lua_pushstring(L, res.string_ref.c_str());
    lua_setfield(L, t, "string_ref");

    // byte_range_len — present iff the statement carries a span; nil when
    // absent (has_byte_range_len distinguishes "carries 0" from "absent").
    if (res.has_byte_range_len) {
        lua_pushinteger(L, static_cast<lua_Integer>(res.byte_range_len));
    } else {
        lua_pushnil(L);
    }
    lua_setfield(L, t, "byte_range_len");

    // captures — the per-statement variables (empty table when none).
    lua_newtable(L);
    int caps = lua_gettop(L);
    for (size_t i = 0; i < res.captures.size(); ++i) {
        const refdb::StatementCapture& c = res.captures[i];
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

        lua_rawseti(L, caps, static_cast<int>(i + 1));  // captures[i+1] = sub
    }
    lua_setfield(L, t, "captures");

    return 1;  // the result table
}

// Install the locator-value metatable. Idempotent (luaL_newmetatable returns 0
// + leaves the existing table on the stack when already registered). Stack
// effect: 0.
void SetupMetatable(lua_State* L) {
    if (luaL_newmetatable(L, kLocatorMetatable) == 0) {
        lua_pop(L, 1);  // already registered
        return;
    }
    // Stack: [..., mt]
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");  // mt.__index = mt (methods live on mt)
    lua_pushcfunction(L, Lua_LocatorGc);
    lua_setfield(L, -2, "__gc");
    lua_pushliteral(L, "kcdx.locator.value");
    lua_setfield(L, -2, "__metatable");  // hide the metatable from pak Lua
    lua_pushcfunction(L, Lua_LocatorResolve);
    lua_setfield(L, -2, "resolve");
    lua_pop(L, 1);  // pop mt; restore stack
}

// Push a fresh locator-value userdata carrying `loc`. Stack effect: +1.
void PushLocator(lua_State* L, const refdb::StatementLocator& loc) {
    auto* mem = static_cast<refdb::StatementLocator*>(
        lua_newuserdata(L, sizeof(refdb::StatementLocator)));
    new (mem) refdb::StatementLocator(loc);
    luaL_getmetatable(L, kLocatorMetatable);
    lua_setmetatable(L, -2);
}

// Read a single required string positional (arg `argn`) for a locator
// constructor; raises a Lua error naming `verb` + `paramName` if missing/wrong.
// Raises (does not return (nil,err)) because these are constructors whose
// missing required arg is an author bug to surface at the call site.
std::string CheckStringArg(lua_State* L, int argn, const char* verb,
                           const char* paramName) {
    if (lua_type(L, argn) != LUA_TSTRING) {
        luaL_error(L,
            "kcdx.locator.%s(%s): `%s` (string) is required — see the "
            "kcdx.locator docs for the call shape.",
            verb, paramName, paramName);
    }
    return lua_tostring(L, argn);
}

// ---- the locator constructors --------------------------------------------

int Lua_FunctionEntry(lua_State* L) {
    refdb::StatementLocator loc;
    loc.kind = refdb::StatementLocatorKind::FunctionEntry;
    PushLocator(L, loc);
    return 1;
}

int Lua_FunctionExit(lua_State* L) {
    refdb::StatementLocator loc;
    loc.kind = refdb::StatementLocatorKind::FunctionExit;
    PushLocator(L, loc);
    return 1;
}

int Lua_FirstCallTo(lua_State* L) {
    refdb::StatementLocator loc;
    loc.kind = refdb::StatementLocatorKind::FirstCallTo;
    loc.callee_or_fn = CheckStringArg(L, 1, "first_call_to", "fn");
    PushLocator(L, loc);
    return 1;
}

int Lua_LastCallTo(lua_State* L) {
    refdb::StatementLocator loc;
    loc.kind = refdb::StatementLocatorKind::LastCallTo;
    loc.callee_or_fn = CheckStringArg(L, 1, "last_call_to", "fn");
    PushLocator(L, loc);
    return 1;
}

int Lua_CallTo(lua_State* L) {
    refdb::StatementLocator loc;
    loc.kind = refdb::StatementLocatorKind::CallTo;
    loc.callee_or_fn = CheckStringArg(L, 1, "call_to", "fn");
    PushLocator(L, loc);
    return 1;
}

int Lua_FirstReturn(lua_State* L) {
    refdb::StatementLocator loc;
    loc.kind = refdb::StatementLocatorKind::FirstReturn;
    PushLocator(L, loc);
    return 1;
}

int Lua_LastReturn(lua_State* L) {
    refdb::StatementLocator loc;
    loc.kind = refdb::StatementLocatorKind::LastReturn;
    PushLocator(L, loc);
    return 1;
}

int Lua_ReturnValue(lua_State* L) {
    refdb::StatementLocator loc;
    loc.kind = refdb::StatementLocatorKind::ReturnValue;
    loc.return_value_operand = CheckStringArg(L, 1, "return_value", "v");
    PushLocator(L, loc);
    return 1;
}

int Lua_ReferencesString(lua_State* L) {
    refdb::StatementLocator loc;
    loc.kind = refdb::StatementLocatorKind::ReferencesString;
    loc.string_arg = CheckStringArg(L, 1, "references_string", "s");
    PushLocator(L, loc);
    return 1;
}

int Lua_FirstReadOfCvar(lua_State* L) {
    refdb::StatementLocator loc;
    loc.kind = refdb::StatementLocatorKind::FirstReadOfCvar;
    loc.string_arg = CheckStringArg(L, 1, "first_read_of_cvar", "name");
    PushLocator(L, loc);
    return 1;
}

// The recognized matching{} key set — a typo'd key fails loud (never a silent
// drop of an author constraint, AP14).
const char* kMatchingKnown[] = {
    "kind", "callee", "condition_contains", "reads_cvar", "references_string",
};

// kcdx.locator.matching{ kind=, callee=, condition_contains=, reads_cvar=,
//                        references_string= } — any SUBSET, ANDed. An empty
// matching{} matches the first statement (no constraint).
int Lua_Matching(lua_State* L) {
    if (lua_type(L, 1) != LUA_TTABLE) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.locator.matching{...}: expects a single table of match keys "
            "(any subset of `kind`, `callee`, `condition_contains`, "
            "`reads_cvar`, `references_string`; an empty table matches the "
            "first statement). Call shape: "
            "kcdx.locator.matching{ kind = \"call\", callee = \"IsInCombat\" }.");
        return 2;
    }
    // Reject an unrecognized key before reading anything (fail loud).
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING) {
            const char* key = lua_tostring(L, -2);
            bool known = false;
            for (const char* k : kMatchingKnown) {
                if (std::string(key) == k) { known = true; break; }
            }
            if (!known) {
                std::string bad = key;
                lua_pop(L, 2);  // value + key
                lua_pushnil(L);
                lua_pushfstring(L,
                    "kcdx.locator.matching: unrecognized key '%s' — not a "
                    "recognized match key (kind / callee / condition_contains "
                    "/ reads_cvar / references_string). Check for a typo.",
                    bad.c_str());
                return 2;
            }
        }
        lua_pop(L, 1);  // pop value, keep key for lua_next
    }

    refdb::StatementLocator loc;
    loc.kind = refdb::StatementLocatorKind::Matching;

    lua_getfield(L, 1, "kind");
    if (lua_type(L, -1) == LUA_TSTRING) {
        loc.has_match_kind = true;
        loc.match_kind = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "callee");
    if (lua_type(L, -1) == LUA_TSTRING) {
        loc.has_match_callee = true;
        loc.match_callee = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "condition_contains");
    if (lua_type(L, -1) == LUA_TSTRING) {
        loc.has_match_condition_contains = true;
        loc.match_condition_contains = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "reads_cvar");
    if (lua_type(L, -1) == LUA_TSTRING) {
        loc.has_match_reads_cvar = true;
        loc.match_reads_cvar = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "references_string");
    if (lua_type(L, -1) == LUA_TSTRING) {
        loc.has_match_references_string = true;
        loc.match_references_string = lua_tostring(L, -1);
    }
    lua_pop(L, 1);

    PushLocator(L, loc);
    return 1;
}

// kcdx.locator.matching_pattern("48 8B C1 ...") — the LABELED expert raw-AOB
// hatch. NOT a statement-metadata locator: it carries the pattern for the byte
// path; handed to :resolve it returns found=false with reason
// "matching_pattern_not_statement_locator" (refdb's contract). cornerstones.md
// / AP12 — this is the expert escape hatch, never the common path.
int Lua_MatchingPattern(lua_State* L) {
    refdb::StatementLocator loc;
    loc.kind = refdb::StatementLocatorKind::MatchingPattern;
    loc.aob_pattern = CheckStringArg(L, 1, "matching_pattern", "aob");
    PushLocator(L, loc);
    return 1;
}

const luaL_Reg kFunctions[] = {
    {"function_entry", Lua_FunctionEntry},
    {"function_exit", Lua_FunctionExit},
    {"first_call_to", Lua_FirstCallTo},
    {"last_call_to", Lua_LastCallTo},
    {"call_to", Lua_CallTo},
    {"first_return", Lua_FirstReturn},
    {"last_return", Lua_LastReturn},
    {"return_value", Lua_ReturnValue},
    {"references_string", Lua_ReferencesString},
    {"first_read_of_cvar", Lua_FirstReadOfCvar},
    {"matching", Lua_Matching},
    {"matching_pattern", Lua_MatchingPattern},
    {nullptr, nullptr},
};

// Non-raising detector: is the value at `idx` a kcdx.locator.value userdata?
// Returns the StatementLocator* on a hit, nullptr otherwise (CheckLocator
// raises; this does not, so a hook/statement verb can do arg-type dispatch on
// its optional locator slot). Metatable-identity discriminator.
const refdb::StatementLocator* TestLocator(lua_State* L, int idx) {
    if (lua_type(L, idx) != LUA_TUSERDATA) return nullptr;
    void* p = lua_touserdata(L, idx);
    if (!p) return nullptr;
    if (!lua_getmetatable(L, idx)) return nullptr;
    luaL_getmetatable(L, kLocatorMetatable);
    const bool same = lua_rawequal(L, -1, -2) != 0;
    lua_pop(L, 2);
    return same ? static_cast<const refdb::StatementLocator*>(p) : nullptr;
}

// A short label for a locator kind, for the not-yet-wired teaching diagnostic.
const char* LocatorKindLabel(refdb::StatementLocatorKind k) {
    switch (k) {
        case refdb::StatementLocatorKind::FunctionEntry:    return "function_entry";
        case refdb::StatementLocatorKind::FunctionExit:     return "function_exit";
        case refdb::StatementLocatorKind::FirstCallTo:      return "first_call_to";
        case refdb::StatementLocatorKind::LastCallTo:       return "last_call_to";
        case refdb::StatementLocatorKind::CallTo:           return "call_to";
        case refdb::StatementLocatorKind::FirstReturn:      return "first_return";
        case refdb::StatementLocatorKind::LastReturn:       return "last_return";
        case refdb::StatementLocatorKind::ReturnValue:      return "return_value";
        case refdb::StatementLocatorKind::ReferencesString: return "references_string";
        case refdb::StatementLocatorKind::FirstReadOfCvar:  return "first_read_of_cvar";
        case refdb::StatementLocatorKind::Matching:         return "matching";
        case refdb::StatementLocatorKind::MatchingPattern:  return "matching_pattern";
    }
    return "locator";
}

}  // namespace

// Public arg-type-dispatch accessor for the hook/statement verbs' optional
// `[locator]` positional. function_entry() is the one kind the function-entry
// hook path already honors (it means "the entry" — the existing default); every
// other kind is a statement-level locator surfaced by kind_label for the
// not-yet-wired teaching error. Reads nothing beyond the metatable identity.
bool ReadLocator(lua_State* L, int idx, LocatorView& out) {
    const refdb::StatementLocator* loc = TestLocator(L, idx);
    if (!loc) return false;
    out.is_function_entry =
        (loc->kind == refdb::StatementLocatorKind::FunctionEntry);
    out.kind_label = LocatorKindLabel(loc->kind);
    return true;
}

// Called from lua_bind.cpp::RegisterKcdxTable with the kcdx global table on top
// of the stack. Registers the locator-value metatable, then creates the
// `locator` sub-table inside kcdx. Stack effect: 0.
void bind(lua_State* L) {
    SetupMetatable(L);

    int kcdx_idx = lua_gettop(L);
    lua_newtable(L);
    for (const luaL_Reg* f = kFunctions; f->name; ++f) {
        lua_pushcfunction(L, f->func);
        lua_setfield(L, -2, f->name);
    }
    lua_setfield(L, kcdx_idx, "locator");
}

}  // namespace kcdx::lua_bind_locator
