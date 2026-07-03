# KI-0028 PROBE Z5 — the abstract-stream read SUCCEEDS for every pak asset; the FS takeover is EXONERATED as the black-screen cause

**Date:** 2026-07-02 (run `kcdx-dev_2026-07-02_21-05-00.log`, full-swap arm `probe_z_live_mask=15`, `draw_indexed=0` — the black-screen repro; NO crash, force-quit from black).

## The result — decisive, theory-independent

Z4 proved the engine reader at `0x460b64` takes the ABSTRACT-stream path (`[+0x110]` non-null) on the full swap, calling the abstract stream's `[vtable+0x170]` with the kcdx handle. Z5 logged that read's RETURN (`result_size`) for the first 40 fires. **Every fire returned a plausible, non-zero, CORRECT size:**

| vpath | result_size | kind |
|---|---|---|
| `%engine%/engineassets/sky/stars.dat` | 106956 | pak |
| `%engine%/config/engine_core.thread_config` | 20096 | pak |
| `./whdlversions.json` | 15817 | loose |
| `scripts/utils/tableutils.lua` | 13358 | pak |
| `config/cvargroups/sys_spec_texture.cfg` | 6728 | pak |
| `%engine%/engineassets/defaulttextures.xml` | 6052 | pak |
| `./system.cfg` | 6038 | loose |
| … (all 40) | real byte sizes | pak+loose |

Not one `result_size=0`, not one bogus value. The `.cfg`/`.xml`/`.lua`/`.ent`/`.dat` files all return their real byte counts. **The abstract-stream read of kcdx's served assets SUCCEEDS on the full swap.**

## What this EXONERATES

The last three candidate mechanisms for the full-swap black screen are all now falsified by direct measurement:

1. **FOpen-return-type / raw-CRT-on-handle-int (fix a′'s target)** — FALSIFIED by Z4: the raw path is never taken on the full swap.
2. **Abstract-stream read FAILURE** — FALSIFIED by Z5: every abstract read succeeds with a correct size.
3. **kcdx serving wrong bytes / wrong sizes** — FALSIFIED: the sizes are correct; the vanilla-differential (PROBE W) already showed zero divergences.

**The FS takeover serves every file correctly on the full swap** — opens mint handles, the engine's abstract stream reads them, correct sizes come back. This is the same verdict as Z2.2 (kFamNone renders) and the vanilla-differential (kcdx never answers wrong), now confirmed at the byte-read level for the full swap.

## The reframe this FORCES

**KI-0028's black screen is NOT a filesystem problem.** The files load correctly; `draw_indexed` is still 0. The wedge is DOWNSTREAM of file I/O — in what the engine does with correctly-loaded data under the swap. The entire "FOpen returns a real FILE*" fix direction (Reframe 10 fix a′/b/c) is aimed at a mechanism that does not cause the black screen. It would fix the Z2.3-open CRASH (the open-only arm) — a real but SEPARATE defect — without touching the full-swap black.

This is the THIRD time the FS has been exonerated for KI-0028 (Z2.2 mechanism-innocent, PROBE W zero-divergence, now Z5 reads-succeed). The differentiator the swap introduces that causes the black is NOT in the file bytes served. It is in some OTHER state the vtable swap perturbs that the engine's render/geometry path consumes — NOT the file contents.

## What is now the open question (feeds the next probe / a step-back)

If every file reads correctly yet `draw_indexed=0`, the swap perturbs something that is NOT file content. Candidates NOT yet ruled out (each a fresh probe axis, NOT a file-read theory):
- A pointer-identity / object-identity difference the swap introduces (the engine gets a different CCryPak object or a different wrapper-object identity that a later render-path equality/type check keys on) — PROBE U (reswap) checked the CCryPak vtable identity but not downstream object identities.
- A control-flow difference: the swap makes the engine take a different BRANCH (not a wrong read) — e.g. an enumeration ORDER, a metadata answer that steers a loader, a find-iterator result — that skips geometry submission. PROBE W's enum-differential logged zero, but only for the callers it gated.
- Something entirely outside the FS slots the swap touches (the seating hook's timing, a side effect of owning the object).

The FS-read mechanism is CLOSED (exonerated). The next investigation is a STEP-BACK to "what does the swap change that is NOT a file read," not another file-path probe. This likely warrants the fresh-frame reframe (results-driven: 3rd FS-exoneration = the FS frame is exhausted).

## Note on the crash zip

`crash_2026-07-02_21-05-00.zip` has logs but NO minidump — the session did not fault (zero FAULTED_FRAMES_END); it ran black (heartbeat alive, window visible, `fg_is_ours=0`) and was force-quit. No faulting stack to read; consistent with KI-0028 being a WEDGE, not a crash. A live invasive cdb capture of the wedged process (per the KI-0028 memory: `-p` + `qd`) would show the main/render thread state but the process is now gone — owed on the next black-screen run if the wedge-location question is pursued.
