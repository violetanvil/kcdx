# FINDINGS — CCryPak read-family slot dispatch ABIs (open+read cutover, step 3.2)

Captured 2026-06-15. The durable process-output the open+read cutover impl
sub-step reads: for every CCryPak vtable slot the cutover flips THUNK->KCDX, the
exact member-call arg-shape the ENGINE dispatches that slot with, so each kcdx
impl matches it (a wrong signature is silently called with garbage args -
AP2/AP19, a green build never catches it).

**The original pass was a reconciliation/extraction pass, NOT fresh disassembly** -
every slot below except 66 is read from an existing dump or the prior recon, cited
per slot (reverse-engineering.md reuse-first; see "Reuse sources" at the end).
**AMENDED 2026-06-15:** the lone UNVERIFIED slot (66) - reuse exhausted, no body in
any prior dump - was then resolved with a fresh tier-5 Ghidra dump (the co-located
`dump_slot66.java` + `_slot66_dump.txt`; see "## Slot 66 - freshly dumped"). Trust-
labels per working-artifacts.md:

- **BODY-VERIFIED** - the full decompiled body was read (this pass, from a cited
  prior dump) and the member arg-shape read directly off the decompile / the
  seeded DB signature.
- **LEAF-IDENTIFIED-ARGS-INFERRED** - front1 marks the slot **V** with a named
  libc leaf (fgets/ungetc/ferror/_errno/fflush/_fileno/fseek/_ftelli64/feof) on a
  decompiled body or resolved string, and the member arg-shape follows the libc
  leaf shape over the uniform handle-deref dispatch pattern, but the slot own
  full body was not re-decompiled this pass. The role is primary-evidence; the
  exact arg LIST is an agent inference from the leaf + the family pattern.
- **UNVERIFIED** - no evidence; flagged loudly, never an invented ABI.

All slots are __fastcall member functions; this (rcx) = CCryPak*. RVA =
VA - 0x180000000. The slot->+offset->FUN binding is read off the live CCryPak vtable
@ VA 0x183A95FA8 (RTTI .?AVCCryPak@@); every binding below agrees across
front1, the readpath decomp, and the slot35-recon - never in dispute.

---

## The three OPEN slots (already seeded by name - confirmed from the DB export, for the record)

These resolve by name (no new AP18 row); their signatures are confirmed from
data/db-export/ (the curated CSV export, the tier-1 reuse source).

| slot | +off | RVA | seed id | DB version-row signature (address_versions_seed.csv) | trust |
|---|---|---|---|---|---|
| 1 | +0x8 | 0x6205C | **id 152** AdjustFileName | ptr (ptr this, cstr pName, ptr outBuf, u32 nFlags) | BODY-VERIFIED (DB + slot35-recon) |
| 36 | +0x120 | 0x4614A0 | **id 131** FOpen | ptr (ptr this, cstr pName, cstr szMode, u32 nFlags) | BODY-VERIFIED (DB + id-131 body prose) |
| 35 | +0x118 | 0x2418DE4 | **kcdx_id 160** FOpenRaw | ptr (ptr, cstr, cstr, ptr, i32) = FILE*-like(this, pName, szMode, outResolvedBuf, int bufCap) | BODY-VERIFIED (slot35-recon _slot35_dump.txt) |

- **Slot 1 AdjustFileName (id 152)** - resolves a vpath to a path STRING. 4-arg:
  (this, pName, outBuf, nFlags). Returns resolved char*. Member ABI body-verified
  (front1 + slot35-recon (**(this+8))(this, pName, scratch, 0)).
- **Slot 36 FOpen (id 131)** - engine-wide open-by-path. 4-arg:
  (this, pName, szMode, nFlags), nFlags is u32. Returns FILE*-like. id-131
  prose: body does path strlen + 0x800 cap + mode-string parse; a body-wide
  stack-arg analysis confirms 4-arg fastcall.
- **Slot 35 FOpenRaw (kcdx_id 160)** - open-into-caller-buffer. 5-arg:
  (this, pName, szMode, outResolvedBuf, int bufCap). Returns FILE* (or 0).
  Body @ slot35-recon: resolves pName via slot 1, copies the resolved name into
  outResolvedBuf (clamped <=2048 = 0x800), opens via the _wfopen-backed
  primitive FUN_1809b2b28 (RVA 0x9B2B28), registers the handle ([*this+0x268]).

