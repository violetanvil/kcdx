// CAP-21 — kcdx.hook mode="mid": capture read/write + run/skip.
//
// Companion DLL for the FIRST test of the NEW kcdx.hook mid surface.
// mode=mid intercepts ONE instruction inside a function
// and reads/writes named register/memory captures; the callback returns
// "skip" (or true) to skip the captured instruction, or returns nothing
// to let it run.
//
// Like cap-04 (the legacy [[mid_hook]] test) this builds CONTROLLED
// machine code so the capture offset + register are deterministic
// regardless of the compiler — but unlike cap-04 it uses ONLY
// restructured surfaces: the stub is hand-written into RWX memory here
// (kcdx.hook is the surface under test, driven from plugin.lua), no
// legacy [[trampoline]]/[[mid_hook]] TOML.
//
// Each stub is `int fn(int seed)` (seed in ECX, MS x64 ABI):
//
//   +0:  89 C8           mov eax, ecx     ; eax = seed (zero-extends rax)
//   +2:  48 83 C0 64     add rax, 0x64    ; rax += 100   <-- HOOK at +2
//   +6:  90              nop              ; padding for MinHook's 5-byte patch
//   +7:  90              nop
//   +8:  C3              ret
//
// The mid hook captures `rax` AT +2 (before the add executes), so rax ==
// seed. The four sub-tests, all hooking at offset +2 capturing rax:
//   CAP-21-read   callback asserts c.rax:get()==10, returns nothing
//                   -> add runs -> 110
//   CAP-21-write  callback c.rax:set(1000), returns nothing
//                   -> add runs on 1000 -> 1100
//   CAP-21-skip   callback returns "skip" -> add NEVER runs -> rax==seed
//                   -> 10
//   CAP-21-run    callback returns nothing -> add runs -> 110
//                 (control: same as read but no capture assertion, proves
//                  return-nothing always runs the captured instruction)

#include <windows.h>
#include "kcdx/Interfaces.h"

