// CAP-20 — kcdx.hook 4-mode chaining + arg/return mutation + wstr.
//
// Companion DLL for the first test of the NEW kcdx.hook surface
// (Phase 2b sub-4). It exposes one distinct NATIVE target per sub-test
// (so the sub-tests don't interfere — each MinHook detour sits on its
// own function, mirroring cap-04's target_a/b/c/d approach), hands their
// addresses to plugin.lua, and AFTER the hooks are applied (on
// kcdxMessage_InputLoaded, which now fires after ApplyZone) calls each
// target and asserts the hook changed the observed behavior.
//
// plugin.lua installs the hooks via kcdx.hook{ address=, <mode>= }; this
// DLL owns the targets + the expected-value assertions. The split is
// deliberate: kcdx.hook is the surface under test (driven from Lua); the
// DLL just provides deterministic targets + verification, using only
// restructured-model surfaces (no legacy behavior TOML).
//
// Targets + the transformation each sub-test's hook applies (the
// expected values are the contract shared with plugin.lua):
//   Add_Before(10)  before: seed+=1  -> original (s+100) = 111
//   Add_After(10)   after:  ret+=1000 -> 110 + 1000      = 1110
//   Add_Replace(10) replace returns 42 (original skipped)= 42
//   Add_Around(10)  around: 2 * orig(seed)               = 2*110 = 220
//   Add_Chain(10)   two before hooks (+1 then *2 by load order)
//                   load order: pri 10 does seed+=1 (->11), pri 20 does
//                   seed*=2 (->22), original +100 -> 122
//   WLen(L"abc")    before replaces arg with L"abcd"; WLen sees len 4
//   Add_Conflict(10) two replace hooks (=>7 then =>99) on one target;
//                   replace is exclusive in v1 so they can't coexist.
//                   Load order: the first (=>7) wins, the second (=>99)
//                   is REJECTED. Result is 7, NOT 99 — value-
//                   distinguishable proof the first won + second lost.

#include <windows.h>
#include <cwchar>
#include "kcdx/Interfaces.h"

namespace {
const char* kName = "ts_cap_20_hook_modes";
kcdxLogger  gLog;
const kcdxInterface* g_api = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
}  // namespace

// --- synthetic native targets (real C ABI; one per sub-test) ---------------
// noinline + volatile so the optimizer keeps a real, hookable body.
//
// CRITICAL: each target carries a UNIQUE volatile constant (`tag`) so the
// bodies are NOT byte-identical. Without this, MSVC's identical-COMDAT-
// folding (/OPT:ICF, on by default in Release) merges byte-identical
// functions to ONE address — every target would resolve to the same VA
// and all hooks would pile onto one shared chain. The tag defeats ICF.
// All targets still semantically return seed+100 (the tag is a discarded
// volatile read), so the test's expected values are unchanged.
#define TARGET(name, tag) extern "C" __declspec(noinline) int name(int seed) { \
    volatile int s = seed; volatile int unique = (tag); (void)unique; \
    return s + 100; }
TARGET(Cap20_Add_Before,   0x2001)
TARGET(Cap20_Add_After,    0x2002)
TARGET(Cap20_Add_Replace,  0x2003)
TARGET(Cap20_Add_Around,   0x2004)
TARGET(Cap20_Add_Chain,    0x2005)
TARGET(Cap20_Add_Conflict, 0x2006)
TARGET(Cap20_Add_Dyncall,  0x2007)  // for the dynamic_call round-trip test (unhooked)
#undef TARGET

extern "C" __declspec(noinline) int Cap20_WLen(const wchar_t* s) {
    if (!s) return -1;
    return static_cast<int>(wcslen(s));
}

// Hand each target's exact address to Lua as a lightuserdata (exact for
// pointer-magnitude VAs; PushInteger would truncate under
// LUA_NUMBER=float — see lua-precision.md).
#define ADDR_FN(luaName, target) \
    static int luaName(struct lua_State* L, void* ud) { \
        static_cast<const kcdxLuaApi*>(ud)->PushLightUserdata( \
            L, reinterpret_cast<void*>(&target)); \
        return 1; }
ADDR_FN(Lua_AddrBefore,   Cap20_Add_Before)
ADDR_FN(Lua_AddrAfter,    Cap20_Add_After)
ADDR_FN(Lua_AddrReplace,  Cap20_Add_Replace)
ADDR_FN(Lua_AddrAround,   Cap20_Add_Around)
ADDR_FN(Lua_AddrChain,    Cap20_Add_Chain)
ADDR_FN(Lua_AddrConflict, Cap20_Add_Conflict)
ADDR_FN(Lua_AddrDyncall,  Cap20_Add_Dyncall)
ADDR_FN(Lua_AddrWLen,     Cap20_WLen)
#undef ADDR_FN

namespace {

void Check(const char* sub, bool pass, const char* reasonPass,
           const char* reasonFail) {
    if (pass) {
        gLog.Info("VERIFY", "PASS %s: %s", sub, reasonPass);
        g_api->ReportTestResult(g_self, sub, 1, reasonPass);
    } else {
        gLog.Error("VERIFY", "FAIL %s: %s", sub, reasonFail);
        g_api->ReportTestResult(g_self, sub, 0, reasonFail);
    }
}

// Fires AFTER the kcdx.hook chain is applied (InputLoaded now follows
// ApplyZone). Call each target directly — the MinHook detour fires for
// any caller, including us — and assert the observed behavior matches
// the hook's contract.
void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    gLog.Info("VERIFY", "InputLoaded — invoking hooked targets");