> **Cross-doc note surfaced (NOT for me to resolve - see Surfaced items in the
> digest).** The id-152 DB prose carries a CORRECTION (2026-06-03, gated, AP19)
> stating **FOpen (slot 36) does NOT call slot 1** - FOpen mints its handle
> independently (FOpen body @0x4614A0 contains no call to 0x6205c). The
> slot35-recon table caption ("a kcdx slot-1 owns slot 35 resolution for free")
> is about slot **35** (FOpenRaw, which DOES call slot 1 - read in its body), NOT
> slot 36. No conflict on slot 35; the slot-36-calls-slot-1 edge is the one the DB
> prose falsified, and nothing in THIS pass re-asserts it. Recorded so the impl
> sub-step does not infer a slot-36->slot-1 edge.

---

## The READ-FAMILY slot dispatch ABIs (the slots the cutover flips THUNK->KCDX)

The handle-tag dispatch mechanism (the read family shared shape): the handle is
a tagged union - a small index+1 value < pakEntryCount = a pak entry in
[this+0x40] (the pak arm), anything else = a real FILE*-class handle (the OS
arm). The loose-vs-pak decision was decided at FOpen-time; each read slot just
dispatches on the tag. (front3 mechanism, confirmed body-read in the readpath
decomp; slot ROLE labels per the slot35-recon reconciliation - front3 read-slot
role labels are superseded.)

### Core read family - slots 38 / 39 / 40 / 41 / 53 / 54 / 55 / 56

| slot | +off | RVA | role (body evidence) | member dispatch ABI | trust | evidence |
|---|---|---|---|---|---|---|
| 38 | +0x130 | 0x461304 | **FReadRaw-by-pak-index** - param_5-1 < pakEntryCount over [this+0x40]; pak arm -> FUN_1804618b4 (read-raw leaf, str "FRead did not read expected number of byte"); OS arm -> FUN_1804d7ab4 (CRT fread). NO fopen. | size_t (this, void* buf, size_t size, size_t count, longlong taggedHandle/pakIndex) - 5-arg | BODY-VERIFIED | _readpath_decomp.txt L259-284 (full body): FUN_180461304(p1, p2, p3, p4, p5); pak arm FUN_1804618b4(entry, p2, p3, p4); OS arm FUN_1804d7ab4(p2, p3, p4, p5) |
| 39 | +0x138 | 0x51E1F8 | **FReadRaw / cached-read** - handle-tag dispatch; OS arm fseek(h,0,0) then FUN_1804d7ab4 (CRT fread). A READ. | size_t (this, void* buf, size_t size, FILE* handle) - 4-arg | BODY-VERIFIED | _readpath_decomp.txt L293-317 (full body): FUN_18051e1f8(p1, p2, p3, p4 FILE*); OS arm fseek(p4,0,0); FUN_1804d7ab4(p2,1,p3,p4) |
| 40 | +0x140 | 0x51CD00 | **FGetCachedFileData** - dispositive string "!Cannot have more then 1 FGetCachedFileData at the same time"; single-instance cached read | void* (this, FILE* handle, longlong* outSizeDst) - 3-arg | BODY-VERIFIED | _readpath_decomp.txt L97-146 (full body): FUN_18051cd00(p1, p2 FILE*, p3 longlong*); writes *p3 = size, returns cached buffer ptr |
| 41 | +0x148 | 0xA700C8 | **FWrite** - fwrite over the handle | size_t (this, const void* buf, size_t size, size_t count, FILE* handle) - fwrite-shaped over the handle-deref dispatch | LEAF-IDENTIFIED-ARGS-INFERRED | front1 **V** role (leaf fwrite, feats=WRITE); id-131 prose names it the FWrite sibling slot (+0x148). Body not re-decompiled this pass; arg-list follows fwrite(buf,size,count,FILE*) over the family handle dispatch. |
| 53 | +0x1A8 | 0x46068C | **FSeek** - fseek | int (this, FILE* handle, long offset, int origin) - fseek-shaped (handle-tagged) | LEAF-IDENTIFIED-ARGS-INFERRED | front1 **V** role (leaf fseek, feats=SEEK). Body not re-decompiled this pass; arg-list follows fseek(stream,offset,origin) mapped over the engine handle. Pak arm seeks the pak cursor (FUN_180461964); OS arm fseeks the FILE*. |
| 54 | +0x1B0 | 0x460CDC | **FTell** - _ftelli64 | __int64 (this, FILE* handle) - _ftelli64-shaped (handle-tagged) | LEAF-IDENTIFIED-ARGS-INFERRED | front1 **V** role (leaf _ftelli64, feats=TELL). Body not re-decompiled; arg-list follows _ftelli64(stream) over the engine handle. |
| 55 | +0x1B8 | 0x4609D0 | **FClose** - handle-deref + FUN_180460958 | int (this, FILE* handle) - fclose-shaped (handle-tagged) | LEAF-IDENTIFIED-ARGS-INFERRED | front1 **V*** role (FINDINGS id-131 sibling +0x1B8; handle-deref + FUN_180460958). id-131 prose names it the FClose sibling. Body not re-decompiled this pass; arg-list follows fclose(stream) over the engine handle. |
| 56 | +0x1C0 | 0x961D48 | **FEof** - feof-equivalent, handle-deref | int (this, FILE* handle) - feof-shaped (handle-tagged) | LEAF-IDENTIFIED-ARGS-INFERRED | front1 **V** role (feats=EOF; handle-deref). Body not re-decompiled; arg-list follows feof(stream) over the engine handle. |

