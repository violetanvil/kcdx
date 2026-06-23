# KI-0028 — the single ordered VANILLA INIT+FS MAP, boot → the level-load abort's tested condition

**Date:** 2026-06-22 · **Trust:** synthesis re-grounded (AP19). Every load-bearing step + cross-front
call-edge below was re-read in the OWNING body / cited dump THIS pass; no step is blind-stitched. Where a
front marked something unverified, the marker is carried forward verbatim in §"Still-unverified load-bearing edges".

Image base `0x180000000`; WHGame.dll `release_1_5_1164953_841`. RVA = VA − 0x180000000.
gEnv `0x18492B800`; pCryPak global `0x18492B850` (gEnv+0x50); CCryPak vtable VA `0x183A95FA8`.

**Apex question the map answers:** *what does the level-load abort TEST, and which kcdx slot-logic deviation
flips that tested condition?* — answered in §10 (the tested condition) + §11 (the deviation), falsifiable.

---

## Stage 0 — boot reaches CSystem::Init; pCryPak is read ONCE, before the ModManager ctor

`CSystem::Init` @ `0x7A6C64` (re-read `ccrypak-init-order-recon/_csysinit_fs_order.txt` this pass):

- The body makes **71 direct CALLs before** the ModManager ctor call site at `0x7A76FE`.
- **gEnv+0x50 (pCryPak) is read exactly ONCE in the Init body** — at `0x7A7225` (`mov rcx,[rip+0x4184624]`),
  which feeds a `call [rax+0x238]` (a CCryPak vtable slot, +0x238) at `0x7A723A`. This is BEFORE the ctor call.
- **ZERO `CCryPak::FOpen` (slot 36, `0x4614A0`) calls appear in the Init body itself** — Init wires pCryPak,
  it does not open files through it inline.
- The ctor call site `0x7A76FE → 0xDA0EB0` (ModManager_ctor) is where kcdx's takeover brackets (project memory
  `init-cycle-ownership`: kcdx_id resolution at the ModManager_ctor hook; `g_kcdxReadyEvent` gate).

`fsRole`: **construct/wire** — pCryPak is seated and reachable before any FS-driving subsystem runs.

## Stage 1 — CCryPak construct / seat (swap mechanism EXONERATED — input, not open)

The CCryPak object is constructed and its vtable pointer is reachable at `*(gEnv+0x50)`. kcdx's full-swap
overwrites `[obj+0x00]` with its own 102-slot vtable at construct-time; thunked slots forward to the engine body.

