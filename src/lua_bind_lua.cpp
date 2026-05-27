// kcdx.lua.* — Lua-VM introspection helpers.
//
// Ships a single function: kcdx.lua.cfunction_address.
// More may follow (kcdx.lua.cfunction_for_address, _registry_dump, etc.)
// but this is the load-bearing one.
//
// What this enables: combined with kcdx.memory.dynamic_hook, pak Lua
// can now hook the C function backing a registered Lua callable.
//
//   local addr = kcdx.lua.cfunction_address(System.LogAlways)
//   kcdx.memory.dynamic_hook({
//       name   = "log_intercept",
//       target = addr,
//       ...
//   })
//
// Why it has to live on the C side: lua_tocfunction is a C-API-only
// function (the Lua-side `tostring(fn)` returns "function: 0x..."
// but that address is the lua_State-internal callable representation,
// not the underlying C function pointer). kcdx, sitting at the C
// side, can call lua_tocfunction; pak Lua cannot.

#include <cstdint>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "dev.h"
#include "log.h"
#include "lua_bind_helpers.h"
#include "lua_memory.h"

namespace kcdx::lua_bind_lua {

namespace {

// kcdx.lua.cfunction_address(fn) -> kcdx.memory.pointer userdata, or (nil, errmsg).
//
// Return type is `kcdx.memory.pointer` userdata, NOT an integer. This
// is mandatory on KCD2: CryEngine compiled Lua 5.1 with
// `LUA_NUMBER=float` (single-precision, 24-bit mantissa), so any value
// at pointer magnitude (~2^47) silently rounds to a 16MB grid when
// pushed through `lua_pushinteger`/`lua_pushnumber`. A C function
// pointer like 0x7FFD46781D00 round-trips to 0x7FFD46800000, which
// is then non-executable garbage. See `docs/lua-number-precision.md`
// for the probe results that pinned this and the rule that fell out
// of it: pointers must not round-trip through lua_Number.
//
// To hand the result to `kcdx.memory.dynamic_hook`, pass the userdata
// directly as `target` — it already understands pointer userdata.
//
//   local p = kcdx.lua.cfunction_address(System.LogAlways)
//   kcdx.memory.dynamic_hook({ name = "...", target = p, ... })
//
// If a plugin truly needs the integer form (e.g., for logging), call
// `p:get_address()` and accept the float-precision artifact.
int Lua_CFunctionAddress(lua_State* L) {
    if (!lua_iscfunction(L, 1)) {
        lua_pushnil(L);
        lua_pushliteral(L, "kcdx.lua.cfunction_address: argument 1 is "
                           "not a C function (lua_iscfunction returned false)");
        return 2;
    }
    lua_CFunction fn = lua_tocfunction(L, 1);
    if (!fn) {
        // Shouldn't reach here given the iscfunction check, but defensive.
        lua_pushnil(L);
        lua_pushliteral(L, "kcdx.lua.cfunction_address: lua_tocfunction "
                           "returned null");
        return 2;
    }
    uintptr_t  fn_addr   = (uintptr_t)fn;
    int        arg_type  = lua_type(L, 1);
    const void* arg_objp = lua_topointer(L, 1);

    KCDX_DEV("LUA", "CFUNCTION_ADDR/enter",
        kcdx::dev::KV("L",          (const void*)L),
        kcdx::dev::KV("arg_type",   arg_type),
        kcdx::dev::KV("arg_topointer", arg_objp),
        kcdx::dev::KV("tocfunction",   (const void*)fn));

    // Push as pointer userdata — sidesteps LUA_NUMBER=float precision loss.
    kcdx::lua_bind_helpers::PushPointer(
        L, kcdx::lua_memory::pointer(fn_addr));

    KCDX_DEV("LUA", "CFUNCTION_ADDR/pushed",
        kcdx::dev::KV("fn_addr",  (uintptr_t)fn_addr),
        kcdx::dev::KV("channel",  "kcdx.memory.pointer userdata"));
    return 1;
}

// kcdx.lua._probe_numbers() -> nil
//
// Dev-mode-only diagnostic. Pushes integers across the precision
// boundary through lua_pushinteger and reads them back via both
// lua_tointeger and lua_tonumber. Each probe logs to kcdx-dev.log
// under category LUA / NUMBER_PROBE/* so we can characterize
// CryEngine's Lua 5.1 numeric build (LUA_NUMBER width,
// LUA_INTEGER width, integer-subtype yes/no).
//
// Reason this is a probe instead of a constant patch: stock Lua
// 5.1 is double-based and the round-trip is exact for our pointer
// magnitudes; CryEngine has clearly modified the build. We need
// to know HOW it was modified before we pick a fix for the
// cfunction_address VA-truncation we hit at 0x7FFD46781D00.
int Lua_ProbeNumbers(lua_State* L) {
    KCDX_DEV("LUA", "NUMBER_PROBE/sizes",
        kcdx::dev::KV("sizeof_lua_Number",   (unsigned long long)sizeof(lua_Number)),
        kcdx::dev::KV("sizeof_lua_Integer",  (unsigned long long)sizeof(lua_Integer)),
        kcdx::dev::KV("sizeof_void_ptr",     (unsigned long long)sizeof(void*)),
        kcdx::dev::KV("sizeof_long_long",    (unsigned long long)sizeof(long long)));

    // Each probe: pushes input via lua_pushinteger, reads back via
    // both integer and number paths. Type code helps distinguish
    // integer-subtype (LUA_TNUMBER with integer flag) from plain
    // LUA_TNUMBER (float/double).
    auto probe_push_pull = [&](const char* label, uint64_t input) {
        lua_pushinteger(L, (lua_Integer)input);
        int          ty       = lua_type(L, -1);
        int          is_num   = lua_isnumber(L, -1);
        lua_Integer  back_i   = lua_tointeger(L, -1);
        lua_Number   back_n   = lua_tonumber(L, -1);
        KCDX_DEV("LUA", "NUMBER_PROBE/push_pull",
            kcdx::dev::KV("label",        label),
            kcdx::dev::KV("input_hex",    (uintptr_t)input),
            kcdx::dev::KV("input_dec",    (unsigned long long)input),
            kcdx::dev::KV("type",         ty),
            kcdx::dev::KV("isnumber",     is_num),
            kcdx::dev::KV("back_int_hex", (uintptr_t)back_i),
            kcdx::dev::KV("back_int_dec", (long long)back_i),
            kcdx::dev::KV("back_num",     (double)back_n));
        lua_pop(L, 1);
    };

    // Tier 1: small values that fit in every numeric encoding
    probe_push_pull("zero",            0ULL);
    probe_push_pull("one",             1ULL);
    probe_push_pull("0xDEADBEEF_32b",  0xDEADBEEFULL);     // 32-bit, classic
    probe_push_pull("0x7FFFFFFF_int32_max", 0x7FFFFFFFULL);
    probe_push_pull("0x80000000_int32_signbit", 0x80000000ULL);
    probe_push_pull("0xFFFFFFFF_uint32_max", 0xFFFFFFFFULL);

    // Tier 2: precision-boundary probes for IEEE 754 single (24-bit mantissa).
    // If LUA_NUMBER is float, the first one is exact, the second is not.
    probe_push_pull("two_pow_24",      (1ULL << 24));        // exact for float
    probe_push_pull("two_pow_24_plus_1", (1ULL << 24) + 1);  // first non-rep float

    // Tier 3: precision-boundary probes for IEEE 754 double (53-bit mantissa).
    probe_push_pull("two_pow_53",      (1ULL << 53));
    probe_push_pull("two_pow_53_plus_1", (1ULL << 53) + 1);

    // Tier 4: the actual pointer magnitude that's failing.
    // Real LuaDispatchShim address from our existing dev-log traces.
    // This MUST round-trip cleanly for cfunction_address to work.
    probe_push_pull("shim_hi_bits",    0x7FFD00000000ULL);   // top bits only
    probe_push_pull("shim_real",       0x7FFD46781D00ULL);   // actual shim
    probe_push_pull("shim_real_plus_1", 0x7FFD46781D01ULL);
    probe_push_pull("shim_aligned_16M", 0x7FFD46800000ULL);  // what we got back

    // Tier 5: number-path probes (lua_pushnumber direct).
    // Tells us whether the issue is in pushinteger->pushnumber
    // adapter vs in lua_Number itself.
    auto probe_num_push_pull = [&](const char* label, double input) {
        lua_pushnumber(L, (lua_Number)input);
        int          ty     = lua_type(L, -1);
        lua_Number   back_n = lua_tonumber(L, -1);
        lua_Integer  back_i = lua_tointeger(L, -1);
        KCDX_DEV("LUA", "NUMBER_PROBE/num_push_pull",
            kcdx::dev::KV("label",        label),
            kcdx::dev::KV("input_double", input),
            kcdx::dev::KV("type",         ty),
            kcdx::dev::KV("back_num",     (double)back_n),
            kcdx::dev::KV("back_int_hex", (uintptr_t)back_i));
        lua_pop(L, 1);
    };
    probe_num_push_pull("num_zero",       0.0);
    probe_num_push_pull("num_pi",         3.14159265358979323846);
    probe_num_push_pull("num_two_pow_24", (double)(1ULL << 24));
    probe_num_push_pull("num_two_pow_53", (double)(1ULL << 53));
    probe_num_push_pull("num_shim_real",  (double)0x7FFD46781D00ULL);

    // Tier 6: lightuserdata path — completely sidesteps numeric
    // encoding. If this round-trips clean, that's our escape hatch.
    {
        void* p = (void*)(uintptr_t)0x7FFD46781D00ULL;
        lua_pushlightuserdata(L, p);
        int   ty       = lua_type(L, -1);
        void* back     = lua_touserdata(L, -1);
        KCDX_DEV("LUA", "NUMBER_PROBE/lightud",
            kcdx::dev::KV("input_ptr",  p),
            kcdx::dev::KV("type",       ty),
            kcdx::dev::KV("back_ptr",   back));
        lua_pop(L, 1);
    }

    return 0;
}

const luaL_Reg kFunctions[] = {
    {"cfunction_address", Lua_CFunctionAddress},
    {"_probe_numbers",    Lua_ProbeNumbers},
    {nullptr, nullptr},
};

}  // namespace

// Called from lua_bind.cpp::RegisterKcdxTable, with the kcdx global
// table on top of the stack. Creates the `lua` sub-table inside it.
// Stack effect: 0.
void bind(lua_State* L) {
    int kcdx_idx = lua_gettop(L);
    lua_newtable(L);
    for (const luaL_Reg* f = kFunctions; f->name; ++f) {
        lua_pushcfunction(L, f->func);
        lua_setfield(L, -2, f->name);
    }
    lua_setfield(L, kcdx_idx, "lua");
}

}  // namespace kcdx::lua_bind_lua