### Read variants - slots 43 / 44 / 46 / 47 / 57 / 58 / 59 (+ slot 66, see below)

front1 names each variant libc leaf on a decompiled body / resolved string (the
ROLE is primary-evidence **V**); the member arg-shape follows the leaf over the
uniform handle-deref dispatch (FUN_1804613d0/FUN_180462014 then the libc
primitive - the "File IO over a handle" family, front1 family-5). None of these
variant bodies was re-decompiled this pass.

| slot | +off | RVA | role + libc leaf (front1) | member dispatch ABI (leaf-shaped) | trust |
|---|---|---|---|---|---|
| 43 | +0x158 | 0x2418C40 | **FGets** - line read, leaf fgets | char* (this, char* buf, int maxCount, FILE* handle) - fgets-shaped | LEAF-IDENTIFIED-ARGS-INFERRED |
| 44 | +0x160 | 0x241A960 | **FGetc** - read one, handle-deref + FUN_18241aa0c | int (this, FILE* handle) - fgetc-shaped | LEAF-IDENTIFIED-ARGS-INFERRED |
| 46 | +0x170 | 0x460C08 | **fileno / handle-int** - leaf _fileno | int (this, FILE* handle) - _fileno-shaped | LEAF-IDENTIFIED-ARGS-INFERRED |
| 47 | +0x178 | 0x241DD6C | **FUngetc** - leaf ungetc | int (this, int ch, FILE* handle) - ungetc-shaped | LEAF-IDENTIFIED-ARGS-INFERRED |
| 57 | +0x1C8 | 0x16A2024 | **FError** - leaf ferror | int (this, FILE* handle) - ferror-shaped | LEAF-IDENTIFIED-ARGS-INFERRED |
| 58 | +0x1D0 | 0x2418B34 | **FGetErrno / FErrno** - leaf _errno | int (this, FILE* handle) - _errno-shaped (reads errno off the handle/CRT) | LEAF-IDENTIFIED-ARGS-INFERRED |
| 59 | +0x1D8 | 0x2418A90 | **FFlush** - leaf fflush | int (this, FILE* handle) - fflush-shaped | LEAF-IDENTIFIED-ARGS-INFERRED |

### Slot 66 - NOT a confirmed read-family slot (do NOT invent it)

The brief asked to confirm whether slot 66 is a real read-family slot. **front1
marks slot 66 (+0x210, RVA 0x241A3BC) i (INFERRED, needs-confirm)** - role
"GetFileno / handle-fileno variant" from a _fileno leaf-call shape only, NOT a
decompiled body or resolved string. It is a fileno-VARIANT (the same family as
slot 46), not a core read/seek/eof slot, and its role is the LOWEST confidence in
the set (front1 i, not V). The step-2 step doc lists slot 66 among the
variants the cutover flips; if the impl sub-step needs slot 66 exact ABI it must
DUMP slot 66 body fresh (it was not in any prior dump). I do not invent an ABI
for it. **Status: ~~UNVERIFIED~~ -> RESOLVED 2026-06-15 (body-verified, fresh
Ghidra dump). See "## Slot 66 - freshly dumped (body-verified)" below - the
front1 `i` role label ("GetFileno / handle-fileno variant") was WRONG: `_fileno`
is only an intermediate step; the real role is FGetModificationTime.**

---

## Slot 66 - freshly dumped (body-verified)

Resolved 2026-06-15 by a fresh Ghidra dump (reverse-engineering.md tier 5 - reuse
was exhausted for this one slot, no body in any prior dump). The dump re-confirmed
the binding off the live vtable and decompiled + disassembled the full body. This
is the ONE flipped read-family slot whose ABI was genuinely UNVERIFIED; it is now
**BODY-VERIFIED**, read directly off the decompile + the register disasm (never
inferred from the name or the front1 `i` fingerprint - AP2/AP19).

