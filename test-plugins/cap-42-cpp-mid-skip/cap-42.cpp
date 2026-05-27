// CAP-42 — kcdxHookInterface::Mid C++ return-skip parity.
//
// The C++ PEER of CAP-21-skip / CAP-21-run. cap-21 proves the Lua mid
// surface's run/skip (callback returns "skip"/nothing) by installing from
// plugin.lua via kcdx.hook{ mode="mid" }. There was NO C++ mid test until
// now — cap-42 is the FIRST consumer of kcdxHookInterface::Mid, and it
// specifically proves the kcdxHookInterface_Version 2 int-return: a C++ mid
// callback returns kcdxMidResult (Run=0 / Skip=1) to RUN or SKIP the captured
// instruction. Pre-feature (the void Mid ABI of v1) a C++ mid hook had no skip
// channel at all; this plugin is that channel's regression net.
//
// All-C++ DLL, NO plugin.lua, NO scripting interface — the install is via
// kcdxHookInterface::Mid from C++ (the whole point: proving the C++ surface).
//
// Like cap-21 this builds CONTROLLED machine code so the capture offset +
// register are deterministic regardless of the compiler. Each stub is
// `int fn(int seed)` (seed in ECX, MS x64 ABI):
//
//   +0:  89 C8           mov eax, ecx     ; eax = seed (zero-extends rax)
//   +2:  48 83 C0 64     add rax, 0x64    ; rax += 100   <-- CAPTURE/HOOK at +2
//   +6:  90              nop              ; padding for MinHook's 5-byte patch
//   +7:  90              nop
//   +8:  C3              ret
//
// The mid hook captures `rax` AT +2 (before the add), so rax == seed. Two
// distinct stubs/allocations so the two hooks don't interfere:
//   CAP-42-cpp-mid-skip  callback returns kcdxMidResult_Skip -> add NEVER runs
//                          -> fn(10) == 10  (THE return-skip proof)
//   CAP-42-cpp-mid-run   callback returns kcdxMidResult_Run  -> add runs
//                          -> fn(10) == 110 (control: 0-return does not skip)

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "kcdx/Interfaces.h"

