# FRONT 1 — the full ICryPak/CCryPak vtable surface (102 slots mapped)

Captured 2026-06-02. The complete hook/replace surface inventory for the CCryPak
vtable @ VA 0x183A95FA8 (RTTI `.?AVCCryPak@@`; reached via `*(gEnv+0x50)`, gEnv id
1010, pCryPak id 132). Trust: PRIMARY EVIDENCE — fresh Ghidra (12.1) decompile of
WHGame.dll `release_1_5_1164953_841`, image base 0x180000000. Every role anchored on
a decompiled body, leaf-call set, member-offset touch, or a RESOLVED string literal
(AP3 — never a canonical header). No live probe; runtime-effect claims flagged.

Producers (this dir + `third-party-ghidra/ghidra_scripts/`):
- `PakVtableSurface.java` → `_front1_surface_raw.txt` — all 102 slots, one-line fingerprint
  (params / return shape / leaf calls / member-offset feats / resolved strings).
- `PakVtableRoles.java` → `_front1_roles_raw.txt` — full decompiles of the 17 slots whose
  role the fingerprint left ambiguous.

The vtable is **102 slots (0..101)**; slot 102 (+0x300) is non-exec → VTABLE END
(`_front1_surface_raw.txt`: `<non-exec -> VTABLE END at slot 102>`). This SUPERSEDES the
prior "~96-slot" estimate (`_searchpath_api_raw.txt` capped its loop at 96; it never hit
the end). Slots 0..95 RVAs agree exactly with that prior dump.

> Note on `params=0 ret=val` in the fingerprint: the surface script read Ghidra's
> default (unanalyzed) signature, which under-reports params. The real ABI of every slot
> is __fastcall member (rcx=this). Arg counts below come from the FULL decompiles in
> `_front1_roles_raw.txt` and the prior fronts' abi_walker work, not the fingerprint.

---

## The complete slot → RVA → role table

`this` (rcx) = CCryPak* throughout. RVA = file offset (VA − 0x180000000).
Confidence: **V** = role on a decompiled body / resolved string this run; **V*** = role
verified by a PRIOR front (cited); **i** = inferred from leaf-call shape, needs-confirm.