**SETTLED (PROBE-Z, `ki0028-findfirst-replay-contract-recon/PROBE-Z-swap-mechanism-exonerated.md`, re-read):**
a NO-OP thunkswap (kcdx's vtable installed, `kcdx_owned=0`, every slot forced to thunk) renders
(`draw_indexed=29772` vs black baseline 0). The swap MECHANISM — `[obj+0x00]` overwrite, object identity,
seat timing, index build — is INNOCENT. The cause is a slot family's LOGIC.

> **CAVEAT carried forward (ROOT-CAUSE 20:49 correction):** the PROBE-Z no-op run is NOT a clean control —
> it reached the MAIN MENU and requested `level.pak` ZERO times; it never reached the kutnohorsko LEVEL load.
> The full swap reaches the level load and aborts. The two runs failed at DIFFERENT stages. So PROBE-Z exonerates
> the swap *mechanism* (a thunkswap renders), but the no-op-vs-full comparison does NOT by itself isolate which
> slot family flips the level-load abort — that is pinned below from the abort body + the live FS trace, not from PROBE-Z.

`fsRole`: **seat** (timing characterization; not an FS op).

## Stage 2 — subsystem order: search-path + pakPriority registration

The resolver's `this`-struct fields are populated during init (`phase8.5-pak-resolver/front4` + `searchpath-registrar-mechanism2.md`, re-grounded):

- **Search-path vector** `[this+0x198 .. +0x1a0]` — AddMod (slot 19, `0x19AF1A8`) push_backs a root; slot-1
  AdjustFileName iterates this SAME vector back-to-front (registration order = search precedence).
- **Alias table** `[this+0x1b0 .. +0x1b8]` — SetAlias (slot 22, `0x241C068`) / GetAlias (slot 24, `0x1790B54`),
  a `{from,fromLen,to,toLen}` prefix-substitution redirect.
- **Data root** `[this+0x188]`; **pakPriority cvar** `[this+0x228 → +0x20]` (`VF_NULL`, freely cfg-settable —
  `MECHANISM-CONFIRMED-pakpriority-loose.md`). Default mode 2 = PAK-first.

`fsRole`: **resolve-state setup** — the tables slot-1 consults are filled before first open.

## Stage 3 — pak discovery + mount (init/level-load-time; MOUNT-ONCE)

The mount path (`phase8.5-pak-resolver/front2` + `fs-takeover-pak-mount-recon/FINDINGS.md`, re-grounded). Ordered:

1. **Mount entry** — OpenPack (slot 6 explicit-root `0xDA4E5C` / slot 7 auto-root `0x193CB14`) or OpenPacks-glob
   (slot 9 `0x4D9BB0` / slot 10 `0x197C598`) ingests a pak / a `*.pak` glob. All converge on the register worker.
2. **OpenPacks glob worker** `0x4D9C4C` — `strchr '*'/'?'`: no wildcard → one register call; wildcard → enumerate
   matching pak files, open each. The mod-absorb / engine C_ModManager enabled-list path enters here.
3. **Register worker** `0x4D4824` → split-aware open `0x4D495C` handles `<name>-part%d` up to 100 parts (the level
   paks ship as `cestool-part0..5`, `hlod-part0..5`, `svo-part0..2`), each part one per-part mount leaf `0x4D526C`.
4. **Per-part mount leaf** `0x4D526C` → archive factory at `(*this+0x240)` = slot 72 `0x4D5580`. **STEP 1 runs the
   .pak path through AdjustFileName = vtable SLOT 1, flag `0x10000`, BEFORE opening the file.** LOAD-BEARING LINK:
   the .pak path itself is slot-1-resolved, and **kcdx owns slot 1 on a full swap** (`fs-takeover-pak-mount-recon` Q1, AP19-clean).
5. **Factory STEP 2-5** — cache-lookup `0x4D5990` (refcount if mounted); STEP 3 cache-miss opens the FILE via
   `thunk_FUN_1809b2b28(name,'rb')` (logs "Cannot open Pak file %s" on null); STEP 4 consistency; STEP 5 ZipDir CDR
   parse `0x4D5ED4`+`0x18247B838` builds the in-memory dir index → ICryArchive.
6. **Rank-insert** `0x4D70A4` inserts into the loaded-pak vector `[this+0x120 .. +0x128]` (stride 0x38; entry
   `{bound-root@0, pakObj@0x28, ZipDir-archive@0x30}`) before the bit-10-flagged tail band; resolver `0x4631F0`
   iterates back-to-front → mount order = resolution precedence.
7. **MOUNT-ONCE** — all positively-read OpenPack/OpenPacks/ClosePakByIndex drivers are init/level-load-time; no
   gameplay-driven re-mount path was found (`fs-takeover-pak-mount-recon` Q2: slot100 release leaf `0x4607E4`, 6
   callers all FClose/teardown). Runtime serves from already-mounted paks via the slot-1 chokepoint.

`fsRole`: **mount** (open + ZipDir-index + register into the resolution vector). Slot-1 AdjustFileName on the .pak
path (step 4) is the kcdx-owned chokepoint EVEN during mount.

## Stage 4 — resolve / AdjustFileName / open (the single chokepoint)

`RESOLUTION-OWNERSHIP-synthesis.md` + `fs-takeover-readslot-abi-recon/FINDINGS.md` (re-grounded):

- **Slot 1 AdjustFileName `0x6205C`** is the SINGLE resolution chokepoint. Every by-name vtable consumer (opens
  35/36/38, existence 45/67/70, sizes 92/93, attrs 68, folder 13, enumerate 14, delete 49, copy 52, CRC 81/82) calls
  it first. It returns a path STRING, not a handle. Owning slot 1 owns resolution for BOTH asset classes (mmapped + handle-consumed).
- **Slot 36 FOpen `0x4614A0`** mints a TAGGED handle. **CORRECTION (DB prose, `fs-takeover-readslot-abi-recon`):
  FOpen does NOT itself call slot 1** — it mints its handle from its own pak lookup `FUN_180462ddc`, independent of
  the slot-1 string resolve. (The "every consumer calls slot 1 first" funnel is the BY-NAME consumers; FOpen's
  internal pak test is a separate path.)
- **kcdx slot-1 behavior** (`ki0028-adjustfilename-consumer-recon`, re-grounded): loose hit → disk path; PAK hit →
  returns `pName` UNCHANGED (raw, un-normalized); miss → thunks the engine original (string-only). **DIVERGENCE C
  (un-normalized `%engine%`-rooted return) is FALSIFIED as a DIRECT wedge driver** — all 37 provenance-verified
  consumers in 31 funcs feed the result to a file op; ZERO branch on the string's FORM (only 3 trailing-separator
  checks on a local copy). The residual: a kcdx_FOpen re-resolution of the raw form could mismatch the engine's
  `Data/`-rooted key (the KI-0026 alias-namespace class) and surface a wrong file LATER — an FOpen-resolution
  question, not an AdjustFileName-consumer-branch one.