namespace {
const char* kName = "cap_21_mid_hook";
kcdxLogger  gLog;
const kcdxInterface*      g_api   = nullptr;
kcdxPluginHandle          g_self  = kcdxInvalidPluginHandle;
kcdxTrampolineInterface*  g_tramp = nullptr;

// The controlled stub: int fn(int seed) -> seed + 100. HOOK at +2.
const unsigned char kStub[] = {
    0x89, 0xC8,                   // +0  mov eax, ecx
    0x48, 0x83, 0xC0, 0x64,       // +2  add rax, 0x64   (hook here)
    0x90,                         // +6  nop
    0x90,                         // +7  nop
    0xC3,                         // +8  ret
};
constexpr int kMidOffset = 2;  // capture site (the `add rax`)

using StubFn = int (*)(int);

// MEMORY-capture stub: int fn(int* slot) -> *slot + 100. HOOK at +0, capturing
// the memory expr `[rcx]` (rcx = the int* arg, MS x64 ABI). The callback writes
// a new value through `[rcx]:set(v)`; the captured `mov eax,[rcx]` then RE-runs
// and reads the mutated value, so the add lands on v. Proves the new Context64
// memory-deref WRITEBACK machinery (F4-F9), not just the F1 GPR form.
//
//   +0:  8B 01           mov eax, [rcx]   ; eax = *slot   <-- HOOK, capture [rcx]
//   +2:  48 83 C0 64     add rax, 0x64    ; rax += 100
//   +6:  90 90           nop nop
//   +8:  C3              ret
const unsigned char kMemStub[] = {
    0x8B, 0x01,                   // +0  mov eax, [rcx]   (hook here, capture [rcx])
    0x48, 0x83, 0xC0, 0x64,       // +2  add rax, 0x64
    0x90, 0x90,                   // +6  nop nop
    0xC3,                         // +8  ret
};
constexpr int kMemMidOffset = 0;  // capture site (the `mov eax,[rcx]`)
using MemStubFn = int (*)(int*);

// XMM-capture stub: int fn(float seed) -> (int)seed. HOOK at +0, capturing
// `xmm0` (seed, MS x64 ABI). The callback writes a new float via `xmm0:set(v)`;
// the captured `cvttss2si rax, xmm0` then RE-runs and converts the mutated lane,
// so the result is (int)v. Proves the new Context64 XMM-lane read/write
// machinery (F3), beyond the GPR form.
//
//   +0:  F3 48 0F 2C C0  cvttss2si rax, xmm0  ; rax = (int)xmm0  <-- HOOK, capture xmm0
//   +5:  90 90 90        nop nop nop
//   +8:  C3              ret
const unsigned char kXmmStub[] = {
    0xF3, 0x48, 0x0F, 0x2C, 0xC0, // +0  cvttss2si rax, xmm0  (hook here, capture xmm0)
    0x90, 0x90, 0x90,             // +5  nop nop nop
    0xC3,                         // +8  ret
};
constexpr int kXmmMidOffset = 0;  // capture site (the `cvttss2si`)
using XmmStubFn = int (*)(float);

// Four GPR-form RWX allocations + one memory-form + one XMM-form (distinct pages
// -> distinct VAs; no ICF concern since these are runtime data, not linker-
// folded functions).
struct Stub { void* mem = nullptr; StubFn fn = nullptr; };
Stub g_read, g_write, g_skip, g_run;
struct MemStub { void* mem = nullptr; MemStubFn fn = nullptr; };
MemStub g_mem;
struct XmmStub { void* mem = nullptr; XmmStubFn fn = nullptr; };
XmmStub g_xmm;

// Allocate the stub from the kcdx BRANCH POOL (RWX memory within ±2 GB of
// WHGame.dll's .text), NOT raw VirtualAlloc(nullptr, ...). A real plugin
// hooks code inside loaded modules — always near other module code, inside
// MinHook's ±1 GB trampoline window (vendor/minhook/src/buffer.c
// MAX_MEMORY_RANGE = 0x40000000). A raw-heap stub (~0x1D8…) lands an
// ASLR-dependent distance away; MinHook's MH_CreateHook trampoline allocator
// (the mid install path: AddMid -> InstallRuntime -> MH_CreateHook) then
// fails MH_ERROR_MEMORY_ALLOC when no free page is in range — flaky.
// Allocating near WHGame mirrors a real in-module target and is deterministic.
// (cap-07 proves AllocateFromBranchPool returns rel32-reachable memory; the
//  pool is ±2 GB of WHGame.dll .text.)
bool AllocStub(Stub& s) {
    if (!g_tramp) return false;
    s.mem = g_tramp->AllocateFromBranchPool(g_self, sizeof(kStub));
    if (!s.mem) return false;
    memcpy(s.mem, kStub, sizeof(kStub));
    FlushInstructionCache(GetCurrentProcess(), s.mem, sizeof(kStub));
    s.fn = reinterpret_cast<StubFn>(s.mem);
    return true;
}

bool AllocMemStub(MemStub& s) {
    if (!g_tramp) return false;
    s.mem = g_tramp->AllocateFromBranchPool(g_self, sizeof(kMemStub));
    if (!s.mem) return false;
    memcpy(s.mem, kMemStub, sizeof(kMemStub));
    FlushInstructionCache(GetCurrentProcess(), s.mem, sizeof(kMemStub));
    s.fn = reinterpret_cast<MemStubFn>(s.mem);
    return true;
}

bool AllocXmmStub(XmmStub& s) {
    if (!g_tramp) return false;
    s.mem = g_tramp->AllocateFromBranchPool(g_self, sizeof(kXmmStub));
    if (!s.mem) return false;
    memcpy(s.mem, kXmmStub, sizeof(kXmmStub));
    FlushInstructionCache(GetCurrentProcess(), s.mem, sizeof(kXmmStub));
    s.fn = reinterpret_cast<XmmStubFn>(s.mem);
    return true;
}

// Hand a stub's exact (capture-site) address to Lua. The hook installs at
// the capture site itself — base + kMidOffset — so the author passes the
// resolved capture VA as `address` and uses offset=0 (the address already
// points at the `add`). (We could pass the base + offset=kMidOffset; both
// resolve to the same VA. Passing the exact site keeps plugin.lua simple.)
#define ADDR_FN(luaName, stub) \
    static int luaName(struct lua_State* L, void* ud) { \
        static_cast<const kcdxLuaApi*>(ud)->PushLightUserdata( \
            L, reinterpret_cast<void*>( \
                   reinterpret_cast<unsigned char*>(stub.mem) + kMidOffset)); \
        return 1; }
ADDR_FN(Lua_AddrRead,  g_read)
ADDR_FN(Lua_AddrWrite, g_write)
ADDR_FN(Lua_AddrSkip,  g_skip)
ADDR_FN(Lua_AddrRun,   g_run)
#undef ADDR_FN

// The mem + xmm stubs hook at offset +0 (the captured instruction is first), so
// the capture-site VA IS the base. Separate macro (different offset constant).
#define ADDR_FN0(luaName, stub) \
    static int luaName(struct lua_State* L, void* ud) { \
        static_cast<const kcdxLuaApi*>(ud)->PushLightUserdata( \
            L, stub.mem); \
        return 1; }
ADDR_FN0(Lua_AddrMem, g_mem)
ADDR_FN0(Lua_AddrXmm, g_xmm)
#undef ADDR_FN0

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

// Fires AFTER the kcdx.hook mid chain is applied (InputLoaded follows
// ApplyZone). Call each stub directly — the mid detour fires for any caller —
// and assert the captured-instruction behavior.
void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    gLog.Info("VERIFY", "InputLoaded — invoking mid-hooked stubs");

