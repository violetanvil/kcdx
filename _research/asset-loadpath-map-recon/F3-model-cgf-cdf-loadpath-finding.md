# FRONT F3 — the MODEL (.cgf/.cdf/.chr/.skin) load path: open + read lane

Captured 2026-06-03. Trust: PRIMARY EVIDENCE — fresh Ghidra 12.1 decompile of
WHGame.dll `release_1_5_1164953_841`, image base 0x180000000. Every edge below is READ
in the owning function body (AP19 / §3.5) with the call site cited; an unread edge is
marked "unverified — not read." NO live probe — this is the static load-path map.

Producers (this dir + `third-party-ghidra/ghidra_scripts/`):
- `ModelOpenSiteDiscover.java` → `_model_opensite_raw.txt` — .cgf/.cdf/.chr/.skin + CGF/StatObj/
  ChunkFile/CharacterManager/Cry3DEngine string xrefs → 221 referencing fns, each scanned for
  the verified open/read/stream targets in its decompiled body.
- `ModelChunkReaderTrace.java` → `_model_chunkreader_raw.txt` — full decomp of
  C3DEngine::LoadChunkFileContent's chain (CLoaderCGF::LoadCGF + the CGF content-instance ctor).
- `ChunkFileReadOpenSite.java` → `_chunkfile_read_opensite_raw.txt` — resolves CReadOnlyChunkFile's
  vtable and decompiles slot 4 (= the OPEN/Read), + the CGF parser FUN_1806a24e4.
- `CharLoaderOpenSite.java` → `_char_loader_opensite_raw.txt` — CryCHRLoader/CharacterManager/
  SkinLoader anchor fns + the direct-caller count of CReadOnlyChunkFile::Read.

NOTE: the partial `_model_loadpath_raw.txt` + `ModelLoadPathDiscover.java` left by the prior run
carried NO model data — `_model_loadpath_raw.txt` is only the Ghidra startup log ending in a
project-LockException abort (the prior run never acquired the lock). Re-run from scratch this turn.

---

## VERDICT (one line for the ledger)

**FOpen loose lane — Around-FOpen COVERS the synchronous model open.** A .cgf/static-mesh (and
the synchronous .chr/.skin chunk read) is OPENED via `CReadOnlyChunkFile::Read` (FUN_18051cba0)
calling `ICryPak::FOpen` (slot 36, +0x120) on `gEnv->pCryPak`, and READ via slot 40
(FGetCachedFileData) OR slot 45 (GetFileSize) + slot 38 (FReadRaw) — the SAME tagged-handle
machinery the verified Around-FOpen seam (commit e6e8e27) serves (identical to F1 texture, F4
audio). **NOT mmap** (zero CreateFileMapping/MapViewOfFile imports engine-wide — F1 ground truth,
re-grounded). **NO pre-open size/existence gate** — FOpen is called directly on the path; the only
size read (slot 45) is on the ALREADY-OPENED handle. **MIXED, like F1:** the ASYNC character path
(CryCHRLoader::EndStreamSkel / EndStreamSkinAsync) is delivered by the streaming chunk-loader = F5's
MOUNT/STREAM lane (pak-resident, NOT reached by Around-FOpen). Confidence: HIGH on the synchronous
FOpen lane (open + both read arms read in the body); the async character lane is identified by its
stream-completion callbacks but its stream-OPEN seam is the F5-verified mount lane, flagged below.

---

## THE EVIDENCE CHAIN (all body-read, cited)

### 1. The model class strings land on CONSUMERS, not the open site

`.cgf`/`.cdf`/StatObj/ChunkFile/CharacterManager xrefs (`_model_opensite_raw.txt` STEP 1, 221 fns)
point at the HIGH-LEVEL loaders, READ:
- **`FUN_18052dd60` = `CObjManager::LoadStatObj`** (string `"CObjManager::LoadStatObj: Default object
  not found (%s)"`, `"Failed to load cgf: %s, '_lod' meshes…"`). Cache/lookup layer; its `+0x130`
  call is on `DAT_18492b890` (a string-normalize helper), NOT pCryPak — no file open here.
- **`FUN_18051ce88` = `C3DEngine::LoadChunkFileContent`** (string `"%s: Failed to load chunk file:
  '%s'" … "C3DEngine::LoadChunkFileContent"`). It builds a `CReadOnlyChunkFile` content instance
  (`FUN_18048c838`, `*param_1 = CReadOnlyChunkFile::vftable`) and delegates to `FUN_180980d4c`.
- **`FUN_180c9073c` (.cdf)** = CameraAttachmentManager — a CONSUMER that requests a `.cdf` by NAME
  via CharacterManager CreateInstance (`(*plVar5 + 0x18)(plVar5,
  "Objects/characters/assets/dialog_cameras/dialog_cameras.cdf", 0)`), NOT a loader.

