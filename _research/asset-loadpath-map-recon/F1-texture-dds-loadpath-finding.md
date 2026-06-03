# FRONT F1 — the TEXTURE (.dds) load path: read API + metadata gate

Captured 2026-06-03. Trust: PRIMARY EVIDENCE — fresh Ghidra 12.1 decompile of
WHGame.dll `release_1_5_1164953_841`, image base 0x180000000, plus a raw byte-grep of
the DLL for import names. Every edge below is READ in the owning function body (AP19 /
§3.5), with the call site cited; an unread edge is marked "unverified — not read."
NO live probe — this is the static load-path map; the runtime confirm is a separate step.

Producers (this dir + `third-party-ghidra/ghidra_scripts/`):
- `TextureLoadPathDiscover.java` → `_texture_discover_raw.txt` — .dds/texture string xrefs,
  mmap-API symbol scan, slot-83 selector, FGetCachedFileData direct-ref count.
- `TextureStreamReadPath.java` → `_texture_streamread_raw.txt` — async/file-open Win32 import
  callers, StreamPrepare + TextureStreaming.cpp bodies, StreamEngine string anchors.
- `StreamEngineReadPrim.java` → `_streamengine_readprim_raw.txt` — the StreamEngine read
  primitive bodies (DirectStorage selector, CStreamEngine init, StreamEngine.cpp,
  StreamAsyncFileRequest.cpp) + CCryPak-offset scan.
- `StreamReqOpenLeaves.java` → `_streamreq_openleaves_raw.txt` — the request's read leaves
  (CCryFile::ReadRaw + pak-entry lookup) + request callers + FOpen-caller handle-mint scan.
- `StreamOpenSite.java` → `_streamopensite_raw.txt` — the CTexture stream callers + the
  CCryFile::Open caller cluster + size/exist scan.
- `StreamOpenConfirm.java` → `_streamopenconfirm_raw.txt` — the open site body (CCryFile::Open
  call) + CCryFile::GetLength body.

---

## CLAIM 1 — the texture load entry is the CryEngine StreamEngine, NOT a direct FOpen at the loader

