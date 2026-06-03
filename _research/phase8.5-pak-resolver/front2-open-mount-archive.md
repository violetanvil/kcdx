# Front 2 — the pak OPEN / MOUNT / ARCHIVE machinery (how a .pak is ingested + ranked)

Captured 2026-06-02. Additive to `subresolver-decompiled-mechanism.md`,
`searchpath-registrar-mechanism2.md`, `FINDINGS.md`, and the prior probe-U.5
dumps (`_u5_worker2.txt` / `_u5b_worker.txt`). Trust level: PRIMARY EVIDENCE
(fresh Ghidra decompile of WHGame.dll release_1_5_1164953_841, image base
0x180000000). Every slot binary-read from the CCryPak vtable @ VA 0x183A95FA8
(AP3 — not a canonical-header label). Every ABI/role from the decompile body
(AP2 — no prologue-guessing). No live probe; runtime-effect items flagged
NEEDS-LIVE-CONFIRM.

Producers (this dir + `third-party-ghidra/ghidra_scripts/`):
`PakOpenMount.java` (`_f2_openmount_raw.txt` — full 96-slot vtable dump, slot
verification, OpenPack/CDR string anchors, the membership search + per-part mount
leaf + wrapper/worker decompiles), `PakOpenMount2.java`
(`_f2_openmount2_raw.txt` — archive factory slot-72, CDR-build leaves, the
OpenPacks-glob entry, the rank-insert helper). The prior U.5 dump is reused, not
re-walked.

---

## The open/mount vtable surface (VERIFIED slots)

The CCryPak vtable @ 0x183A95FA8 carries a family of pak-open methods. Slots read
live from the table (the full 96-slot dump is in `_f2_openmount_raw.txt`):

| slot | +off | RVA | function | role |
|---|---|---|---|---|
| 6 | +0x30 | 0xDA4E5C | `FUN_180da4e5c` | **OpenPack** (explicit bind-root) — VERIFIED slot 6 |
| 7 | +0x38 | 0x193CB14 | `FUN_18193cb14` | **OpenPack** (auto bind-root from path dir) |
| 9 | +0x48 | 0x4D9BB0 | `FUN_1804d9bb0` | **OpenPacks** (glob; one overload) |
| 10 | +0x50 | 0x197C598 | `FUN_18197c598` | **OpenPacks** (glob; other overload) |
| 72 | +0x240 | 0x4D5580 | `FUN_1804d5580` | **archive-object factory** (opens file + builds ICryArchive) |

Cross-check against prior fronts (same table, same build): slot 1 = AdjustFileName
`FUN_18046205c`, slot 19 = AddMod `FUN_1819af1a8`, slot 36 = FOpen `FUN_1804614a0`,
slot 41 +0x148 FWrite, slot 55 +0x1B8 FClose — all match `FINDINGS.md` /
`searchpath-registrar-mechanism2.md` exactly, validating this table read.

The "OpenPacks" string (plural, as a logged token) does NOT exist in `.rdata`
(scanned 0x183A00000..0x185100000) — OpenPacks is a C++ method, not a log string.
The mod-absorb `OpenPacks` call (`src/mod_absorb/enabled_list_builder.cpp:57`)
binds to slot 9/10 → the glob worker `FUN_1804d9c4c` (below).

---

## The open call-tree (top-down, VERIFIED)

```
OpenPacks slot 9/10 (FUN_1804d9bb0 / FUN_18197c598)   [glob entry]
  -> FUN_1804d9c4c  glob worker  (strchr '*' / '?' -> enumerate-or-single)
       -> FUN_1804d4824  register worker            (per matched/single path)
OpenPack slot 6 (FUN_180da4e5c)  [explicit root]
  -> AdjustFileName slot1 on BOTH path (flags 0x50000) and bind-root
  -> FUN_1804d4824  register worker
OpenPack slot 7 (FUN_18193cb14)  [auto root = strrchr(path,'\\')]
  -> FUN_1804d4824  register worker
        -> FUN_1804d495c  open+register (SPLIT-pak aware: "<name>-part%d", up to 100)
             -> FUN_1804d526c  per-part mount leaf  (mounts ONE pak)
                  -> (*this+0x240) slot72 FUN_1804d5580  archive factory
                       -> FUN_1804d5ed4 + FUN_18247b838  ZipDir CDR cache BUILD
                       -> FUN_1824159b8 / FUN_1804d5d9c  wrap -> ICryArchive obj
                  -> FUN_1804d70a4  rank-insert into mounted-pak vector [this+0x120..+0x128]
        -> FUN_18087c9e8 (on this+0x308)  post-register fixup (priority/order table)
```

