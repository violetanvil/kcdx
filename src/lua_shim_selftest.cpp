#include "lua_shim_selftest.h"

#include <cstdio>   // snprintf
#include <cstring>  // strcmp, memcmp, strlen

extern "C" {
#include "lua.h"      // LUA_TSTRING/TNIL/TNUMBER/TTABLE, LUA_GLOBALSINDEX, lua_State
#include "lauxlib.h"  // luaL_Reg (the cap-79-stubs luaL_register row)
}

#include "hooks.h"  // kcdx::hooks::CurrentLuaState — the live VM the shim calls into
#include "log.h"
#include "lua_shim.h"
#include "test.h"

// cap-79 self-test — see lua_shim_selftest.h for why this lives in engine code.

namespace kcdx::lua_shim {

namespace {

constexpr const char* kRowRoundtrip = "cap-79-roundtrip";
constexpr const char* kRowGated     = "cap-79-internal-gated";
constexpr const char* kRowStubs     = "cap-79-stubs";
constexpr const char* kRowGcBarrier = "cap-79-gc-barrier";
constexpr const char* kRowLayout    = "cap-79-layout";
constexpr const char* kCategory     = "LUA_SHIM";

// The probe value: a fixed token (the "42" the brief names, carried in a string
// because lua_pushinteger is a SEAM member not wired this step — the round-trip
// rides lua_pushlstring, a RESOLVED id-30 forwarder).
constexpr const char* kProbeValue = "kcdx-shim-roundtrip-42";

}  // namespace

void RunSelfTestOnce() {
    static bool s_reported = false;
    if (s_reported) {
        return;
    }

    lua_State* L = ::kcdx::hooks::CurrentLuaState();
    if (L == nullptr) {
        // VM not captured yet (first lua_pcall has not fired) — retry next tick.
        // NOT a FAIL: the live state binds shortly after boot.
        return;
    }

    const LuaApi& api = g_api();

    char reason[512];

    // --- CAP-79-roundtrip ---------------------------------------------------
    // Guard the forwarded members we are about to call. A null member means the
    // forward layer is not wired for it — a real regression in this step,
    // reported as FAIL (not skipped), because these four are RESOLVED members
    // (ids 30 / 39 / 28 / 32), not seam. The round-trip uses settop(-2) to pop
    // the one slot it pushes (NOT lua_gettop, which is itself a seam member not
    // wired this step), so the live stack is restored without needing it.
    if (api.lua_pushlstring == nullptr || api.lua_tolstring == nullptr ||
        api.lua_type == nullptr || api.lua_settop == nullptr) {
        s_reported = true;
        std::snprintf(reason, sizeof(reason),
            "FAIL: a RESOLVED shim member is null — lua_pushlstring=%p "
            "lua_tolstring=%p lua_type=%p lua_settop=%p. The forward layer did "
            "not wire a seeded LUA_API symbol; Resolve() either did not run or "
            "mis-populated the table.",
            (void*)api.lua_pushlstring, (void*)api.lua_tolstring,
            (void*)api.lua_type, (void*)api.lua_settop);
        LOG_ERROR_KV(kCategory, "roundtrip_null_member",
            ::kcdx::log::KV("lua_pushlstring", (const void*)api.lua_pushlstring),
            ::kcdx::log::KV("lua_tolstring",  (const void*)api.lua_tolstring),
            ::kcdx::log::KV("lua_type",       (const void*)api.lua_type),
            ::kcdx::log::KV("lua_settop",     (const void*)api.lua_settop));
        kcdx::test::ReportResult(kRowRoundtrip, false, reason);
        // The gating row does not depend on the live VM — still evaluate it.
    } else {
        // Push the probe string through the shim, read it back through the
        // shim, assert byte-identity + type. We work above the current top by
        // pushing then popping exactly one slot, so the engine's live stack is
        // left exactly as found.
        const size_t probeLen = std::strlen(kProbeValue);
        api.lua_pushlstring(L, kProbeValue, probeLen);   // forwarded id 30
        const int   t   = api.lua_type(L, -1);            // forwarded id 28
        size_t      got = 0;
        const char* s   = api.lua_tolstring(L, -1, &got); // forwarded id 39

        const bool typeOk = (t == LUA_TSTRING);
        const bool lenOk  = (got == probeLen);
        const bool valOk  = (s != nullptr && got == probeLen &&
                             std::memcmp(s, kProbeValue, probeLen) == 0);

        api.lua_settop(L, -2);  // pop the probe slot — restore the live stack

        s_reported = true;  // the round-trip ran; one-shot regardless of outcome
        if (typeOk && lenOk && valOk) {
            std::snprintf(reason, sizeof(reason),
                "PASS: pushed \"%s\" (len=%zu) through shim lua_pushlstring and "
                "read it back identical via lua_tolstring (type=LUA_TSTRING, "
                "len matched, bytes matched). The forward layer reaches WHGame's "
                "compiled Lua and the resolved RVAs are the right functions.",
                kProbeValue, probeLen);
            LOG_INFO_KV(kCategory, "roundtrip_pass",
                ::kcdx::log::KV("probe", kProbeValue),
                ::kcdx::log::KV("len", (unsigned long long)probeLen));
            kcdx::test::ReportResult(kRowRoundtrip, true, reason);
        } else {
            std::snprintf(reason, sizeof(reason),
                "FAIL: shim round-trip mismatch — pushed \"%s\" (len=%zu); read "
                "back type=%d (want %d LUA_TSTRING), len=%zu, value=\"%s\". The "
                "forward layer reached the wrong function, or the resolved RVA "
                "is not lua_pushlstring/lua_tolstring on this build.",
                kProbeValue, probeLen, t, LUA_TSTRING, got,
                s ? s : "(null)");
            LOG_ERROR_KV(kCategory, "roundtrip_fail",
                ::kcdx::log::KV("probe", kProbeValue),
                ::kcdx::log::KV("type", (long long)t),
                ::kcdx::log::KV("want_type", (long long)LUA_TSTRING),
                ::kcdx::log::KV("got_len", (unsigned long long)got),
                ::kcdx::log::KV("got_value", s ? s : "(null)"));
            kcdx::test::ReportResult(kRowRoundtrip, false, reason);
        }
    }

    // --- CAP-79-internal-gated ----------------------------------------------
    // The gate must DISCRIMINATE: true for the four VM-lifecycle functions,
    // false for an ordinary member. A tautology gate (always-true or
    // always-false) fails one half and is caught here.
    const char* internalNames[] = {
        "lua_newstate", "lua_close", "lua_setallocf", "lua_atpanic"};
    const char* ordinaryNames[] = {
        "lua_pushlstring", "lua_pcall", "lua_settop"};

    bool allInternalGated = true;
    const char* firstUngatedInternal = nullptr;
    for (const char* n : internalNames) {
        if (!IsInternalOnly(n)) {
            allInternalGated = false;
            firstUngatedInternal = n;
            break;
        }
    }

    bool noOrdinaryGated = true;
    const char* firstMisgatedOrdinary = nullptr;
    for (const char* n : ordinaryNames) {
        if (IsInternalOnly(n)) {
            noOrdinaryGated = false;
            firstMisgatedOrdinary = n;
            break;
        }
    }

    if (allInternalGated && noOrdinaryGated) {
        std::snprintf(reason, sizeof(reason),
            "PASS: IsInternalOnly gates lua_newstate/lua_close/lua_setallocf/"
            "lua_atpanic (true) and passes lua_pushlstring/lua_pcall/lua_settop "
            "(false) — the plugin-surface gate the kcdxLuaApi rewire reads "
            "discriminates correctly (not a tautology).");
        LOG_INFO_KV(kCategory, "gating_pass",
            ::kcdx::log::KV("internal_gated", "4/4"),
            ::kcdx::log::KV("ordinary_passed", "3/3"));
        kcdx::test::ReportResult(kRowGated, true, reason);
    } else {
        std::snprintf(reason, sizeof(reason),
            "FAIL: internal-only gate does not discriminate — %s%s%s. A mod "
            "author could destroy/replace the game VM (if an internal slipped "
            "through) or a normal call is wrongly blocked.",
            firstUngatedInternal ? "internal NOT gated: " : "",
            firstUngatedInternal ? firstUngatedInternal :
                (firstMisgatedOrdinary ? "ordinary WRONGLY gated: " : ""),
            firstMisgatedOrdinary ? firstMisgatedOrdinary : "");
        LOG_ERROR_KV(kCategory, "gating_fail",
            ::kcdx::log::KV("first_ungated_internal",
                firstUngatedInternal ? firstUngatedInternal : "(none)"),
            ::kcdx::log::KV("first_misgated_ordinary",
                firstMisgatedOrdinary ? firstMisgatedOrdinary : "(none)"));
        kcdx::test::ReportResult(kRowGated, false, reason);
    }

    // --- CAP-79-stubs -------------------------------------------------------
    // Exercise representative seam STUB classes through g_api against the
    // live VM (L is non-null here — RunSelfTestOnce returns early above while it
    // is not yet captured). Each sub-check is falsifiable; the row FAILS if any
    // reads back wrong. The live state is restored to the entry depth at the end.
    {
        const bool stubsWired =
            api.lua_pushnil != nullptr && api.lua_pushboolean != nullptr &&
            api.lua_pushinteger != nullptr && api.lua_gettop != nullptr &&
            api.luaL_register != nullptr && api.lua_getfield != nullptr &&
            api.lua_settop != nullptr && api.lua_toboolean != nullptr &&
            api.lua_tointeger != nullptr;

        if (!stubsWired) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: a seam stub member is null — pushnil=%p "
                "pushboolean=%p pushinteger=%p gettop=%p luaL_register=%p. "
                "Resolve() did not wire the stub layer (WireStubs not called or "
                "a stub address is null).",
                (void*)api.lua_pushnil, (void*)api.lua_pushboolean,
                (void*)api.lua_pushinteger, (void*)api.lua_gettop,
                (void*)api.luaL_register);
            LOG_ERROR_KV(kCategory, "stubs_null_member",
                ::kcdx::log::KV("lua_pushnil", (const void*)api.lua_pushnil),
                ::kcdx::log::KV("lua_gettop", (const void*)api.lua_gettop),
                ::kcdx::log::KV("luaL_register",
                    (const void*)api.luaL_register));
            kcdx::test::ReportResult(kRowStubs, false, reason);
        } else {
            const int entryTop = api.lua_gettop(L);  // gettop stub: baseline

            // (a) pushnil → type LUA_TNIL.
            api.lua_pushnil(L);
            const bool nilOk = (api.lua_type(L, -1) == LUA_TNIL);

            // (b) pushboolean(1) → toboolean true.
            api.lua_pushboolean(L, 1);
            const bool boolOk = (api.lua_toboolean(L, -1) != 0);

            // (c) pushinteger(42) → type LUA_TNUMBER and tointeger == 42.
            api.lua_pushinteger(L, 42);
            const bool intTypeOk = (api.lua_type(L, -1) == LUA_TNUMBER);
            const bool intValOk  = (api.lua_tointeger(L, -1) == 42);

            // (d) gettop arithmetic: three values pushed since entryTop.
            const int afterTop = api.lua_gettop(L);
            const bool depthOk = (afterTop == entryTop + 3);

            // (e) luaL_register installs a named global table read back via
            // lua_getfield. An empty reg list (only the {NULL,NULL} sentinel)
            // installs the table without any functions — the table's presence is
            // the assertion. Register under a kcdx-private name so it cannot
            // collide with an engine library.
            static const luaL_Reg kEmptyReg[] = {{nullptr, nullptr}};
            api.luaL_register(L, "kcdx_shim_selftest_tbl", kEmptyReg);
            // luaL_register leaves the new lib table on the stack top.
            const bool regOnStackOk = (api.lua_type(L, -1) == LUA_TTABLE);
            // and it is reachable as a global of that name.
            api.lua_getfield(L, LUA_GLOBALSINDEX, "kcdx_shim_selftest_tbl");
            const bool regGlobalOk = (api.lua_type(L, -1) == LUA_TTABLE);

            // Restore the live stack to the entry depth (drop everything pushed:
            // nil, bool, int, the lib table left by luaL_register, and the
            // getfield result). settop(entryTop) trims back exactly.
            api.lua_settop(L, entryTop);

            const bool allOk = nilOk && boolOk && intTypeOk && intValOk &&
                               depthOk && regOnStackOk && regGlobalOk;
            if (allOk) {
                std::snprintf(reason, sizeof(reason),
                    "PASS: stub classes exercised against the live VM — "
                    "pushnil→nil, pushboolean→true, pushinteger(42)→TNUMBER+42, "
                    "gettop counted +3 from entry depth %d, luaL_register "
                    "installed a global table read back via getfield. The "
                    "kcdx-side stub bodies reach WHGame's live state correctly.",
                    entryTop);
                LOG_INFO_KV(kCategory, "stubs_pass",
                    ::kcdx::log::KV("entry_top", (long long)entryTop),
                    ::kcdx::log::KV("after_top", (long long)afterTop));
                kcdx::test::ReportResult(kRowStubs, true, reason);
            } else {
                std::snprintf(reason, sizeof(reason),
                    "FAIL: a stub class read back wrong — nil=%d bool=%d "
                    "intType=%d intVal=%d depth=%d(entry %d, after %d) "
                    "regOnStack=%d regGlobal=%d. A kcdx-side stub body does not "
                    "match the verified Lua semantics on this build.",
                    nilOk, boolOk, intTypeOk, intValOk, depthOk, entryTop,
                    afterTop, regOnStackOk, regGlobalOk);
                LOG_ERROR_KV(kCategory, "stubs_fail",
                    ::kcdx::log::KV("nil_ok", (long long)nilOk),
                    ::kcdx::log::KV("bool_ok", (long long)boolOk),
                    ::kcdx::log::KV("int_type_ok", (long long)intTypeOk),
                    ::kcdx::log::KV("int_val_ok", (long long)intValOk),
                    ::kcdx::log::KV("depth_ok", (long long)depthOk),
                    ::kcdx::log::KV("reg_on_stack_ok",
                        (long long)regOnStackOk),
                    ::kcdx::log::KV("reg_global_ok", (long long)regGlobalOk));
                kcdx::test::ReportResult(kRowStubs, false, reason);
            }
        }
    }

    // --- CAP-79-gc-barrier --------------------------------------------------
    // The GC-barrier-safety invariant: the barrier primitive (luaC_barrierf, id
    // 127) is resolved and available to any GC-pointer-writing stub. No seam
    // stub writes a GC pointer into a black object (the verified push bodies
    // write only stack slots, which need no barrier; the barrier-callers are
    // RESOLVED forwarders), so a live barrier-CALL is not constructible here —
    // the falsifiable row is the structural availability the design names. FAILS
    // if luaC_barrierf did not resolve.
    {
        const bool barrierOk = GcBarrierBacked();
        if (barrierOk) {
            std::snprintf(reason, sizeof(reason),
                "PASS: the GC write-barrier primitive luaC_barrierf (id 127) "
                "resolved by name and is backed — any GC-pointer-writing stub "
                "has its barrier available (the GC-barrier-safety invariant). No "
                "seam stub writes a GC pointer into a black object, so "
                "this asserts availability, not a live call.");
            LOG_INFO_KV(kCategory, "gc_barrier_pass",
                ::kcdx::log::KV("luaC_barrierf", "resolved"));
            kcdx::test::ReportResult(kRowGcBarrier, true, reason);
        } else {
            std::snprintf(reason, sizeof(reason),
                "FAIL: luaC_barrierf (id 127) did NOT resolve — a future "
                "GC-pointer-writing stub would write into a black GC object "
                "WITHOUT the barrier, letting the incremental GC free live "
                "objects. BindStubPrimitives should have bailed; this row is "
                "the live guard that it did not silently proceed.");
            LOG_ERROR_KV(kCategory, "gc_barrier_fail",
                ::kcdx::log::KV("luaC_barrierf", "unresolved"));
            kcdx::test::ReportResult(kRowGcBarrier, false, reason);
        }
    }

    // --- CAP-79-layout ------------------------------------------------------
    // The mainthread self-pointer invariant: G(L)->mainthread (g+0xB0) == L,
    // validated against the LIVE state. This is the falsifiable proof that
    // WHGame's lua_State/global_State layout still matches the verified offsets
    // every stub reads — a future game update that shifts a struct field fails
    // LOUD here, not silently in a stub's wrong-offset read.
    {
        const bool layoutOk = ValidateLayout(L);
        if (layoutOk) {
            std::snprintf(reason, sizeof(reason),
                "PASS: the mainthread self-pointer invariant holds against the "
                "live VM — G(L)->mainthread (g+0xB0) == L. The verified "
                "lua_State/global_State offsets the stubs read (l_G @ L+0x20, "
                "mainthread @ g+0xB0) match this game build; the stubs' field "
                "reads are correct.");
            LOG_INFO_KV(kCategory, "layout_pass",
                ::kcdx::log::KV("invariant", "mainthread_self_pointer"));
            kcdx::test::ReportResult(kRowLayout, true, reason);
        } else {
            std::snprintf(reason, sizeof(reason),
                "FAIL: the mainthread self-pointer invariant is BROKEN — "
                "G(L)->mainthread != L. WHGame's struct layout no longer matches "
                "the verified offsets (a game update shifted a field). Every "
                "stub reading these offsets is now wrong; kcdx must NOT touch the "
                "VM. See the LUA_SHIM layout_validate_* ERROR line for which "
                "field diverged.");
            LOG_ERROR_KV(kCategory, "layout_fail",
                ::kcdx::log::KV("invariant", "mainthread_self_pointer"));
            kcdx::test::ReportResult(kRowLayout, false, reason);
        }
    }

    kcdx::test::EmitSummaryIfChanged("cap-79 lua-shim");
}

}  // namespace kcdx::lua_shim
