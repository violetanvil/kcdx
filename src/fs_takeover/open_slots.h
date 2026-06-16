#pragma once

// The kcdx OPEN-family slot impls — the resolution + open slots the takeover
// flips THUNK→KCDX (file-system-takeover design §4.5 / §5).
//
//   slot 1  AdjustFileName — the unified-index resolution chokepoint. Resolves
//           EVERY name to a real disk-path STRING (§5: a miss is NOT a hand-back;
//           slot-1 resolves every name). Asset hit → the index ByteSource's
//           disk path; miss → thunk the captured ORIGINAL AdjustFileName body
//           (safe — it returns a string, touches no handle, no CRT).
//   slot 36 FOpen      — open-by-path. Resolves via the index, opens the
//           byte-source on kcdx's CRT, MINTS a kcdx handle-id (§5: EVERY FOpen
//           mints a kcdx handle — asset, non-asset, write alike).
//   slot 35 FOpenRaw   — open-into-caller-buffer. Same mint, plus it copies the
//           resolved name into the caller's outResolvedBuf (clamped ≤2048).
//
// Every member-call ABI is built to _research/fs-takeover-readslot-abi-recon/
// FINDINGS.md (the BODY-VERIFIED open-slot signatures). On x64 Windows there is
// ONE calling convention (the x64 ABI: RCX,RDX,R8,R9,stack), so a plain function
// `void* fn(void* self, ...)` IS the member-call shape (`self`==`this` in RCX) —
// mirroring the existing KcdxFOpenMarker slot impl (no explicit __fastcall).

#include <cstdint>

namespace kcdx::fs_takeover {

// The captured-original AdjustFileName body (slot 1), captured at swap time from
// the live object's original vtable (the same capture mechanism the prior spike
// used for slot 36 — the captured slot moves to slot 1 now that slot 36 is a
// full kcdx impl). kcdx_AdjustFileName thunks through to it on an index MISS (and
// a pak hit) — the §5 long-tail resolution: it returns a STRING, touches no
// handle, no CRT, so it cannot reintroduce the cross-CRT straddle. The swap
// stores it via SetOriginalAdjustFileName.
using AdjustFileNameOrigFn_t =
    void* (*)(void* self, const char* pName, void* outBuf, uint32_t nFlags);
void SetOriginalAdjustFileName(AdjustFileNameOrigFn_t fn);

// === The KCDX open-slot impls (the fn pointers the per-slot table KCDX rows
// point at; wired into the kcdx vtable by vtable_swap). ======================

// slot 1 — AdjustFileName. ABI (FINDINGS BODY-VERIFIED, id 152):
//   ptr (this, cstr pName, ptr outBuf, u32 nFlags)
// Resolves pName to a disk-path string written into outBuf (returns outBuf), via
// the unified index on an asset hit, else the captured original on a miss.
void* kcdx_AdjustFileName(void* self, const char* pName, void* outBuf,
                          uint32_t nFlags);

// slot 36 — FOpen. ABI (FINDINGS BODY-VERIFIED, id 131):
//   ptr (this, cstr pName, cstr szMode, u32 nFlags)
// Resolves via the index, opens the byte-source on kcdx's CRT, mints + returns a
// kcdx handle-id. Returns 0 on a failed open (loud).
void* kcdx_FOpen(void* self, const char* pName, const char* szMode,
                 uint32_t nFlags);

// slot 35 — FOpenRaw. ABI (FINDINGS BODY-VERIFIED, kcdx_id 160):
//   ptr (this, cstr pName, cstr szMode, ptr outResolvedBuf, int bufCap)
// Same open+mint as FOpen, plus copies the resolved name into outResolvedBuf
// (clamped to bufCap, ≤2048). Returns the kcdx handle-id, or 0 on a failed open.
void* kcdx_FOpenRaw(void* self, const char* pName, const char* szMode,
                    void* outResolvedBuf, int bufCap);

}  // namespace kcdx::fs_takeover
