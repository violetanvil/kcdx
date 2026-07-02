# KI-0028 PROBE Z2.3-open — the OPEN family alone CRASHES: engine calls fileno() on kcdx's handle, not a FILE*

**Date:** 2026-07-02 (run `kcdx-dev_2026-07-02_12-55-33.log`, dump `kcdx_2026-07-02_12-55-33.dmp`)
**Swap state:** `probe_z_live_mask=1` (kFamOpen only), `kcdx_owned=3` (slots 1/35/36 live; read/metadata/enum THUNK to engine original).
**Outcome:** CRASH (a third outcome — not black, not menu). Dump read FIRST per discipline.

## The crash — ground truth from the dump

- **Exception:** `c0000005` Access violation, READ of address `0x1b`. `FAILURE_BUCKET_ID: NULL_CLASS_PTR_READ_c0000005_ucrtbase.dll!fileno`.
- **Faulting instruction:** `ucrtbase!fileno+0x23: mov eax, dword ptr [rcx+18h]` with **`rcx=3`** → `[3+0x18]=[0x1b]` → AV.
- **`fileno()` was called with a `FILE*` of literally `3`** — an integer, not a pointer. `fileno` extracts the OS fd from a `FILE*` by reading `[FILE*+0x18]`; handed `3` it dereferences `0x1b` and faults.
- **Stack:** up through `wh::game::C_Game::CreateInstance` (the KI-0028 wedge region; the `ffxFsr2ResourceIsNull+…` labels are nearest-export noise, real frames inside CreateInstance). tid=33804.

## The mechanism — falsifiable, source-confirmed (AP17-grade)

**What value was wrong:** kcdx's `FOpen` (slot 36) returns a **kcdx handle** — a small integer token (observed `3`), NOT a CRT `FILE*`. `src/fs_takeover/open_slots.cpp`: `_wfopen_s(&fp, ...)` gets a real `FILE* fp`, then `MintLoose` mints a kcdx handle `h` and `return h;` (line 119; log at 269 "kcdx FOpen minted a kcdx handle on kcdx's CRT"). The kcdx handle is an integer/opaque token, not the FILE* pointer.

**Who wrote it, in what order:** the engine calls kcdx's FOpen (open family live) → gets handle `3` back → then the engine's ORIGINAL code path calls `fileno(3)` directly on that return value (read/metadata thunked, so the engine's own read/handle path runs, not kcdx's).

**Why the original path made the wrong read inevitable:** kcdx's handle model is coherent ONLY when kcdx ALSO owns the read/handle slots (they understand the kcdx handle and map it to kcdx's real FILE*). With only the open family live, the engine's original `fileno`/read code receives a kcdx handle-integer it interprets as a `FILE*` → dereferences it → AV. The engine calls `fileno()` (or a CRT function expecting a real FILE*) DIRECTLY on FOpen's return, bypassing kcdx's read slots.

## What this establishes for KI-0028

- **The OPEN family is directly implicated** — kcdx's FOpen return TYPE (handle vs FILE*) is the fault axis. The bisection isolated it: kFamNone renders (open thunks → engine gets a real FILE*), kFamOpen-only crashes (engine gets kcdx's handle at a fileno call).
- **The engine makes at least one CRT call (`fileno`) directly on FOpen's return** — a path that assumes FOpen returns a real `FILE*`, which kcdx's full-takeover handle model does NOT satisfy. This is a takeover completeness gap: FOpen's contract with the engine is "return something the engine can pass to fileno()", and kcdx's handle breaks it.

## The connecting question (NOT yet established — next)

Does this SAME handle-vs-FILE* mismatch cause the BLACK SCREEN in the full swap (kFamAll)? Under kFamAll, kcdx owns the read slots too, so kcdx's read/close consume the handle correctly and this particular `fileno` crash does not fire — but the engine's DIRECT `fileno(handle)` call still gets a kcdx handle, and if the engine uses the fd for something kcdx's slots don't intercept (a raw OS read, an fd passed to another API), it would silently get wrong data → the backdrop load fails → black. This is the likely bridge from "open-only crashes" to "full-swap goes black," but it is NOT yet proven. Next: find WHERE in CreateInstance the engine calls fileno() on FOpen's return, and what it does with the fd — swap-ON vs the kFamNone (working) path.
