#pragma once

// === LOC-DUMP PROBE (LocalizeString key-capture) ====================
//
// The localization runtime-dump feature's live probe. It proves the key→text
// resolution path can be observed in-game before any dump machinery (table
// walk, key<->id map, output format) is built:
//
//   (1) Hooking the CLocalizedStringsManager CONSTRUCTOR captures the manager
//       `this` pointer (stored in an atomic) — needed to resolve the live
//       vtable for installing the LocalizeString hooks below.
//   (2) Hooking the TWO public LocalizeString overloads (manager vtable slots
//       21 / 22, offsets 0xA8 / 0xB0) fires with a readable key string and the
//       gameplay caller-return-address per call. These slots are the
//       string-keyed text lookup the gameplay UI/HUD uses; their callers ARE
//       the gameplay frames (the slots thunk into the inner FUN_18051d534).
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
//   - LocalizeString public overloads: vtable slot 21 (offset 0xA8,
//     FUN_18051d514, CryStringT overload) + slot 22 (offset 0xB0,
//     FUN_18242e770, raw C-string overload). Resolved at runtime off the
//     captured instance's live vtable — NOT hardcoded overload RVAs. The slot-1
//     by-int-ID getter targeted by a prior step was proven to be
//     GetLanguageName (the WRONG function) — see findings §"GETTER BODY READ".
//
// Dev-mode-only. The probe IS the verification of the slot-21/22 LocalizeString
// ABIs: if the live launch shows readable key strings + gameplay caller
// return-addresses, the captured-key path holds.

namespace kcdx::probes::loc_dump_probe {

// Arm the probe: install the ctor hook against WHGame.dll. WHGame.dll is the
// module kcdx.dll is injected into, so by the worker-thread install point it
// is always mapped. Idempotent. Returns true on success. The two LocalizeString
// hooks (vtable slots 21 / 22) are installed lazily, ONCE, from inside the ctor
// detour the first time a manager `this` is captured (the live vtable is read
// off that instance to resolve both overload addresses).
//
// Dev-mode-gated: a no-op (returns false) when dev mode is off.
bool Install();

}  // namespace kcdx::probes::loc_dump_probe
