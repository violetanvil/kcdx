# fs-takeover pak-mount recon — FINDINGS

Two checkable facts resolved for the file-system-takeover pak-management slot
model (step 3.4 / design §4.5/§6), before the design settles. Game
`release_1_5_1164953_841`, image base 0x180000000, CCryPak vtable VA
0x183A95FA8 (RTTI `.?AVCCryPak@@`). Evidence: reuse-first (front-1/front-2/
synthesis + ki0012 mount recon) for the call-tree + vector layout, FRESH
capstone disassembly for the slot-71 body/ABI, IAT resolution, vtable-slot
verification, and the caller xref. Artifacts this dir: `disasm_slot71_body.py`,
`xref_slot71_100.py`, `xref_indirect_bytes.py`, `xref_real_mount.py`,
`resolve_iat.py` + their `_*.txt` raw dumps.

## Q1 — slot 71 is NOT OpenPack/mount; it is an existence / IsFolder-class check

Front-1's "slot 71 = OpenPack/mount (i — inferred)" label is **WRONG**; the body
disproves it. Resolves synthesis OQ #6 (the slot-71-vs-72 mount-entry conflict).

- **Slot index VERIFIED:** RVA `0x7ad468` = vtable `0x183A95FA8 + 0x238` = slot 71
  (read from `.rdata`).
- **Resolved ABI (member __fastcall, read from the body — AP2-clean):**
  `bool slot71(CCryPak* this /rcx/, const char* path /rdx/, uint8_t /r8b/, uint32_t /r9d/)`
  — kcdx DSL `bool (ptr, u8, i32)`. Both return paths set `al` from `sil` → a
  **bool**, never an index/handle/pointer.
- **What it does:** (1) calls `[rax+8]` = vtable slot 1 AdjustFileName to resolve
  the path (READ in slot 71's body at `0x7ad526` — AP19-clean), flags word from
  args 3+4; (2) normalizes separators into a stack buffer; (3) inner
  `FUN_1807ad66c` calls `GetFileAttributesW` (IAT `0x3a02840`, resolved) and tests
  `al & 0x10` = FILE_ATTRIBUTE_DIRECTORY → bool. The "pak-open family" leaves
  front-1 cited (`0x7ad6d4`/`0x7ad76c`) are CryStringT `rfind`/`find` (path
  dir/ext split), not pak ops.
- **NO loaded-pak-vector touch, NO CreateFile, NO ZipDir parse, NO slot-72
  factory call, NO rank-insert.** It is an existence/folder check — same family
  as slots 67/68/70 (and it sits among them in `src/fs_takeover/vtable_table.cpp`).

**The REAL OpenPack/mount entry** (front-2, re-confirmed by direct-call edges):
OpenPack slot 6 (`0xDA4E5C`) / 7 (`0x193CB14`), OpenPacks slot 9 (`0x4D9BB0`) /
10 (`0x197C598`) → register worker `0x4D4824` → per-part leaf `0x4D526C` →
archive factory **slot 72** (`0x4D5580`) builds the ZipDir CDR + rank-inserts
`0x4D70A4` into the loaded-pak vector `[this+0x120..+0x128]`.

## Q2 — MOUNT-ONCE (startup / init-time); no gameplay-driven mount/unmount found

Pak mount (OpenPack/OpenPacks) + unmount (ClosePakByIndex slot 100, VERIFIED at
vtable +0x320) are init/load-time. Known drivers all startup: the kcdx mod
loader's OpenPacks at init (`src/mod_absorb/enabled_list_builder.cpp:57`,
front-2), the engine `C_ModManager` OpenPacks loop (ki0012 recon), OpenCachePak
(cache setup). Slot-100 release leaf `FUN_1804607e4` (`0x4607E4`) callers are all
FClose/teardown-family. CryEngine architecture corroborates (paks mounted at
init/level-load; runtime serves file data from already-mounted paks via the
slot-1 chokepoint, not per-access mounts).

### Call sites, classified
- slot 71 (`0x7ad468`): 0 direct callers; not a mount fn — out of scope.
- slot 100 ClosePakByIndex (`0x2418f78`): 0 direct callers (virtual); release
  leaf `0x4607E4` has 6 direct callers, all FClose/teardown — no live caller.
- real mount entries (6/7/9/10, factory 72): 0 direct callers (virtual); known
  drivers all startup/init; internal mount tree re-confirmed by direct edges.
- No resolvable gameplay/live mount caller in the scanned set.

## Recommendation for the design's pak-mount model — mount-once thin-shim, 2 fixes

1. **Fix the slot-71 label** in `src/fs_takeover/vtable_table.cpp`:
   `Thunk(71, "OpenPack/mount")` → existence/IsFolder-class. Own it on the same
   basis as 67/70: index-hit answers existence; miss thunks the captured
   original — NOT a pass-through mount thunk. (This is the §4.5 existence/
   metadata family, mislabeled — design §4.5's slot-71 grouping is a defect to
   correct.)
2. **Slots 6/7/9/10 + factory 72 + ClosePakByIndex 100 are mount-once.** kcdx
   reads paks with its own reader into its own index at init (design §6; kcdx
   already owns the init cycle), so these can be **thin shims over the index**
   (mount = register-into-index / no-op; unmount = index-removal / no-op) — no
   stateful runtime add/remove the read path must observe, because no gameplay
   re-mount path exists.

## AP19 edge NOT read — reported unverified, NOT asserted

The specific function(s) driving OpenPack/OpenPacks/ClosePakByIndex through the
vtable AT RUNTIME are UNVERIFIED: the dispatch is virtual and the displacements
are non-discriminating (`[reg+0x238]` matches 512 sites, `[reg+0x320]` 56,
colliding with unrelated vtables — not attributable to CCryPak without dataflow).
So MOUNT-ONCE rests on positively-read init-time drivers + the engine-architecture
lead, NOT on a proof-of-absence. No caller body proving a gameplay mount/unmount
edge was read; none is asserted.

**One cheap live probe closes the residual risk (NOT blocking the design):** log
every OpenPack/OpenPacks/ClosePakByIndex invocation with an init-vs-gameplay
phase tag for one session — zero post-init events confirms MOUNT-ONCE and locks
the thin-shim model. A `/debug`-class runtime question, deliberately left as a
probe to run during step-3.4 build/acceptance.
