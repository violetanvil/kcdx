// CAP-22 — kcdx.hook mode="callsite": redirect ONE call instruction.
//
// Companion DLL for the first test of the NEW kcdx.hook callsite surface
// (Phase 2b sub-6). mode="callsite" rewrites the rel32 displacement of
// ONE specific E8 near-call so only THAT caller is redirected through the
// hook chain; every other caller of the same callee is untouched. That
// isolation is the whole point of callsite vs function-entry, and it is
// what this test proves.
//
// Shape (mirrors cap-20: surface driven from plugin.lua, targets +
// assertions owned here; verify on InputLoaded, after ApplyZone):
//
//   Cap22_Helper(x)            the SHARED callee: returns x + 100.
//   Cap22_Caller_Before(s)     returns Helper(s)  -- its E8 is redirected
//   Cap22_Caller_After(s)      returns Helper(s)  -- its E8 is redirected
//   Cap22_Caller_Around(s)     returns Helper(s)  -- its E8 is redirected
//   Cap22_Caller_Replace(s)    returns Helper(s)  -- its E8 is redirected
//   Cap22_Caller_Control(s)    returns Helper(s)  -- NEVER hooked
//
// All five callers call the SAME Helper. plugin.lua installs a callsite
// redirect on each of the four redirected callers' E8 site (NOT on
// Helper). The redirects, value-distinguishable:
//   before  : Helper sees s+1   -> (s+1)+100         (Caller_Before(10)=111)
//   after   : Helper's ret +1000 -> (s+100)+1000     (Caller_After(10)=1110)
//   around  : 2 * orig(s)        -> 2*(s+100)        (Caller_Around(10)=220)
//   replace : returns 42, Helper NOT called from here(Caller_Replace(10)=42)
//   control : unchanged          -> s+100            (Caller_Control(10)=110)
//
// The control assertion (110) AND a DIRECT Helper(10)==110 assertion both
// prove the callee itself is untouched — only the four redirected call
// sites change. That is callsite isolation.
//
// Exposing the EXACT call-site to Lua: each caller is __declspec(noinline)
// and makes a REAL cross-function call to Helper, so a genuine E8 rel32
// exists in its body (verified at runtime: we scan and assert). A unique
// volatile tag per caller defeats MSVC identical-COMDAT-folding (/OPT:ICF)
// so the five callers stay distinct functions. The DLL scans each caller's
// first bytes for the E8 whose computed target == &Helper, converts that
// instruction's VA to a module-relative RVA, and hands Lua the
// `target_callsite = { rva = "cap-22.dll @ rva 0x..." }` locator string —
// exercising the rva escape-hatch locator (locked design #3: the callsite
// locator is pattern | address_id | rva). The callers live in cap-22.dll,
// a loaded module, so ResolveCallsite's rva path (OpenModule + base + rva)
// resolves them deterministically.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "kcdx/Interfaces.h"

namespace {
const char* kName = "ts_cap_22_callsite_redirect";
kcdxLogger  gLog;
const kcdxInterface* g_api = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
}  // namespace

// --- shared callee -----------------------------------------------------------
// noinline so the callers make a real E8 call to it (not an inlined add).
extern "C" __declspec(noinline) int Cap22_Helper(int x) {
    volatile int v = x;
    return v + 100;
}

// --- callers (one redirected E8 site each + one control) ---------------------
// Each makes a REAL call to Cap22_Helper (so a real E8 rel32 exists in the
// body), is noinline (so the call survives), and carries a UNIQUE volatile
// tag (so /OPT:ICF can't fold the byte-identical bodies to one address —
// same defense cap-20 uses).
//
// CRITICAL: the call must NOT be tail-call-optimized into a `jmp Helper`
// (opcode E9) — the callsite redirect rewrites an E8 near-call, and the
// E8-scan + the engine's opcode check both require an E8. We defeat the
// tail call by USING the result after the call (`+ unique - unique`, a
// no-op that still forces the call to be non-tail so MSVC emits
// `call Helper` (E8) + a following `ret`, not `jmp Helper`). The
// `+ unique - unique` nets to zero, so every caller still returns
// Helper(seed); the contract values above hold.
#define CALLER(name, tag) \
    extern "C" __declspec(noinline) int name(int seed) { \
        volatile int unique = (tag); \
        int r = Cap22_Helper(seed); \
        return r + unique - unique; }
