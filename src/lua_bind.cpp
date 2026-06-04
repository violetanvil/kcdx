#include "lua_bind.h"

#include <atomic>

extern "C" {
#include "lua.h"
}

#include "dev.h"
#include "log.h"
#include "lua_memory.h"
#include "scripting.h"  // for LogLuaStateSnapshot (heap-corruption diag)
#include "messaging.h"

// kcdx.lua.* helpers live in lua_bind_lua.cpp. Forward-decl bind()
// here so RegisterKcdxTable can call it. (No header needed for a
// single function with this signature.)
namespace kcdx::lua_bind_lua   { void bind(lua_State* L); }
namespace kcdx::lua_bind_dev   { void bind(lua_State* L); }
namespace kcdx::lua_bind_test  { void bind(lua_State* L); }
namespace kcdx::lua_bind_bytes { void bind(lua_State* L); }
namespace kcdx::lua_bind_code  { void bind(lua_State* L); }
namespace kcdx::lua_bind_alias { void bind(lua_State* L); }
namespace kcdx::lua_bind_declare { void bind(lua_State* L); }
namespace kcdx::lua_bind_scan  { void bind(lua_State* L); }
namespace kcdx::lua_bind_hook  { void bind(lua_State* L); }
namespace kcdx::lua_bind_on      { void bind(lua_State* L); }
namespace kcdx::lua_bind_publish { void bind(lua_State* L); }
namespace kcdx::lua_bind_command { void bind(lua_State* L); }
namespace kcdx::lua_bind_cvar    { void bind(lua_State* L); }
namespace kcdx::lua_bind_cosave  { void bind(lua_State* L); }
namespace kcdx::lua_bind_log     { void bind(lua_State* L); }
namespace kcdx::lua_bind_addr  { void bind(lua_State* L); }
namespace kcdx::lua_bind_assets  { void bind(lua_State* L); }
namespace kcdx::lua_bind_plugin  { void bind(lua_State* L); }
namespace kcdx::zone_gate        { void bind(lua_State* L); }

// scripting_interface drains the queue of pending RegisterFunction
// calls into the live state. Pull the real header so the call
// reference resolves cleanly.
#include "scripting_interface.h"

namespace kcdx::lua_bind {

// Flipped by RegisterKcdxTable after _G.kcdx is populated + the
// kcdxMessage_LuaReady message is fired. Read by IsKcdxGlobalReady()
// (used by lua_bind_dev::Lua_OnReady to fast-path the
// already-ready case).
std::atomic<bool> g_kcdx_ready{false};

void RegisterKcdxTable(lua_State* L) {
    if (!L) {
        LOG_WARN("LUA_BIND", "RegisterKcdxTable called with null L");
        return;
    }
    LOG_INFO("LUA_BIND", "RegisterKcdxTable ENTER L=%p", (void*)L);
    kcdx::scripting::LogLuaStateSnapshot(L, "RegisterKcdxTable.enter");

    // Stack discipline: kcdx::lua_memory::bind() expects the kcdx
    // table at stack top. We create it, push it to top, populate via
    // bind(), then pop it after lua_setglobal.
    LOG_INFO("LUA_BIND", "  before kcdx (lowercase) table creation");
    kcdx::scripting::LogLuaStateSnapshot(L, "RegisterKcdxTable.before_newtable_kcdx");
    lua_newtable(L);
    kcdx::scripting::LogLuaStateSnapshot(L, "RegisterKcdxTable.after_newtable_kcdx");
    int kcdx_idx = lua_gettop(L);
    lua_pushliteral(L, "0.1.0");
    lua_setfield(L, kcdx_idx, "version");
    LOG_INFO("LUA_BIND", "  after  kcdx table creation; entering sub-binders");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_memory::bind");
    kcdx::lua_memory::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_memory::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_bind_lua::bind");
    kcdx::lua_bind_lua::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_bind_lua::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_bind_dev::bind");
    kcdx::lua_bind_dev::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_bind_dev::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_bind_test::bind");
    kcdx::lua_bind_test::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_bind_test::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_bind_bytes::bind");
    kcdx::lua_bind_bytes::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_bind_bytes::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_bind_code::bind");
    kcdx::lua_bind_code::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_bind_code::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_bind_alias::bind");
    kcdx::lua_bind_alias::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_bind_alias::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_bind_declare::bind");
    kcdx::lua_bind_declare::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_bind_declare::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_bind_scan::bind");
    kcdx::lua_bind_scan::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_bind_scan::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_bind_hook::bind");
    kcdx::lua_bind_hook::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_bind_hook::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_bind_on::bind");
    kcdx::lua_bind_on::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_bind_on::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_bind_publish::bind");
    kcdx::lua_bind_publish::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_bind_publish::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_bind_command::bind");
    kcdx::lua_bind_command::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_bind_command::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_bind_cvar::bind");
    kcdx::lua_bind_cvar::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_bind_cvar::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_bind_cosave::bind");
    kcdx::lua_bind_cosave::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_bind_cosave::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_bind_log::bind");
    kcdx::lua_bind_log::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_bind_log::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_bind_addr::bind");
    kcdx::lua_bind_addr::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_bind_addr::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_bind_assets::bind");
    kcdx::lua_bind_assets::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_bind_assets::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::lua_bind_plugin::bind");
    kcdx::lua_bind_plugin::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::lua_bind_plugin::bind");

    LOG_INFO("LUA_BIND", "    before kcdx::zone_gate::bind");
    kcdx::zone_gate::bind(L);
    LOG_INFO("LUA_BIND", "    after  kcdx::zone_gate::bind");

    LOG_INFO("LUA_BIND", "  before lua_setglobal(\"kcdx\")");
    lua_setglobal(L, "kcdx");
    LOG_INFO("LUA_BIND", "  after  lua_setglobal(\"kcdx\")");

    // Drain any kcdxScriptingInterface::RegisterFunction calls that
    // arrived from plugin DLLs during kcdxPlugin_Load (which happens
    // BEFORE this point — plugins load right after the version table
    // is parsed, but the kcdx global only gets created here). Any
    // further calls apply directly because g_table_ready flips true.
    LOG_INFO("LUA_BIND", "  before ApplyPendingToTable (plugin queued registrations)");
    kcdx::scripting_interface::ApplyPendingToTable(L);
    LOG_INFO("LUA_BIND", "  after  ApplyPendingToTable");

    // Mark ready BEFORE firing the message so a listener that checks
    // IsKcdxGlobalReady() during dispatch sees true.
    g_kcdx_ready.store(true, std::memory_order_release);

    KCDX_DEV("SCRIPTING", "GLOBAL_READY",
        kcdx::dev::KV("L", static_cast<const void*>(L)));

    LOG_INFO("LUA_BIND", "  before FireEngineMessage(LuaReady)");
    kcdx::messaging::FireEngineMessage(kcdxMessage_LuaReady);
    LOG_INFO("LUA_BIND", "  after  FireEngineMessage(LuaReady)");

    LOG_INFO("LUA_BIND", "RegisterKcdxTable EXIT");
}

bool IsKcdxGlobalReady() {
    return g_kcdx_ready.load(std::memory_order_acquire);
}

}  // namespace kcdx::lua_bind
