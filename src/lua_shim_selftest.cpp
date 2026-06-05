#include "lua_shim_selftest.h"

#include <cstdio>   // snprintf
#include <cstring>  // strcmp, memcmp, strlen

extern "C" {
#include "lua.h"    // LUA_TSTRING, lua_State
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
            "(false) — the plugin-surface gate the P5 kcdxLuaApi rewire reads "
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

    kcdx::test::EmitSummaryIfChanged("cap-79 lua-shim-forward");
}

}  // namespace kcdx::lua_shim