    int r;
    r = g_read.fn(10);
    Check("CAP-21-read", r == 110,
          "capture read (c.rax:get()==10 asserted in Lua); add ran -> 110",
          "mid capture read or run-through wrong (expected 110)");

    r = g_write.fn(10);
    Check("CAP-21-write", r == 1100,
          "capture write (c.rax:set(1000)); add ran on 1000 -> 1100",
          "mid capture :set() did not change downstream result "
          "(expected 1100)");

    r = g_skip.fn(10);
    Check("CAP-21-skip", r == 10,
          "callback returned 'skip'; add NEVER ran -> rax stays 10",
          "mid return 'skip' did not skip the captured instruction "
          "(expected 10)");

    r = g_run.fn(10);
    Check("CAP-21-run", r == 110,
          "callback returned nothing; add ran -> 110",
          "mid return-nothing did not run the captured instruction "
          "(expected 110)");

    // CAP-21-mem — MEMORY capture writeback. The stub is `*slot + 100`; the mid
    // captures `[rcx]` (= *slot) at the `mov eax,[rcx]` site and :set(1000)s the
    // memory. The captured mov then reads 1000 -> add -> 1100. FALSIFIABLE: 110
    // means the deref-write missed (*slot stayed 10); any other value means the
    // address computation from Context64 register fields was wrong.
    static int memSlot = 10;
    int rm = g_mem.fn(&memSlot);
    Check("CAP-21-mem", rm == 1100,
          "memory capture [rcx]:set(1000) wrote through the derefed address; "
          "mov re-read 1000 -> add -> 1100",
          "mid memory-capture writeback wrong (expected 1100; 110 = write "
          "missed, *slot stayed 10)");

    // CAP-21-xmm — XMM lane capture writeback. The stub is `(int)seed`; the mid
    // captures `xmm0` (the float seed) at the `cvttss2si rax,xmm0` site and
    // :set(50.0)s the lane. The captured cvttss2si then converts 50.0 -> rax=50.
    // FALSIFIABLE: 10 means the xmm lane write missed (seed survived); any other
    // value means the lane read/write was wrong.
    int rx = g_xmm.fn(10.0f);
    Check("CAP-21-xmm", rx == 50,
          "xmm0 capture :set(50.0) wrote the lane; cvttss2si converted 50.0 "
          "-> 50",
          "mid XMM-capture writeback wrong (expected 50; 10 = lane write "
          "missed, seed survived)");
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
    g_tramp = static_cast<kcdxTrampolineInterface*>(
        api->QueryInterface(kcdxInterface_Trampoline,
                            kcdxTrampolineInterface_Version));
    if (!scripting || !messaging || !g_tramp) {
        gLog.Error("INIT", "QueryInterface(Scripting/Messaging/Trampoline) null");
        api->ReportTestResult(g_self, "CAP-21-read", 0,
            "QueryInterface returned null");
        return true;
    }

    // Stubs come from the branch pool (fetched above) so MinHook's mid-hook
    // trampoline allocator finds a page in range deterministically. A null
    // alloc fails LOUD — no silent fall back to VirtualAlloc (that would
    // reintroduce the ASLR flakiness this fix removes).
    if (!AllocStub(g_read) || !AllocStub(g_write) ||
        !AllocStub(g_skip) || !AllocStub(g_run) ||
        !AllocMemStub(g_mem) || !AllocXmmStub(g_xmm)) {
        gLog.Error("INIT", "AllocateFromBranchPool for a stub failed");
        api->ReportTestResult(g_self, "CAP-21-read", 0,
            "AllocateFromBranchPool(branch) for a stub returned null");
        return true;
    }

    void* luaApi = (void*)scripting->lua;
    bool ok = true;
    ok = scripting->RegisterFunction(g_self, "cap21", "addr_read",  Lua_AddrRead,  luaApi) && ok;
    ok = scripting->RegisterFunction(g_self, "cap21", "addr_write", Lua_AddrWrite, luaApi) && ok;
    ok = scripting->RegisterFunction(g_self, "cap21", "addr_skip",  Lua_AddrSkip,  luaApi) && ok;
    ok = scripting->RegisterFunction(g_self, "cap21", "addr_run",   Lua_AddrRun,   luaApi) && ok;
    ok = scripting->RegisterFunction(g_self, "cap21", "addr_mem",   Lua_AddrMem,   luaApi) && ok;
    ok = scripting->RegisterFunction(g_self, "cap21", "addr_xmm",   Lua_AddrXmm,   luaApi) && ok;
    if (!ok) {
        gLog.Error("INIT", "RegisterFunction failed");
        api->ReportTestResult(g_self, "CAP-21-read", 0,
            "RegisterFunction(cap21.addr_*) failed");
        return true;
    }

    messaging->RegisterListener(g_self, nullptr, OnMessage);
    gLog.Info("INIT", "stubs allocated; verify runs on InputLoaded");
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