CALLER(Cap22_Caller_Before,  0x2201)
CALLER(Cap22_Caller_After,   0x2202)
CALLER(Cap22_Caller_Around,  0x2203)
CALLER(Cap22_Caller_Replace, 0x2204)
CALLER(Cap22_Caller_Control, 0x2205)
#undef CALLER

namespace {

// Find the E8 near-call inside `fn` whose computed target == &Cap22_Helper.
// Scans up to `window` bytes from the function entry. Returns the VA of the
// E8 instruction, or 0 if none found (which would fail the test loudly).
// This is a deterministic locate of the exact call site to redirect.
uintptr_t FindHelperCallSite(void* fn, size_t window = 64) {
    const uint8_t* base = reinterpret_cast<const uint8_t*>(fn);
    const uintptr_t helper = reinterpret_cast<uintptr_t>(&Cap22_Helper);
    for (size_t i = 0; i + 5 <= window; ++i) {
        if (base[i] != 0xE8) continue;
        int32_t disp = 0;
        memcpy(&disp, base + i + 1, 4);
        const uintptr_t site = reinterpret_cast<uintptr_t>(base + i);
        const uintptr_t target =
            site + 5 + static_cast<uintptr_t>(static_cast<int64_t>(disp));
        if (target == helper) return site;
    }
    return 0;
}

// Cache the located call-site VAs (computed once at load).
uintptr_t g_siteBefore = 0, g_siteAfter = 0, g_siteAround = 0,
          g_siteReplace = 0;

// This DLL's own module base, for converting a located call-site VA into
// the module-relative RVA the `target_callsite = { rva = "<module> @ rva
// 0x..." }` locator consumes. The callers live in cap-22.dll (a loaded
// module), so ResolveCallsite's rva path (OpenModule + base + rva)
// resolves them. This exercises the rva escape-hatch locator form
// (locked design #3: pattern | address_id | rva — all three supported).
uintptr_t g_moduleBase = 0;

// Hand a located call-site as the rva-locator STRING to Lua:
//   "cap-22.dll @ rva 0x<offset>"
// (the rva form the callsite locator parses). Built from the site VA minus
// this module's base. The hex offset is non-zero (the E8 is past the
// function entry, which is past the module base).
char g_rvaBefore[64], g_rvaAfter[64], g_rvaAround[64], g_rvaReplace[64];

void BuildRvaString(char* out, size_t cap, uintptr_t siteVa) {
    const uintptr_t rva = siteVa - g_moduleBase;
    _snprintf_s(out, cap, _TRUNCATE, "cap-22.dll @ rva 0x%llx",
                (unsigned long long)rva);
}

#define SITE_FN(luaName, rvaVar) \
    static int luaName(struct lua_State* L, void* ud) { \
        static_cast<const kcdxLuaApi*>(ud)->PushString(L, rvaVar); \
        return 1; }
SITE_FN(Lua_SiteBefore,  g_rvaBefore)
SITE_FN(Lua_SiteAfter,   g_rvaAfter)
SITE_FN(Lua_SiteAround,  g_rvaAround)
SITE_FN(Lua_SiteReplace, g_rvaReplace)
#undef SITE_FN

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

// Fires AFTER the callsite redirects are applied (InputLoaded follows
// ApplyZone). Call each caller directly and assert the redirected call
// site changed THAT caller's observed behavior, while the control caller
// and a direct Helper call are UNAFFECTED.
void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    gLog.Info("VERIFY", "InputLoaded — invoking callsite-redirected callers");

    int r;
    r = Cap22_Caller_Before(10);
    Check("CAP-22-before", r == 111,
          "callsite before: Helper saw 10->11; +100 = 111",
          "callsite before-arg-mutation did not take effect at the site");

    r = Cap22_Caller_After(10);
    Check("CAP-22-after", r == 1110,
          "callsite after: Helper ret 110 -> 1110",
          "callsite after-return-mutation did not take effect at the site");

    r = Cap22_Caller_Around(10);
    Check("CAP-22-around", r == 220,
          "callsite around: 2 * orig(10)=2*110 = 220",
          "callsite around did not wrap the call correctly");

    r = Cap22_Caller_Replace(10);
    Check("CAP-22-replace", r == 42,
          "callsite replace: returned 42; Helper not called from this site",
          "callsite replace did not override the call result");

    // ISOLATION — the control caller calls the SAME Helper but its E8 was
    // never redirected, so it must be unchanged.
    r = Cap22_Caller_Control(10);
    Check("CAP-22-control-unaffected", r == 110,
          "control caller of the SAME Helper is unchanged (110) — proves "
          "callsite redirect is per-call-site, not per-callee",
          "control caller changed — the redirect leaked to other callers "
          "of the shared callee (callsite isolation broken)");