**How found:** `.dds` string xrefs (discovery §2) point at *registration/default-texture*
sites (`%ENGINE%/EngineAssets/Textures/*.dds` → FUN_1807aa2e4 etc.), not a hot-path loader.
The real loader is the **texture streamer**: anchors `CTexture::StreamPrepare`
(FUN_1808654b4), `TextureStreaming.cpp` (FUN_1805f9df4), and the StreamEngine cluster
(`StreamEngine.cpp` FUN_1804898d0, `StreamAsyncFileRequest.cpp` FUN_1804647fc,
`CStreamEngine` FUN_180d2a2b8). READ: `StreamPrepare` (FUN_1808654b4) only ALLOCATES the
persistent mip chain (`FUN_1807b8d20` + the "Failed to allocate memory for persistent mip
chain" error) — it does NO file I/O; it sets up the buffers the streamer fills.

**Entry points (function + RVA), READ:**
- `FUN_1807b54b4` (RVA 0x7b54b4) — the per-mip texture stream-in: open → resolve handle tag
  → read. (`_streamopensite_raw.txt`.)
- `FUN_1807b5d58` (RVA 0x7b5d58) — sibling (async variant); same open→resolve→read shape.

## CLAIM 2 — .dds is NOT memory-mapped (hypothesis KILLED at ground truth)

**READ (independent of Ghidra):** a raw byte-grep of `WHGame.dll` for the mmap/async import
names returns **zero** `CreateFileMapping[AW]` / `MapViewOfFile[Ex]` / `UnmapViewOfFile` /
`OpenFileMapping[AW]`. The ONLY async-IO-family name present is `GetOverlappedResult` (x2).
The Ghidra symbol scan agrees: all mmap APIs "NOT PRESENT in symbol table"
(`_texture_discover_raw.txt` §1). **There is no memory-mapping anywhere in the binary** — so
the ledger's "(memory-mapped)" label on the .dds class is FALSE. The earlier guess that .dds
"reached the loose-disk check through a streamer/FGetCachedFileData" (front3 inference) is
HALF right: it IS the streamer — but the streamer reads through CCryPak/CCryFile, not mmap.

## CLAIM 3 — the texture file is OPENED via CCryFile::Open → ICryPak::FOpen (slot 36)

**READ** in the open site `FUN_1807b5ed4` (RVA 0x7b5ed4; `_streamopenconfirm_raw.txt`),
called as the first step of both stream entries (`FUN_1807b54b4` line `iVar1 =
FUN_1807b5ed4(param_1, local_138)`):
- It calls `FUN_1804628a0` (the resolver string build), then for the pak-priority arm
  `(**(code**)(*plVar16 + 8))(plVar16, *(param_1+0x20), local_828, 2)` = **AdjustFileName
  (slot 1, `*pCryPak+8`)** on the texture vpath.
- It ends with `cVar10 = FUN_1804605bc(param_2, local_938, &DAT_183a53718, 8)` =
  **CCryFile::Open** (FUN_1804605bc, the front-3 id-136 helper), opening the texture file
  with the mode string at `DAT_183a53718` and flags `8`; the opened CCryFile (`param_2`)
  becomes the stream request's source struct.
- CCryFile::Open routes to **ICryPak::FOpen (vtable +0x120 = slot 36)** when an ICryPak is
  bound (the normal game case), else `fopen_s` — this is the front-3 VERIFIED CCryFile::Open
  body (`this+0x110` = bound ICryPak, `this+0x108` = the returned handle). **Cross-front edge
  re-grounded here:** the open at FUN_1807b5ed4 is the SAME FOpen seam, just wrapped in
  CCryFile and driven by the streamer.

## CLAIM 4 — the bytes are READ via CCryFile::ReadRaw, which dispatches FRead-family (slot 38) on the handle — the SAME handle-tag dispatch as .lua

**READ** the read path end-to-end:
- The stream request `FUN_1804647fc` (RVA 0x4647fc, `StreamAsyncFileRequest.cpp`) operates on
  the request struct `param_3`, whose file handle sits at `param_3+0x108` (the CCryFile
  handle offset). It calls `FUN_180463984(pCryPak, &local_70, *(param_3+0x108))` to resolve
  the handle index against the pak-handle vector, then branches: `local_70==0` (loose) →
  `FUN_180460b08(param_3, lVar17)`; `local_70!=0` (pak) → `FUN_180464b88(...)`.
  (`_streamengine_readprim_raw.txt` / `_streamreq_openleaves_raw.txt`.) NOTE: the earlier
  scan's "+0x138/+0x140" HITs in this body were FALSE POSITIVES — they are `local_60+0x138` /
  `local_60+0x140`, a LOCAL struct, NOT the CCryPak vtable (read and corrected here).
- `FUN_180463984` (RVA 0x463984) READ = **CCryPak pak-handle-by-index lookup**: `param_3-1 <
  (vec.end-vec.begin)/0x18` → returns the pak entry; the canonical index-vs-count idiom
  (identical to front3's FRead dispatch). This is the handle-tag resolve.
- `FUN_180460b08` (RVA 0x460b08) READ = **CCryFile::ReadRaw**:
  `plVar1 = *(param_1+0x110)` (bound ICryPak); if non-null →
  `(**(code**)(*plVar1 + 0x130))(plVar1, dst, 1, len, *(param_1+0x108))` = **ICryPak vtable
  +0x130 = slot 38 (FOpen-by-pak-index / FReopen, the read-family handle dispatcher)** on the
  handle at +0x108; if null → `fread(dst, 1, len, *(FILE**)(param_1+0x108))` (raw CRT fread on
  the FILE*). This is exactly the front-3 tagged-handle dispatch (pak-pseudo-handle vs real
  FILE*), reached here through CCryFile.

So a texture stream read reaches **FOpen (slot 36) at open-time + the FRead-family handle
dispatch at read-time** — the SAME machinery .lua (handle-consumed) uses, just (a) wrapped in
CCryFile and (b) pumped by the async StreamEngine in 0x100000-ish mip chunks rather than one
synchronous FRead.

## CLAIM 5 — NO pre-open path-based size/existence gate; the only size read is post-open ON THE HANDLE

**READ:** neither stream entry nor the open site calls a by-name GetFileSize (slot 45, +0x168)
or IsFileExist (slot 67, +0x218) with a PATH before opening (the size/exist scan over
FUN_1807b54b4 / FUN_1807b5d58 / FUN_1807b5ed4 reported only StreamReq + pCryPak tokens, no
+0x168/+0x218). The async entry FUN_1807b5d58 calls `FUN_180460b64` AFTER the open —
`FUN_180460b64` (RVA 0x460b64) READ = **CCryFile::GetLength**: it reads the size from the
ALREADY-OPENED handle (`ftell`/`fseek`/`ftell` on the FILE* at +0x108, or ICryPak vtable
+0x170 = slot 45 GetFileSize called ON the handle at +0x108) — for buffer allocation, not as a
pre-open admission gate. **A substitute served through the opened handle reports its own size
naturally; nothing rejects a different-sized substitute before the read.**

---

## VERDICT (for the ledger)

**FOpen+FRead reachable** — texture (.dds) loads OPEN via CCryFile::Open → ICryPak::FOpen
(slot 36) and READ via CCryFile::ReadRaw → the FRead-family handle dispatch (slot 38 / OS-arm
`fread`), the SAME tagged-handle machinery the verified Around-FOpen seam (commit e6e8e27)
serves — driven by the async StreamEngine. **NOT memory-mapped** (no mmap imports exist in the
binary, killed at ground truth). **NO pre-open size/existence gate** — the only size read is
CCryFile::GetLength on the already-opened handle.

**Confidence:** HIGH on the read-API identity and the no-mmap fact (every edge read in a body
+ a byte-grep of the binary). The open→FOpen edge is re-grounded on the front-3 VERIFIED
CCryFile::Open body (cross-front, cited), not re-walked this turn.

**Unverified — not read (flagged, not asserted):**
1. The DirectStorage path. `FUN_180d2ad38` carries "Error creating DirectStorage StreamEngine.
   Fallbacking to normal stream engine instead." — KCD2 prefers **DirectStorage** (dstorage.dll,
   an EXTERNAL module) for streaming and falls back to the CryPak StreamEngine read path mapped
   above. Whether a texture served via the DirectStorage arm bypasses CCryPak FOpen entirely
   (DirectStorage opens its own file/pak handles) is NOT read this turn — it lives in dstorage.dll
   /the DirectStorage init, outside the functions read here. This is the one path where the
   Around-FOpen seam could MISS a texture; it must be resolved (which arm is live at runtime, and
   does the DirectStorage arm touch FOpen) before any "FOpen serves ALL .dds loads" claim.
2. That the Around-FOpen-returned kcdx FILE* serves a substitute .dds end-to-end IN-GAME — a
   runtime probe, not a static fact. (The probe-archive `fopen-override.md` confirmed the .lua
   case live; the .dds case via the streamer is the analogous next probe.)
