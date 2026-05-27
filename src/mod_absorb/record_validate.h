#pragma once

#include <cstddef>
#include <string>

// Synthesized-record self-validation — the takeover's build-time guard.
//
// Before kcdx repoints the engine's enabled-list vector at its rebuilt list
// (the SELECT detour), every synthesized I_Mod record is walked and asserted
// well-formed against the known native invariants. A malformed record is caught
// + named LOUD by kcdx HERE, instead of surfacing later as an opaque native
// multi-gigabyte allocation that fatally crashes the load during MOUNT (the
// keystone crash class: the engine reads a record string field's length word to
// size every copy it makes — a garbage length drives a huge allocation that
// fails fatally). docs/mod-loader-absorb.md "The I_Mod record layout" + the
// CryString header section describe the invariants this enforces.
//
// The invariants validated per record (all must hold or the record FAILS):
//   - The two vtable slots (+0x00 primary, +0x18 sub-object): each non-null AND
//     inside the WHGame.dll image range. A null/out-of-range vtable crashes
//     MOUNT on the first virtual dispatch. (The expected values are the
//     Address-Library-resolved I_Mod vtable pair; the robust check is non-null +
//     in-image, never a hardcoded address.)
//   - Each of the 8 CryString string fields (+0x08/+0x10/+0x20/+0x28/+0x30/
//     +0x38/+0x40/+0x48): a pointer to char DATA whose immediately-preceding
//     16-byte header is a valid CryString header — nLength (int32 at data-8) ==
//     strlen(data) [the load-bearing invariant — a garbage nLength is exactly
//     what crashes MOUNT], nRefs (int32 at data-12) >= 1, nAllocSize (int32 at
//     data-4) >= nLength. A null field pointer, or a header whose nLength is
//     negative / absurd / != strlen, FAILS.
//
// SAFETY: the validator must not itself crash on a malformed record (catching
// one is the whole point). The per-field header + strlen read is SEH-guarded —
// a wild pointer logs "field unreadable" + FAILS the record rather than AV-ing
// the validator. strlen is capped at a sane maximum so a non-terminated buffer
// does not scan forever (over-cap = FAIL).

namespace kcdx::mod_absorb {

// Validate one synthesized 0x70-byte I_Mod record against the native
// invariants. Returns true iff EVERY invariant holds. On failure, logs an ERROR
// (category MOD_ABSORB) naming the mod's load-order name + id, WHICH field, and
// WHICH invariant failed, plus the consequence ("would crash MOUNT — record
// dropped, the mod will NOT load") — mirroring the existing drop-and-log
// discipline in BuildEnabledList's null-BuildRecord path. The caller DROPS a
// record that fails (never repoints it into the engine).
//
// `rec` is the I_Mod* (record byte base). `loadOrderName` / `id` name the mod
// for the diagnostic. A null `rec` FAILS (logged).
bool ValidateSynthRecord(const void* rec,
                         const std::string& loadOrderName,
                         const std::string& id);

}  // namespace kcdx::mod_absorb
