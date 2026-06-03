# F4 — Audio (.ogg / FMOD bank) load path: CLAIMS + evidence

Front F4 of the asset-loadpath-map. Read-only measurement (Type B): claims + cited
evidence, no assembled conclusion, gates nothing. Every call-edge below is read in the
OWNING function's decompiled body this run (AP19) — no edge inferred from a slot label,
name, or address proximity. Image base 0x180000000; build `release_1_5_1164953_841`.

Trust: PRIMARY EVIDENCE — pefile import-table dump + capstone byte-wise IAT-xref scan +
fresh Ghidra 12.1 decompile of WHGame.dll. Scripts + raw dumps co-located in this dir.

## The question

When the engine loads/streams audio, does it reach the verified Around-FOpen seam
(FOpen slot 36 @ 0x4614A0 + FRead/FGetCachedFileData slot 40 @ 0x51CD00), or a separate
audio-middleware / async path that bypasses CCryPak?

## What the audio middleware IS (primary evidence — import table)

WHGame.dll dynamically imports **`fmod.dll` + `fmodstudio.dll`** (and `bink2w64.dll` for
video). Audio middleware = **FMOD Studio**. `_fmod_iat_*.txt` + import dump.

Two load-bearing imports present:
- `FMOD::System::setFileSystem(...)` — the file-I/O **override** API (install custom
  open/close/read/seek callbacks; FMOD then routes ALL its file I/O through them).
- `FMOD::Studio::System::loadBankFile(const char* filename, ...)` — banks loaded **by
  filename**, NOT by memory buffer (`loadBankMemory`/`loadBankCustom` are NOT imported).
  So FMOD opens the bank file itself — but through the installed callbacks.

`createSound`/`createStream` (also imported) take a filename + the FMOD core `System*`,
so loose sounds (`.ogg`) likewise open through the same installed callbacks.

## The callback registration (read in the owning body)

`setFileSystem` is imported and called **exactly once** — call site RVA 0xD30040, inside
`FUN_180d2fde4` (the FMOD Studio init; the decompile carries the literal source path
`CryEngine/CrySoundSystem/implementations/CryAudioImplFmod/FmodWrapper.cpp`). The call,
read in that body:

```
FMOD::System::setFileSystem(*coreSystem,
    FUN_181224d1c,   // useropen
    FUN_1813437b4,   // userclose
    FUN_180460788,   // userread
    FUN_1810b6d84,   // userseek
    0x0, 0x0,        // userasyncread, userasynccancel = NULL  -> synchronous, NOT async
    -1);             // blockalign default
```

Async read/cancel are NULL → FMOD uses the **synchronous** read callback. No async/stream
bypass at the file-I/O layer. (`_fmod_filesystem_decomp.txt`.)

Same body also: `createSound(core, "EngineAssets/Sound/short-silence.ogg", ...)` and
`loadBankFile(studio, <master bank path>, ...)` — both open by name, both will use the
callbacks above.

## The four callbacks dispatch through CCryPak (each body read)

All four callbacks dispatch through the global `DAT_18492b850` via its vtable at CCryPak
slot offsets (`_fmod_callbacks_decomp.txt`):

| FMOD callback | fn | vtable off | CCryPak slot | role |
|---|---|---|---|---|
| useropen  | FUN_181224d1c | +0x120 | **slot 36 = FOpen** (0x4614A0) | open by name |
| userread  | FUN_180460788 | +0x130 | slot 38 = FUN_180461304 | read by handle |
| userseek  | FUN_1810b6d84 | +0x1a8 | **slot 53 = FSeek** (0x46068C) | seek |
| userclose | FUN_1813437b4 | +0x1b8 | **slot 55 = FClose** (0x4609D0) | close |
| (size, inside useropen) | FUN_180460c08 | +0x170 | slot 46 = FUN_180460c08 | filesize |

`DAT_18492b850` identity = the CCryPak singleton (gEnv->pCryPak, id 132): +0x1a8 and
+0x1b8 are FSeek/FClose (front-1 V rows) and +0x120 is FOpen — the vtable is CCryPak's.

**useropen `FUN_181224d1c`** (read in body):
```
lVar2 = (**(*DAT_18492b850 + 0x120))(DAT_18492b850, name, &DAT_183a53718, 0); // slot 36 FOpen
if (lVar2 == 0) return 0x12;          // FMOD_ERR_FILE_NOTFOUND on FOpen-fail
size  = (**(*DAT_18492b850 + 0x170))(DAT_18492b850, lVar2);                   // slot 46 size
*handle = lVar2; *filesize = size;
```
→ audio open IS `CCryPak::FOpen(name)`. The handle FMOD holds is FOpen's return.