So the model open is one indirection below the string anchors — inside the chunk reader.

### 2. CLoaderCGF::LoadCGF delegates the OPEN to a CReadOnlyChunkFile vtable call

`FUN_180980d4c` = `CLoaderCGF::LoadCGF` (string `"CLoaderCGF::LoadCGF"`), READ
(`_model_chunkreader_raw.txt`):
- `cVar1 = (**(code **)(*param_4 + 0x18))(param_4)` — "already open?" predicate on the chunk file
  object `param_4` (= the `CReadOnlyChunkFile` content instance from `FUN_18048c838`).
- `else (**(code **)(*param_4 + 0x20))(param_4, param_3)` — **the OPEN of the chunk file** (slot 4,
  +0x20 on the CReadOnlyChunkFile vtable; `param_3` = the model path).
- then `FUN_1806a24e4(...)` = the CGF data PARSER — operates on the already-opened `param_4` via
  vtable reads (`(*param_4 + 0x10)`, etc.); it names NO open target (READ — `LANES: none named`).

`FUN_18048c838` sets `*param_1 = CReadOnlyChunkFile::vftable` (READ) — so `param_4`'s vtable slot 4
is `CReadOnlyChunkFile::Read`.

### 3. CReadOnlyChunkFile::Read (FUN_18051cba0) = the OPEN site — FOpen slot 36, both read arms on the handle

Resolved `CReadOnlyChunkFile::vftable` (meta-ptr @ 0x183a38b30; vftable start +8 = 0x183a38b38);
slot 4 (vftable+0x20) = **`FUN_18051cba0`** (RVA 0x51CBA0, the call target of `(*param_4+0x20)`).
READ end-to-end (`_chunkfile_read_opensite_raw.txt`):

```c
undefined8 FUN_18051cba0(longlong param_1 /*this=CReadOnlyChunkFile*/, undefined8 param_2 /*path*/) {
  ...
  // OPEN — ICryPak::FOpen, slot 36 (+0x120) on gEnv->pCryPak (DAT_18492b850):
  lVar2 = (**(code **)(*DAT_18492b850 + 0x120))
            (DAT_18492b850, param_2, &DAT_183a53718,            // path, mode-string "rb"-class
             -(*(char *)(param_1 + 0x3d) != '\0') & 2);          // flags (0 or 2)
  *(longlong *)(param_1 + 0x40) = lVar2;                         // store the FOpen handle
  if (lVar2 == 0) { pcVar4 = "Failed to open file '%s'"; ... }   // open-failed error
  else {
    if (*(char *)(param_1 + 0x3f) == '\0') {                     // READ ARM A (stream-into-engine-buf)
      uVar3 = (**(code **)(*DAT_18492b850 + 0x140))(DAT_18492b850, lVar2, &local_res8); // slot 40 FRead/FGetCachedFileData on the handle
      *(undefined8 *)(param_1 + 0x30) = uVar3;
    } else {                                                     // READ ARM B (read-whole-into-alloc-buffer)
      lVar2 = (**(code **)(*DAT_18492b850 + 0x170))(DAT_18492b850, lVar2);              // slot 45 GetFileSize ON THE HANDLE (post-open)
      ... alloc buffer (DAT_18549b480) of that size ...
      lVar2 = (**(code **)(*DAT_18492b850 + 0x138))(DAT_18492b850, allocBuf, size, handle); // slot 38 FReadRaw size bytes
      if (lVar2 != size) { return FUN_1821fb998(); }             // short-read error
    }
    ... else "Failed to get memory for file '%s'" ...
  }
}
```

- `DAT_18492b850` = **gEnv+0x50 = pCryPak** (id 132, verified — FINDINGS.md line 141; re-grounded
  here, the same global F4/F5/F6 deref). `+0x120` = vtable slot 36 = **ICryPak::FOpen** (id 131,
  RVA 0x4614A0). `&DAT_183a53718` = the same `"rb"`-class mode-string constant F1's texture open
  (`FUN_1807b5ed4`) and F4's audio open use. So the model OPEN is byte-for-byte the F1/F4 FOpen
  loose-lane open.
- Read arm A: `+0x140` = slot 40 = **FGetCachedFileData / FRead** on the handle@+0x40 — the exact
  slot-40 read F3-phase8.5 and F1 verified (handle-tag dispatch: pak-pseudo-handle vs real FILE*).
