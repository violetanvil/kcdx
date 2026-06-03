# Finding — FOpen loose-file handle = a raw CRT `FILE*`; FRead routes it by INDEX-vs-COUNT, and a `FILE*` always exceeds the (small) pak-handle count → served by the OS arm

Captured 2026-06-03 (fresh Ghidra decompile, tier 5). Reuse-first ladder: tier-1
seed row 131 carried only the ABI; tier-2 dump `_abi_1804614a0.txt` was linear
DISASM (handle unreadable) → fresh decompile. Scripts/raw:
`third-party-ghidra/ghidra_scripts/DumpFOpenHandleConstruct.java`;
`_fopen_handle_decomp.txt` (FOpen body), `_fopen_handle_producers.txt` (loose
producers), `_fread_bound_decider.txt` (the dispatch decider `FUN_180427e40`).
Every claim below is READ in the named body (AP19 / §3.5), with the call/return
site cited. A first pass MISCHARACTERIZED the FRead dispatch as an address-range
test and was HALTED by the §4.5 gated verifier; this version corrects it with the
decider's body read.

## The complete, body-read mechanism

**FOpen returns one of TWO handle kinds, split by pak-membership** (FOpen body
`_fopen_handle_decomp.txt`, fn `FUN_1804614a0`):

- **PAK found** (`local_888 != 0` after the pak lookup `FUN_180462ddc`, :225-263):
  the handle is **`index + 1`** into the pak-handle vector at `param_1[8]` — walks
  for a free slot `uVar18` (:234-238), writes the entry (:260), returns `uVar18+1`
  (:262). A SMALL integer.
- **LOOSE / no pak** (the `local_888 == 0` sub-paths + the `else` at :267): returns
  a **CRT `FILE*`** — `FUN_1809b2b28()` on the `(param_4 & 0x10)==0` path (:196/:277),
  or `FUN_182423e08()` on the `& 0x10` path (:199/:280). BOTH producers return a
  CRT `FILE*`:
  - `FUN_1809b2b28` (`_fopen_handle_producers.txt`): `FILE *` return; `pFVar6 =
    _wfopen(...)` then `return pFVar6` — a raw, untagged CRT `FILE*`.
  - `FUN_182423e08` (`_fread_bound_decider.txt`): `FILE *` return; Win32
    `CreateFile`-style (`dwCreationDisposition`, `_FileHandle`) → a CRT `FILE*`.

**FRead dispatches by INDEX-vs-COUNT, not address-range** (FRead `FUN_18051cd00`,
`_research/phase8.5-pak-resolver/_readpath_decomp.txt` disasm :151-175):
- `:cd12 MOV RBP,RDX` → RBP = the handle (param_2).
- `:cd3a LEA R8,[RBP-1]` → `R8 = handle − 1` (Ghidra renders this `&param_2[-1]._tmpfname+7`,
  but the raw instruction is literally `handle − 1`).
- `:cd3e CALL FUN_180427e40([RBX+0x40])` → RAX = **the pak-handle vector ELEMENT
  COUNT**. `FUN_180427e40` body (`_fread_bound_decider.txt`):
  `return (param_1[1] - *param_1 >> 3) * -0x5555555555555555;` — the canonical
  `(end-begin)/sizeof` vector-size idiom; `>>3` × inverse-of-3 = ÷0x18 (the
  0x18-byte pak-handle stride). A small integer (the count of mounted-pak open
  handles).
- `:cd43 CMP R8,RAX; :cd46 JNC` → `handle−1 ≥ count` (unsigned) → **else / OS arm**;
  `handle−1 < count` → pak arm (which uses `R8` as the index: `:cd4c-53` =
  `base + R8*0x18`).
- The else/OS arm (decomp :117-143): `fseek(param_2,0,0)` + CRT read leaf
  `FUN_1804d7ab4(buf,1,len,param_2)` — consumes `param_2` as a `FILE*`.

**Why a `_wfopen` `FILE*` is always served by the OS arm (the decisive link, now
read not inferred):** a pak handle is `index+1`, so `handle−1 = index < count` →
pak arm (correct). A loose `FILE*` is a heap/CRT pointer (≈ `0x000001A2_xxxxxxxx`),
so `handle−1` is astronomically larger than a small vector count → `≥ count` → the
unsigned `JNC` is taken → **OS arm → direct CRT `fseek`+read on the `FILE*`.** The
two handle kinds never collide because pak handles occupy `[1 .. count]` and a real
`FILE*` is enormous. End-to-end consistent.

## What this means for the Around-FOpen seam (the design fact)

**An Around-`CCryPak::FOpen` hook serves a kcdx loose substitute by returning a
plain CRT `FILE*` opened on kcdx's own overlay file** (`_wfopen`/`fopen` the
overlay, return that `FILE*`). FRead's index-vs-count dispatch routes any real
`FILE*` to the OS arm and reads it directly — no engine wrapper, no pak-vector
slot to mint, no path-search to satisfy. This is the mechanism the handle-consumed
class (`.lua`/`.xml`) needs — the class the path-redirect approach FAILED
(`step1-data-relative-handle-consumed-fails.md`, where the mode-2 pak-only search
skipped the loose file). Returning the handle directly skips the engine's search
entirely.

## Verified vs unverified (AP19 / §4)

- **VERIFIED (read this turn, cited):** FOpen's loose branch returns a CRT `FILE*`
  (both producers); the pak branch returns `index+1`. FRead's dispatch is
  index-vs-count (`FUN_180427e40` = vector element count). A `FILE*`'s `handle−1`
  exceeds a small count → OS arm → CRT read. The Around-hook handle shape = a raw
  CRT `FILE*`.
- **UNVERIFIED (flagged, not asserted):** (1) whether the Around-cFn ABI can return
  a pointer-width `FILE*` into FOpen's `rv` slot cleanly — a kcdx hook-chain
  mechanics question settled at probe-build, not a binary fact. (2) Whether FRead
  is the SOLE read entry point a `.lua` consumer uses (vs CCryFile buffering wrapping
  it — likely benign since CCryFile routes through FOpen, but not read this turn).
  (3) The RUNTIME question — does an Around-FOpen-returned kcdx `FILE*` actually
  serve a substitute `.lua` end-to-end in-game — is the NEXT step, a /debug-style
  probe, NOT this static finding.

## Next

Static precondition MET (pending the re-gate): the Around-FOpen hook returns a raw
CRT `FILE*`; FRead serves it. The runtime probe (does a substitute `.lua` serve
end-to-end) is the next step — agent writes/builds/deploys, user launches, agent
reads the log.