`fsRole`: **resolve** (slot-1 string) + **open** (slot-36 handle mint).

## Stage 5 — handle lifecycle (the open-family HOW-not-WHAT deviation)

Vanilla FOpen (`0x4614A0`) + read family (`asset-fopen-handle-recon` + `fs-takeover-slot35-recon`, re-grounded):

1. **Scope lock** — vanilla FOpen acquires the pak-mgr SRW lock via `[this+0x200]` (`FUN_1804613d0` prologue) on
   EVERY open. kcdx uses its OWN private `g_poolLock`; never touches `[this+0x200]`.
2. **Two handle arms** — PAK arm returns `index+1` into the pak-handle vector `[this+0x40]` (scans a free slot,
   may GROW/shift the vector, mutating `[this+0x40]`/`[this+0x48]`, writes the entry); LOOSE arm returns a raw CRT
   `FILE*` (`FUN_1809b2b28` _wfopen, or `FUN_182423e08` CreateFileA→_fdopen).
3. **Register side-effects** — on a successful LOOSE open, vanilla FOpen fires `(*this+0x268)(this, handle, name)`
   (register-handle) AND `(*this+0x2c8)(this, …)` (post-open hook). FOpenRaw (slot 35) fires the identical pair.
   **kcdx FOpen/FOpenRaw fire NEITHER** (`open_slots.cpp` OpenResolvedAndMint mints via MintLoose/MintPak only).
   **This is D1, the load-bearing deviation.**
4. **Read dispatch** — the read family decides by INDEX-vs-COUNT: `R8 = handle-1`; `RAX = FUN_180427e40([this+0x40])`
   = the pak-handle vector element count (÷0x18); `handle-1 < count` → pak arm, else → OS arm. A real FILE* (huge
   ptr) always lands OS; a pak `index+1` (1..count) always pak — no collision.
5. **kcdx handle** — kcdx mints `(id<<1)|1` ODD handles (smallest 3) for EVERY open. Because kcdx OWNS every
   read/metadata/find slot, no engine code runs the `handle-1<count` test on a kcdx handle; 16-byte FILE* alignment
   guarantees an odd handle never aliases the OS arm. kcdx never mints a pak-vector index+1 and never mutates `[this+0x40]`.