- Read arm B: `+0x170` = slot 45 GetFileSize **on the open handle**, then `+0x138` = slot 38
  FReadRaw of `size` bytes into a kcdx-side-allocated buffer (the `bCopyData` mode). The `+0x3f`
  flag (set from `FUN_18048c838`'s `param_2`) selects A vs B.
- BOTH read arms read from the **FOpen-minted handle** at `param_1+0x40`. No mmap, no CreateFile,
  no StreamAsyncFileRequest in the body (`LANES: vt+0x120(FOpen slot36)` only — READ).

### 4. The chunk reader is the single open-site; the character class shares it

- `CReadOnlyChunkFile::Read` (FUN_18051cba0) has **exactly 1 direct caller**
  (`_char_loader_opensite_raw.txt`) — reached through the `(*param_4+0x20)` VIRTUAL call in
  LoadCGF, which every CGF/CHR/skin synchronous chunk read funnels through. The CryCHRLoader
  functions sit in the SAME 0x51Cxxx code cluster (FUN_18051c9b4, FUN_18051db24) directly adjacent
  to the chunk reader at 0x51CBA0.
- The CHARACTER (.chr/.skin) loaders carry `"%s: The Chunk-Loader failed to load the file."` with
  `"CryCHRLoader::EndStreamSkel"` (FUN_18051c9b4) and `"CryCHRLoader::EndStreamSkinAsync"`
  (FUN_18051db24) — both are stream-COMPLETION callbacks that CONSUME a chunk-loader result; they
  name NO open target (`LANES: none named`). So character data is delivered by the chunk loader
  (synchronous → FOpen slot 36 as in §3; async → the streaming variant = F5 lane).

---

## SIZE / EXISTENCE GATE — none before the open

READ in FUN_18051cba0: FOpen (slot 36) is called DIRECTLY on the path — no by-name GetFileSize
(slot 45 +0x168) or IsFileExist (slot 67 +0x218) precedes it. The only size read is slot 45
(+0x170) on the ALREADY-OPENED handle, in read-arm B, for buffer allocation — NOT a pre-open
admission gate. Identical to F1: a substitute served through the FOpen handle reports its own size
naturally; nothing rejects a different-sized substitute before the read. (The F6 size-mismatch
mechanism applies only if a consumer separately calls the by-name slot-45 gate; no such gate is in
the model open path — unverified whether any model consumer calls it elsewhere, "not read.")

---

## CONFIDENCE MAP

VERIFIED (decompiled this run, cited):
- The model OPEN = `CReadOnlyChunkFile::Read` (FUN_18051cba0, RVA 0x51CBA0) → `ICryPak::FOpen`
  slot 36 (+0x120) on gEnv->pCryPak (DAT_18492b850), handle stored at this+0x40.
- Both read arms (slot 40 FGetCachedFileData; slot 45 GetFileSize + slot 38 FReadRaw) read FROM
  that FOpen handle. No mmap / CreateFile / stream call in the open-site body.
- The vtable resolution (CReadOnlyChunkFile::vftable @ 0x183a38b38; slot 4 = FUN_18051cba0) and the
  LoadCGF→`(*param_4+0x20)` delegation chain.
- CReadOnlyChunkFile::Read has 1 direct caller (the shared chunk open-site); CryCHRLoader's
  EndStreamSkel/EndStreamSkinAsync consume a chunk-loader result (string-confirmed).
- DAT_18492b850 = gEnv+0x50 = pCryPak (id 132) — cross-front re-grounded (FINDINGS.md, F4/F5/F6).

UNVERIFIED — not read (flagged, not asserted):
1. The ASYNC character/skin stream-OPEN seam. EndStreamSkel/EndStreamSkinAsync are the COMPLETION
   callbacks; the stream-BEGIN (CryCHRLoader::BeginStream / StartStreaming) that mints the read was
   not decompiled this turn. By F5 (already verified) the streaming engine reads the pak the
   CCryPak MOUNT opened (CreateFileA at archive factory slot 72), NOT via FOpen slot 36 — so a
   pak-resident streamed character is the MOUNT lane (not Around-FOpen). This is the same
   loose-vs-pak split F1 found for textures; not re-walked here.
2. That an Around-FOpen-returned kcdx FILE* serves a substitute .cgf end-to-end IN-GAME — a runtime
   probe, not a static fact (the analogous next probe to F1's .dds case; the .lua case is already
   live-confirmed in probe-archive `fopen-override.md`).

## SEED-ROW CANDIDATES (AP18 — FLAGGED, NOT written; need user sign-off)

Only if kcdx ever needs to name the model open-site directly (likely NOT — owning FOpen slot 36 /
resolver slot 1 already covers it, fronts 1/3-phase8.5):
- `CReadOnlyChunkFile::Read` — FUN_18051cba0, RVA 0x51CBA0 (the synchronous model/chunk open-site:
  FOpen slot36 + slot40/45/38 read on the handle).
Already seeded/used (resolve by name, no new row): FOpen id 131 (slot 36), gEnv_pCryPak id 132.