**userread `FUN_180460788`** (read in body):
```
bytesread = (**(*DAT_18492b850 + 0x130))(DAT_18492b850, buf, 1, size, handle); // slot 38
*bytesread_out = bytesread;
return (bytesread < size) ? 0x10 /*FMOD_ERR_FILE_EOF*/ : 0;
```
→ audio read goes through slot 38 (+0x130), NOT slot 40 (+0x140 = the FGetCachedFileData
seam). See next section for what slot 38 IS.

## What slot 38 (the read target) actually is — read in its body

Front-1 labeled slot 38 "FOpen-by-pak-index/FReopen". Its body (`FUN_180461304`,
`_fmod_readslots_decomp.txt`) is a **read-by-handle dispatcher**, the same index-vs-pak-count
shape as the verified FRead seam (slot 40):

```
FUN_180461304(this, buf, elemsize, count, handle):
  if (handle-1 < pak_count)                      // pak-resident
      FUN_1804618b4(pak_entry, buf, elemsize, count);   // read from pak stream
  else                                           // loose / OS-backed handle
      FUN_1804d7ab4(buf, elemsize, count, handle);      // OS fread wrapper
```

`FUN_1804d7ab4` (the OS arm) is the **SAME OS-read primitive the verified FGetCachedFileData
(slot 40) OS arm calls** — slot 40's body (read this run for contrast) does
`FUN_1804d7ab4(cachebuf, 1, size, handle)` on its loose arm. So slot 38 and slot 40 are
sibling CCryPak read-dispatchers; both route a loose handle to `FUN_1804d7ab4`.

Slot 46 size callback (`FUN_180460c08`) is the same dispatch: pak arm reads pak-entry size;
OS arm does `_fileno(handle)` + `_fstat64i32` → confirms the loose handle is a CRT `FILE*`
(matches the verified foundation: Around-FOpen returns a CRT FILE*).

## Verdict (for the ledger)

**FOpen reachable — audio opens via CCryPak::FOpen (slot 36); reads via slot 38 (a
FRead-family dispatcher), NOT the slot-40 FGetCachedFileData seam.** No CCryPak bypass: no
Win32 CreateFile, no FMOD-internal fopen, no async/stream path at the file layer (async
callbacks NULL). All four FMOD file callbacks are CCryPak vtable calls (open=36, read=38,
seek=53, close=55), read in their bodies.

Confidence: **READ** (every edge cited in the owning body this run) for:
open→slot36 FOpen, read→slot38, seek→slot53, close→slot55, slot38/slot40 both→FUN_1804d7ab4.

## CAVEAT for the synthesizer (the Around-FOpen-seam coverage nuance)

The Around-FOpen seam (commit e6e8e27) is FOpen + **FRead = FGetCachedFileData (slot 40,
0x51CD00)**. Audio's READ does NOT go through slot 40 — it goes through **slot 38**
(+0x130). Both reach the same OS primitive `FUN_1804d7ab4` on the loose arm, so a kcdx
hook that owns **FOpen (slot 36)** sees the audio OPEN (the open IS FOpen), and a kcdx
loose handle returned from FOpen would be served by slot 38's OS arm exactly as slot 40's
OS arm serves it (same `FUN_1804d7ab4`, same CRT FILE* shape — slot 46 proves the handle
is a FILE* via _fileno/_fstat). BUT: the seam as verified covered FOpen+slot40; whether
the Around-FOpen handle is correctly consumed by **slot 38** (FMOD's actual read path) is
a DISTINCT consumer edge from the slot-40 consumer the seam was verified against. Slot 38's
OS arm calling the same `FUN_1804d7ab4` is read-verified here; that the kcdx-minted handle
satisfies slot 38's index-vs-count gate (handle-1 >= pak_count → OS arm) is the same
predicate slot 40 uses (read this run in both bodies) — so it holds by the same mechanism,
but this is a CROSS-SLOT claim the gate should confirm before it ships as "audio is covered."

## Not checked (out of F4 scope / honest-uncertainty)

- Whether a metadata pre-check (slots 45/67 GetFileSize/IsFileExist) gates an audio
  substitution before the read — that is front F6 (cross-cutting metadata gate).
- `bink2w64.dll` (video) file I/O — separate middleware, not audio; not investigated.
- Runtime confirmation that execution actually reaches these callbacks for a given .ogg
  (a `/debug`/live-probe question, not a static edge) — the ledger's planned multi-class
  runtime overlay probe covers it.