6. **Find-handle (63/64/65)** — vanilla FindClose (`0x18097383c`) object-derefs the handle (refcount @+8, virtual
   dtor through handle+0x0); kcdx returns integer `(id<<1)|1`. **DIVERGENCE B (object-straddle) FALSIFIED** — all 53
   boot triplet consumers use the `-1<h` test + opaque pass-back kcdx's integer satisfies. Off the critical path.

`fsRole`: **open/read** — the kcdx-owned handle representation replacing the vanilla tagged-union; D1 (skipped
+0x268/+0x2c8) is the prime remaining suspect on this axis.

## Stage 6 — the level load (C_Game::CreateInstance → CET sequence)

The dump (`kcdx_2026-06-22_18-21-45.dmp` .ecxr; re-read `raw-level-load-abort.txt`) puts the main thread in
`MessageBoxA` under `WHGame!C_Game::CreateInstance`, frame chain through RVA `0x23ACACA`, with ZERO kcdx frames on
any of 194 threads (live invasive cdb, ROOT-CAUSE 20:49). Boot ran ~2.5 min, reached the kutnohorsko LEVEL load,
and the engine itself aborted. The per-level CryAction client-establishment task `CET_PrepareLevel` raises.

`fsRole`: **orchestration** — the FS that populates the level record happens one+ frames ABOVE this CET (the
level-info / resourcelist loader, NOT traced to its body this corpus).

## Stage 7 — THE ABORT'S TESTED CONDITION (body-read, falsifiable)

`CET_PrepareLevel` (CryAction `CET_LevelLoading.cpp`; raise-site RVA `0x23ACACA`, hot/cold-split body at `0x183EB4C`).
The abort is `RaiseException(0xD2)` — DISTINCT from KI-0026's `0xC8`/CSystem::FatalError (`0x2459810`). Body, read:

```
0x183eb5b  mov  rcx,[rip+0x3c5753e]   ; .data 0x1854960a0 = CryAction/Game singleton
0x183eb62  call 0x66bbf0             ; GETTER: rdx=[rcx+0x88]; if rdx==0 ret 0; else rax=[rdx+0x58]
0x183eb67  mov  edi,2                 ; default CET result
0x183eb6c  test rax,rax
0x183eb6f  je   0x23ac994            ; rax==0 (empty current-level record) -> banner/rejoin
0x183eb7d  call 0x183ecac            ; copy record->[0xc8] (the LEVEL-NAME CryString) into a local
0x183ebaa  mov  rbx,[rsp+0x50]        ; rbx = the copied level-name buffer
0x183ebaf  cmp  dword ptr [rbx-8],0   ; <== TESTED CONDITION: CryStringT LENGTH header == 0 ?
0x183ebb3  je   0x183ebc7            ; length==0 -> skip (ret)
0x183ebb5  cmp  byte [rip+0x30ece7c],0; .data 0x18492ba38 report-gate flag
0x183ebbc  je   0x23ac9be            ; flag==0 -> FATAL epilogue
...epilogue 0x23aca57..0x23acac5:
0x23aca94  mov  rcx,[rip+0x257ee25]   ; gEnv+0xC0 = 0x18492b8c0 (ISystem)
0x23aca9e  call [rax+0x660]          ; ISystem vcall -> bool (dialog-gate predicate)
0x23acaba  call [rip+0x1656888]      ; MessageBoxA(NULL,"The level can't be loaded, exiting...",cap,0x2010)
0x23acac0  mov  ecx,0xd2
0x23acac5  call 0x23dc960            ; -> RaiseException(0xD2)  [ALWAYS — the controlled exit]
```

**The tested condition, in falsifiable terms:**

