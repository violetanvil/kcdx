# FRONT 5 — the streaming / async-I/O path: does any large-asset load BYPASS CCryPak::FOpen?

Captured 2026-06-03. Trust level: PRIMARY EVIDENCE — fresh-Ghidra (12.1) decompile
of WHGame.dll `release_1_5_1164953_841`, image base 0x180000000. Every claim below
is READ in the cited body (AP19 / §3.5). Slots cross-checked against front1's
binary-read vtable @ 0x183A95FA8. No live probe; this is a static load-path map.

Producers (this dir + `third-party-ghidra/ghidra_scripts/`):
- `StreamEngineOpenPath.java` → `_streamengine_open_*` (Win32-file-API caller scan +
  streaming-string-anchor scan + per-fn BYPASS-vs-via-FOpen classification).
- `StreamReadLeafDecomp.java` → `_streamread_leaf_decomp.txt` (the streaming read leaf
  + its caller bodies).
- `ZipDirHandleSource.java` → `_ziphandle_source.txt` (the four band CreateFile
  producers — which one mints the streaming handle).
- `ZipUncachedOpenChain.java` → `_zip_uncached_chain.txt` (the mount-path open step +
  the streaming fallback read leaf).

---

## VERDICT (one line for the ledger)

**No bypass — the streaming engine reads from the SAME pak file the CCryPak mount
opened; it does NOT have an independent file-open path that skips CCryPak resolution.**
The streaming engine performs its OWN Win32 `ReadFile`/`SetFilePointer` (not FRead),
but ONLY on a HANDLE minted DURING the CCryPak pak-mount (archive factory slot 72), to
the pak the resolver already chose. Confidence: VERIFIED by body-read for the
pak-streaming path; the open seam is `CreateFileA` inside the mount, NOT `FOpen` slot 36
(an open-seam nuance, see "The one nuance" below).

---

## THE EVIDENCE CHAIN (all body-read, cited)

### 1. A streaming engine EXISTS and reads with its own Win32 ReadFile

- `CStreamEngine` is real (string `'CStreamEngine'` referenced by `FUN_180d2a2b8`;
  `StreamEngine.cpp` / `StreamAsyncFileRequest.cpp` build-path strings in the read
  functions). A DirectStorage variant exists but FALLS BACK: string
  `"Error creating DirectStorage StreamEngine. Fallback..."` (`FUN_180d2ad38`),
  cvar `wh_sys_streaming_directstorage_enabled` default 0 (`_pakpriority_cvar_reg.txt`).
- The streaming READ leaf is **`ZipDir::ReadFileStreaming` = `FUN_180464b88`** (RVA
  0x464B88). Body (`_streamread_leaf_decomp.txt`):
  - `:63 SetFilePointer(*(HANDLE*)(param_1 + 0x10), ...)` — Win32 seek on a HANDLE at
    zipDir+0x10 (`m_zipFile`, named verbatim in the error string).
  - `:83 ReadFile(*(HANDLE*)(param_1 + 0x10), *(LPVOID*)(param_1 + 0x20), nToRead,
    &nRead, (LPOVERLAPPED)0x0)` — a **synchronous** `ReadFile` (overlapped arg NULL)
    into the cache's read buffer at zipDir+0x20.
  - Error strings: `"ZipDir::ReadFileStreaming ReadFile() failed (m_zipFile:%p,
    pFileEntry:%p)..."`, `"...SetFilePointer() failed..."`.
  - This path is GATED by `sys_UncachedStreamReads` — `:51 (DAT_1849272b8 != 0)`. When
    off (or the uncached handle is absent, `:53 m_zipFile == INVALID_HANDLE`), it FALLS
    BACK at `:121` to `FUN_180461a5c`.

### 2. The streaming HANDLE (`m_zipFile`) is opened during the CCryPak pak MOUNT

The decisive read — `_zip_uncached_chain.txt`, `FUN_1804d5b74` (the ZipDir cache
builder's open step, **caller = `FUN_1804d5580` = archive factory slot 72**, front2's
mount-path RW arm):