namespace {

// Manifest bare name — must match [plugin].name in kcdx.toml.
const char* kName = "cap_42_cpp_mid_skip";

const kcdxInterface*      g_api   = nullptr;
const kcdxHookInterface*  g_hook  = nullptr;
kcdxPluginHandle          g_self  = kcdxInvalidPluginHandle;
kcdxTrampolineInterface*  g_tramp = nullptr;
kcdxLogger                g_log;

// One-shot guard so the InputLoaded backstop fires a loud FAIL only if the
// install path bailed at Load (no hook installed) — never leave a row PENDING.
bool g_installed_ok = false;

// The controlled stub: int fn(int seed) -> seed + 100. CAPTURE/HOOK at +2.
const unsigned char kStub[] = {
    0x89, 0xC8,                   // +0  mov eax, ecx
    0x48, 0x83, 0xC0, 0x64,       // +2  add rax, 0x64   (capture/hook here)
    0x90,                         // +6  nop
    0x90,                         // +7  nop
    0xC3,                         // +8  ret
};
constexpr int kMidOffset = 2;  // capture site (the `add rax`)

using StubFn = int (*)(int);

// Two distinct branch-pool allocations (distinct VAs; no ICF concern — these
// are runtime data, not linker-folded functions).
struct Stub { void* mem = nullptr; StubFn fn = nullptr; };
Stub g_skip, g_run;

// Allocate the stub from the kcdx BRANCH POOL (RWX within ±2 GB of WHGame.dll's
// .text), NOT raw VirtualAlloc(nullptr, ...). A real plugin hooks code inside
// loaded modules — always near other module code, inside MinHook's ±1 GB
// trampoline window. A raw-heap stub lands an ASLR-dependent distance away;
// MinHook's mid-install trampoline allocator (Mid -> InstallRuntime ->
// MH_CreateHook) then fails MH_ERROR_MEMORY_ALLOC when no free page is in range
// — flaky. Allocating near WHGame mirrors a real in-module target and is
// deterministic (cap-07 proves AllocateFromBranchPool returns rel32-reachable
// memory; the pool is ±2 GB of WHGame.dll .text). Same
// reasoning + same call as cap-21's AllocStub.
bool AllocStub(Stub& s) {
    if (!g_tramp) return false;
    s.mem = g_tramp->AllocateFromBranchPool(g_self, sizeof(kStub));
    if (!s.mem) return false;
    memcpy(s.mem, kStub, sizeof(kStub));
    FlushInstructionCache(GetCurrentProcess(), s.mem, sizeof(kStub));
    s.fn = reinterpret_cast<StubFn>(s.mem);
    return true;
}

// === The v2 int-return Mid callbacks =================================
//
// Mid callback ABI is `int cFn(kcdxHookCaptureValue* values, int count)`
// (kcdxHookInterface_Version >= 2). Return kcdxMidResult_Skip to skip the
// captured `add rax,0x64`; kcdxMidResult_Run (or 0) to let it run. Capture
// writes (values[i].value_*) would apply in BOTH cases — these callbacks read
// nothing and write nothing, so the run/skip RETURN is the sole variable.

extern "C" int Cap42_Skip_Cb(kcdxHookCaptureValue* values, int count) {
    (void)values; (void)count;
    return kcdxMidResult_Skip;   // skip the captured `add rax,0x64`
}

extern "C" int Cap42_Run_Cb(kcdxHookCaptureValue* values, int count) {
    (void)values; (void)count;
    return kcdxMidResult_Run;    // let the `add` run
}

// === Tiny per-row PASS/FAIL helper (cap-36 idiom) ====================

void Report(const char* row, bool pass, const char* reason) {
    if (pass) g_log.Info ("CAP42", "PASS %s: %s", row, reason);
    else      g_log.Error("CAP42", "FAIL %s: %s", row, reason);
    g_api->ReportTestResult(g_self, row, pass ? 1 : 0, reason);
}

// === Install one C++ mid hook at stub+kMidOffset capturing rax =======
//
// Raw-address Mid: target is null (the [advanced] raw-address locator); the
// capture site is opts.address. Mid needs NO signature for the raw-address
// capture form (Interfaces.h: "For the Mid sub-verb this may also be null").
// One positional rax:i64 capture (name=null) — proves the capture array is
// accepted even though these callbacks ignore the values.
bool InstallMid(const Stub& s, void* cb, const char* rowName) {
    static const kcdxHookCapture kCaps[] = { { "rax", "i64", nullptr } };  // positional
    kcdxHookOptions opts = {};
    opts.owningPlugin = g_self;
    opts.address      = reinterpret_cast<uintptr_t>(
                            reinterpret_cast<unsigned char*>(s.mem) + kMidOffset);
    opts.captures     = kCaps;
    opts.captureCount = 1;
    opts.name         = rowName;
    kcdxHookHandle h = g_hook->Mid(/*target=*/nullptr, cb, &opts);
    if (h == 0) {
        g_log.Error("CAP42",
            "Mid install for %s returned 0 (see HOOK_INTERFACE engine log "
            "for the teaching error)", rowName);
        return false;
    }
    return true;
}

// === InputLoaded — verify (after ApplyZone — cap-21's timing) ========
//
// The mid detours are LIVE by InputLoaded (it follows ApplyZone). Call each
// stub directly — the detour fires for any caller — and assert.

void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;

    if (!g_installed_ok) {
        // Install bailed at Load — report loud FAIL on both rows rather than
        // leaving them silent-PENDING (cap-29 backstop discipline).
        const char* reason =
            "C++ mid install did not complete at kcdxPlugin_Load (Hook/"
            "Trampoline QueryInterface null, branch-pool alloc null, or a "
            "Mid() install returned 0) — both rows FAIL via the backstop";
        Report("CAP-42-cpp-mid-skip", false, reason);
        Report("CAP-42-cpp-mid-run",  false, reason);
        return;
    }