| slot | +off | RVA | fn | role | conf | evidence |
|---|---|---|---|---|---|---|
| 0 | +0x0 | 0x24169fc | FUN_1824169fc | **RTTI/dtor or type-id** (vtable slot 0 — MSVC `vector deleting destructor` / scalar dtor) | i | slot-0 convention; leaves FUN_182415ed8 |
| 1 | +0x8 | 0x46205c | FUN_18046205c | **AdjustFileName** — the path-resolution core (search-path loop + data-root prefix + pakPriority gate); returns resolved `char*` | V* | subresolver-decompiled-mechanism.md; feats SP_VEC,OSFILE |
| 2 | +0x10 | 0x41a900 | FUN_18041a900 | accessor (Init / config getter; thin) | i | no leaves, ret=val |
| 3 | +0x18 | 0x3b70f0 | FUN_1803b70f0 | void setter / no-op stub | i | empty body, ret=void |
| 4 | +0x20 | 0x463b08 | FUN_180463b08 | accessor → FUN_182419004 | i | single forward |
| 5 | +0x28 | 0x4d999c | FUN_1804d999c | config/string accessor | i | CryStringT helper leaves (FUN_1804d…) |
| 6 | +0x30 | 0xda4e5c | FUN_180da4e5c | config/string op (chkstk = stack buffer) | i | __chkstk + CryStringT leaves |
| 7 | +0x38 | 0x193cb14 | FUN_18193cb14 | **AddPakToValidate / pak-path register** — warns on absolute pak path | V | STR="Pak file %s has absolute path %s, which is strange"; strrchr |
| 8 | +0x40 | 0x4d9508 | FUN_1804d9508 | string/config accessor | i | CryStringT leaves |
| 9 | +0x48 | 0x4d9bb0 | FUN_1804d9bb0 | void config setter (stack buf) | i | __chkstk, ret=void |
| 10 | +0x50 | 0x197c598 | FUN_18197c598 | config/path op | i | CryStringT + FUN_1823cb440 |
| 11 | +0x58 | 0x4d940c | FUN_1804d940c | string accessor | i | CryStringT leaves |
| 12 | +0x60 | 0x241af94 | FUN_18241af94 | **pak-array predicate** (compares against loaded-pak array; `_stricmp`) | i | feats=PAKARR; leaf _stricmp |
| 13 | +0x68 | 0x2419280 | FUN_182419280 | **IsFolder / dir-exists** — AdjustFileName(slot1)+`_findfirst64` on `<path>\` | V | decomp: calls slot1, appends '\\', `_findfirst64`→bool |
| 14 | +0x70 | 0x241d2e8 | FUN_18241d2e8 | **ForEachFile / FindFiles-callback** — AdjustFileName + `_findfirst64`/`_findnext64` loop, calls a per-file callback via vtable+0x78 (slot 15) | V | decomp: findfirst/findnext loop, `(*slot15)(this,…,name)` |
| 15 | +0x78 | 0x241d1b8 | FUN_18241d1b8 | **per-file callback target** for slot 14 (builds full path, invoked per dir entry) | i | invoked as vtable+0x78 inside slot 14 |
| 16 | +0x80 | 0x241d410 | FUN_18241d410 | pak-array + OS-file op (void) | i | feats=PAKARR,OSFILE; tail FUN_180cdc760 |
| 17 | +0x88 | 0x4d775c | FUN_1804d775c | **pak-membership test by name** (`strstr` + normalize + pak array) | i | feats=PAKARR; leaves CryStringT norm + strstr |
| 18 | +0x90 | 0x241b504 | FUN_18241b504 | pak-array op (void) | i | feats=PAKARR; tail FUN_180cdc760 |
| 19 | +0x98 | 0x19af1a8 | FUN_1819af1a8 | **AddMod** — push_back a search-path ROOT onto `[this+0x198]` (dedup `_stricmp`) | V* | searchpath-registrar-mechanism2.md; feats=SP_VEC |
| 20 | +0xa0 | 0x241ce08 | FUN_18241ce08 | **RemoveMod** — scan+erase from the search-path vector | V* | searchpath-registrar-mechanism2.md; leaf FUN_18241f740 (erase) |
| 21 | +0xa8 | 0x241a390 | FUN_18241a390 | **GetMod(i)** — bounds-checked `vec[i]` over `[+0x198..+0x1a0]` | V* | searchpath-registrar-mechanism2.md; feats=SP_VEC |
| 22 | +0xb0 | 0x241c068 | FUN_18241c068 | **SetAlias / AddAlias** — registers a `from,to` alias pair | V | STR="PAK ALIAS:%s,%s\n" + CryPak.cpp; strchr |
| 23 | +0xb8 | 0x9b4540 | FUN_1809b4540 | **alias-table op** (insert/replace into `[+0x1b0]`; `_strlwr`+memmove) | V | feats=ALIAS; _stricmp/_strlwr/memmove |
| 24 | +0xc0 | 0x1790b54 | FUN_181790b54 | **GetAlias / lookup** in the alias table (`_stricmp` scan) | V | feats=ALIAS; leaf _stricmp, ret=val |
| 25 | +0xc8 | 0x241b58c | FUN_18241b58c | alias/pak helper (void; tail FUN_180c3aa90) | i | tail-call only |
| 26 | +0xd0 | 0x241df88 | FUN_18241df88 | void helper (tail FUN_180cdc760) | i | tail-call only |
| 27 | +0xd8 | 0x7aa250 | FUN_1807aa250 | void stub / flag setter | i | empty |
| 28 | +0xe0 | 0x17909e4 | FUN_1817909e4 | **MakeDir / game-root check** — creates game folder, validates casing | V | STR="Game folder %s not found…", "Wrong letter casing of the game root folder!"; _findfirst64/_findclose |
| 29 | +0xe8 | 0x1a72400 | FUN_181a72400 | accessor (ret=val, no leaves) | i | thin getter |
| 30 | +0xf0 | 0x9f0ec0 | FUN_1809f0ec0 | void op (tail FUN_1809f10d4) | i | tail-call |
| 31 | +0xf8 | 0x1a72420 | FUN_181a72420 | accessor (thin getter) | i | thin getter |
| 32 | +0x100 | 0x2419854 | FUN_182419854 | **FindPakFileByCRC / zip-by-name lookup** | V | STR="CRC:Looking for a zip called %s, fullpath %s", "CRC: Match. offset %u, size %u" |
| 33 | +0x108 | 0x241a4e4 | FUN_18241a4e4 | **GetPakInfo / enumerate loaded paks** — mallocs an array, walks the loaded-pak vector `[+0x120..+0x128]`, copies each pak's name/size (thread-locked on `+0xf0`) | V | decomp: malloc(count*0x28), per-pak FUN_18241f358, returns array |
| 34 | +0x110 | 0x2419354 | FUN_182419354 | **free the GetPakInfo array** (companion to slot 33) | V | decomp: free/free(tail) |
| 35 | +0x118 | 0x2418de4 | FUN_182418de4 | **FOpenRaw / open-into-caller-buffer** — AdjustFileName(slot1, flag 0), opens via FUN_1809b2b28, registers handle (vtable+0x268/+0x2c8) | V | decomp: slot1 + FUN_1809b2b28 + handle-register |
| 36 | +0x120 | 0x4614a0 | FUN_1804614a0 | **FOpen** — the engine-wide open-by-path (path 2048 cap + mode parse) | V* | FINDINGS.md (id 131); feats=PATHCAP2048 |
| 37 | +0x128 | 0x2418ec8 | FUN_182418ec8 | **FClose-variant / release-handle** (tail FUN_1809b2b28 — the slot-35 opener's pair) | i | tail FUN_1809b2b28 |
| 38 | +0x130 | 0x461304 | FUN_180461304 | **FOpen-by-pak-index / FReopen** — opens the i-th loaded pak's stream (`[+0x40..+0x48]` pak-stream vector), else falls back to FUN_1804d7ab4 | V | decomp: indexes pak-stream vector by param_5, FUN_1804618b4 |
| 39 | +0x138 | 0x51e1f8 | FUN_18051e1f8 | **FReadRaw / cached-read** (seek+read into buffer) | V | feats=SEEK; handle-deref FUN_1804613d0 |
| 40 | +0x140 | 0x51cd00 | FUN_18051cd00 | **FGetCachedFileData** — single-instance cached file read | V | STR="!Cannot have more then 1 FGetCachedFileData at the same time"; feats=SEEK |
| 41 | +0x148 | 0xa700c8 | FUN_180a700c8 | **FWrite** — `fwrite` over the handle | V* | FINDINGS.md; feats=WRITE; leaf fwrite |
| 42 | +0x150 | 0x2418ed4 | FUN_182418ed4 | **FWriteRaw / put-block** (handle write helper) | i | handle-deref + FUN_180aae644 |
| 43 | +0x158 | 0x2418c40 | FUN_182418c40 | **FGets** — line read (`fgets`) | V | leaf fgets |
| 44 | +0x160 | 0x241a960 | FUN_18241a960 | **FGetc / read one** | i | handle-deref + FUN_18241aa0c |
| 45 | +0x168 | 0x2418b48 | FUN_182418b48 | **GetFileSize-by-name** (3-arg: this,name,bDiskOnly) — AdjustFileName, then OS size (vtable+0x228) or pak-dir size (FUN_1804631f0); pakPriority-gated | V | decomp: slot1 + OS size + FUN_1804631f0; feats=OSFILE |
| 46 | +0x170 | 0x460c08 | FUN_180460c08 | **FEof-or-Fileno / handle-int** (`_fileno`) | i | handle-deref + _fileno |
| 47 | +0x178 | 0x241dd6c | FUN_18241dd6c | **FUngetc** (`ungetc`) | V | leaf ungetc |
| 48 | +0x180 | 0x241acec | FUN_18241acec | handle op (read/scan helper) | i | handle-deref FUN_180462014 |
| 49 | +0x188 | 0x241ccec | FUN_18241ccec | **RemoveFile / delete** — `remove` + `SetFileAttributesA` | V | STR="File '%s' can't be deleted, szFullPath: '%s'"; remove |
| 50 | +0x190 | 0x19d1f58 | FUN_1819d1f58 | **RemoveDir / rmdir-variant** | i | FUN_1819d1fbc (shared with slot 49 family) |
| 51 | +0x198 | 0x462770 | FUN_180462770 | accessor (thin; no leaves) | i | thin getter |
| 52 | +0x1a0 | 0x2417d6c | FUN_182417d6c | **CopyFile** (`CopyFileA`) | V | leaf CopyFileA |
| 53 | +0x1a8 | 0x46068c | FUN_18046068c | **FSeek** (`fseek`) | V | feats=SEEK; leaf fseek |
| 54 | +0x1b0 | 0x460cdc | FUN_180460cdc | **FTell** (`_ftelli64`) | V | feats=TELL; leaf _ftelli64 |
| 55 | +0x1b8 | 0x4609d0 | FUN_1804609d0 | **FClose** | V* | FINDINGS.md; handle-deref + FUN_180460958 |
| 56 | +0x1c0 | 0x961d48 | FUN_180961d48 | **FEof** (`feof`-equivalent) | V | feats=EOF; handle-deref |
| 57 | +0x1c8 | 0x16a2024 | FUN_1816a2024 | **FError** (`ferror`) | V | feats=FERROR; leaf ferror |
| 58 | +0x1d0 | 0x2418b34 | FUN_182418b34 | **FGetErrno** (`_errno`) | V | leaf _errno |
| 59 | +0x1d8 | 0x2418a90 | FUN_182418a90 | **FFlush** (`fflush`) | V | leaf fflush |
| 60 | +0x1e0 | 0x241c4c0 | FUN_18241c4c0 | **GetPoolRealloc** (pool memory accessor) | V | STR="CCryPak::GetPoolRealloc" |
| 61 | +0x1e8 | 0x76cd7c | FUN_18076cd7c | pool/alloc accessor (tail FUN_18076bdb0) | i | shared with slot 99 |
| 62 | +0x1f0 | 0x3dbcac | FUN_1803dbcac | pool/string op | i | FUN_18076cae8 (pool) |
| 63 | +0x1f8 | 0x973058 | FUN_180973058 | path-build op (CryStringT) | i | FUN_1804628a0/629b4/62a28 (resolver string helpers) |
| 64 | +0x200 | 0x41d640 | FUN_18041d640 | **IsFileExist-by-handle / RegisterFileOpenCallback toggle** — locks `+0x138`, calls FUN_18041d6a0, returns bool-1 | V | decomp: lock +0x138, FUN_18041d6a0, ret bVar-1 |
| 65 | +0x208 | 0x97383c | FUN_18097383c | pak/alias op (tail FUN_180cdc760) | i | FUN_180973994 |
| 66 | +0x210 | 0x241a3bc | FUN_18241a3bc | **GetFileno / handle-fileno variant** (`_fileno`) | i | handle-deref + _fileno |
| 67 | +0x218 | 0x463ec4 | FUN_180463ec4 | **IsFileExist (3-arg: this,name,location)** — AdjustFileName, then pak-membership (FUN_1804631f0) and/or OS disk-existence (FUN_1819c9cb4), pakPriority+location gated | V | decomp: slot1 + FUN_1804631f0 + FUN_1819c9cb4, param_3 location switch |
| 68 | +0x220 | 0x241ac8c | FUN_18241ac8c | **GetFileAttributes / IsFolder(disk)** — AdjustFileName + `GetFileAttributesA`, returns FILE_ATTRIBUTE_DIRECTORY bit | V | decomp: slot1 + GetFileAttributesA → dir bit |
| 69 | +0x228 | 0x4d5d58 | FUN_1804d5d58 | **GetFileStat (`_stat64`)** — OS stat of a resolved path | V | leaf _stat64 |
| 70 | +0x230 | 0x241abcc | FUN_18241abcc | **IsFileExist (2-arg)** — AdjustFileName + pak-dir entry, excludes dir entries (`!= 0xd`) | V | decomp: slot1 + FUN_1804631f0, type check |
| 71 | +0x238 | 0x7ad468 | FUN_1807ad468 | **OpenPack / mount archive** (large; reads pak header, registers into loaded-pak array) | i | __chkstk + FUN_1807ad6d4/76c/ae228 (pak-open family); see Front-on-mount |
| 72 | +0x240 | 0x4d5580 | FUN_1804d5580 | **CheckPakConsistency / TestArchive** — seek+read integrity test | V | STR="PAK file '%s' - test of consistency failed - can't seek/read", "Too big PAK file '%s'"; feats=PATHCAP2048,SEEK,OSFILE |
| 73 | +0x248 | 0xf3f314 | FUN_180f3f314 | handle op (read/seek helper) | i | handle-deref FUN_180462014 |
| 74 | +0x250 | 0x241c978 | FUN_18241c978 | void config/list op | i | FUN_182484b8c |
| 75 | +0x258 | 0x241c9a8 | FUN_18241c9a8 | void op (tail FUN_18076e41c) | i | tail-call |
| 76 | +0x260 | 0x241ca98 | FUN_18241ca98 | void stub | i | empty |
| 77 | +0x268 | 0x241c9f4 | FUN_18241c9f4 | **%USER% expansion / user-dir check** (`_strnicmp` against "%USER%") | V | STR="%USER%"; leaf _strnicmp |
| 78 | +0x270 | 0x6121d8 | FUN_1806121d8 | accessor (thin) | i | no leaves |
| 79 | +0x278 | 0xd2a958 | FUN_180d2a958 | void op (FUN_180b67040) | i | single forward |
| 80 | +0x280 | 0x1a72470 | FUN_181a72470 | accessor (thin getter) | i | no leaves |
| 81 | +0x288 | 0x241769c | FUN_18241769c | **ComputeCRC32 (CryFile path)** — opens via FOpen(slot36)+seek+read loop, CRCs the bytes (FUN_180612560) | V | decomp: uses slots +0x120/+0x1a8/+0x170/+0x130/+0x1b8; CRC accumulate |
| 82 | +0x290 | 0x2417a3c | FUN_182417a3c | **ComputeMD5 (CryFile path)** — same open/read loop, MD5 (init consts 0x67452301…) | V | decomp: MD5 init constants + read loop |
| 83 | +0x298 | 0x241785c | FUN_18241785c | **GetFileData-with-method-log** (CryFile vs CIO open path selector) | V | STR="CRC: Used CryFile method, fp is 0x%p", "CRC: Used CIO method, fp is 0x%p"; feats=SEEK |
| 84 | +0x2a0 | 0x241caa0 | FUN_18241caa0 | void op (thunk FUN_181c1ba30) | i | thunk + FUN_18240d810 |
| 85 | +0x2a8 | 0x241df94 | FUN_18241df94 | void op (memmove; list-copy) | i | thunk + memmove |
| 86 | +0x2b0 | 0x60dc80 | FUN_18060dc80 | accessor (thin) | i | no leaves |
| 87 | +0x2b8 | 0x2418150 | FUN_182418150 | accessor (thin) | i | no leaves |
| 88 | +0x2c0 | 0x8c2170 | FUN_1808c2170 | void stub | i | no leaves |
| 89 | +0x2c8 | 0x83f6d0 | FUN_18083f6d0 | accessor (thin) | i | no leaves; called by slot 35 as +0x268? no — see note |
| 90 | +0x2d0 | 0x19dd0c0 | FUN_1819dd0c0 | void stub | i | no leaves |
| 91 | +0x2d8 | 0x1a72460 | FUN_181a72460 | **GetPakPriority / cvar read** — returns `*(*(this+0x228)+0x20)` (the sys_pakPriority value) | V | decomp: `return *(this+0x228)->[0x20]`; feats=OSFILE |
| 92 | +0x2e0 | 0x2419c00 | FUN_182419c00 | **GetFileSizeOnDisk / GetFileSize(uncompressed)** — AdjustFileName + pak-dir entry, returns compressed-size+base | V | decomp: slot1 + FUN_1804631f0 + size compute |
| 93 | +0x2e8 | 0x463a24 | FUN_180463a24 | **GetFileSizeCompressed / GetArchivePath** — AdjustFileName + pak-dir entry, FUN_180463abc | V | decomp: slot1(via gEnv) + FUN_1804631f0 + FUN_180463abc |
| 94 | +0x2f0 | 0x2417e40 | FUN_182417e40 | **RegisterSystemSearchPath / SetGameFolder** — touches search-path vector with literal "System" | V | feats=SP_VEC; STR="System" |
| 95 | +0x2f8 | 0x2417c40 | FUN_182417c40 | void config op (FUN_1804cdb98) | i | single forward |
| 96 | +0x300 | 0x2418180 | FUN_182418180 | void config op (FUN_1804cd928) | i | single forward |
| 97 | +0x308 | 0x2419a78 | FUN_182419a78 | op → FUN_181dd81f0/7fd0 | i | two forwards |
| 98 | +0x310 | 0x241c4f0 | FUN_18241c4f0 | **PoolTempAlloc** (pool memory accessor) | V | STR="CCryPak::PoolTempAlloc" |
| 99 | +0x318 | 0x76cd7c | FUN_18076cd7c | **PoolFree / pool accessor** (same fn as slot 61) | V | identical to slot 61 (pool family) |
| 100 | +0x320 | 0x2418f78 | FUN_182418f78 | **ClosePakByIndex / release-pak-stream(i)** — indexes the pak-stream vector `[+0x40..+0x48]`, releases the i-th | V | decomp: indexes +0x40 vector by param_2, FUN_1804607e4 |
| 101 | +0x328 | 0x973294 | FUN_180973294 | **FindFirst / create CCryPakFindData iterator** — allocates a `CCryPakFindData` (its vftable assigned), inits the find-handle list | V | decomp: `*puVar1 = CCryPakFindData::vftable` (resolved RTTI symbol) |

(slot 102 = VTABLE END.)

---

## The canonical-ICryPak families this maps onto (the replace surface, grouped)

1. **Path resolution (the heart):** slot 1 AdjustFileName (V*). Every by-name method
   (13,14,35,45,67,68,70,92,93 + FOpen 36) calls `(*this+8)` = slot 1 FIRST, then acts
   on the resolved `char*`. To OWN resolution, kcdx replaces/hooks slot 1 — every
   consumer inherits it. (This is the load-bearing finding for synthesis Q3.)
2. **Search-path / mod roots:** slot 19 AddMod, 20 RemoveMod, 21 GetMod (V*), + 94
   RegisterSystemSearchPath. The `[+0x198..+0x1a0]` vector slot 1 iterates.
3. **Alias substitution:** slot 22 SetAlias, 23 alias-insert, 24 GetAlias. The
   `[+0x1b0..+0x1b8]` prefix-substitution table (NOT additive; per mechanism-2 doc).
4. **Open by path:** slot 36 FOpen (V*, id 131), 35 FOpenRaw, 38 FOpen-by-pak-index.
5. **File IO over a handle (the libc-backed block 39..59,66,73):** 39 FReadRaw,
   40 FGetCachedFileData, 41 FWrite (V*), 43 FGets, 44 FGetc, 46 fileno, 47 FUngetc,
   53 FSeek, 54 FTell, 55 FClose (V*), 56 FEof, 57 FError, 58 FErrno, 59 FFlush.
   Uniform shape: handle-deref `FUN_1804613d0`/`FUN_180462014`, then the libc primitive.
6. **Existence / metadata by name:** 45 GetFileSize, 67 IsFileExist(3), 68 GetAttributes,
   69 GetFileStat, 70 IsFileExist(2), 92/93 GetFileSize-on-disk/compressed, 13 IsFolder.
7. **Directory enumeration:** 14 ForEachFile (findfirst/findnext), 15 its per-entry
   callback, 101 FindFirst→CCryPakFindData iterator, 28 MakeDir/game-root.
8. **Pak/archive management:** 7 AddPakToValidate, 17 pak-membership, 32 FindPakByCRC,
   33 GetPakInfo + 34 free, 71 OpenPack/mount (i — see the mount front), 72 TestArchive,
   100 ClosePakByIndex, 91 GetPakPriority.
9. **CRC/hash + delete/copy:** 81 CRC32, 82 MD5, 83 GetFileData(method-logged),
   49 RemoveFile, 50 RemoveDir, 52 CopyFile, 77 %USER% expansion.
10. **Pool memory + lifecycle:** 0 dtor, 60 GetPoolRealloc, 98 PoolTempAlloc, 61/99 pool.

---

## Contribution to the 4 synthesis questions

**Q3 (exactly what to hook vs replace) — the load-bearing contribution.** The complete
surface shows resolution funnels through ONE method: **slot 1 AdjustFileName (RVA 0x6205C)**.
Every by-name consumer in the vtable (open 35/36/38, exist 45/67/70, size 92/93, attrs 68,
folder 13, enumerate 14, delete 49, copy 52, CRC 81/82) calls `(*this+8)` to resolve the
virtual path BEFORE touching disk/pak. So a kcdx-owned resolver that owns slot 1 owns ALL
of them — the per-open FOpen(slot 36) hook (mechanism 1) is the surgical subset; the full
replace targets slot 1. FOpen-only misses the 9 other by-name surfaces (a mod that
overlays a file the engine opens via slot 35/38 or checks via slot 67 would be invisible
to a FOpen-only hook).

**Q1 (salvageable/reusable).** The handle-IO block (39..59) is pure libc-over-a-handle —
kcdx reuses it unchanged by returning a real engine handle from its resolver; no need to
reimplement read/seek/close. The search-path (19/20/21) + alias (22/23/24) registrars are
reusable AS-IS for registering a kcdx mod root (subject to the mechanism-2 pakPriority
caveat already documented). GetPakInfo (33) + GetPakPriority (91) are reusable read-only
introspection for a kcdx resolver to enumerate what the engine loaded.

**Q2 (full-replace vs partial).** The surface argues for **resolver-replace, IO-reuse**:
replace/own slot 1 (resolution policy) + optionally slot 36 (open), KEEP the handle-IO
block, the pak-membership/pak-array internals, and the directory-iterator factory (101).
Full vtable swap is unnecessary — 60+ slots are libc-IO or pool/lifecycle that kcdx has no
reason to alter. Partial = own the resolution + open slots, delegate the rest to original.

**Q4 (load a STOCK Nexus/Workshop pak unchanged).** The pak-management family is intact
and reusable: OpenPack(71)/GetPakInfo(33)/FindPakByCRC(32)/TestArchive(72) are how the
engine mounts and indexes a `.pak`. A kcdx resolver that registers a stock mod's pak via
the existing mount path (or AddMod of its mod root) keeps stock pak loading working — kcdx
owns the loose-overlay resolution policy (slot 1) WITHOUT disturbing the pak mount/index
machinery a Nexus/Workshop pak rides on. (The exact mount entry = slot 71; characterizing
its ABI is the mount front's job — flagged i here.)

---

## Seed-row candidates (AP18 — FLAGGED, NOT written; need user sign-off)

Already-seeded siblings (resolve by name, no new row): FOpen id 131 (slot 36), gEnv_pCryPak
id 132. Already-flagged by prior fronts: AdjustFileName slot 1 (RVA 0x6205C), AddMod slot 19
(RVA 0x19AF1A8), RemoveMod slot 20, GetMod slot 21.

NEW candidates this front surfaces — flag only the ones a kcdx-owned resolver would target:
- **CCryPak::GetFileSize (by name)** — slot 45, RVA 0x2418B48. 3-arg `(this, name, bDiskOnly)`.
- **CCryPak::IsFileExist (3-arg)** — slot 67, RVA 0x463EC4. `(this, name, location)`.
- **CCryPak::IsFileExist (2-arg)** — slot 70, RVA 0x241ABCC.
- **CCryPak::GetFileAttributes / IsFolder(disk)** — slot 68, RVA 0x241AC8C.
- **CCryPak::FindFiles / ForEachFile** — slot 14, RVA 0x241D2E8 (+ callback slot 15, RVA 0x241D1B8).
- **CCryPak::FindFirst (CCryPakFindData factory)** — slot 101, RVA 0x973294.
- **CCryPak::OpenPack / mount archive** — slot 71, RVA 0x7AD468 (ABI needs the mount front).
- **CCryPak::GetPakInfo** — slot 33, RVA 0x241A4E4 (+ free companion slot 34, RVA 0x2419354).
- **CCryPak::GetPakPriority** — slot 91, RVA 0x1A72460 (reads `*(*(this+0x228)+0x20)`).
- **CCryPak::FSeek / FTell / FEof / FFlush** — slots 53/54/56/59 (RVA 0x46068C / 0x460CDC /
  0x961D48 / 0x2418A90) — the residual handle-IO surface beyond the seeded FOpen/FWrite/FClose,
  only if kcdx returns its own (non-engine) handle and must reimplement them.

## Confidence map (this front)

VERIFIED (decompiled body / resolved string / resolved RTTI symbol, this build):
- 102-slot vtable extent (end at slot 102, non-exec).
- 60 slots role-named on a decompiled body or a resolved string anchor (the **V**/**V*** rows).
- The resolution-funnel fact: every by-name method calls slot 1 first (read directly in the
  slot 13/14/35/45/67/68/70/92/93 decompiles — `(**(code**)(*param_1 + 8))(param_1,name,buf,…)`).
- The handle-IO uniform shape (handle-deref + libc primitive).

INFERRED — needs-confirm (the **i** rows, ~42 slots): thin accessors/stubs and the
pool/config slots named from leaf-call shape only; the mount slot 71 ABI; the exact
FindData iterator vtable. None is load-bearing for Q3 (resolution funnels through slot 1,
which is VERIFIED). No build/boot claim made.
