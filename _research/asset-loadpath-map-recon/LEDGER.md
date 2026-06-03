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
| F1 | Texture `.dds` (memory-mapped) | how does the engine load a texture — does it FOpen+FRead, or memory-map (CreateFileMapping/MapViewOfFile), or stream? (it overrode live via FOpen — but which READ path?) | NOT STARTED | — |
| F2 | Script/XML `.lua`/`.xml` (handle-consumed) | confirmed reaches FOpen+FRead (front3); re-ground: is FRead the consumer, any upstream size/exist check? | NOT STARTED | — |
| F3 | Model `.cgf`/`.cdf` | the model load entry — FOpen+FRead, mmap, or the streaming engine (IStreamEngine)? | NOT STARTED | — |
| F4 | Audio `.ogg`/sound | audio load/stream entry — almost certainly streamed/async; does it touch FOpen at all? | DONE (`F4-audio-findings.md`) | **FOpen reachable** — middleware = FMOD (imports `fmod.dll`+`fmodstudio.dll`). FMOD opens via CCryPak::FOpen (slot 36) — wired by `FMOD::System::setFileSystem(open,close,read,seek,...)` @ FUN_180d2fde4; callbacks: open=slot36, read=slot38, seek=slot53, close=slot55 (each body-read). NO bypass: no Win32 CreateFile, no FMOD fopen, async callbacks NULL (synchronous). Banks/sounds load by FILENAME (`loadBankFile`/`createSound`), not memory buffer. CAVEAT: audio READ uses slot 38 (+0x130), NOT the slot-40 (FGetCachedFileData) seam FRead was verified against — both reach the same OS primitive `FUN_1804d7ab4` via the same index-vs-count gate (read this run), so an FOpen-owning seam covers audio, but the slot-38 consumer edge is distinct from slot-40 and should be gate-confirmed. |
| F5 | Streamed / level / large data | the streaming engine path (slot 38 FReopen / pak-stream vector / async) — does ANY large-asset path bypass FOpen entirely? | NOT STARTED | — |
| F6 | Metadata gate (cross-cutting) | do the size/existence surfaces (slots 45 GetFileSize / 67 IsFileExist, which DO call AdjustFileName) gate asset loads — i.e. would a different-sized substitute be rejected before the read? | DONE (`F6-metadata-gate-finding.md`) | MIXED — slot 45 sizes from pak-dir entry (mode-2 default) or OS stat VIA slot 1, NOT via FOpen → an FOpen-ONLY override mis-sizes a different-sized substitute (size-mismatch MECHANISM verified, decompiled); slot 67 is existence-only (cannot mis-size). BUT no common size-gated consumer was READ to confirm it fires ("unverified — not read"). Slot-1 (id 152) seam closes the gap; FOpen-only is the surgical subset that leaves it open. |

## Synthesis (after fronts return — re-grounded, then gated)

Pending. The synthesizer re-reads each cross-front edge in the owning body before
asserting the per-class verdict; the coverage conclusion gates before it becomes
the "any asset" design authority.

## Next

Fronts dispatched as read-only measurement subagents (Type B) returning claims+evidence;
synthesizer re-grounds; gate; then ONE multi-class runtime probe overlays a
representative asset of each FOpen-reachable class and observes which serve.