    g_log.Info("CAP42", "InputLoaded — invoking mid-hooked stubs");

    char reason[256];

    int r_skip = g_skip.fn(10);
    const bool pass_skip = (r_skip == 10);
    snprintf(reason, sizeof(reason),
        "%s — Cap42 skip stub fn(10)=%d (expected 10; C++ mid callback "
        "returned kcdxMidResult_Skip so the captured `add rax,0x64` was "
        "SKIPPED, rax stays the seed). Pre-feature (void Mid ABI) this skip "
        "channel did not exist",
        pass_skip ? "C++ mid return-skip took effect"
                  : "C++ mid return-skip did NOT skip the add (expected 10)",
        r_skip);
    Report("CAP-42-cpp-mid-skip", pass_skip, reason);

    int r_run = g_run.fn(10);
    const bool pass_run = (r_run == 110);
    snprintf(reason, sizeof(reason),
        "%s — Cap42 run stub fn(10)=%d (expected 110; C++ mid callback "
        "returned kcdxMidResult_Run so the captured `add` RAN -> 10+100). "
        "Control: proves a 0-return does NOT spuriously skip",
        pass_run ? "C++ mid return-run let the add run"
                 : "C++ mid return-run did NOT run the add (expected 110)",
        r_run);
    Report("CAP-42-cpp-mid-run", pass_run, reason);
}

}  // namespace

// === kcdxPlugin_Load ==================================================

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    g_log  = kcdxLogger(api, g_self);
    g_log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)",
               api->kcdxVersion);

    g_hook = static_cast<const kcdxHookInterface*>(
        api->QueryInterface(kcdxInterface_Hook, kcdxHookInterface_Version));
    g_tramp = static_cast<kcdxTrampolineInterface*>(
        api->QueryInterface(kcdxInterface_Trampoline,
                            kcdxTrampolineInterface_Version));
    auto* messaging = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));

    if (messaging) {
        messaging->RegisterListener(g_self, nullptr, OnMessage);
    }

    if (!g_hook || !g_tramp) {
        g_log.Error("INIT",
            "QueryInterface(Hook v%u / Trampoline v%u) returned null — both "
            "rows will FAIL at InputLoaded", kcdxHookInterface_Version,
            kcdxTrampolineInterface_Version);
        if (!messaging) {
            // No backstop possible — report FAIL synchronously.
            api->ReportTestResult(g_self, "CAP-42-cpp-mid-skip", 0,
                "QueryInterface(Hook/Trampoline) null and no Messaging backstop");
            api->ReportTestResult(g_self, "CAP-42-cpp-mid-run", 0,
                "QueryInterface(Hook/Trampoline) null and no Messaging backstop");
        }
        return true;
    }

    // Branch-pool stubs so MinHook's mid-hook trampoline allocator finds a page
    // in range deterministically. A null alloc fails LOUD — no silent fall back
    // to VirtualAlloc (that would reintroduce the ASLR flakiness cap-21 fixes).
    if (!AllocStub(g_skip) || !AllocStub(g_run)) {
        g_log.Error("INIT", "AllocateFromBranchPool for a stub failed");
        return true;  // backstop reports both rows FAIL at InputLoaded
    }

    if (!InstallMid(g_skip, (void*)&Cap42_Skip_Cb, "cap42_mid_skip") ||
        !InstallMid(g_run,  (void*)&Cap42_Run_Cb,  "cap42_mid_run")) {
        return true;  // backstop reports both rows FAIL at InputLoaded
    }

    g_installed_ok = true;
    g_log.Info("INIT",
               "two C++ mid hooks installed; verify runs on InputLoaded");
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
