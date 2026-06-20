# KI-0026 probe archive — the graphics-init 0xC8 read/size/mode investigation

The instrumentation that resolved KI-0026 (graphics-init `0xC8` `CSystem::FatalError`
on `%engine%/config/engine_core.thread_config`). Captured here at close per the
no-residue discipline (`.claude/rules/working-artifacts.md`); the in-source probes
are removed. The next investigation reconstructs any of these from the recipes
below — never from live source.

Full diagnostic trail + the verified mechanism: `docs/known-issues/closed/KI-0026-fs-takeover-metadata-slots-graphics-init-fatal.md`.

## What each probe answered (verdicts)

- **PROBE K (read-family trace)** — DECISIVE: the read family IS reached with the
  correct kcdx handle, correctly tagged (`tag=1` everywhere); the handle-id-straddle
  theory is dead. (`FS_BOOT_TRACE read slot=… handle=… tag=…` in `boot_trace.h`.)
- **PROBE M (pak-mount lifecycle tap)** — EXONERATED: 5 mount-slot stubs installed,
  ZERO `pak_lifecycle_event` fired before the crash; not the cause. (Standalone
  `probe_m_pak_lifecycle.{h,cpp}`.)
- **PROBE P / P2 (CSystem::FatalError boot-window capture + the resolution deref)** —
  DECISIVE: the `0xC8` is `CSystem::FatalError` on a FAILED LOAD of
  `%engine%/config/engine_core.thread_config` (message arg deref'd from the dump);
  the miss-arm assumed miss⇒loose, but the file is pak-resident in `Engine/Engine.pak`.
  (Standalone `probe_p_fatalerror.{h,cpp}` + the P2 resolution block in
  `OpenResolvedAndMint`.)
- **PROBE P-read (served-bytes vs ground-truth + size/fd discriminator)** — DECISIVE:
  the READ is byte-correct (`got=20096`, head/tail match) but `Fileno=-1` +
  `req_count=0xFFFFFFFF` → the engine sizes its read off slot 46, which kcdx had as
  fileno. (The `probeThreadCfg` handle tag in `file_handle.{cpp,h}` +
  `ProbeLogThreadCfgRead`/`Size` in `boot_trace.h`, gated in the read slots.)
- **PROBE Q-mode (loose-open mode capture)** — DECISIVE: the engine opens
  `settings.xml` with mode `"rbx"` (`mode_hex=72 62 78`); `x` on a read base
  fast-fails kcdx's strict UCRT. (A `probe_qmode_premode` log immediately before
  `_wfopen_s` in `OpenLooseAndMint` — already removed with the mode fix.)

## Reusable recipes (reconstruct from these, not from source)

### Boot-window gate
A boot-window predicate so the path-blind read family pays nothing after boot:
`BootWindowActive()` (true until the first post-boot tick / a frame counter), gating
every probe log. Pattern: `if (BootWindowActive() && <discriminator>) LOG_DEBUG_KV(...)`.

### Per-handle thread-config discriminator (PROBE P-read)
The read family is path-blind, so tag the handle of interest at OPEN time and gate
read-side logging on the tag:
- Add a `bool probeThreadCfg` to the pool's `OpenFile` slot.
- At mint, when the resolved key contains the target (`key.find("thread_config")`),
  call `ProbeMarkThreadCfg(handle)` (sets the slot flag under the pool lock).
- Read slots gate on `ProbeIsThreadCfg(h)`.
- Log the served bytes vs ground truth: `ProbeLogThreadCfgRead(slot, handle, reqSize,
  reqCount, want, got, buf)` (dumps head64/tail32 bounds-clamped to `got`), and the
  size/fd queries: `ProbeLogThreadCfgSize(slot, handle, ret)` (FGetSize/FTell/Fileno).

### CSystem::FatalError boot-window capture (PROBE P/P2)
Hook `CSystem::FatalError` (resolve by name via the DB), log `err_id` + the format/
message varargs SEH-safe (`a3`=fmt `"%s"`, `a4`=the real string — deref the dump if
the log line doesn't capture it), forward to the original (do not suppress). For the
resolution detail, in the open path's miss arm log the inbound vpath, the captured-
original AdjustFileName's resolved disk path, and `GetFileAttributesW` existence.

### Pre-_wfopen mode capture (PROBE Q-mode)
The CRT mode fast-fail is synchronous inside `_wfopen_s`, so log the raw mode bytes
BEFORE the call (the logger fflushes per line → the line survives the fast-fail):
`LOG_ERROR_KV("FS_OPEN", "probe_qmode_premode", KV::BareStr("mode", m),
KV::BareStr("mode_hex", <hex of m[0..12]>), KV("disk", diskPath))`.

### Mount-lifecycle tap (PROBE M)
Standalone `probe_m_pak_lifecycle.{h,cpp}`: wrap the pak mount/open slots (6/7/9/10/100)
with a trampoline logging `mount_slot_wrapped` + `pak_lifecycle_event`. (Exonerated for
KI-0026; kept as a recipe for a future mount-path question.)
