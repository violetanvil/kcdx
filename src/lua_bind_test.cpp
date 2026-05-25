// kcdx.test.* — Lua-side test-suite reporting surface.
//
// Pak Lua test plugins call kcdx.test.report(name, pass, reason) to
// record their pass/fail. Mirrors the C++ api->ReportTestResult call.
// Both feed the same aggregator (src/test.h) and the same kcdx.log
// "suite: X/Y passing as of <message>" roll-up.

#include <cstdint>
#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "test.h"

namespace kcdx::lua_bind_test {

namespace {

// kcdx.test.report(name, pass, reason) -> nil
//
// Arguments:
//   name   (string)  : matrix row ID (e.g. "CAP-05")
//   pass   (boolean) : true = passing, false = failing
//   reason (string)  : optional, one-sentence explanation
int Lua_Report(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TBOOLEAN);
    bool pass = lua_toboolean(L, 2) != 0;
    const char* reason = "";
    if (lua_gettop(L) >= 3 && lua_isstring(L, 3)) {
        reason = lua_tostring(L, 3);
    }
    kcdx::test::ReportResult(name, pass, reason);
    return 0;
}

const luaL_Reg kFunctions[] = {
    {"report", Lua_Report},
    {nullptr, nullptr},
};

}  // namespace

// Called from lua_bind.cpp::RegisterKcdxTable, with the kcdx global table
// on top of the stack. Creates the `test` sub-table inside it. Stack
// effect: 0.
void bind(lua_State* L) {
    int kcdx_idx = lua_gettop(L);
    lua_newtable(L);
    for (const luaL_Reg* f = kFunctions; f->name; ++f) {
        lua_pushcfunction(L, f->func);
        lua_setfield(L, -2, f->name);
    }
    lua_setfield(L, kcdx_idx, "test");
}

}  // namespace kcdx::lua_bind_test
