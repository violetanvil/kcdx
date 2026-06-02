# `src/probes/bugsplat_ctor_probe.cpp` archived probe — PROBE Z

The bugsplat_ctor_probe FILE stays LIVE in `src/` (its `HookedCtor` / `Install` /
`ArmLdrInstall` / LDR machinery is the proven before_game-hook install prototype,
called live from `dllmain.cpp` — KEEP). Only its INTERNAL PROBE Z `#if 0` block (a
loader-lock asmjit smoke test) was extracted here; stripping it leaves the live
install machinery untouched and removes the dead diagnostic residue.

**Verdict:** VERIFIED — asmjit codegen + `branch_pool` `VirtualAlloc` +
`runtime_func_t` dtor all complete under the Windows loader lock at the bugsplat
install site (`jit_ptr=0x7FFF91990000` + dtor in 1ms, baseline matrix unchanged at
113/149). Pre-VM sites can migrate to `hook_chain::AddC`.
**Root cause:** engine direct-MH sites bypass `hook_chain` → MinHook returns
`MH_ERROR_ALREADY_CREATED` on plugin install at the same target.
**Backlink:** `docs/known-issues/` cap-59 KI §Reframe 2026-05-29b + §Resolution
(post-PROBE-α); also recorded in `docs/tech-debt/TD-0003-engine-direct-hook-migration.md`.
**Revival hint:** re-add the two blocks below (the `RunProbeZ` definition + its call
site in `Install()`) if a future loader-lock asmjit/alloc regression is suspected.
Requires `#include "../rom_borrowed/runtime_func_t.h"` and `#include <asmjit/asmjit.h>`.

### Wiring — `RunProbeZ` definition + SEH helper (file-scope anonymous namespace)

```cpp
bool kcdx_probe_z_pre(const kcdx::rom::runtime_func_t::parameters_t* /*params*/,
                      const uint8_t /*parameters_count*/,
                      kcdx::rom::runtime_func_t::return_value_t* /*return_value*/,
                      const uintptr_t /*target_func_ptr*/) {
    return true;
}

std::atomic<bool> kProbeZRan{false};

// SEH-only helper: no C++ objects in scope so __try/__except is legal here.
static uintptr_t SehCallMakeJit(kcdx::rom::runtime_func_t* rf,
                                const asmjit::FuncSignature* sig,
                                void* targetForNearVa,
                                bool* outFaulted) {
    uintptr_t jit = 0;
    __try {
        jit = rf->make_jit_func(*sig, asmjit::Arch::kX64,
                                &kcdx_probe_z_pre, nullptr,
                                reinterpret_cast<uintptr_t>(targetForNearVa));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outFaulted = true;
    }
    return jit;
}

void RunProbeZ(void* targetForNearVa) {
    bool expected = false;
    if (!kProbeZRan.compare_exchange_strong(expected, true,
                                            std::memory_order_acq_rel)) {
        return;
    }

    LOG_DEBUG_KV("PROBE_Z", "entered",
        log::KV("target", targetForNearVa),
        log::KV::BareStr("note",
            "loader-lock asmjit smoke test at bugsplat install site"));

    LOG_DEBUG_KV("PROBE_Z", "start_make_jit_func",
        log::KV("nearVa", targetForNearVa));

    // Heap-allocate runtime_func_t + FuncSignature so their dtors live outside
    // the SEH frame. Dtor of runtime_func_t disables-without-pOriginal —
    // safe even if make_jit_func returns 0 or faulted.
    auto* rf = new kcdx::rom::runtime_func_t();
    auto* probeSig = new asmjit::FuncSignature(asmjit::CallConvId::kCDecl,
                                               asmjit::FuncSignature::kNoVarArgs,
                                               asmjit::TypeId::kVoid);
    bool faulted = false;
    uintptr_t jit = SehCallMakeJit(rf, probeSig, targetForNearVa, &faulted);
    delete probeSig;

    if (faulted) {
        LOG_DEBUG_KV("PROBE_Z", "make_jit_func_faulted",
            log::KV::BareStr("verdict",
                "SEH fault during codegen/alloc under loader lock — bugsplat must stay direct-MH"));
    } else if (jit == 0) {
        LOG_DEBUG_KV("PROBE_Z", "make_jit_func_returned_zero",
            log::KV::BareStr("verdict",
                "codegen or branch_pool allocation failed under loader lock — bugsplat must stay direct-MH or pre-init branch_pool earlier"));
    } else {
        LOG_DEBUG_KV("PROBE_Z", "make_jit_func_ret",
            log::KV("jit_ptr", (void*)jit),
            log::KV::BareStr("verdict",
                "asmjit codegen + branch_pool VirtualAlloc both completed under loader lock — pre-VM sites can migrate to hook_chain::AddC"));
    }

    delete rf;  // dtor runs here; if it hangs under loader lock the next line won't appear

    LOG_DEBUG_KV("PROBE_Z", "runtime_func_dtor_ok",
        log::KV::BareStr("note", "rf dtor completed under loader lock"));
}
```

### Wiring — call site (in `Install()`, after `MH_Initialize`, before `MH_CreateHook`)

```cpp
RunProbeZ(target);
```