**Binding (re-confirmed):** CCryPak vtable @ VA 0x183A95FA8, slot 66 = +0x210 ->
0x18241A3BC. The dump asserted `vtable[+0x210] == 0x18241A3BC` = CONFIRMED (neighbours
for sanity: slot 65 +0x208 -> 0x18097383C, slot 67 +0x218 -> 0x180463EC4).

| slot | +off | RVA | role (body evidence) | member dispatch ABI | trust | evidence |
|---|---|---|---|---|---|---|
| 66 | +0x210 | 0x241A3BC | **FGetModificationTime / GetFileMTime** - returns the file's last-write time as a packed FILETIME (`__int64`). NOT a fileno getter (`_fileno` is only an intermediate step to reach `GetFileTime`). Handle-tag dispatches like the rest of the read family. | __int64 (this /*rcx*/, FILE* handle /*rdx*/) - 2-arg | BODY-VERIFIED | _slot66_dump.txt L42-150 (binding L42-46; full decompile L53-91; disasm L96-150) |

- **Member ABI (verified, read off the decompile + disasm):**
  `__int64 __fastcall slot66(CCryPak* this /*rcx*/, FILE* handle /*rdx*/)` - **2-arg
  fastcall**. `this`=RCX (used as the SRW reentrant lock `this+0x10`, the pak-entry
  vector `this+0x40`/`this+0x48`, `this+0x20`); `handle`=RDX (the tagged-union handle).
  Return = RAX = `undefined8` = a 64-bit packed FILETIME.
- **Handle-tag dispatch: YES** - the core read-family shape. The disasm computes
  `RDI = handle - 1` (`LEA RDI,[RDX-0x1]`) and compares it against the pak-entry count
  `(this+0x48 - this+0x40) / 0x18` (the `* -0x5555...` after `SAR ...,0x3` = divide by
  3 then by 8 -> /0x18, each pak entry = 0x18 bytes). `handle-1 < pakEntryCount` ->
  PAK arm; else -> OS arm. Same tagged-union (`index+1` vs real `FILE*`) decided at
  FOpen-time that slots 38/39 dispatch on.
- **Body summary (what each arm does):**
  - **PAK arm** (`handle-1 < pakEntryCount`): reads the pak entry at
    `[this+0x40] + 8 + (handle-1)*0x18`, dereferences `+0x28`, and calls
    `FUN_180eb72b0` - which converts the entry's stored **DOS date/time** (packed
    16-bit fields at entry+0x1c / +0x1e: `wYear=(x>>9)+0x7bc, wMonth, wDay, wHour,
    wMinute, wSecond`) via `SystemTimeToFileTime` into a FILETIME. (If the entry
    already cached a FILETIME at +0x20/+0x24, it returns that directly.)
  - **OS arm** (else): `_fileno(handle)` -> `_get_osfhandle(fd)` ->
    `GetFileTime(hFile, &create, &access, &lastWrite)` -> returns the **last-write**
    FILETIME (`local_res8`) as `__int64`.
  - Both arms are wrapped in the read family's reentrant-lock acquire
    (`FUN_180462014`, TLS-tid + SRW) / release (`FUN_180506f94`) around `this+0x10`.
- **front1 correction:** front1 marked the role "GetFileno / handle-fileno variant"
  off the `_fileno` leaf-call shape alone (the `i` fingerprint). The body shows
  `_fileno` is only the first hop of an `_fileno -> _get_osfhandle -> GetFileTime`
  chain; the slot RETURNS a file modification time, not an fd. **Role corrected to
  FGetModificationTime; this is why a name/fingerprint guess (AP2) is never the ABI.**
- **Cutover impact:** the kcdx impl flips this slot THUNK->KCDX and must match
  `__int64 (CCryPak* this, FILE* handle)` exactly (2 args, RDX=handle, RAX=FILETIME),
  handle-tag dispatching on `handle-1 < pakEntryCount`. A wrong arity (treating it as
  the 1-arg `_fileno` shape front1 implied) would be called with a garbage RDX and
  return junk - the AP2/AP19 silent-miscall the brief flagged.
- **Trust label: BODY-VERIFIED** (dump cite: `_slot66_dump.txt`, this dir; producer
  script `dump_slot66.java`, this dir).

---

## UNVERIFIED slots - the blocker list for the impl sub-step