`FUN_1804d4824` callers (3, all VERIFIED): `FUN_1804d9c4c` (OpenPacks glob),
`FUN_180da4e5c` (OpenPack slot 6), `FUN_18193cb14` (OpenPack slot 7).

### Slot 6 vs slot 7 — explicit vs auto bind-root

- **Slot 6** `FUN_180da4e5c`: caller supplies both the pak path (arg2) AND the
  bind-root (arg3); both are run through AdjustFileName (slot 1, `*this+8`) — the
  bind-root at flags `0x50000`, the pak path at `0`.
- **Slot 7** `FUN_18193cb14`: caller supplies only the pak path; the bind-root is
  DERIVED as the path's directory (`strrchr(path,'\\')`). If the path has no
  separator it warns `"Pak file %s has absolute path %s, which is strange"` (the
  string anchored at 0x1846a9ea8). Otherwise both feed `FUN_1804d4824`.

### The glob worker `FUN_1804d9c4c` (RVA 0x4D9C4C) — OpenPacks core

VERIFIED: `strchr(param_4,'*')` then `strchr(param_4,'?')`. No wildcard → ONE
`FUN_1804d4824(this, root, path, flags)` register call, and (if a result-vector
arg is passed) the opened name is appended to it. Wildcard present → the
else-branch enumerates matching pak files (glob expansion) and opens each,
collecting names into the caller's result vector at `param_6`. This is the entry
that ingests `*.pak` patterns — the mod-absorb glob.

---

## The directory-index BUILD (the BUILD, not the search)

The per-part mount leaf `FUN_1804d526c` opens one archive via the factory at
`(*this+0x240)` = **slot 72 `FUN_1804d5580`** (VERIFIED slot 72). The factory:

1. AdjustFileName (slot 1) the path with flag `0x10000`.
2. `FUN_1804d5990(this, name)` — a CACHE lookup; returns the already-open archive
   object if this pak is already mounted (refcount path), avoiding a re-parse.
3. Cache miss → `thunk_FUN_1809b2b28(name, &DAT_183a53718="rb")` opens the FILE.
   On a null FILE it logs `"Cannot open Pak file %s"`.
4. A consistency pass (seek to end, read the trailing region) emits
   `"PAK file '%s' - test of consistency failed - ..."` / `"Too big PAK file"`.
5. **Index build:** `FUN_1804d5ed4(builder)` constructs a ZipDir cache builder,
   then `FUN_18247b838(builder, &out, name)` reads the file's **Central Directory
   Record into an in-memory directory index** (the read-only `uVar1&8==0` arm;
   the read-write arm uses `FUN_1804d5b74` / `FUN_18247b724`). The built index is
   wrapped into the ICryArchive object by `FUN_1824159b8` (RO) / `FUN_1804d5d9c`
   (RW) and returned (refcounted: `plVar10[1]++`).

