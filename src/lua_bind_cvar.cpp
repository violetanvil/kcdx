// kcdx.cvar.* — read a game CVar's value by name.
//
// A GROUPED capability DOMAIN (like kcdx.log.* / kcdx.console.*), NOT a
// top-level verb: kcdx.cvar.get_int / .get_bool / .get_float, each a "read a
// value" reader taking one positional string arg (the CVar name). A thin Lua
// binder over the engine cvar:: core (src/cvar.{h,cpp}, built in step 1) — the
// engine resolves the console + ICVar accessors by name; the author supplies
// only the CVar string they already hold (from a modding wiki, the in-game ~
// console, or a config). No address/offset/ABI ever crosses to the author —
// the disassembler test is satisfied by construction.
//
//   local p = kcdx.cvar.get_int("sys_pakPriority")  -- a Lua number, or nil
//   local v = kcdx.cvar.get_int("g_difficulty") or 0 -- nil-coalesce the miss
//   if kcdx.cvar.get_bool("e_shadows") then ... end
//   local f = kcdx.cvar.get_float("c_fov")
//
// Two distinct failure shapes (mirrors Lua_ConsolePrint in
// lua_bind_command.cpp):
//   * BAD ARGUMENT (name not a string / missing) -> (nil, teaching error
//     string), return 2. The call shape itself was wrong; teach the author.
//   * VALID CALL, CVar/surface MISS (cvar::GetInt/GetFloat returned false —
//     no such CVar, console not ready) -> a single nil, return 1. The call
//     shape was fine; the CVar simply has no value to give, so the author can
//     write `kcdx.cvar.get_int("x") or default`. This is NOT a silent-success
//     defect (anti-patterns.md): the miss is logged loud at the engine layer
//     (cvar:: logs every miss), and nil is an observable miss to the author —
//     never a silent fabricated 0. A second (nil, reason) return was weighed
//     and rejected: a CVar read is a value-or-nothing lookup whose idiomatic
//     Lua shape is `x or default`; a forced two-value return defeats that
//     ergonomic, and the human-facing reason already lands in the log.
//
// get_bool is computed HERE (cvar::GetInt then != 0) — there is intentionally
// NO cvar::GetBool in the engine module; a CVar's boolean reading is just its
// int reading being non-zero.
//
// Lua bridge (lua-bridge.md): raw Lua C API only — lua_pushcfunction +
// lua_setfield into a cvar sub-table on the kcdx global. No kcdx-side
// static-const sentinel; the frealloc canary stays zero.
//
// Lua precision (lua-precision.md): a CVar's INT value is a plain int (not a
// pointer / VA), pushed via lua_pushinteger — the established kcdx idiom for an
// integer return (see lua_bind_declare.cpp / lua_bind_scan.cpp). LUA_NUMBER is
// float in the CryEngine build, so an int value >= 2^24 rounds through Lua's
// number representation; CVar ints are small flags/modes/priorities in
// practice, well below that. A CVar's FLOAT value goes via lua_pushnumber.
// Neither value is an address, so the pointer-precision hazard does not apply.

#include "lua_bind_cvar.h"

extern "C" {
#include "lua.h"
}

#include "cvar.h"

namespace kcdx::lua_bind_cvar {

namespace {

// kcdx.cvar.get_int(name) -> number | nil
//
// On success pushes the CVar's int reading as a Lua number (return 1). On a bad
// arg pushes (nil, teaching error) (return 2). On a valid-call CVar/surface miss
// pushes a single nil (return 1) so the author can `... or default`.
int Lua_CvarGetInt(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.cvar.get_int(name): expects a single string argument — the "
            "CVar name to read (e.g. kcdx.cvar.get_int(\"sys_pakPriority\")). "
            "Returns the CVar's integer value, or nil if no such CVar exists "
            "yet — use `kcdx.cvar.get_int(name) or default`.");
        return 2;
    }
    const char* name = lua_tostring(L, 1);

    int value = 0;
    if (!kcdx::cvar::GetInt(name, &value)) {
        // Valid call, the CVar/surface just has no value (cvar:: logged the
        // miss). nil — not a fabricated 0 — so the miss is observable.
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(value));
    return 1;
}

// kcdx.cvar.get_bool(name) -> boolean | nil
//
// boolean = (cvar::GetInt(name) != 0). Same failure shapes as get_int.
// get_bool is computed at this binding layer — no cvar::GetBool exists.
int Lua_CvarGetBool(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.cvar.get_bool(name): expects a single string argument — the "
            "CVar name to read (e.g. kcdx.cvar.get_bool(\"e_shadows\")). "
            "Returns true if the CVar's integer value is non-zero, false if "
            "zero, or nil if no such CVar exists yet.");
        return 2;
    }
    const char* name = lua_tostring(L, 1);

    int value = 0;
    if (!kcdx::cvar::GetInt(name, &value)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushboolean(L, value != 0 ? 1 : 0);
    return 1;
}

// kcdx.cvar.get_float(name) -> number | nil
//
// On success pushes the CVar's float reading via lua_pushnumber. Same failure
// shapes as get_int.
int Lua_CvarGetFloat(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.cvar.get_float(name): expects a single string argument — the "
            "CVar name to read (e.g. kcdx.cvar.get_float(\"c_fov\")). Returns "
            "the CVar's float value, or nil if no such CVar exists yet — use "
            "`kcdx.cvar.get_float(name) or default`.");
        return 2;
    }
    const char* name = lua_tostring(L, 1);

    float value = 0.0f;
    if (!kcdx::cvar::GetFloat(name, &value)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, static_cast<lua_Number>(value));
    return 1;
}

}  // namespace

void bind(lua_State* L) {
    // kcdx.cvar.* — a GROUPED capability domain (NOT a top-level verb). Built
    // exactly like kcdx.console.* (lua_bind_command.cpp) / kcdx.log.*: a
    // lua_newtable, per-fn lua_pushcfunction/lua_setfield, then one
    // lua_setfield onto the kcdx table. The kcdx table stays at kcdx_idx
    // throughout (lua_setfield pops the sub-table it consumes), leaving the
    // stack exactly as the next sub-binder expects it — balanced.
    int kcdx_idx = lua_gettop(L);
    lua_newtable(L);
    lua_pushcfunction(L, Lua_CvarGetInt);
    lua_setfield(L, -2, "get_int");
    lua_pushcfunction(L, Lua_CvarGetBool);
    lua_setfield(L, -2, "get_bool");
    lua_pushcfunction(L, Lua_CvarGetFloat);
    lua_setfield(L, -2, "get_float");
    lua_setfield(L, kcdx_idx, "cvar");
}

}  // namespace kcdx::lua_bind_cvar