- `:16 lVar3 = FUN_1809b2b28(pakPath, "rb"); *(param_1+8) = lVar3;` — opens the pak
  file with the standard CryPak opener (`FUN_1809b2b28`, the `_wfopen`-backed producer
  front1 / `asset-fopen-handle-recon` identified) → the buffered `FILE*`.
- `:28 FUN_1804d6910(plVar1, pakPath)` — ADDITIONALLY opens the SAME pak path as an
  UNCACHED Win32 HANDLE and stores it at `[param_1+0x10]` (= `m_zipFile`). Body of
  `FUN_1804d6910` (`_ziphandle_source.txt`): `CreateFileA(pakPath, GENERIC_READ,
  FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING|NORMAL, NULL)`.
  Failure is NON-FATAL — `:30 "Failed to open '%s' for unbuffered IO."` (buffered
  FILE* still serves).

So the mount opens **two handles to the same mounted pak**: a buffered `FILE*` for the
normal read family, and an uncached HANDLE for the streaming engine's direct `ReadFile`.
Both target the pak the CCryPak resolver chose; neither is an asset-specific open.

### 3. The streaming caller resolves THROUGH the CCryPak pak machinery

`FUN_1804647fc` (the streaming-read caller, "File:'%s', pak:'%s'" — `_streamread_leaf_decomp.txt`):
- `:165-173` resolves via `DAT_18492b850` (= gEnv+0x50 = pCryPak) through
  `FUN_180463984` + `FUN_180463abc` (pak-path / archive accessors over the loaded-pak
  array) — the same machinery front2/front4 mapped.
- `:227` calls the streaming leaf with the ZipDir object + file-entry FROM that pak
  resolution (`*(local_70+0x18)`, `*(local_70+0x28)`).
- `:232` build-path `StreamEngine\StreamAsyncFileRequest.cpp:0x32f`,
  `"Error: Streaming read failed. File:'%s', pak:'%s', offset:%u, size:%u"` — proves
  the streamed bytes come FROM a pak, offset+size into it.

### 4. The streaming fallback IS the CCryPak/ZipDir read family

`FUN_180461a5c` (the `:121` fallback — `_zip_uncached_chain.txt`) carries
`"ZipDir::FRead failed..."`, `"ZipDir::FSeek failed..."`, `ZipDirCache.cpp:0x15f`
`"Could not decompress file..."` — it is the standard CCryPak/ZipDir pak read (seek +
decompress), reusing the buffered FILE* arm. So even when the uncached fast-path is
off, the bytes still come through CCryPak's pak machinery.

---

## WHAT THIS MEANS FOR THE "load ANY asset" / Around-FOpen COVERAGE QUESTION

- **A pak-resident asset that streams is NOT a coverage hole for pak-overlay control.**
  Its bytes come from a pak the CCryPak resolver (slot 1 AdjustFileName / the mount
  machinery) already selected. A kcdx resolver that owns pak mount + resolution
  (front2/front4: own slot 1, drive OpenPack) controls which pak the streamer reads —
  the streamer just reads faster (uncached ReadFile) from that already-chosen pak.
- **BUT the streaming fast-path is NOT served by an Around-`CCryPak::FOpen` (slot 36)
  hook.** The streaming `m_zipFile` HANDLE is minted by a direct `CreateFileA` inside
  the archive factory (`FUN_1804d6910`), NOT by `FOpen` slot 36 (0x4614A0). The verified
  Around-FOpen seam (commit e6e8e27) serves the **handle-consumed FRead path** (`.lua`,
  config, `CCryFile` — front3); it does NOT intercept the streaming engine's per-pak
  uncached HANDLE. So a kcdx LOOSE overlay returned via Around-FOpen is invisible to the
  streaming path — a streamed class would read the pak's bytes through `m_zipFile`, not
  the kcdx FILE*.