**The CDR parse (the build's core) is the ZipDir cache reader** — the functions
that own the CDR-corruption strings (xref'd from their known addresses):

| function | RVA | CDR-parse role (string anchor) |
|---|---|---|
| `FUN_1804d6e70` | 0x4D6E70 | CDREnd reader — "Cannot find Central Directory Record…", "The file is too small … CDREnd structure" |
| `FUN_1804d6c18` | 0x4D6C18 | central-dir bounds — "The central directory offset or size are out of range…" |
| `FUN_1808b8ba8` | 0x8B8BA8 | "Archive contains corrupted CDR.", "Central Directory record is either corrupt, or truncated…" |
| `FUN_1808b8d6c` | 0x8B8D6C | "Central Directory contains file descriptors pointing outside the archive…" |

This is a standard ZIP Central-Directory parse → the per-pak directory index. It
is exactly the index the resolver's `FUN_1804631f0` later BINARY-SEARCHES (the
two halves now meet: this front = build, the sub-resolver front = search).

`OpenCachePak` `FUN_18243fc40` (RVA 0x243FC40, CrySystem PlatformOS_PC.cpp:0x2aa)
is a THIN wrapper that opens a `%%USER%%/cache/%s` pak via FOpen-mode
`*(pCryPak+0x30)` (slot 6 OpenPack) at flags `0x10010404` — a cache-pak path, not
a general mount; it logs `"OpenCachePak '%s' ERROR during OpenPack '%s'"`. Not on
the mod-mount path.

---

## The mounted-pak DATA STRUCTURE + RANKING (the mount-order answer)

VERIFIED from `FUN_1804631f0` (search side) + `FUN_1804d526c` + `FUN_1804d70a4`
(mount side) — one std::vector, two equivalent member views:

- **The vector:** begin `*(this+0x120)` (== `this[0x24]` qword index), end
  `*(this+0x128)` (== `this[0x25]`), **stride 0x38 bytes per entry**. (0x24*8 =
  0x120; the per-part leaf addresses it as `param_1+0x24`, the resolver as
  `param_1+0x120` — same vector.)
- **Per entry (0x38 bytes), at `entry`:** `*(entry)` = the bound-ROOT CryStringT
  (memcmp'd against the requested path prefix in the search); `*(entry+0x28)` =
  the pak object whose `(*vt+0x70)` returns the pak FLAGS (search tests bit 0xb =
  skip, bit 10 = ordering region; mount sets bit 0x200/etc.); `*(entry+0x30)` =
  the ZipDir archive/cache object whose directory index is binary-searched.
  (The search reads these as `lVar17-0x38 / -0x10 / -0x8` because it walks from
  the END backward.)
- **Re-entrant guard:** SRW lock at `this+0x100`, owner-thread id `this+0x10c`,
  recursion depth `this+0x108`, an atomic refcount at `this+0xf0`. Every search +
  every mount takes it (thread-safe; 680-xref hot path per `FINDINGS.md`).

**Ranking / mount order (VERIFIED):**

- The resolver `FUN_1804631f0` iterates the vector **back-to-front** (`e = end;
  e != begin; e -= 0x38`) — so a LATER position in the vector is consulted FIRST.
- The mount leaf `FUN_1804d526c` computes the insert position by walking from the
  END backward and **skipping every entry whose pak flag-bit 10 is set**
  (`uVar7 >> 10 & 1`), then calls the rank-insert helper `FUN_1804d70a4` at that
  position. So a newly-mounted pak lands AFTER the unflagged entries but BEFORE
  the bit-10-flagged tail region — a deliberate priority band, not a blind append.
- `FUN_1804d70a4` (RVA 0x4D70A4) is a std::vector insert-at-position: if
  pos == end it push_backs; otherwise it shifts the tail forward by 0x38 and
  constructs the new 0x38-byte entry at the computed slot (so insertion order =
  resolution precedence).
- **A duplicate guard:** before opening, the mount leaf scans the SAME vector and
  `_stricmp`s the new pak path + bound-root against every existing entry; an exact
  match short-circuits (re-mount is a no-op / refcount touch), so the same pak is
  never double-mounted.
- `FUN_18087c9e8(this+0x308)` runs after a successful register batch — a fixup
  over a small fixed-size table at `this+0x308` (priority/order bookkeeping, a
  16-entry `0xffff`-terminated list), not the main pak vector.

The split-pak handler `FUN_1804d495c` mounts `<name>-part0.pak`, `-part1`, … up to
100 parts, tracking the max part index at `this+0x318`; each part is one
`FUN_1804d526c` mount (one vector entry). It warns
`"Both split and single pak exist - '%s'. Please delete the older pak/paks."`.

---

## CONTRIBUTION TO THE 4 SYNTHESIS QUESTIONS

**Q (this front's charge): Can kcdx mount/own paks via this machinery, and how
does pak-vs-pak ranking work?**

1. **Salvageable / reusable.** YES — the open/mount surface is a clean, fully
   virtual vtable API on `*(gEnv+0x50)` (`FINDINGS.md` id 132): OpenPack (slot 6
   explicit-root / slot 7 auto-root), OpenPacks-glob (slot 9/10), and the archive
   factory (slot 72) are all callable from a kcdx hook post-init. kcdx CAN drive
   the engine to mount its own paks by calling slot 6/9 on the live CCryPak with a
   plugin-supplied pak path + bind-root — the same call the engine makes for stock
   paks. This is the pak counterpart to AddMod (slot 19) on the search-path side.
   NEEDS-LIVE-CONFIRM: that a kcdx-driven OpenPack from our hook point takes effect
   (HIGH expected; the API is reachable and the vector is the same one the resolver
   reads).

2. **Full-replace vs partial.** For PAK assets, kcdx does NOT need to reimplement
   the ZipDir CDR parser or the directory-index/binary-search — that machinery
   (slot 72 + `FUN_1804d5580` + the `FUN_1804d6e70`/`FUN_18247b838` CDR build +
   `FUN_1804631f0` search) is correct and reusable as-is. A kcdx-owned resolver
   that wants pak-level overlays can MOUNT a kcdx pak via slot 6/9 and let the
   engine's own ranking place it — partial reuse, not full replace, for the pak
   path. (Loose-file overlays remain the separate FOpen-redirect story per the
   sub-resolver front; this front is the PAK lane.)

3. **What to hook vs replace.** To OWN pak load-ORDER, the lever is the mount-rank
   insert: the engine inserts each new pak before the bit-10-flagged tail and the
   resolver consults back-to-front, so **mount order = precedence** (last-mounted
   among the unflagged band wins first). kcdx can control precedence by (a)
   controlling WHEN it calls OpenPack relative to the engine's own mounts (already
   the model the init-cycle-ownership work establishes — kcdx owns init), or (b)
   hooking the per-part mount leaf `FUN_1804d526c` / the rank-insert `FUN_1804d70a4`
   to force a kcdx pak to the winning end. Replacing the WHOLE pak subsystem is
   unnecessary; hooking the mount + the order-insert is the surgical lever.

4. **Loads a STOCK Nexus/Workshop pak unchanged.** YES, natively — a stock pak is
   ingested by the exact slot-6/9 path mapped here (the same the engine uses for
   its own paks: AdjustFileName → register worker → split-aware open → archive
   factory → CDR index build → rank-insert). A kcdx-owned resolver that DRIVES this
   machinery (rather than bypassing it) loads any well-formed `.pak` with no
   plugin-side change — kcdx need only decide the bind-root + mount order. The mod
   loader's existing `OpenPacks` call already exercises this; kcdx owning the
   resolver means owning the WHEN/ORDER of these calls, not rewriting the ingest.

---

## CONFIDENCE MAP

VERIFIED (decompiled/slot-read, this build):
- OpenPack slot 6 `FUN_180da4e5c` (RVA 0xDA4E5C) — slot binary-confirmed.
- OpenPack slot 7 `FUN_18193cb14` (RVA 0x193CB14) — auto-root variant.
- OpenPacks glob worker `FUN_1804d9c4c` (RVA 0x4D9C4C), reached via slot 9
  `FUN_1804d9bb0` (RVA 0x4D9BB0) / slot 10 `FUN_18197c598` (RVA 0x197C598).
- Register worker `FUN_1804d4824` (RVA 0x4D4824); split-aware open `FUN_1804d495c`
  (RVA 0x4D495C); per-part mount leaf `FUN_1804d526c` (RVA 0x4D526C).
- Archive factory slot 72 `FUN_1804d5580` (RVA 0x4D5580) — slot binary-confirmed;
  builds the ICryArchive + invokes the ZipDir CDR index build.
- CDR-parse leaves `FUN_1804d6e70` / `FUN_1804d6c18` / `FUN_1808b8ba8` /
  `FUN_1808b8d6c` (string-anchored).
- Mounted-pak vector: begin `this+0x120`, end `this+0x128`, stride 0x38, entry
  layout {root@0, pakObj@0x28, archive@0x30}, SRW guard @ this+0x100.
- Rank-insert `FUN_1804d70a4` (RVA 0x4D70A4): insert-at-position; mount order =
  resolution precedence (resolver iterates back-to-front; new pak inserts before
  the bit-10 tail). Duplicate-mount guard by `_stricmp` over the vector.

NEEDS-LIVE-CONFIRM (runtime facts, not in any body):
- That a kcdx-driven OpenPack/OpenPacks from a post-(gEnv+0x50)-init hook actually
  mounts + wins precedence as the static analysis predicts (the vector is the same
  one the resolver reads — HIGH expected; a probe that OpenPacks a known kcdx pak
  then re-resolves a contained vpath would close it).
- The exact bit-10 semantics in a live mount sequence (which engine paks carry it,
  hence where a kcdx mount lands in the band) — readable by a GetMod-style readback
  or a logged dump of the vector at boot.

---

## SEED-ROW CANDIDATES (AP18 — FLAGGED, NOT written; need user approval)

IF kcdx drives pak mounting via this machinery (recommended per Q1–Q4):

- **`CCryPak::OpenPack`** — `FUN_180da4e5c`, RVA 0xDA4E5C, CCryPak vtable slot 6
  (+0x30). Mount one pak with an explicit bind-root. Primary mount entry.
- **`CCryPak::OpenPacks`** — glob worker `FUN_1804d9c4c`, RVA 0x4D9C4C, reached via
  slot 9 `FUN_1804d9bb0` (RVA 0x4D9BB0, +0x48) / slot 10 `FUN_18197c598`
  (RVA 0x197C598, +0x50). Mount a `*.pak` glob (the mod-absorb entry).
- Secondary (only if kcdx hooks the mount internals directly): archive factory
  slot 72 `FUN_1804d5580` (RVA 0x4D5580, +0x240); rank-insert `FUN_1804d70a4`
  (RVA 0x4D70A4); per-part mount leaf `FUN_1804d526c` (RVA 0x4D526C).

Siblings already seeded / flagged by other fronts: FOpen (id 131, slot 36),
gEnv_pCryPak (id 132), AdjustFileName resolver `FUN_18046205c` (slot 1, flagged),
AddMod `FUN_1819af1a8` (slot 19, flagged).
