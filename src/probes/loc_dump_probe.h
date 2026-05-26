#pragma once

// === LOC-DUMP PROBE (step 1 — minimal live probe) ===================
//
// The localization runtime-dump feature's first, minimal step. This probe
// proves TWO gating unknowns hold in-game before any of the dump machinery
// (table walk, key<->id map, output format) is built:
//
//   (1) Hooking the CLocalizedStringsManager CONSTRUCTOR captures the manager
//       `this` pointer (stored in an atomic for later use).
//   (2) Hooking the by-INT-ID localization getter (vtable slot 1, decompiled
//       signature `char* (this, uint id)`) fires with the expected ABI — the
//       caller return-address and the `id` arg read correctly.
//
// This is OBSERVE-ONLY: it never mutates args or return; it always calls the
// original unmodified. It mirrors bugsplat_ctor_probe's install machinery
// (dev-mode-gated, idempotent latch, MinHook detour, atomic orig-pointer),
// but targets WHGame.dll (the game's own module) rather than BugSplat64.dll.
//
// Verified RE facts (from
// _research/parallel-ghidra-research/LOC-MANAGER-FINDINGS.md):
//   - CLocalizedStringsManager ctor: FUN_1809f0ce4, RVA 0x9f0ce4. First store
//     is `*this = vtable`; hooking it captures `this` (RCX, arg 1).
//   - by-ID getter: vtable slot 1 (offset 0x8). Resolved at runtime off the
//     captured instance's live vtable — NOT a hardcoded getter RVA.
//
// Dev-mode-only. The probe IS the verification of the slot-1 ABI: if the live
// launch shows readable `id` values, the `char* (this, uint id)` shape holds.

namespace kcdx::probes::loc_dump_probe {

// Arm the probe: install the ctor hook against WHGame.dll. WHGame.dll is the
// module kcdx.dll is injected into, so by the worker-thread install point it
// is always mapped. Idempotent. Returns true on success. The by-ID getter
// hook is installed lazily, ONCE, from inside the ctor detour the first time a
// manager `this` is captured (the live vtable is read off that instance).
//
// Dev-mode-gated: a no-op (returns false) when dev mode is off.
bool Install();

}  // namespace kcdx::probes::loc_dump_probe
