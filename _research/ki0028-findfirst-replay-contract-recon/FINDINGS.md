# KI-0028 — engine ORIGINAL FindFirst/FindNext/FindClose replay contract

**Date:** 2026-06-22 · **Trust:** primary evidence (prior Ghidra body reads, reused).
**Tier:** 2 — answered entirely from prior `_research/` decompiles; NO fresh Ghidra.

## Question

To build the PROBE Y enumeration vanilla-differential, kcdx must REPLAY the captured
engine ORIGINAL FindFirst (slot 63) / FindNext (slot 64) / FindClose (slot 65) and
drain the iterator correctly. PROBE Y.2 observed live: original FindFirst returns a
64-bit POINTER (e.g. `0x000001F3xxxxxxxx`), FindNext returns `0` with a valid changing
name every call, and the drain loop (assuming `<0` = exhausted) NEVER terminates
(100k on a 3-entry dir). What is the PRODUCER contract, and why does the drain run away?

## The contract (VERIFIED — bodies read in prior recon)

Source: `_research/ki0028-findfirst-straddle-recon/FINDINGS.md` (slot bodies read) +
`_research/ki0027-find-data-abi-recon/FINDINGS.md` (the consumer loop + find-data ABI,
two corroborating consumers).

**FindFirst (slot 63, engine `0x180973058`):**
- Success → returns a **pointer to a `CCryPakFindData` object** (vtable@+0x0,
  refcount@+0x8, entry-count@+0x18); slot-101 factory allocs it + bumps refcount.
- No match → returns **`-1`**.
- The consumer gate is `if (-1 < lVar5)` — a positive pointer vs `-1`. kcdx's replay
  `if (h >= 0)` matches (a heap pointer is a large positive intptr_t).

**FindNext (slot 64, engine `0x18041d640`):** returns **`≥0` to continue, `-1` to stop**
— the consumer loop is `do { … } while (-1 < iVar3)`. The exhaust convention the replay
assumed (`<0` = stop) IS CORRECT. FindNext operates **CCryPak-internal find-state at
`pak+0x138`/`pak+0x148`** (NOT the returned handle interior).

**FindClose (slot 65, engine `0x18097383c`):** takes `(self, handle)`; virtual-dtor's the
`CCryPakFindData` object through `handle+0x0`, removes it from the find-list at `pak+0x168`.
Required to release the object. kcdx's replay calls it — correct.

## ROOT CAUSE of the runaway (mechanism, falsifiable)

The exhaust convention was NOT wrong. The bug is the **find-data buffer being WIPED
between FindNext calls.**

The engine consumer (KI-0027 recon, `FUN_180974484`) declares `byte local_158[36]` ONCE,
calls FindFirst into it, then loops calling FindNext into the **SAME `local_158` WITHOUT
re-zeroing it**:

```c
  lVar5 = FindFirst(pak, pattern, local_158, 0);   // fills local_158
  if (-1 < lVar5) {
    do {
      ... read local_158[0] (attr), &local_134 (name @ +0x24) ...
      iVar3 = FindNext(pak, lVar5, local_158);      // SAME buffer, NOT re-zeroed
    } while (-1 < iVar3);
    FindClose(pak, lVar5);
  }
```

The find-data header bytes **0x01–0x23** (the KI-0027 recon's "size/time/reserved" region,
unread by the table-glob consumers) carry the engine's **per-call iteration cursor / state**.
The engine FindNext reads that state to know which entry is next.

kcdx's PROBE Y replay did `std::memset(buf, 0, sizeof(buf))` **inside the drain loop before
every FindNext call** → it wiped the iteration cursor in the header → the engine FindNext
restarted from entry 0 every call → it returned `0` (continue) forever with the first
entry's name → 100k runaway on a 3-entry dir.

**Falsifiable prediction:** remove the per-call memset (zero the buffer ONCE before
FindFirst, never again in the loop) → the drain terminates and `levels/*.*` → 3 entries,
`Config/CVarGroups/*.cfg` → 14 (matching kcdx's own FS_BOOT_TRACE counts).

## The fix for the replay (enum_diff_probe.cpp)

- Zero `buf` ONCE before FindFirst. Do NOT memset inside the FindNext loop.
- Keep `if (more < 0) break;` — the convention is correct.
- Keep the FindClose. Keep a generous runaway cap as a defensive backstop only.

## Reuse

- The find-data header bytes 0x01–0x23 carry FindNext's iteration STATE — never zero the
  find-data buffer between FindNext calls. (Extends the KI-0027 ABI: attr@0x00,
  name@0x24, **state@0x01–0x23**.)
- FindNext exhaust = return `-1` (`≥0` continues). FindFirst no-match = `-1`. FindClose
  object-dtors through handle+0x0.
