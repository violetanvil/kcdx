# KI-0028 PROBE Z4 — the raw-CRT reader (0x460b64) takes the ABSTRACT path on the full swap, NOT the raw path; ucrtbase resolves fully

**Date:** 2026-07-02 (run `kcdx-dev_2026-07-02_17-45-28.log`, full-swap arm `probe_z_live_mask=15`, `kcdx_owned=31`, `draw_indexed=0` — the black-screen repro, NO crash).
**Two sub-probes, one launch.** Both fired decisively.

## Z4.1 (F4) — ucrtbase resolves the full stdio family in-process: RESOLVED

`ucrtbase_resolution_summary resolved=14 total=14`. Every name fix a′ needs — `_wfopen`/`fread`/`_fseeki64`/`_ftelli64`/`feof`/`fwrite`/`fflush`/`ferror`/`fgetc`/`ungetc`/`fgets`/`_fileno`/`fclose`/`_get_osfhandle` — resolves on `ucrtbase.dll` **by name, directly** via `GetProcAddress(GetModuleHandleW(L"ucrtbase.dll"), …)`. No apiset fallback needed. **The fix a′ fn-table mechanism is confirmed buildable.**

## Z4.2 (F2) — the fork-decider: the raw-CRT path is NEVER taken at 0x460b64 on the full swap

The reader at `0x460b64` dispatches on `[wrapper+0x110]` (the abstract stream): non-null → abstract-stream `[vtable+0x170]` path; null → the raw-CRT path (`ftell`/`fseek`/`fread`/`fileno` on the FILE* at `[wrapper+0x108]`) — the Z2.3-open crash site.

**Observed across the whole run (hook armed, 40 logged fires + the uncapped one-shot shout):**
- **`raw_path_taken=0` on ALL 40 fires** — `[+0x110]` is a stable non-null pointer (`abstract_stream_110=2416440932176` = `0x232…`, identical every fire). The reader takes the **abstract-stream path** every time.
- **`raw_reader_got_kcdx_handle` shouts = 0** for the entire run. That shout fires the FIRST time the RAW path is taken with a kcdx handle at `[+0x108]`; it is NOT capped by the 40-fire log limit (gated only by a one-shot flag). **Zero shouts = the raw path was never taken with a kcdx handle, the entire boot.**
- **`[+0x108]` DOES hold a kcdx handle-int** (`raw_value_108` = 3/5/7 = ids 1/2/3, `is_kcdx_handle=1`), for pak vpaths (`engine_core.thread_config`, `defaulttextures.xml`, `sky/stars.dat`, `fonts/default.xml`, the `config/cvargroups/*.cfg` — all `how=index-pak` in FS_BOOT_TRACE). But because `[+0x110]` is non-null, the reader **never operates that handle-int via raw CRT** — it dispatches to the abstract stream instead.

## What this OVERTURNS and what it leaves standing

**Overturned:** the assumption (Z2-3 §"the one link NOT yet runtime-proven", line 54) that `0x460b64`'s RAW path is where the full-swap black screen stalls. It is NOT — on the full swap, `0x460b64` takes the ABSTRACT path every time, so `fileno(handle-int)` (the Z2.3-open crash mechanism) does NOT fire here on the full swap. The Z2.3-open crash reproduces `[+0x110]==null` ONLY in the open-only arm (kFamOpen), not the full swap.

**Still standing (unchanged):**
- **The ROOT CAUSE is still proven** — kcdx's FOpen returns a handle-int not a FILE* (`open_slots.cpp`), and the Z2.3-open crash proved the engine CAN consume that return via raw CRT. That mechanism is real.
- **But WHERE it bites on the full swap is now open again.** `0x460b64` is exonerated as the full-swap stall (abstract path taken). The black screen on the full swap is NOT this reader running raw CRT on a handle-int.

## The two things this forces (NOT yet answered — feeds the fix design)

1. **The [+0x110] abstract stream is non-null AND [+0x108] holds a kcdx handle simultaneously.** WHO populates `[+0x110]`, and does the abstract-stream path (`[vtable+0x170]`) SUCCEED in reading the pak asset? If the abstract path reads correctly, these opens are fine and the black screen is elsewhere entirely (not a FOpen-return-type problem at all on the full swap). If the abstract path FAILS (reads wrong/zero), THAT is the full-swap defect — and it is a DIFFERENT mechanism than "raw CRT on a handle-int."
2. **Is fix a′ even addressing the full-swap black screen?** fix a′ (return a real ucrtbase FILE* for loose) fixes the RAW-path consumption. But the full swap never takes the raw path at `0x460b64`. So a′ fixes the Z2.3-open CRASH (open-only arm) but its connection to the full-swap BLACK SCREEN is now unproven — the black screen may be an abstract-stream-path failure, not a raw-CRT-FILE* failure.

## Next (the reframe this forces)

The fork the user was deciding (pak-needs-a-FILE*) is PREMATURE — the raw path isn't taken on the full swap at all. The real next probe: **does the abstract-stream `[vtable+0x170]` read at `0x460b64` SUCCEED or FAIL on the full swap?** Instrument the abstract path's return (bytes read / result) and compare full-swap vs the working (kFamNone) arm. That is the actual full-swap-black mechanism question — `0x460b64` raw-vs-abstract is answered (abstract), the abstract-path SUCCESS is the open unknown.

This is a Gate-A-relevant reframe: it may move the fix off "FOpen returns a real FILE*" entirely, toward "the abstract-stream object kcdx's open path installs at [+0x110] reads correctly." NO fix should be built until the abstract-path success/failure is observed.
