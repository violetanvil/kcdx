#include "cap81_vm_adopt_selftest.h"

#include <atomic>
#include <cstdio>   // snprintf

extern "C" {
#include "lua.h"      // LUA_TTABLE, LUA_GLOBALSINDEX, lua_State
}

#include "hooks.h"        // CurrentLuaState — the live adopted state
#include "log.h"
#include "lua_shim.h"     // g_api (read the adopted state through WHGame's body) + ValidateLayout
#include "lua_vm_build.h" // BuiltState / InterceptFired — the keystone observables
#include "test.h"

// cap-81 keystone self-test — see cap81_vm_adopt_selftest.h for why this lives in
// engine code. Reads the LIVE adopted state via kcdx::hooks::CurrentLuaState and
// the kcdx-built state via lua_vm_build::BuiltState; asserts the engine adopted
// kcdx's state (one VM), the mainthread invariant holds on it, and both kcdx.*
// and CryEngine's own scripts live on it.

namespace kcdx::cap81_vm_adopt_selftest {

namespace {

constexpr const char* kRowSingle    = "cap-81-single-state";
constexpr const char* kRowMainthread = "cap-81-mainthread";
constexpr const char* kRowKcdxTables = "cap-81-kcdx-tables";
constexpr const char* kRowCryScript  = "cap-81-cryengine-script";
constexpr const char* kCategory      = "LUA_VM_BUILD";

// Read a named _G global's Lua type through the shim (WHGame's body — one Lua
// body, no second-VM ambiguity, read-only so no GC write / sentinel). Pushes
// _G.<name>, reads its type, pops it (settop(-2)) so the live stack is restored.
// Returns the Lua type tag (LUA_TNIL if absent), or -1 if a needed shim member
// is null.
int GlobalType(const kcdx::lua_shim::LuaApi& api, lua_State* L,
               const char* name) {
    if (api.lua_getfield == nullptr || api.lua_type == nullptr ||
        api.lua_settop == nullptr) {
        return -1;
    }
    api.lua_getfield(L, LUA_GLOBALSINDEX, name);  // push _G.<name>
    const int t = api.lua_type(L, -1);
    api.lua_settop(L, -2);                          // pop — restore the stack
    return t;
}

// Read _G.<outer>.<inner>'s type the same way (e.g. kcdx.log) — proves the
// sub-table is populated, not just that the outer table exists. Returns the inner
// type, LUA_TNIL if the outer is not a table / the inner is absent, or -1 on a
// null shim member.
int NestedType(const kcdx::lua_shim::LuaApi& api, lua_State* L,
               const char* outer, const char* inner) {
    if (api.lua_getfield == nullptr || api.lua_type == nullptr ||
        api.lua_settop == nullptr) {
        return -1;
    }
    api.lua_getfield(L, LUA_GLOBALSINDEX, outer);   // push _G.<outer>
    if (api.lua_type(L, -1) != LUA_TTABLE) {
        api.lua_settop(L, -2);
        return LUA_TNIL;
    }
    api.lua_getfield(L, -1, inner);                 // push _G.<outer>.<inner>
    const int t = api.lua_type(L, -1);
    api.lua_settop(L, -3);                          // pop both — restore the stack
    return t;
}

}  // namespace

void RunSelfTestOnce() {
    static bool s_reported = false;
    if (s_reported) {
        return;
    }

    lua_State* live = ::kcdx::hooks::CurrentLuaState();
    lua_State* built = ::kcdx::lua_vm_build::BuiltState();
    const bool interceptFired = ::kcdx::lua_vm_build::InterceptFired();

    // Retry-until-ready (NOT a FAIL): the live state binds at the first lua_pcall,
    // and the intercept fires at CScriptSystem::Init. Both happen shortly after
    // boot; report only once both have landed so the single-state comparison is
    // meaningful. A premature report would race the adoption and false-FAIL.
    if (live == nullptr || built == nullptr || !interceptFired) {
        return;
    }

    s_reported = true;
    const kcdx::lua_shim::LuaApi& api = kcdx::lua_shim::g_api();
    char reason[640];

    // --- CAP-81-single-state ------------------------------------------------
    // The intercept worked: one state, the engine adopted kcdx's. live==built
    // proves the engine's lua_pcall runs on the SAME state kcdx built — no
    // second VM. (built non-null + interceptFired are guaranteed by the gate
    // above, but stated in the reason for the audit trail.)
    {
        const bool oneState = (live == built);
        if (oneState) {
            std::snprintf(reason, sizeof(reason),
                "PASS: ONE lua_State — the engine adopted kcdx's. kcdx built "
                "L=%p on the worker; the lua_newstate intercept fired at "
                "CScriptSystem::Init (adoption path executed); the engine's "
                "live lua_pcall state == the kcdx-built state (no second VM). "
                "The dual-Lua hazard is killed by construction.",
                (void*)built);
            LOG_INFO_KV(kCategory, "cap81_single_state_pass",
                ::kcdx::log::KV("built", (const void*)built),
                ::kcdx::log::KV("live", (const void*)live));
            kcdx::test::ReportResult(kRowSingle, true, reason);
        } else {
            std::snprintf(reason, sizeof(reason),
                "FAIL: TWO lua_States — adoption did NOT take. kcdx built L=%p "
                "(intercept fired=%d) but the engine's live lua_pcall runs on a "
                "DIFFERENT state L=%p — the engine built its own second VM "
                "(the intercept returned the wrong state, or a fallback path "
                "captured the engine's state). The dual-Lua hazard is LIVE. See "
                "the MID_HOOK lua_pcall.divergent_L ERROR for the mismatch.",
                (void*)built, interceptFired ? 1 : 0, (void*)live);
            LOG_ERROR_KV(kCategory, "cap81_single_state_fail",
                ::kcdx::log::KV("built", (const void*)built),
                ::kcdx::log::KV("live", (const void*)live),
                ::kcdx::log::KV("intercept_fired", interceptFired ? 1 : 0));
            kcdx::test::ReportResult(kRowSingle, false, reason);
        }
    }

    // --- CAP-81-mainthread --------------------------------------------------
    // The mainthread self-pointer invariant on the ADOPTED state. ValidateLayout
    // logs the specific diverged field on failure (fail loud).
    {
        const bool layoutOk = kcdx::lua_shim::ValidateLayout(live);
        if (layoutOk) {
            std::snprintf(reason, sizeof(reason),
                "PASS: the mainthread self-pointer invariant holds on the "
                "ADOPTED state — G(L)->mainthread (g+0xB0) == L=%p. The state the "
                "engine adopted has the verified lua_State/global_State layout; "
                "the shim's offset reads are correct on it.",
                (void*)live);
            kcdx::test::ReportResult(kRowMainthread, true, reason);
        } else {
            std::snprintf(reason, sizeof(reason),
                "FAIL: the mainthread self-pointer invariant is BROKEN on the "
                "adopted state (L=%p) — G(L)->mainthread != L. The adopted "
                "state's layout does not match the verified offsets (a game "
                "update shifted a field, or adoption returned a wrong state). "
                "See the LUA_SHIM layout_validate_* ERROR for which field "
                "diverged.",
                (void*)live);
            kcdx::test::ReportResult(kRowMainthread, false, reason);
        }
    }

    // --- CAP-81-kcdx-tables -------------------------------------------------
    // The kcdx.* surface lives on the adopted state. _G.kcdx is a table AND a
    // populated sub-table (kcdx.log) is present — proves the surface registered,
    // not just an empty placeholder.
    {
        const int kcdxT = GlobalType(api, live, "kcdx");
        const int logT  = NestedType(api, live, "kcdx", "log");
        const bool tablesOk = (kcdxT == LUA_TTABLE && logT == LUA_TTABLE);
        if (tablesOk) {
            std::snprintf(reason, sizeof(reason),
                "PASS: the kcdx.* surface is present on the adopted state — "
                "_G.kcdx is a table and kcdx.log is a populated sub-table. The "
                "kcdx authoring surface lives on the ONE adopted VM.");
            kcdx::test::ReportResult(kRowKcdxTables, true, reason);
        } else {
            std::snprintf(reason, sizeof(reason),
                "FAIL: the kcdx.* surface is NOT present on the adopted state — "
                "type(_G.kcdx)=%d (want %d LUA_TTABLE), type(_G.kcdx.log)=%d "
                "(want %d). The kcdx surface never registered on the state the "
                "engine adopted (-1 = a shim read member is null; the surface "
                "registers on the live state at the first-tick bootstrap).",
                kcdxT, LUA_TTABLE, logT, LUA_TTABLE);
            LOG_ERROR_KV(kCategory, "cap81_kcdx_tables_fail",
                ::kcdx::log::KV("kcdx_type", (long long)kcdxT),
                ::kcdx::log::KV("kcdx_log_type", (long long)logT));
            kcdx::test::ReportResult(kRowKcdxTables, false, reason);
        }
    }

    // --- CAP-81-cryengine-script --------------------------------------------
    // CryEngine's own script bindings coexist on the adopted state. _G.System is
    // a table — CScriptSystem::Init ran luaL_openlibs + its registrars ON kcdx's
    // state (the narrow-intercept design's intent: the engine builds the rest of
    // the VM on our state). The observable that CryEngine's Lua scripts run on
    // the adopted VM.
    {
        const int systemT = GlobalType(api, live, "System");
        const bool cryOk = (systemT == LUA_TTABLE);
        if (cryOk) {
            std::snprintf(reason, sizeof(reason),
                "PASS: a CryEngine script path runs on the adopted state — "
                "_G.System is a table (CScriptSystem::Init ran luaL_openlibs + "
                "its extension registrars ON kcdx's state). CryEngine's own Lua "
                "scripts coexist on the ONE adopted VM.");
            kcdx::test::ReportResult(kRowCryScript, true, reason);
        } else {
            std::snprintf(reason, sizeof(reason),
                "FAIL: CryEngine scripts are NOT bound on the adopted state — "
                "type(_G.System)=%d (want %d LUA_TTABLE). The engine's "
                "CScriptSystem::Init did not bind its scripts to kcdx's state "
                "(adoption corrupted the boot sequence, or _G.System resolves "
                "elsewhere on this build; -1 = a shim read member is null).",
                systemT, LUA_TTABLE);
            LOG_ERROR_KV(kCategory, "cap81_cryengine_script_fail",
                ::kcdx::log::KV("system_type", (long long)systemT));
            kcdx::test::ReportResult(kRowCryScript, false, reason);
        }
    }

    kcdx::test::EmitSummaryIfChanged("cap-81 vm-adopt");
}

}  // namespace kcdx::cap81_vm_adopt_selftest
