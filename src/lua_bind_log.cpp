// kcdx.log.* — Lua-side logging surface.
//
// The first surface built to the full authoring-sublanguage convention:
// a grouped capability domain
// (kcdx.log.<level>), positional "do a thing" args (category, message).
//
//   kcdx.log.info ("CATEGORY", "message")
//   kcdx.log.warn ("CATEGORY", "message")
//   kcdx.log.error("CATEGORY", "message")
//   kcdx.log.debug("CATEGORY", "message")   -- dev-mode only
//   kcdx.log.trace("CATEGORY", "message")   -- dev-mode only
//
// Args are positional and pre-formatted: the author does their own
// string building with Lua's string.format if they need it
//   kcdx.log.info("DMG", string.format("hit for %.1f", dmg))
// rather than the engine reimplementing printf marshaling across the
// Lua boundary. This keeps the everyday call trivial (the common case
// is a literal message) and avoids a format-string foot-gun.
//
// This mirrors the C++ author surface (the kcdxLogger struct in
// include/kcdx/Interfaces.h) per the one-model-two-languages rule:
// kcdx.log.info(cat, msg) <-> log.Info(cat, fmt, ...). Both route to
// kcdx::log.
//
// Until per-plugin attribution from a Lua call site is wired through to
// kcdx::log's per-plugin streams, these emit to the engine
// log with the author-supplied category. The category tag is the
// author's; the engine log already prefixes the source.

#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "log.h"

namespace kcdx::lua_bind_log {

namespace {

// Pull (category, message) from the Lua stack. category defaults to
// "LUA" when omitted; message defaults to empty. Both coerced via
// luaL_tolstring-style leniency: accept string or number.
void ReadArgs(lua_State* L, std::string& category, std::string& message) {
    category = "LUA";
    message  = "";
    if (lua_isstring(L, 1)) category = lua_tostring(L, 1);
    if (lua_isstring(L, 2)) message  = lua_tostring(L, 2);
    // One-arg convenience: kcdx.log.info("just a message") — treat the
    // sole string as the message under the default category.
    if (lua_gettop(L) == 1 && lua_isstring(L, 1)) {
        message  = lua_tostring(L, 1);
        category = "LUA";
    }
}

int Lua_Info(lua_State* L) {
    std::string c, m; ReadArgs(L, c, m);
    kcdx::log::EmitEngine(kcdx::log::Level::Info, c.c_str(), m.c_str());
    return 0;
}
int Lua_Warn(lua_State* L) {
    std::string c, m; ReadArgs(L, c, m);
    kcdx::log::EmitEngine(kcdx::log::Level::Warn, c.c_str(), m.c_str());
    return 0;
}
int Lua_Error(lua_State* L) {
    std::string c, m; ReadArgs(L, c, m);
    kcdx::log::EmitEngine(kcdx::log::Level::Error, c.c_str(), m.c_str());
    return 0;
}
int Lua_Debug(lua_State* L) {
    std::string c, m; ReadArgs(L, c, m);
    kcdx::log::EmitEngine(kcdx::log::Level::Debug, c.c_str(), m.c_str());
    return 0;
}
int Lua_Trace(lua_State* L) {
    std::string c, m; ReadArgs(L, c, m);
    kcdx::log::EmitEngine(kcdx::log::Level::Trace, c.c_str(), m.c_str());
    return 0;
}

const luaL_Reg kFunctions[] = {
    {"info",  Lua_Info},
    {"warn",  Lua_Warn},
    {"error", Lua_Error},
    {"debug", Lua_Debug},
    {"trace", Lua_Trace},
    {nullptr, nullptr},
};

}  // namespace

// Called from lua_bind.cpp::RegisterKcdxTable with the kcdx global table
// on top of the stack. Creates the `log` sub-table inside it. Stack
// effect: 0.
void bind(lua_State* L) {
    int kcdx_idx = lua_gettop(L);
    lua_newtable(L);
    for (const luaL_Reg* f = kFunctions; f->name; ++f) {
        lua_pushcfunction(L, f->func);
        lua_setfield(L, -2, f->name);
    }
    lua_setfield(L, kcdx_idx, "log");
}

}  // namespace kcdx::lua_bind_log