> The engine's **current-level record** — `Game->[0x88]->[0x58]` (the ILevelSystem current-level object, carrying
> the level-name CryString at `[+0xc8]`), reached from the CryAction/Game singleton at `.data 0x1854960a0` — is
> **not validly populated** by the time `CET_PrepareLevel` runs: either the getter returns rax==0 (the `[0x88]` or
> `[0x58]` link is null — empty record) OR the level-name CryString's length header `[rbx-8]` is 0 (no name set).
> Either state, with the report-gate flag clear, reaches the `MessageBoxA` + `RaiseException(0xD2)` epilogue.

The record/name is populated by the level's own metadata read. **What the abort tests is therefore a downstream
consequence of an FS read** — specifically the level's `resourcelist` / level-info read that should fill that record.

## Stage 8 — which kcdx deviation flips it (the apex answer)

Two non-exclusive candidate flips, both routing through kcdx-owned slots, RANKED by live evidence:

**CANDIDATE 1 (LEADING — live-observed, `ROOT-CAUSE-existence-overreport.md` 20:49 correction, re-read):**
on a clean FULL swap (`kcdx_owned=31 probe_z_live_mask=15`, kFamAll), kcdx returns `result=0`/`how=miss-original`
for the level resource files the level load needs:
`loose_open_failed vpath="levels/kutnohorsko/auto_resourcelist.txt" errno=2` (kcdx tried a LOOSE disk open of
`data\levels\kutnohorsko\auto_resourcelist.txt`, file-not-found), then `how=miss-original result=0`; SAME for
`resourcelist.txt`. **These files live INSIDE the indexed level paks yet do not resolve.** If the level-info loader
reads `resourcelist.txt` to populate the current-level record, a kcdx miss on it leaves `Game->[0x88]->[0x58]` /
the name @`[+0xc8]` empty → the Stage-7 gate fires → `0xD2`. This is a kcdx **resolve/serve** deviation on a
`levels/` vs `data/levels/` key-normalization OR a pak-content indexing gap for these specific in-pak files (the
20:49 correction explicitly leaves WHICH unpinned — leading but unverified).

**CANDIDATE 2 (handle-lifecycle, D1 — `raw-handle-lifecycle.txt`, re-read):** kcdx FOpen/FOpenRaw SKIP the
`+0x268` register-handle slot and `+0x2c8` post-open hook that EVERY vanilla open fires. If the level-info loader
opens its files and a later validation/teardown WALKS the engine's registered-open-handle structure (which kcdx
leaves empty) and the level-record population depends on it, the same record-empty state could flip. Strongest on
the open-family axis PROBE-Z named as the prime lead — but the `+0x268` body is unread (see below), so this is
a structurally-plausible candidate, not a confirmed driver.

**Both candidates produce the SAME observable** (empty current-level record → `0xD2`); they are distinguished only
by WHERE the record-population fails — a serve miss on resourcelist (Candidate 1, live-observed) vs a handle-
registration side-effect the loader depends on (Candidate 2, structural). Candidate 1 is the LEADING answer because
it is directly observed in the failing-run log; Candidate 2 is the open-family lead that the abort body does not
rule out.

---

## Still-unverified load-bearing edges (carried forward; each could change the apex answer)

1. **The level-info loader that writes `Game->[0x88]->[0x58]` / the name @`[+0xc8]` from a file read was NOT traced
   to its body.** The edge from "kcdx misses `resourcelist.txt`" → "the current-level record is empty" is inferred
   from the corpus FS trace + the abort body, NOT read from that loader. The decisive missing read: does the loader
   consume `resourcelist.txt` (Candidate 1) — and via which slot (FOpen vs an existence probe) — to set the record?

2. **Which abort branch the full-swap boot takes is not distinguishable from the dump** — getter-rax==0 (empty
   record, `0x183eb6f`) vs name-length-0 (`0x183ebaf`). Both reach the same `0xD2` epilogue; the dump confirms only
   execution is in the `MessageBoxA`. A probe that reads `Game->[0x88]`, `[0x58]`, and the name length at the abort
   would split them — and would tell whether the record is null (link missing) or present-but-unnamed (read partial).

