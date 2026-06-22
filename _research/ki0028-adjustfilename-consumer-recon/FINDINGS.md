# KI-0028 — AdjustFileName (slot 1) consumer recon · DIVERGENCE C

**Date:** 2026-06-22 · **Trust:** primary evidence (binary xref + body reads, AP19-clean).
**Verdict: DIVERGENCE C (un-normalized pak path from AdjustFileName) is FALSIFIED as a DIRECT no-present-wedge driver.**

## Question

Slot-diff found kcdx's AdjustFileName (CCryPak vtable +0x8) returns the RAW input `pName` on a PAK
hit (e.g. `%engine%/config/...`), where the engine original returns the NORMALIZED `Data/`-rooted
path. kcdx's assumption is "every consumer is kcdx_FOpen, which re-resolves" — UNPROVEN. Does ANY
provenance-verified consumer that calls AdjustFileName through `gEnv->pCryPak` **branch on the
returned string's FORM** (its prefix / `%engine%`-vs-`Data/` rooting / a string-compare on the
result) — and is any such consumer in the WINDOW/SWAPCHAIN/PRESENT/DISPLAY-MODE/message-pump cluster,
forking boot toward the no-present wedge?

## Method (reuse-first)

Reused the proven instrument from `../ki0028-metadata-consumer-recon/` (linear capstone DRIFTS on
WHGame — do not use it). Byte-scanned the **680** `mov r64,[rip]` loads of the pCryPak global
**0x18492B850** (= gEnv 0x18492B800 + 0x50), correlated each to `mov vt,[reg]; call [vt+0x8]` —
the call edge AND receiver==pCryPak both read (AP19-clean). Then body-read each consumer's
post-call window to see what it DOES with the returned `char*` (rax). Scripts + raw dumps here.

Slot offset: slot 1 AdjustFileName = vtable **+0x8** (slot N at vtable + N*8).

## Result (VERIFIED — binary read)

- **37 provenance-verified call sites in 31 distinct enclosing funcs.** (Far fewer than the
  "hundreds" of total AdjustFileName callers — most callers reach it through wrapper helpers, not
  directly through `gEnv->pCryPak`; only the direct-through-pCryPak edge is in scope and AP19-provable.)
- **Bucket breakdown — what each consumer does with the returned path:**
  - **File-op consumer (passes the result straight to a file operation): all 31 funcs.** The return
    feeds FOpen (slot +0x10), IsFileExist3 (slot +0x218), an fopen wrapper (with `r+b`/`w+b` mode),
    a CryString copy-then-open, FWrite (+0x28), or a config/XML open vtable call. The path's FORM is
    never the branch condition — the string is consumed as a file handle's argument.
  - **String-FORM branch (prefix test / compare on `%engine%`-vs-`Data/` rooting): 0 funcs.**
  - The only `cmp byte ptr` against an ASCII constant anywhere in the 37 windows is a trailing-
    **separator** check (`/`=0x2f, `\`=0x5c) at three sites — a generic "does the copied buffer end
    in a separator, if not append one" path-builder on a LOCAL copy, NOT a root-prefix branch.
    No site compares the result against a fixed root literal (`Data`, `engine`, `%engine%`).

- **Window/present/display cluster — every one is a file-op consumer (bodies read):**
  - `0x245b5cc` (**r_WindowType**) site `0x245b5df`: result -> `je -> call [vt+0x10]` = **FOpen**. Opens a file; no form branch.
  - `0x245df70` (**r_Fullscreen**) site `0x245e039`: result -> CryString copy (`call 0x4f692c`), then a cvar `cmp r8d,-1` (a GLOBAL, not the returned string). No form branch.
  - `0x1e03c30` (**DriverD3D.cpp**) site `0x1e03ca8`: result -> string-copy into a MAX_PATH local (`call 0x4ab5db4`, rdx=0x104), then strlen + trailing-`/`/`\` separator-append. Branches on the COPIED buffer's last char, not on `%engine%`-vs-`Data/`.
  - `0xbb1fe4` (**PipelineStateCacheManager.cpp**) site `0xbb2061`: result -> `call 0xbb20e0` returning `al` (IsFileExist-style bool), `test al,al; je` skip. Existence boolean + separator-append; no form branch.
  - `0x24265d8` (**cursor** — "Unable to load cursor from windows resource id") site `0x242679d`: result -> `call 0x4d455c` (formatted-string/log build), then ret. No form branch.

- **Strongest 2-3 consumers body-read (the most boot-relevant):**
  - **`0x1e03c30` DriverD3D.cpp** — the actual D3D render driver (where present/swapchain lives). AdjustFileName's return is copied to a local path buffer and separator-normalized, then used as a filename. It does NOT inspect whether the path is `%engine%`-rooted; the un-normalized form flows through the same copy/open path as any other. CLEAN.
  - **`0x245b5cc` r_WindowType / `0x245df70` r_Fullscreen** — display-mode config funcs. Each opens/streams a file built from the returned path (FOpen / CryString-open). The path is a file argument; the branch after is on a cvar global / FOpen handle, never on the path's root form. CLEAN.
  - **`0x241b340` level-cache** (`Level cache pak file %s does not exist`) site `0x241b40b`: result -> slot-67 **IsFileExist3** (`call [vt+0x218]`), `test al,al; jne use / je "does not exist"`. The same existence gate the metadata-recon already body-read from the slot-67 side — an existence boolean, not a path-form branch.

## Consequence

No provenance-verified AdjustFileName consumer — including every window/present/display-mode/render
consumer in scope — branches on the returned string's FORM. Every consumer feeds the result to a
file operation or a generic separator-normalization on a copied buffer. kcdx's "the consumer always
re-resolves via a file op" assumption holds across all 31 funcs: the un-normalized `%engine%`-rooted
return is consumed as a file-op argument (which kcdx_FOpen re-resolves), never as a string whose
prefix steers boot toward no-present. Divergence C cannot DIRECTLY drive the no-present wedge.

This completes the static falsification of all three return-contract divergences (A existence-timing,
B find-handle straddle, C un-normalized path) as DIRECT wedge drivers — corroborating PROBE M/PROBE L
from the consumer side.

**Residual (NOT statically traceable, same shape as the metadata-recon residual):** an un-normalized
path consumed by a file op could, multi-hop, cause a WRONG file/handle to resolve (if kcdx_FOpen's
re-resolution of the raw `%engine%/...` form ever mismatches the engine's `Data/`-rooted key — the
KI-0026 alias-namespace mismatch is exactly this class), whose effect surfaces LATER as the
un-presentable swapchain. That is a kcdx_FOpen alias-resolution question (KI-0026), not an
AdjustFileName-consumer-branch question — needs a live swap-on/off probe of the FOpen resolution
path, not more static consumer work.

## Reuse

- pCryPak global = `0x18492B850` (gEnv `0x18492B800` + 0x50). 680 loads. AdjustFileName = vtable **+0x8**.
- `correlate_adjustfilename_slot.py` — byte-scan + correlate, parameterized on `SLOTOFF` (set 0x8 here). Re-target to any slot by changing one constant.
- `classify_adjustfilename_callers.py` — resolved-string classifier (window/present/display/gfx vocab flags). Reads func list from the correlation output.
- `read_returns.py` — post-call-window dumper; flags file-op slots + CMP/TEST/PREFIX on the result. The load-bearing instrument (it answers "branches on form?" directly).
- Raw dumps: `_slot1_callers.txt`, `_slot1_classified.txt`, `_slot1_returns.txt`.
- The byte-scan+correlate approach is the reusable xref instrument for any CCryPak slot; linear capstone is unreliable on WHGame.
