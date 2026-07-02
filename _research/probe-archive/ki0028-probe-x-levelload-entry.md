# PROBE X — KI-0028 level-load-entry reach (CResourceList::Load after-hook)

**Retired:** 2026-07-02 (falsified). **KI:** KI-0028. **Verdict:** RED HERRING — falsified as a probe target.

## What it asked

Does the engine BEGIN loading a level swap-ON? After-hook `CResourceList::Load @ 0x4dcb60` (the first level-resource read), armed BEFORE the swap decision so it fires on both arms. Outcome map:
- **A (the bet):** fires swap-OFF, NOT swap-ON → an upstream gate stops level-load before it begins.
- **B:** fires swap-ON then early-returns → level-load starts but a served manifest parses empty.
- **C:** fires identically both paths → the gate is downstream of the resource-list read.

## The finding — FALSIFIED (Reframe 8 + Y.6)

`CResourceList::Load` fires **ZERO times on BOTH arms** — including the WORKING swap-OFF menu (`load_calls=0`, confirmed in the Y.6 run `kcdx-dev_2026-07-02_10-29-07.log`, and in the earlier Reframe-8 A/B). So `load_calls=0` swap-ON proves NOTHING about the swap — **this function is not on the menu/backdrop boot path at all** (identical trap to PROBE R2: a swap-ON zero that is also zero on the path that works). `0x4dcb60` is the *new-game / explicit level-entry* resource-list read, NOT the menu-backdrop load. The menu backdrop (Y.6: working menu reaches `draw_indexed=68024` via a ~27s transition) loads geometry through a DIFFERENT entry that `CResourceList::Load` is not on.

**Do NOT re-chase `0x4dcb60` for the menu-backdrop question.** The surviving axis is the ~27s menu-backdrop-load transition (Y.6), whose trigger is NOT this function.

## Reusable wiring (the recipe — reconstruct from here, not from source)

An after-hook on a WHGame RVA via MinHook, armed before the swap, with a guarded CryString-arg reader:

- **Target:** `CResourceList::Load @ RVA 0x4dcb60` (WHGame.dll release_1_5_1164953_841, base 0x180000000). SOURCE: `_research/ki0028-vanilla-init-fs-map/LOADER-TRACE.md` + `_bodies.txt`, body-read.
- **ABI (body-read prologue):** 3-arg `__fastcall` — `rcx=this` (level-context obj), `rdx=arg2`, `r8=arg3` (both CryString path/name args the body copies into `this+0x178`/`this+0x180`). Return discarded; mirror as `void*` to keep the trampoline honest. `using ResListLoadFn_t = void*(__fastcall*)(void* self, void* arg2, void* arg3);`
- **Arm mechanism:** `MH_Initialize` → `pe::OpenModule(L"WHGame.dll")` → `MH_CreateHook(base+rva, &detour, &orig)` → `MH_EnableHook`. CAS-latched `g_armed` (arm once). Armed from `HookedConstructStore` in seating_hook BEFORE the PROBE F noswap return, so it fires on both arms.
- **CryString arg read (guarded):** `SafeAsciiPrefix(const void* p, char out[64])` — SEH-guarded (`__try/__except`) copy of up to 63 printable ASCII bytes from a candidate CryString char* (the arg IS the char*; `*arg` is the first char); stops at first non-printable → truncates. Never faults on a null/bad pointer. Reusable for ANY hook that logs a CryString arg.
- **Summary watcher:** a bounded dedicated-thread poll (40 × 3s reads ≈ 2 min) flushing `g_loadCalls` so the A/B diff is readable without catching the single enter line. Same sanctioned diagnostic-poll shape as PROBE R/K/P (one thread, nothing suspended).

The `SafeAsciiPrefix` + MinHook-after-hook-a-WHGame-RVA + bounded-summary-watcher recipe is the reusable core; the target `0x4dcb60` is dead for this question.
