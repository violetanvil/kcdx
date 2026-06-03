# Asset load-path map — per-class call-graph investigation (ledger)

Goal: establish, PER ASSET CLASS, whether its load path goes through `FOpen`+`FRead`
(the verified Around-FOpen seam serves it) OR through memory-map / streaming / async
I/O (a different seam), AND whether a metadata check (size/existence, slots 45/67)
upstream would reject a substitute before the read. This is the coverage map the
"load ANY asset" claim needs — not one mechanism generalized (the AP19 trap).

Discipline: this is a MULTI-FRONT disassembly (§4.5). Each front returns CLAIMS +
evidence (the load-entry body + which read API it reaches), NOT assembled
conclusions. The synthesizer (me) re-grounds every cross-front edge in the owning
body; the per-class load-path conclusions GATE through a body-read verifier before
they ship as authority (§4.5). A front states only what it read in its class's
load path.

Verified foundation (commit e6e8e27, gated PROCEED): Around-FOpen returns a CRT
FILE*; FRead (FUN_18051cd00 = FGetCachedFileData) routes index-vs-count, serves a
loose FILE* via the OS arm. So: any class whose load path reaches FRead's OS arm
through FOpen is served by the verified seam. The open question per class: DOES it?

## Front ledger — one row per asset class's load path