    // ISOLATION — a DIRECT Helper call must also be unchanged (the callee
    // itself was never hooked; only four call sites were rewritten).
    r = Cap22_Helper(10);
    Check("CAP-22-callee-unaffected", r == 110,
          "direct Helper(10) is unchanged (110) — the callee is untouched; "
          "only the call sites were rewritten",
          "direct Helper call changed — the callee was hooked, not just "
          "the call site (callsite isolation broken)");
}

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    gLog   = kcdxLogger(api, g_self);
    gLog.Info("INIT", "kcdxPlugin_Load called");

    // This DLL's load base (for site VA -> module RVA conversion).
    g_moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"cap-22.dll"));
    if (!g_moduleBase) {
        // Fall back to the module that contains this code (covers a rename).
        HMODULE self = nullptr;
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&Cap22_Helper), &self);
        g_moduleBase = reinterpret_cast<uintptr_t>(self);
    }

    // Locate each redirected caller's E8 call-to-Helper site. If any fails
    // to locate (optimizer inlined the call, or no E8 in the window), the
    // call-site locator can't be handed to Lua — report FAIL loudly rather
    // than silently install nothing.
    g_siteBefore  = FindHelperCallSite((void*)&Cap22_Caller_Before);
    g_siteAfter   = FindHelperCallSite((void*)&Cap22_Caller_After);
    g_siteAround  = FindHelperCallSite((void*)&Cap22_Caller_Around);
    g_siteReplace = FindHelperCallSite((void*)&Cap22_Caller_Replace);
    if (!g_moduleBase || !g_siteBefore || !g_siteAfter || !g_siteAround ||
        !g_siteReplace) {
        gLog.Error("INIT", "could not locate an E8 call-to-Helper in a "
                   "caller body (before=0x%p after=0x%p around=0x%p "
                   "replace=0x%p)", (void*)g_siteBefore, (void*)g_siteAfter,
                   (void*)g_siteAround, (void*)g_siteReplace);
        api->ReportTestResult(g_self, "CAP-22-before", 0,
            "no E8 call-to-Helper found in a caller body (compiler inlined "
            "the call or folded the function), or module base unresolved");
        return true;
    }

    // Build the rva-locator strings handed to Lua.
    BuildRvaString(g_rvaBefore,  sizeof(g_rvaBefore),  g_siteBefore);
    BuildRvaString(g_rvaAfter,   sizeof(g_rvaAfter),   g_siteAfter);
    BuildRvaString(g_rvaAround,  sizeof(g_rvaAround),  g_siteAround);
    BuildRvaString(g_rvaReplace, sizeof(g_rvaReplace), g_siteReplace);

    auto* scripting = static_cast<kcdxScriptingInterface*>(
        api->QueryInterface(kcdxInterface_Scripting,
                            kcdxScriptingInterface_Version));
    auto* messaging = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    if (!scripting || !messaging) {
        gLog.Error("INIT", "QueryInterface(Scripting/Messaging) null");
        api->ReportTestResult(g_self, "CAP-22-before", 0,
            "QueryInterface returned null");
        return true;
    }

    void* luaApi = (void*)scripting->lua;
    bool ok = true;
    ok = scripting->RegisterFunction(g_self, "cap22", "site_before",  Lua_SiteBefore,  luaApi) && ok;
    ok = scripting->RegisterFunction(g_self, "cap22", "site_after",   Lua_SiteAfter,   luaApi) && ok;
    ok = scripting->RegisterFunction(g_self, "cap22", "site_around",  Lua_SiteAround,  luaApi) && ok;
    ok = scripting->RegisterFunction(g_self, "cap22", "site_replace", Lua_SiteReplace, luaApi) && ok;
    if (!ok) {
        gLog.Error("INIT", "RegisterFunction failed");
        api->ReportTestResult(g_self, "CAP-22-before", 0,
            "RegisterFunction(cap22.site_*) failed");
        return true;
    }

    messaging->RegisterListener(g_self, nullptr, OnMessage);
    gLog.Info("INIT", "call sites located; verify runs on InputLoaded "
              "(before=0x%p after=0x%p around=0x%p replace=0x%p)",
              (void*)g_siteBefore, (void*)g_siteAfter, (void*)g_siteAround,
              (void*)g_siteReplace);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