- **Net for the "load ANY asset" claim:** the streaming engine does NOT bypass CCryPak
  RESOLUTION (no independent search/open of an asset file) — so it is covered by
  resolver/mount ownership. It DOES bypass the FOpen-slot-36 OPEN SEAM (it opens the pak
  via CreateFileA at mount). Therefore a LOOSE-file overlay delivered purely via
  Around-FOpen does NOT reach a streamed-from-pak asset; reaching streamed classes
  requires owning the pak the streamer mounts (resolver/mount lane), not the FOpen
  handle-mint lane. The two lanes are front3 (FOpen handle = loose/`.lua`) vs this front
  (pak mount handle = streamed pak assets).

### The one nuance (open-seam, not resolution)

"Bypasses FOpen" is TRUE at the literal slot-36 level (the streaming handle is a
`CreateFileA`, not a `FOpen` call) but FALSE at the resolution level (the pak it opens
was chosen by CCryPak; it never independently searches for or opens an arbitrary asset
file). For the coverage map the resolution-level answer is the load-bearing one: **no
asset class reaches its bytes through a file the CCryPak resolver did not pick.**

---

## CONFIDENCE MAP

VERIFIED (decompiled this run, cited):
- `ZipDir::ReadFileStreaming` `FUN_180464b88` does Win32 `SetFilePointer`+`ReadFile`
  (synchronous, LPOVERLAPPED=0) on HANDLE `m_zipFile` at zipDir+0x10; gated by
  `sys_UncachedStreamReads` (DAT_1849272b8); falls back to `FUN_180461a5c`.
- The `m_zipFile` HANDLE is opened by `FUN_1804d6910` (`CreateFileA` NO_BUFFERING) inside
  `FUN_1804d5b74`, whose sole caller is archive factory slot 72 `FUN_1804d5580` (the pak
  MOUNT path, front2). Same pak path as the buffered `FILE*` opened at `:16` via
  `FUN_1809b2b28`.
- The streaming caller `FUN_1804647fc` resolves the pak via pCryPak (gEnv+0x50) and reads
  by pak offset/size; the fallback `FUN_180461a5c` is the ZipDir FRead/FSeek family.
- The other three band `CreateFile` producers are NOT asset paths: `FUN_182423e08` = the
  FOpen `&0x10` CRT-FILE* producer (routes through FOpen, front3-consistent);
  `FUN_18192d410` = volume-mount-point query; `FUN_182476768` = minidump writer.
- Classification totals (StreamEngineOpenPath): 92 candidate fns; 13 "BYPASS-CANDIDATE"
  by the crude Win32-open-without-FOpen heuristic, of which only `FUN_180464b88` is an
  ASSET-streaming read — the rest are CRT/std::fs/minidump/volume internals.

NEEDS-LIVE-CONFIRM (not a binary fact; not required for this front's verdict):
- Whether `sys_UncachedStreamReads` is ON by default in the shipped build (would decide
  whether the uncached ReadFile fast-path or the buffered ZipDir fallback is the live
  read path — both read the same mounted pak either way).
- A loose-file overlay for a streamed-from-pak class is confirmed NOT served by
  Around-FOpen ONLY at the static level; a runtime probe (overlay a streamed asset
  loose, observe it ignored) would close it — but the static chain already shows the
  streamer never consults a loose path.

## SEED-ROW CANDIDATES (AP18 — FLAGGED, NOT written; need user sign-off)

Only if kcdx ever needs to own the streaming open/read directly (NOT recommended —
owning resolver+mount, fronts 2/4, already controls which pak the streamer reads):
- `ZipDir::ReadFileStreaming` — `FUN_180464b88`, RVA 0x464B88 (the uncached ReadFile leaf).
- ZipDir uncached opener — `FUN_1804d6910`, RVA 0x4D6910 (the `CreateFileA` NO_BUFFERING
  that mints `m_zipFile`); its parent `FUN_1804d5b74` (RVA 0x4D5B74) is the mount-path
  open step under archive factory slot 72 (already flagged by front2).
- ZipDir non-streaming read fallback — `FUN_180461a5c`, RVA 0x461A5C (ZipDir FRead/FSeek
  family).
Already seeded/flagged: FOpen id 131 (slot 36), gEnv_pCryPak id 132, archive factory
slot 72 `FUN_1804d5580` (front2).