| Front | Asset class | Load-entry question | Status | Verdict (FOpen+FRead? / mmap / stream / metadata-gated?) |
|---|---|---|---|---|
| F1 | Texture `.dds` (NOT mmap — async StreamEngine) | how does the engine load a texture — does it FOpen+FRead, or memory-map (CreateFileMapping/MapViewOfFile), or stream? (it overrode live via FOpen — but which READ path?) | DONE (`F1-texture-dds-loadpath-finding.md`) | **FOpen+FRead reachable (loose lane)** — texture stream OPENS via CCryFile::Open→ICryPak::FOpen(slot36) at `FUN_1807b5ed4` (RVA 0x7b5ed4), READS via CCryFile::ReadRaw (`FUN_180460b08`)→ICryPak slot38 / OS-arm `fread` on the handle@+0x108, pumped by the async StreamEngine (`StreamAsyncFileRequest.cpp` `FUN_1804647fc`); same tagged-handle dispatch as .lua. **NOT mmap** — zero CreateFileMapping/MapViewOfFile imports in the binary (byte-grep + symbol scan; killed at ground truth). **No pre-open path size/exist gate** — only CCryFile::GetLength post-open on the handle. CAVEAT (corroborates F5): the pak-arm read leaf IS F5's `FUN_180464b88` (ZipDir streaming read on the mount-minted CreateFileA handle), so a pak-resident texture is served via the MOUNT lane, not the FOpen handle-mint lane. FLAGGED unverified: the **DirectStorage** arm ("Fallbacking to normal stream engine instead.", `FUN_180d2ad38`) may bypass FOpen entirely via dstorage.dll — resolve which arm is live before "FOpen serves ALL .dds". |
| F2 | Script/XML `.lua`/`.xml` (handle-consumed) | confirmed reaches FOpen+FRead (front3); re-ground: is FRead the consumer, any upstream size/exist check? | DONE (resolved by reuse — F1/F4/F6 reads) | **FOpen loose lane reachable; no pre-open size gate.** Resolved without a fresh front: the `.lua`/`.xml` opener is **CCryFile::Open** (`FUN_1804605bc`, front3's id) — F1 read it on the texture loose path (CCryFile::Open→ICryPak::FOpen slot36), F4 confirmed the same FOpen→slot38/40 dispatch, and F6 read that CCryFile::Open (~24 callers) does NOT pre-size (opens and reads on the handle). So scripts/XML take the FOpen loose lane an Around-FOpen seam covers, with no slot-45/67 admission gate. CAVEAT: same as every class — a PAK-resident `.lua`/`.xml` reads via the mount/stream lane (F5), not the FOpen handle-mint lane; only a LOOSE substitute is reached by Around-FOpen. (No separate F2 finding doc — the reads live in F1/F4/F6.) |
| F3 | Model `.cgf`/`.cdf`/`.chr`/`.skin` | the model load entry — FOpen+FRead, mmap, or the streaming engine (IStreamEngine)? | DONE (`F3-model-cgf-cdf-loadpath-finding.md`) | **FOpen+FRead reachable (loose lane) — MIXED like F1.** Sync model/static-mesh OPENS via `CReadOnlyChunkFile::Read` (`FUN_18051cba0`, RVA 0x51CBA0) → `ICryPak::FOpen` slot36 (+0x120) on gEnv->pCryPak (DAT_18492b850), handle@this+0x40; READS via slot40 FGetCachedFileData (arm A) OR slot45 GetFileSize-on-handle + slot38 FReadRaw (arm B, `+0x3f` flag) — same tagged-handle dispatch as F1/F4, reached through CLoaderCGF::LoadCGF→`(*chunkfile+0x20)`. **NOT mmap** (zero CreateFileMapping/MapViewOfFile imports — F1 ground truth, re-grounded). **NO pre-open size/exist gate** — FOpen called directly on the path; slot45 size read is post-open ON THE HANDLE (arm B, for buffer alloc). CReadOnlyChunkFile::Read = the single shared chunk open-site (1 direct caller, virtual-dispatched). CAVEAT (corroborates F5): the ASYNC character path (`CryCHRLoader::EndStreamSkel`/`EndStreamSkinAsync`, FUN_18051c9b4/FUN_18051db24) is delivered by the STREAMING chunk-loader = F5's MOUNT lane (pak-resident, NOT reached by Around-FOpen). FLAGGED unverified: the async stream-BEGIN seam (CryCHRLoader::BeginStream) was not decompiled — by F5 it's the mount/CreateFileA lane, not FOpen. |
| F4 | Audio `.ogg`/sound | audio load/stream entry — almost certainly streamed/async; does it touch FOpen at all? | DONE (`F4-audio-findings.md`) | **FOpen reachable** — middleware = FMOD (imports `fmod.dll`+`fmodstudio.dll`). FMOD opens via CCryPak::FOpen (slot 36) — wired by `FMOD::System::setFileSystem(open,close,read,seek,...)` @ FUN_180d2fde4; callbacks: open=slot36, read=slot38, seek=slot53, close=slot55 (each body-read). NO bypass: no Win32 CreateFile, no FMOD fopen, async callbacks NULL (synchronous). Banks/sounds load by FILENAME (`loadBankFile`/`createSound`), not memory buffer. CAVEAT: audio READ uses slot 38 (+0x130), NOT the slot-40 (FGetCachedFileData) seam FRead was verified against — both reach the same OS primitive `FUN_1804d7ab4` via the same index-vs-count gate (read this run), so an FOpen-owning seam covers audio, but the slot-38 consumer edge is distinct from slot-40 and should be gate-confirmed. |
| F5 | Streamed / level / large data | the streaming engine path (slot 38 FReopen / pak-stream vector / async) — does ANY large-asset path bypass FOpen entirely? | DONE (`F5-streaming-engine-bypass.md`) | **No RESOLUTION bypass** — `CStreamEngine`/`ZipDir::ReadFileStreaming` (`FUN_180464b88`) does its OWN Win32 `ReadFile`/`SetFilePointer` (synchronous, LPOVERLAPPED=0, not FRead) but ONLY on the `m_zipFile` HANDLE opened DURING the CCryPak pak MOUNT (archive factory slot 72 `FUN_1804d5580` → `FUN_1804d6910` `CreateFileA` NO_BUFFERING, same pak path as the buffered FILE* at `:16`). It reads the pak the resolver chose; it never independently searches/opens an asset file. Gated by `sys_UncachedStreamReads`; falls back to the ZipDir FRead/FSeek family (`FUN_180461a5c`). Open-seam nuance: the streaming handle is minted by `CreateFileA` at mount, NOT FOpen slot 36 — so a LOOSE overlay via Around-FOpen does NOT reach a streamed-from-pak asset (covered by the resolver/mount lane, fronts 2/4, not the FOpen handle-mint lane front3). The 13 crude "BYPASS-CANDIDATE" fns are CRT/std::fs/minidump/volume internals + the FOpen `&0x10` producer; only `FUN_180464b88` is an asset-streaming read. |
| F6 | Metadata gate (cross-cutting) | do the size/existence surfaces (slots 45 GetFileSize / 67 IsFileExist, which DO call AdjustFileName) gate asset loads — i.e. would a different-sized substitute be rejected before the read? | DONE (`F6-metadata-gate-finding.md`) | MIXED — slot 45 sizes from pak-dir entry (mode-2 default) or OS stat VIA slot 1, NOT via FOpen → an FOpen-ONLY override mis-sizes a different-sized substitute (size-mismatch MECHANISM verified, decompiled); slot 67 is existence-only (cannot mis-size). BUT no common size-gated consumer was READ to confirm it fires ("unverified — not read"). Slot-1 (id 152) seam closes the gap; FOpen-only is the surgical subset that leaves it open. |

## Synthesis (all 6 fronts in — re-grounded; GATE-PENDING before it ships as authority)

**STATUS: §4.5 gated body-read verifier PROCEEDED (2026-06-03) — ships as the
asset-system seam's design authority.** The gate verified GATE-1 (every class's
open→FOpen edge read in an owning body), GATE-2 (the decisive lane-split: pak-
resident bytes read on the mount-minted CreateFileA handle, read as an explicit
loose-vs-pak branch in `FUN_1804647fc`; and at `sys_pakPriority 2` the resolver
`FUN_18046205c` tests PAK-ONLY so a vanilla path is never resolved loose — the
decompiled proof that FOpen-alone can't replace a vanilla asset), and GATE-3
(F6's size-mismatch mechanism read + correctly LOW-confidence-flagged). Two
non-blocking notes survive to the runtime-probe step: the texture open→FOpen edge
is a two-hop chain depending on front3's CCryFile::Open body (cross-dir); and the
DirectStorage arm (default OFF) remains FLAGGED-unverified.

The six fronts converge on ONE consistent model. The load-bearing cross-front
edges are re-grounded below (the front that READ each, with its cite).

### The convergent model — every class opens via FOpen, but there are TWO LANES

1. **Every asset class's SYNCHRONOUS open goes through `CCryPak::FOpen` (slot 36,
   +0x120) on gEnv->pCryPak.** Read per class in the owning open-site body:
   texture `FUN_1807b5ed4` (F1), model `CReadOnlyChunkFile::Read` `FUN_18051cba0`
   (F3), audio FMOD `useropen` callback `FUN_181224d1c` (F4), script/XML
   CCryFile::Open `FUN_1804605bc` (F1/F4/F6). No class memory-maps (zero
   CreateFileMapping/MapViewOfFile imports — F1 byte-grep ground truth). **[GATE-1]**

2. **But bytes arrive on one of TWO lanes, and Around-FOpen only owns one:**
   - **LOOSE lane** — when the resolver picks a loose file, FOpen mints a CRT
     `FILE*` (verified foundation, commit e6e8e27); the read family (slot 38/40)
     serves it via the OS arm. **An Around-FOpen hook returning a kcdx `FILE*` is
     served here.** This is the lane that covers a LOOSE substitute of any class.
   - **MOUNT/STREAM lane** — when the resolver picks a PAK-resident asset, the
     bytes are read by `ZipDir::ReadFileStreaming` (`FUN_180464b88`, F5) via Win32
     `ReadFile` on a `CreateFileA` handle **minted at pak-mount** (slot 72
     `FUN_1804d5580`), NOT by FOpen. The async character path (F3:
     CryCHRLoader::EndStream*) and the pak-resident texture path (F1) read here.
     **An Around-FOpen overlay does NOT reach a pak-resident/streamed asset.** **[GATE-2]**

3. **The size query is on the slot-1 resolver lane, not FOpen** (F6): slot-45
   GetFileSize calls AdjustFileName directly and sizes from the pak-dir entry (mode-2
   default), which an FOpen-only override does not change → a different-sized loose
   substitute mis-sizes IF a load pre-sizes via slot-45. (No common pre-size-gated
   consumer was READ to fire — F6 LOW confidence it bites; the model arm-B sizes
   post-open on the handle, F3, so it is NOT exposed to this.)

### What this means for "load ANY asset" (the question that drove this)

**Around-FOpen alone does NOT deliver "any asset."** It delivers a **loose
substitute** of any class (open routed through FOpen → loose lane → served). It
does NOT reach a **pak-resident** asset's bytes (mount/stream lane), and an
FOpen-only override leaves the slot-45 size query stale. The class that "any asset"
needs — replace a vanilla asset (which is pak-resident) AND add a new loose asset —
requires owning the **resolver decision (slot 1 AdjustFileName / the mount lane)**,
NOT just the FOpen handle. This re-converges on the ORIGINAL design §7 instinct
(own the resolver), but now grounded in WHY: the resolver lane is the only seam
that reaches both lanes' byte sources, because it decides WHICH file (loose or pak)
every lane then reads.

The corrected seam picture (gate-pending): **kcdx owns the resolver (slot 1
AdjustFileName decision) so a vanilla→loose-overlay redirect makes the mount/stream
lane read kcdx's file; the FOpen loose-lane + the verified `FILE*` mechanism is the
fallback/add-new path.** The earlier "FOpen-only is the seam" framing is INSUFFICIENT
for the replace-a-pak-asset case — F5's mount-lane finding is the proof.

### Re-grounded cross-front edges (the load-bearing ones)

- "FOpen slot 36 is the universal sync open" — each class's open-site body read by
  its front (F1 0x7b5ed4, F3 0x51cba0, F4 0x181224d1c, F2-via-F1/F4 CCryFile::Open).
- "pak-resident bytes come from the mount-minted CreateFileA, not FOpen" — F5 read
  `FUN_180464b88` + the mount open `FUN_1804d6910` CreateFileA at slot-72 mount.
- "slot-45 sizes via slot-1, not FOpen" — F6 read `FUN_182418b48` body.

## Next

Fronts dispatched as read-only measurement subagents (Type B) returning claims+evidence;
synthesizer re-grounds; gate; then ONE multi-class runtime probe overlays a
representative asset of each FOpen-reachable class and observes which serve.