    int r;
    r = Cap20_Add_Before(10);
    Check("CAP-20-before", r == 111,
          "before mutated arg 10->11; original +100 = 111",
          "before-arg-mutation did not take effect");

    r = Cap20_Add_After(10);
    Check("CAP-20-after", r == 1110,
          "after mutated return 110 -> 1110",
          "after-return-mutation did not take effect");

    r = Cap20_Add_Replace(10);
    Check("CAP-20-replace", r == 42,
          "replace returned 42; original skipped",
          "replace did not override the result");

    r = Cap20_Add_Around(10);
    Check("CAP-20-around", r == 220,
          "around wrapped: 2 * orig(10)=2*110 = 220",
          "around did not wrap the original correctly");

    r = Cap20_Add_Chain(10);
    Check("CAP-20-chain", r == 122,
          "two before hooks chained in load order: (10+1)*2=22; +100 = 122",
          "before-chain load-order composition wrong");

    r = Cap20_WLen(L"abc");
    Check("CAP-20-wstr", r == 4,
          "before replaced wstr 'abc'(3) with 'abcd'; WLen saw 4",
          "wstr arg read/mutate did not round-trip");

    r = Cap20_Add_Conflict(10);
    Check("CAP-20-conflict", r == 7,
          "first replace(=>7) won; second replace(=>99) rejected by load order",
          "conflict resolution wrong (expected first replace to win "
          "with 7; a different value means the second wrongly applied)");
    // The explicit 'second hook rejected' assertion (handle:applied()
    // ==false + reason) lands once the post-apply 'ready' lifecycle
    // event is wired (its own sub); the value contract (==7, not 99)
    // is the observable proof now.
}

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    gLog   = kcdxLogger(api, g_self);
    gLog.Info("INIT", "kcdxPlugin_Load called");

    auto* scripting = static_cast<kcdxScriptingInterface*>(
        api->QueryInterface(kcdxInterface_Scripting,
                            kcdxScriptingInterface_Version));
    auto* messaging = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    if (!scripting || !messaging) {
        gLog.Error("INIT", "QueryInterface(Scripting/Messaging) null");
        api->ReportTestResult(g_self, "CAP-20-before", 0,
            "QueryInterface returned null");
        return true;
    }

    void* luaApi = (void*)scripting->lua;
    bool ok = true;
    ok = scripting->RegisterFunction(g_self, "cap20", "addr_before",   Lua_AddrBefore,   luaApi) && ok;
    ok = scripting->RegisterFunction(g_self, "cap20", "addr_after",    Lua_AddrAfter,    luaApi) && ok;
    ok = scripting->RegisterFunction(g_self, "cap20", "addr_replace",  Lua_AddrReplace,  luaApi) && ok;
    ok = scripting->RegisterFunction(g_self, "cap20", "addr_around",   Lua_AddrAround,   luaApi) && ok;
    ok = scripting->RegisterFunction(g_self, "cap20", "addr_chain",    Lua_AddrChain,    luaApi) && ok;
    ok = scripting->RegisterFunction(g_self, "cap20", "addr_conflict", Lua_AddrConflict, luaApi) && ok;
    ok = scripting->RegisterFunction(g_self, "cap20", "addr_dyncall",  Lua_AddrDyncall,  luaApi) && ok;
    ok = scripting->RegisterFunction(g_self, "cap20", "addr_wlen",     Lua_AddrWLen,     luaApi) && ok;
    if (!ok) {
        gLog.Error("INIT", "RegisterFunction failed");
        api->ReportTestResult(g_self, "CAP-20-before", 0,
            "RegisterFunction(cap20.addr_*) failed");
        return true;
    }

    // CAP-20-addrname (sub-4b): the Address Library NAME locator, verified
    // at the RESOLVE layer. Resolving the readable name must equal
    // resolving the numeric id for the same entry — that equality IS the
    // name->address machinery the kcdx.hook `address_id = "name"` locator
    // relies on. Exact uintptr_t compare (no float loss); no live hook, so
    // no target-collision fragility. lua_pcall is id 1000 in the seed.
    {
        uintptr_t byName = api->ResolveAddressByName("lua_pcall");
        uintptr_t byId   = api->ResolveAddress(1000);
        bool pass = (byName != 0) && (byName == byId);
        char reason[160];
        if (pass) {
            _snprintf_s(reason, sizeof(reason), _TRUNCATE,
                "ResolveAddressByName('lua_pcall') == ResolveAddress(1000) "
                "== 0x%p (name locator resolves correctly)", (void*)byName);
        } else {
            _snprintf_s(reason, sizeof(reason), _TRUNCATE,
                "name/id resolve mismatch: byName=0x%p byId=0x%p",
                (void*)byName, (void*)byId);
        }
        gLog.Info("VERIFY", "CAP-20-addrname: %s", reason);
        api->ReportTestResult(g_self, "CAP-20-addrname", pass ? 1 : 0, reason);
    }

    messaging->RegisterListener(g_self, nullptr, OnMessage);
    gLog.Info("INIT", "targets registered; verify runs on InputLoaded");
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