3. **The `+0x268` register-handle slot BODY (D1) is UNREAD.** A NAME/ROLE CONFLICT stands: `front1-full-vtable-surface.md`
   maps slot 77 (+0x268) to `FUN_18241c9f4` labelled "%USER% expansion / `_strnicmp` against %USER%", yet
   `fs-takeover-slot35-recon` and `fs-takeover-readslot-abi-recon` ASSERT "+0x268 registers the handle" (front1 itself
   flagged the discrepancy). So "register the handle" is an ASSERTED role, never body-confirmed. WHAT structure
   `+0x268` populates and whether ANY level-load/teardown/validation path WALKS it (kcdx leaves it empty) is the
   single un-bisected edge that could flip the abort on the handle axis — owed a fresh body read of `FUN_18241c9f4`
   (and the `+0x2c8` `FUN_18083f6d0` post-open hook).

4. **The level-load-time MOUNT driver is unverified through the vtable.** Mount entries 6/7/9/10 + factory 72 have
   ZERO direct callers (virtual dispatch; `[reg+0x238]`/`[reg+0x320]` displacements collide with unrelated vtables,
   not dataflow-attributable to CCryPak). Whether `C_Game::CreateInstance`'s level load calls OpenPack on
   `data/levels/<lvl>/*.pak` (a MOUNT) vs reads level resource files from already-mounted/indexed pak content (a SERVE)
   is NOT body-pinned. The live trace observed no `FOpen .pak ... result=` line for the level paks — only AdjustFileName
   RESOLVES — so the mount FOpen was not observed either. MOUNT-ONCE rests on positively-read init-time drivers + the
   CryEngine-architecture lead, NOT a proof-of-absence of a level-load-time mount edge.

5. **The recursive-walk "fix" reconciliation is open.** `IndexPakRoot` `directory_iterator`→`recursive_directory_iterator`
   grew the index 46→77 paks / 307k→509k entries and a `18-37` run showed the abort gone; the `20-49` full-swap run has
   the abort BACK. Whether `18-37` was a real fix later regressed or never reached the level load is unreconciled — so
   even with level-pak CONTENTS indexed, the `resourcelist.txt` files inside them resolve to miss (Candidate 1's open core).

6. **The ISystem vtable[+0x660] dialog-gate predicate** (`0x23aca9e`) and the report-gate flags (flagA `0x18492ba38`,
   flagB `0x18492bbd1`) were not body-read — they gate whether the failure REPORTS vs returns, not the load test itself.

7. **CCryFile buffering wrapper handle-inspection** was not body-read (residual from `asset-fopen-handle-recon`): CCryFile
   routes through FOpen so likely benign, but whether a level-load consumer wraps the kcdx handle in CCryFile and inspects
   it differently than the raw FRead family is unverified.

---

## One-line stage chain

boot → `CSystem::Init` (pCryPak read once @`0x7A7225`, before ModManager ctor `0x7A76FE`, zero inline FOpen) →
CCryPak seat (swap mechanism EXONERATED) → search-path/alias/pakPriority tables filled → pak mount
(OpenPacks→register→split→factory slot72, slot-1 AdjustFileName on the .pak path, ZipDir index, rank-insert;
MOUNT-ONCE) → slot-1 resolve chokepoint (kcdx-owned; DIVERGENCE C falsified) → FOpen handle mint (kcdx odd
`(id<<1)|1`, skips +0x268/+0x2c8 = D1) → read family (kcdx owns; tag-dispatch) → `C_Game::CreateInstance` level
load → `CET_PrepareLevel` → **TESTED CONDITION: `Game->[0x88]->[0x58]` current-level record / name@`[+0xc8]`
length empty → `MessageBoxA` + `RaiseException(0xD2)`** ← flipped by kcdx missing the level `resourcelist.txt`
that populates that record (Candidate 1, live-observed) and/or the skipped +0x268 handle-registration (Candidate 2, structural).
