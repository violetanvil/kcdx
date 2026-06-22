# KI-0028 — FindFirst (slot 63) handle-straddle consumer recon

**Date:** 2026-06-22 · **Trust:** primary evidence (Ghidra body reads).
**Verdict: DIVERGENCE B (find-handle straddle) is FALSIFIED. No straddle consumer exists.**

## Question

KI-0028 slot-diff found kcdx FindFirst (slot 63) returns a small int `(id<<1)|1`, where the engine
FindFirst (`0x180973058`) returns a refcounted `CCryPakFindData` OBJECT pointer. Does any boot consumer
deref/refcount/object-release the handle (operate it as an object) — which, fed kcdx's integer `3`,
would fault → the wedge?

## Method

Ghidra 12.1, base 0x180000000, `release_1_5_1164953_841`. Slot 63 = vtable+0x1F8, 64 = +0x200,
65 = +0x208. Discriminator for a GENUINE triplet consumer: a fn calling `+0x1f8` AND (`+0x200` OR
`+0x208`) on the SAME object → 53 functions. Workers: `DumpFindFirstStraddle.java`,
`DumpTripletConsumers.java`. Dumps: `_dump.txt`, `_triplet.txt`.

## Result (VERIFIED — bodies read)

**Handle object ABI:** vtable@+0x0, refcount@+0x8, entry-count@+0x18.
- Engine **FindFirst `0x180973058`**: slot-101 factory allocs the object, `*(obj+8)+=1` (refcount),
  returns the object ptr on success / `-1` on no-entries.
- Engine **FindClose `0x18097383c`** (the KILL site): `*(handle+8)+=1`, removes from CCryPak find-list
  @ `pak+0x168`, `*(handle+8)-=1; if 0 → (**(code**)*handle)(handle,1)` (virtual dtor through
  `handle+0x0`). Fed kcdx's `3`: reads `*(3+8)` + calls `(**(code**)3)(3,1)` → wild read + wild call.
  So a straddle WOULD fault — the divergence is real.
- Engine **FindNext `0x18041d640`**: operates CCryPak-internal find-state (`pak+0x138`/`+0x148`), not
  the handle interior.

**All 53 consumers classified — NONE straddles:**
- ~37 lVarN-captured: handle only `-1`-tested + passed opaque to slot 64/65 (deref-scan clean).
- 5 mechanical-scan-flagged: ALL decompiler variable-reuse FALSE POSITIVES (the var is reassigned to a
  loop index / string buffer after the find loop; the FindFirst return itself is opaque). Each body-read
  + cleared (`FUN_1807abb5c`, `180aea508`, `180bb190c`, `180da19cc`, `1819bb460`).
- 9 "inline-handle" = `+0x1f8/+0x200/+0x208` on a DIFFERENT object (displacement collision, not
  CCryPak) — not find consumers.

## Consequence

DIVERGENCE B is a real ABI mismatch (object-ptr vs integer) that NO shipped consumer exercises — every
slot-63 caller uses the `-1<h` + pass-back idiom kcdx's integer already satisfies. The KI-0028 wedge is
NOT a find-handle straddle. Redirect off the find triplet.

## Reuse

- CCryPakFindData ABI: vtable@0, refcount@+8, entry-count@+0x18. FindClose `0x18097383c` is the
  object-kill site (virtual dtor through handle+0x0). The triplet-consumer discriminator (calls
  +0x1f8 AND +0x200/+0x208 on one object) is the reusable find-consumer finder.
