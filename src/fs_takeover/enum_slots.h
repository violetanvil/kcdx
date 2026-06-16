#pragma once

// The kcdx DIRECTORY-ENUMERATION slot impls — the slots the takeover flips
// THUNK→KCDX so a directory walk enumerates kcdx's UNIFIED set: the index's
// vpaths under a prefix PLUS the engine's own on-disk/pak entries for that
// prefix (the totalizing invariant — kcdx owns the whole filesystem, design §1).
// file-system-takeover design §4.5 "Directory enumeration".
//
// Every member-call ABI is built to the BODY-VERIFIED decompiles in
// _research/phase8.5-pak-resolver/_front1_roles_raw.txt + front1-full-vtable-
// surface.md. On x64 Windows there is ONE calling convention (RCX,RDX,R8,R9,
// stack), so a plain `T fn(void* self, ...)` IS the member-call shape
// (`self`==`this` in RCX) — mirroring open_slots.cpp / read_slots.cpp.
//
// SCOPE NOTE — slots 15 and 101 are NOT flipped to KCDX in this step; see the
// surfaced decisions in the step deliverable. Slot 15 is the engine's INTERNAL
// per-entry callback dispatcher invoked BY slot 14 (it has no independent
// "kcdx answer" — it only builds a path + forwards to the caller's enumeration
// callback against engine member offsets); slot 14's kcdx impl invokes it
// through the OBJECT's vtable slot-15 entry, which stays the engine original
// (THUNK) so the callback contract is preserved verbatim. Slot 101 (the
// CCryPakFindData iterator factory) mints a multi-object-lifetime iterator
// consumed via a SEPARATE object vftable (FindNext/FindClose) — reimplementing
// that lifecycle is a separable concern surfaced as a decision, NOT silently
// chosen. Only slot 14 is a KCDX impl here.

#include <cstdint>

namespace kcdx::fs_takeover {

// slot 14 — ForEachFile / FindFiles. ABI (BODY-VERIFIED, RVA 0x241d2e8):
//   uint8 (this, <opaque cbCtx>, cstr pPathPattern, <opaque userData>)
// Original body: slot1 resolves pPathPattern → _findfirst64/_findnext64 loop →
// per matched entry invokes the per-file callback via the object's vtable+0x78
// (slot 15) as `(*slot15)(this, cbCtx, fullPathStr, userData)`. Returns 0 if no
// entry matched, else 1.
//
// kcdx impl: enumerate the UNIFIED set for the pattern's directory prefix —
//   (1) the engine's on-disk entries (the original _findfirst64/_findnext64 walk
//       over the resolved disk dir — kcdx owns this walk on its own CRT), AND
//   (2) the unified index's PAK-source vpaths under the same prefix — the
//       index-only delta the engine's disk walk cannot see (they live inside
//       paks). The index walk emits ONLY Pak sources: a loose override is a real
//       disk file the (1) walk already enumerated under the resolved dir, so
//       re-emitting it from the index would double-fire its callback. There is
//       no active index-vs-disk comparison; the loose-skip IS the de-dup (a loose
//       vpath is covered by (1), a pak vpath only by (2) — the two sets are
//       disjoint by construction).
// Each matched entry invokes the per-entry callback through the object's slot-15
// entry (kept the engine original — THUNK), so the caller's enumeration callback
// fires for kcdx-served entries exactly as for vanilla ones. Returns 1 iff at
// least one entry (disk OR index) matched.
//
// cbCtx / userData are opaque caller context threaded verbatim to the callback
// (void* width — never inspected by kcdx).
uint8_t kcdx_ForEachFile(void* self, void* cbCtx, const char* pPathPattern,
                         void* userData);

}  // namespace kcdx::fs_takeover