Every CORE read slot (38/39/40) has a BODY-VERIFIED ABI; every flipped open slot
(1/35/36) has a BODY-VERIFIED ABI. The residual ABIs are LEAF-IDENTIFIED (role
primary-evidence, arg-list leaf-inferred) - buildable, but the impl sub-step
SHOULD confirm a LEAF-IDENTIFIED slot exact arg-list against its body before
landing its impl if the leaf shape is ambiguous (e.g. whether FWrite is fwrite-
ordered (buf,size,count) or a CryPak variant; whether FSeek origin is the libc
int or an enum). The one formerly-UNVERIFIED member arg-shape, **slot 66**, was
RESOLVED 2026-06-15 by a fresh Ghidra dump (see "## Slot 66 - freshly dumped
(body-verified)" above) - no UNVERIFIED slot remains.

- **Buildable now (BODY-VERIFIED ABI):** slots 1, 35, 36, 38, 39, 40, **66**.
- **Buildable with a leaf-shaped ABI (LEAF-IDENTIFIED - role is primary-evidence,
  arg-list is the libc leaf over the family handle-dispatch):** slots 41, 43, 44,
  46, 47, 53, 54, 55, 56, 57, 58, 59.
- ~~**UNVERIFIED member arg-shape (a blocker IF the cutover flips it):** slot 66~~
  -> **RESOLVED 2026-06-15 (BODY-VERIFIED):** slot 66 is `__int64 (CCryPak* this,
  FILE* handle)` - 2-arg, handle-tag dispatches, role FGetModificationTime (NOT the
  fileno-variant front1 `i` implied). Fresh dump `_slot66_dump.txt`; no guess.

So: EVERY flipped slot now has a buildable ABI (BODY-VERIFIED or LEAF-IDENTIFIED).
The slot-66 blocker is closed; there is no remaining surfaced blocker.

---

## Reuse sources (the dumps this extraction rests on - NO fresh Ghidra this pass)

- **_research/fs-takeover-slot35-recon/FINDINGS.md** - the authoritative
  slot->FUN->role->ABI table (slots 1/35/36/38/39/40/41/53/54/55/56); gated-verifier-
  confirmed. PRIMARY source.
- **_research/phase8.5-pak-resolver/_readpath_decomp.txt** - full decompiled
  bodies for slots 38 (L259-284), 39 (L293-317), 40 (L97-146) + the handle-deref
  helpers. The BODY-VERIFIED source for the core read slots.
- **_research/phase8.5-pak-resolver/_readpath_leaves.txt** - the pak-arm /
  OS-arm read leaves (FUN_1804618b4 read-raw, FUN_1804d7ab4 CRT fread,
  FUN_18051ce40 pak read).
- **_research/phase8.5-pak-resolver/front1-full-vtable-surface.md** - every
  read/variant slot +offset, RVA, role, V/i mark, and named libc leaf (53=fseek,
  54=_ftelli64, 56=feof, 43=fgets, 44=FGetc, 46=fileno, 47=ungetc, 57=ferror,
  58=_errno, 59=fflush, 66=fileno-variant marked i). The LEAF-IDENTIFIED source.
- **data/db-export/address_names_seed.csv + address_versions_seed.csv** - the
  seeded signatures for ids 131/152/160 (the open slots), confirmed for the record.

## Producers (this dir)

The original extraction was a pure reuse/reconciliation pass (no script). The
slot-66 resolution (2026-06-15) added ONE fresh-dump producer, co-located here:

- **dump_slot66.java** - the headless Ghidra script that re-confirmed the slot-66
  binding off the live vtable and decompiled + disassembled FUN_18241A3BC + its
  leaves. Run: `analyzeHeadless.bat "<ghidra_project>" KCD2 -process WHGame.dll
  -noanalysis -readOnly -scriptPath <this-dir> -postScript dump_slot66.java`.
- **_slot66_dump.txt** - the raw headless output (leading underscore = machine-
  generated). The BODY-VERIFIED source for the slot-66 section above.

The other dumps this FINDINGS cites live in their own recon dirs (Reuse sources, above).

## Trust summary

- PRIMARY EVIDENCE (a body read from a cited dump, or a cited DB row): slots 1,
  35, 36, 38, 39, 40 (full member ABIs) + **slot 66** (full member ABI + role,
  body-verified 2026-06-15 from the fresh `_slot66_dump.txt`) + the role of every
  read/variant slot (front1 V rows).
- AGENT-AUTHORED (an inference): the exact arg-LIST of every LEAF-IDENTIFIED slot
  (41, 43, 44, 46, 47, 53, 54, 55, 56, 57, 58, 59) - the libc-leaf shape over the
  family handle-dispatch, a lead to confirm against the body if ambiguous, not a
  body-read fact. Slot 66 is no longer in this class - its role and ABI are now
  BODY-VERIFIED (and its front1 `i` role label was corrected by the dump).
