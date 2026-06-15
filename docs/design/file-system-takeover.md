# File-system takeover — kcdx IS the engine filesystem (the settled design)

**Status:** v1 (settled 2026-06-14; changelog `file-system-takeover-changelog.md`).
**Supersedes:** the asset-resolution *seam* settled in
[`asset-replacement.md`](asset-replacement.md) §7 (the two-hook
`AdjustFileName`-replace + `FOpen`-overlay mechanism) — that seam is a PARTIAL
takeover (kcdx owns the resolution decision + the loose open, the engine still
owns the read family, the mount machinery, and operates kcdx-minted handles).
This design REPLACES that partial seam with TOTAL ownership: kcdx owns the entire
`CCryPak` file object. The asset-replacement design's AUTHOR-FACING surface (the
`assets/` folder, sidecar declarations, published names, cross-mod references,
the unified overlay precedence) is UNCHANGED and carried forward verbatim — only
the engine-side SEAM beneath it changes from "two hooks + call-through" to "kcdx
is the whole file object."
**Authoritative for:** the file-system-takeover build — the executor builds to
THIS doc, not to a step-doc summary of it (`.claude/rules/spec-conformance.md`).
**Home for:** [KI-0019](../known-issues/KI-0019-ccrypak-fopen-reentrancy-av-on-inventory-open.md)
and [KI-0006](../known-issues/KI-0006-serve-execute-vehicle-not-found.md) — the
cross-CRT `FILE*` crash class this design structurally eliminates (§9).

This is the canonical spec for kcdx taking complete ownership of the engine's
`CCryPak` file system: every file call in WHGame lands in kcdx code, every byte
kcdx serves is minted, read, sought, and closed entirely on kcdx's own CRT, and
the engine's `ucrtbase` never operates a handle kcdx created. The design
optimizes for User UX (no-crash) first — the cross-CRT boundary-straddle that
causes KI-0019/KI-0006 becomes structurally impossible — and Performance second
(one O(1) resolution lookup replaces the engine's per-call search-path walk).

---

## §1 Vision

**kcdx IS the engine's filesystem. The game holds a kcdx-owned `CCryPak` object
and never knows; every file operation the engine performs — open, read, seek,
close, existence, metadata, mount — is kcdx code, and no engine file function
ever operates on a byte kcdx serves.**

The same way kcdx replaced the native mod loader (kcdx is now the mod loader) and
took over game init (kcdx owns the init cycle), kcdx now takes over the entire
file layer. There is no line straddled between two filesystems — kcdx is the one
filesystem. A vanilla pak read and a modded loose-file read are BOTH kcdx reading
bytes on kcdx's runtime; the engine is a consumer of kcdx's file object, not a
co-owner of it.

**v1 success criteria:**
- The game boots, reaches the world, and opens inventory with NO crash — the
  KI-0019 repro (cross-CRT `fseek` on the inventory-open FSR2/DLSS path) cannot
  occur, because kcdx operates every handle on its own CRT (§9).
- Every file call in WHGame (all 350 functions, 680 call sites that reach
  `*(gEnv+0x50)`) dispatches into kcdx code — verified by a probe that logs a
  kcdx-owned marker on the first vanilla file open (P2, §8).
- A vanilla pak asset loads correctly read entirely by kcdx's own PKZIP/DEFLATE
  reader — no engine ZipDir in the path.
- A loose-file mod override and a stock Nexus/Workshop pak mod BOTH load
  unchanged — the author-facing contract from `asset-replacement.md` is preserved
  byte-for-byte (§7).
- A single O(1) resolution lookup per open — no per-call search-path walk, no
  per-call pak bisection (the "no extra hotpath checks" constraint, §5).
- The non-file `CCryPak` slots (pool, CRC/MD5, %USER%, dir-casing) thunk to the
  original engine bodies, and ANY such slot can be flipped to a kcdx
  implementation as a one-line table change — no rearchitecture (the
  reversibility constraint, §4.3).

**Top-level architecture decision: TOTAL takeover via a full vtable-pointer
swap.** kcdx builds its own `CCryPak` vtable — every slot points at a kcdx
implementation (for the file/resolution/mount/existence family) or a thunk to the
original engine body (for the pure-internal slots) — and swaps the vtable pointer
in the live `CCryPak` object at `*(gEnv+0x50)` during init. From that point the
engine holds a kcdx file object and never knows: every `(*(*pak))(...)` call the
engine makes dispatches through kcdx's vtable. This is "kcdx literally IS the
engine filesystem" in the most literal seat-of-ownership form — the engine's own
dispatch mechanism (read the vtable pointer per call, verified) carries every
file call into kcdx with zero per-call interception overhead.

---

## §2 Glossary

- **`CCryPak`** — the engine's filesystem object. One instance, reached at
  runtime via `*(gEnv+0x50)` (gEnv id 11; the pCryPak pointer at +0x50, verified
  id 132). Its vtable (102 slots, VA `0x183A95FA8`, RTTI `.?AVCCryPak@@`) is the
  complete file API the whole engine routes through.
- **The vtable swap** — kcdx replaces the `CCryPak` object's vtable POINTER (the
  qword at `[pCryPak+0x00]`) with a pointer to a kcdx-built vtable. Every
  subsequent `(*(*pak+slot))(pak, …)` call the engine makes lands in kcdx code.
  The swap is the SEATING mechanism for total ownership.
- **The kcdx vtable** — a kcdx-owned array of 102 function pointers, built once at
  init, each slot independently either a **kcdx implementation** or a
  **thunk-to-original** (§4.3). It is a per-slot declarative table, not a
  hardcoded split — the reversibility property.
- **kcdx implementation slot** — a slot kcdx fully owns: it runs kcdx code, on
  kcdx's CRT, and never calls the original engine body. The whole
  file/resolution/read/mount/existence family.
- **Thunk-to-original slot** — a slot whose kcdx vtable entry forwards to the
  ORIGINAL engine `CCryPak` body (the pure-internal plumbing: pool memory,
  CRC/MD5, %USER% expansion, dir-casing, thin config getters). Owned by the engine
  for now; flippable to a kcdx impl as a one-line table change.
- **The unified asset index** — kcdx's single in-memory directory, built at load:
  every vpath → its byte source (a loose disk file, or an entry in a kcdx-read
  pak — vanilla or mod). The one structure every kcdx open consults; one O(1)
  lookup, precedence decided once at build time (§5).
- **Byte source** — where a resolved vpath's bytes live: a loose disk file (a
  path kcdx opens with its own CRT `_wfopen`), or a pak entry (an offset+size+
  compression-method into a pak file kcdx reads with its own PKZIP/DEFLATE
  reader). The index maps every vpath to exactly one byte source.
- **kcdx handle** — the opaque handle kcdx's `FOpen` mints and returns to the
  engine. Under total ownership the engine never INSPECTS or OPERATES it — it
  only holds it and passes it back to kcdx's `FRead`/`FSeek`/`FClose`. kcdx
  chooses the handle representation freely (§4.4); it is operated cradle-to-grave
  on kcdx's CRT.
- **The cross-CRT boundary-straddle** — the KI-0019/KI-0006 defect class: kcdx
  mints a CRT `FILE*` with kcdx's statically-linked CRT, the engine's `ucrtbase`
  then operates it (`fseek`/`fclose`/`get_osfhandle`), the fd is invalid in the
  other CRT, → AV. Total ownership removes the class: a kcdx handle is NEVER
  operated by the engine's CRT, because kcdx owns the read family too (§9).
- **The ready-bracket** — the already-shipping kcdx init mechanism that makes the
  game-init thread WAIT (at the `ModManager_ctor` hook, on `g_kcdxReadyEvent`)
  until kcdx signals ready. P1 (§8, RESOLVED — static binary read) found
  `ModManager_ctor` runs AFTER both the CCryPak construction and the engine's first
  file call, so the swap does NOT seat here — it seats at the CCryPak construction
  site (`CSystem_pCryPak_construct_store`, id 158; §4.1). The ready-bracket remains
  the worker-readiness wait mechanism; it is no longer the swap-seating point.

---

## §3 User Stories & Acceptance Criteria

The author-facing stories are UNCHANGED from `asset-replacement.md` §3 (the
`assets/` folder, sidecar declarations, published names, cross-mod references) —
this design changes the engine-side seam beneath them, not the authoring surface.
The stories here are the NEW ones this takeover introduces, at the
engine-integration level.

- **US-1 — No-crash on inventory open (the KI-0019 acceptance).** As a player, I
  open inventory after loading a save and the game does not crash.
  **Acceptance:** the KI-0019 repro (load save → enter world → open inventory)
  runs clean across repeated launches; no `FAULTED site=… hook=engine.ccrypak_*`
  in `kcdx-dev.log`; the FSR2/DLSS-init file the dump implicated is served by
  kcdx's read path with no `ucrtbase` frame operating a kcdx handle.

- **US-2 — Every file call is kcdx's.** As the engine, every file operation I
  perform dispatches into kcdx.
  **Acceptance:** P2's first-file-open marker fires (a kcdx-owned `LOG_DEBUG_KV`
  on the first vanilla file open after the swap), proving the swapped vtable is
  live and the engine's dispatch reaches kcdx; the vanilla file the engine opens
  is read by kcdx's reader (a kcdx read-marker, not an engine ZipDir trace).

- **US-3 — Vanilla pak read by kcdx.** As kcdx, I read a vanilla pak asset with
  my own PKZIP/DEFLATE reader.
  **Acceptance:** a known vanilla asset that lives in a stock pak (a vpath
  resolved to a pak byte-source) is served correctly in-game (the asset renders /
  loads), and the read path shows kcdx's reader markers, not the engine's ZipDir
  (`FUN_1804607e4` extract / `FUN_18051ce40` pak-arm) on the call stack.

- **US-4 — Stock pak mod loads unchanged.** As a mod user, a Nexus/Workshop
  `.pak` mod loads with no author changes.
  **Acceptance:** a stock pak mod's overriding asset wins where it should (its
  vpath resolves to its pak entry in the unified index), served by kcdx's reader;
  the mod author changed nothing.

- **US-5 — Reversible thunk decision.** As a maintainer, I can flip any
  thunked-to-original slot to a kcdx implementation without rearchitecting.
  **Acceptance:** the kcdx vtable is built from a per-slot declarative table;
  flipping one slot from `THUNK` to a kcdx impl fn pointer is a one-line table
  edit + the impl fn — no change to the swap mechanism, the index, or any other
  slot (§4.3).

---

## §4 The takeover mechanism

### §4.1 The vtable swap (seating)

**Verified game-binary facts** (evidence tier: fresh Ghidra decompile, 5-front
asset-resolution research, captured in `_research/phase8.5-pak-resolver/` —
`FINDINGS.md`, `front1-full-vtable-surface.md`, `RESOLUTION-OWNERSHIP-synthesis.md`):

- The `CCryPak` object is reached via `*(gEnv+0x50)` (gEnv id 11, RVA
  `0x0492B800`; pCryPak at +0x50, id 132 — both VERIFIED).
- Its vtable is at VA `0x183A95FA8` (RVA `0x03A95FA8`), 102 slots
  (slot 102 / +0x300 is non-exec = end), RTTI `.?AVCCryPak@@`, reached as
  `*(void**)pCryPak` (VERIFIED — RTTI COL walk).
- Every by-name file consumer in the engine dispatches through this vtable:
  `WriteCachePak` calls `(*(*pCryPak+0x120))(…)` (slot 36 = FOpen); 350 functions
  / 680 xrefs deref pCryPak (VERIFIED — `_fopen_callers.txt`, 12 independent
  call sites cited).

**The swap:** at init, kcdx builds its own 102-entry vtable and writes its address
into `[pCryPak+0x00]` (the object's vtable pointer slot). Because the engine reads
the vtable pointer from the object on every call (standard C++ virtual dispatch,
VERIFIED by the per-call `(*(*pak+off))` shape at every call site), the swap takes
effect for every subsequent file call process-wide, with zero per-call
interception cost.

**P1 — CCryPak construction timing (RESOLVED, outcome (c) — static binary read,
2026-06-15).** The swap must happen AFTER the `CCryPak` object exists at
`*(gEnv+0x50)` and BEFORE the engine's first file call. P1 settled this STATICALLY
from the WHGame.dll binary (not a live launch — static evidence precedes a live
probe, `.claude/rules/results-driven.md` §4; capture
`_research/probe-archive/p1-ccrypak-construction-order.md`, recon
`_research/ccrypak-init-order-recon/`). Boot-order facts (read from the binary):
inside `CSystem::Init` (RVA `0x7A6C64`), the engine (1) constructs + publishes the
`CCryPak` pointer into gEnv+0x50 via `CSystem_pCryPak_construct_store` (id 158, RVA
`0x9B3C0C`) at `0x1807A71CA`, then (2) makes its FIRST `*(gEnv+0x50)` file call at
`0x1807A723A`, then (3) calls `ModManager_ctor` (the kcdx ready-bracket site) LAST
at `0x1807A76FE`. The first file call therefore PRECEDES the ready-bracket
(outcome (c)) — the `CCryPak` constructor itself is id 159 (RVA `0x00D2A570`).
**Seating decision:** the swap seats at the CCryPak **construction site** (id 158,
the gEnv+0x50 store point), NOT the late ready-bracket — seating at the
ready-bracket would miss every file call `CSystem::Init` makes between the publish
and ModManager_ctor. This SUPERSEDES the earlier "swap in the ready-bracket"
assumption (§4.1's prior provisional clause + §8 P1's outcome (a)); §4.1's swap
text below and step 1.4's anchor are updated to the construction site.

**`assumes` — P2 (swap acceptance, UNVERIFIED — probe before building §8):** that
writing the vtable pointer holds and every consumer dispatches into kcdx. The
per-call deref is verified statically; that the swap is accepted live (no engine
code caches the original vtable pointer, no integrity check) is a runtime fact P2
confirms with a first-file-open marker.

### §4.2 Why a vtable swap over per-function detours

The rejected alternative (MinHook-detour each of the ~30 file-family bodies
through the conflict engine) achieves the same total ownership but: installs N
individual hook sites + footprints (every slot its own detour to maintain), and
pays a trampoline hop per call. The vtable swap is one write, dispatches into
kcdx at native virtual-call cost, and owns ALL 102 slots uniformly (file slots →
kcdx impls, internal slots → thunks) from one declarative table. The trade is a
mechanism kcdx has not used before (a raw vtable-pointer swap) vs N detours
through kcdx's existing conflict-engine path. The swap was chosen: one mechanism,
uniform ownership, no per-call overhead, and the per-slot table is where
reversibility lives (§4.3).

### §4.3 The per-slot declarative vtable — reversibility is first-class

The kcdx vtable is built from a **declarative per-slot table**, NOT a hardcoded
file-vs-internal split. Each of the 102 slots is one row:

```
{ slot, role, impl }   where impl ∈ { KCDX(&kcdx_fn) | THUNK(original_slot_ptr) }
```

- **File/resolution/read/mount/existence/metadata family** → `KCDX(&…)` — kcdx
  owns it (the ~30 slots in §4.5).
- **Pure-internal family** (pool memory: slots 60/61/98/99; CRC/MD5: 81/82/83;
  %USER% expansion: 77; dir-casing/MakeDir: 28; thin config getters: 2/3/4/5/…)
  → `THUNK(original)` — forwards to the engine body for now.

**Reversibility (the user's explicit constraint — do not code us into a box):**
flipping any slot from `THUNK` to `KCDX` is a ONE-LINE table edit (change the row's
`impl`) plus writing the kcdx impl fn. No change to the swap mechanism, the index,
or any other slot. The table is the single point of slot ownership; nothing
downstream hardcodes "slot N is the engine's." A future decision to have kcdx
reimplement CRC, or pool memory, or all 102 slots, is a local, incremental change
— never a rearchitecture. This is a load-bearing design property, not an
implementation detail: the build MUST produce a per-slot table, and a reviewer
checks that no code outside the table assumes a slot's ownership.

**`assumes` — P4 (thunked-slot `this` compatibility, UNVERIFIED — probe before
building §8):** a thunked original slot runs the ORIGINAL engine body against
kcdx's `CCryPak` object. The original bodies read engine member offsets
(`[this+0x40]` pak-stream vector, `[this+0x120..0x128]` loaded-pak array,
`[this+0x198..0x1a0]` search-path vector, `[this+0x1b0]` alias table,
`[this+0x228]` pakPriority cvar — all VERIFIED present in the original object). For
a thunk to work, kcdx must NOT relocate or repurpose those members — i.e. kcdx
keeps the SAME `CCryPak` object (only swaps its vtable pointer), it does NOT build
a fresh object with a different layout. P4 (§8) probes: does a thunked internal
slot (e.g. a pool accessor or CRC) run correctly against the live kcdx-vtable'd
object? This is the load-bearing correctness probe for the thunk approach — and
the reason the swap is **vtable-pointer-only on the existing object**, never a
whole-object replacement.

### §4.4 Handle operation — kcdx serves every read on its own CRT

Under total ownership the read family (FRead/FSeek/FTell/FEof/FWrite/FClose, slots
38/39/40/41/53/54/55/56/57/58/59 + the variants) are ALL kcdx implementations. So:

- kcdx's `FOpen` mints a kcdx handle (kcdx chooses the representation — see below).
- kcdx's `FRead`/`FSeek`/`FClose` operate that handle ENTIRELY on kcdx's CRT —
  for a loose byte-source, a kcdx `_wfopen`'d `FILE*` read with kcdx's `fread`; for
  a pak byte-source, kcdx's PKZIP reader seeking+inflating from the pak file (§6).
- The engine NEVER operates the handle. It holds it and passes it back to kcdx.

This is the mechanical meaning of "kcdx owns it full stop": the handle is born,
read, sought, and closed inside kcdx's runtime. The cross-CRT straddle is
impossible because there is no point at which a kcdx handle crosses into the
engine's CRT (§9). **The rejected alternative** (hand the engine an OS `HANDLE`
the engine adopts via `_open_osfhandle`) assumed an engine code path that the
recon did NOT observe (the recon shows the engine `fseek`s the `FILE*` directly) —
it is moot under total ownership anyway, since kcdx, not the engine, operates the
handle.

**kcdx handle representation:** kcdx owns the whole read family, so it is free to
choose. The simplest representation that satisfies any residual engine assumption
is a **kcdx-side handle table** — `FOpen` returns a small kcdx handle id (or a
kcdx-tagged pointer into a kcdx handle pool); kcdx's read family maps it to the
open byte-source state (loose `FILE*`, or pak read cursor). The engine treats it
as opaque. **`assumes` — P3 (residual raw-handle access, UNVERIFIED — probe
before building §8):** any engine code that BYPASSES the vtable and touches a
handle directly (e.g. a streamer that stashed a raw `FILE*` and `fseek`s it itself
off-vtable) would break a kcdx handle-id. The recon's read-path map showed the
read family is the consumer; P3 probes whether any off-vtable raw-handle access
exists (a streamer / DirectStorage path — `asset-loadpath-map-recon/F5-streaming-
engine-bypass.md` + `step2-directstorage-bypass-finding.md` are the leads to read
at probe-design time). If such a path exists, kcdx's handle representation must be
a real `FILE*`-shaped object that off-vtable code can operate on kcdx's CRT — a
representation constraint P3 settles.

### §4.5 The kcdx-owned slot set (the file family)

From the verified 102-slot map (`front1-full-vtable-surface.md`), the slots kcdx
implements (`KCDX(&…)`), grouped by family. Each rests on the recon's verified
role (evidence tier: fresh Ghidra decompile / resolved string, cited per slot in
the front-1 table):

- **Resolution:** slot 1 `AdjustFileName` (the path-resolution chokepoint every
  by-name consumer calls first). kcdx's impl is the unified-index lookup (§5).
- **Open:** slot 36 `FOpen`, slot 35 `FOpenRaw`, slot 38 `FOpen-by-pak-index`.
- **Read family:** slot 39 `FReadRaw`, slot 40 `FGetCachedFileData`, slot 41
  `FWrite`, slot 43 `FGets`, slot 44 `FGetc`, slot 47 `FUngetc`, slot 53 `FSeek`,
  slot 54 `FTell`, slot 55 `FClose`, slot 56 `FEof`, slot 57 `FError`, slot 58
  `FGetErrno`, slot 59 `FFlush`, slot 46/66 fileno variants.
- **Existence / metadata by name:** slot 13 `IsFolder`, slot 45 `GetFileSize`,
  slot 67 `IsFileExist(3)`, slot 68 `GetFileAttributes`, slot 69 `GetFileStat`,
  slot 70 `IsFileExist(2)`, slot 92 `GetFileSizeOnDisk`, slot 93
  `GetFileSizeCompressed`.
- **Directory enumeration:** slot 14 `ForEachFile` + slot 15 callback, slot 101
  `FindFirst` (`CCryPakFindData` factory).
- **Pak / archive management:** slot 7 `AddPakToValidate`, slot 17
  pak-membership, slot 32 `FindPakByCRC`, slot 33 `GetPakInfo` + slot 34 free,
  slot 71 `OpenPack`/mount, slot 72 `TestArchive`, slot 91 `GetPakPriority`, slot
  100 `ClosePakByIndex`.
- **Search-path / alias / mods:** slot 19 `AddMod`, slot 20 `RemoveMod`, slot 21
  `GetMod`, slot 22 `SetAlias`, slot 23 alias-insert, slot 24 `GetAlias`, slot 94
  `RegisterSystemSearchPath`.
- **Delete / copy:** slot 49 `RemoveFile`, slot 50 `RemoveDir`, slot 52
  `CopyFile`.

The thunked-to-original slots are the remainder: pool memory (60/61/98/99),
CRC/MD5 (81/82/83 — note these OPEN via the file slots, so they get kcdx bytes
even while thunked: they call slot 36/40/53/55 which ARE kcdx's — a thunked
hasher reads kcdx-served bytes for free), %USER% (77), dir-casing/game-root (28),
the dtor (0), and the thin config/accessor slots. The `git`-tracked per-slot table
in the build is the source of truth; this list is the design intent it encodes.

---

## §5 Resolution — the unified asset index

kcdx's slot-1 `AdjustFileName` impl (and the open/existence slots that consult the
same structure) resolves against ONE in-memory index, built at load:

```
vpath (normalized) → ByteSource { kind: Loose | Pak,
                                  loose: { diskPath } |
                                  pak:   { pakFile, offset, size, method, crc } }
```

- **Built at load (cold path):** kcdx walks every byte-source it will serve —
  loose mod-override files (from the existing overlay map / sidecar declarations,
  `asset-replacement.md` §4–5), kcdx-mounted mod paks, and the vanilla paks — and
  records each vpath's winning byte-source. Precedence (overlay wins vanilla;
  load-order + cross-mod resolution from `asset-replacement.md` §4.4/§5.3) is
  decided ONCE here, not per call.
- **One O(1) lookup per open (hot path):** kcdx's `FOpen`/`AdjustFileName`/
  existence slots do a single hash lookup `vpath → ByteSource`. No per-call
  search-path-vector walk, no per-call pak-directory bisection, no per-mode
  existence-table gate. This is the "no extra hotpath checks" constraint: the only
  per-open cost is the one lookup that tells kcdx where the bytes are.
- **The index IS the kcdx filesystem's directory.** It is where overlay
  precedence, load-order, and cross-mod composition all compose — one structure,
  one source of truth for "what does this vpath resolve to."

**Index construction reuses the verified pak format facts** (evidence tier: real
on-disk bytes, 2 Nexus paks, `RESOLUTION-OWNERSHIP-synthesis.md` §4 + front 5):
stock paks are standard PKZIP (`PK\x03\x04`, STORED+DEFLATE per entry, no zip64,
unsigned CDR); each zip entry name IS the vpath (forward-slashed, root-relative).
kcdx parses each pak's central directory once at index-build to record every pak
entry's `{offset, size, method, crc}` — that parse is kcdx's own PKZIP reader
(§6), not the engine's ZipDir.

**`assumes` — index-build vanilla-pak discovery:** kcdx must know which vanilla
paks to index and where they are on disk. kcdx already owns mod-pak + Workshop
discovery (the init-cycle takeover, `init-cycle-ownership` memory); vanilla-pak
discovery (the game's own `Data/*.pak` set + any patch paks) is the additive piece
— a checkable list (enumerate `<game>/Data/*.pak`), not a runtime-mechanism
assumption. Stated here as an index-build input the build resolves, not a probe.

---

## §6 kcdx's own pak reader

kcdx reads pak bytes with its OWN PKZIP central-directory + DEFLATE reader — the
engine's ZipDir is not in the path for any kcdx-served byte.

- **Format (VERIFIED on real files):** standard PKZIP — central directory at EOF,
  local file headers, STORED (method 0) + DEFLATE (method 8), no zip64, unsigned
  CDR. A vendored standard inflate (zlib / miniz — a dependency choice, §10) does
  the DEFLATE; kcdx does the CDR parse + entry seek.
- **Every byte on kcdx's CRT:** opening a pak file is kcdx's `_wfopen` (kcdx CRT);
  seeking to an entry offset is kcdx's `fseek` (kcdx CRT); reading the compressed
  bytes is kcdx's `fread` (kcdx CRT); inflating is the vendored inflater (kcdx's
  allocator). At no point does the engine's `ucrtbase` touch a pak read. This is
  what makes a vanilla pak read structurally crash-free in the cross-CRT sense
  (§9) — the same guarantee a loose read already has.
- **The rejected alternative** (drive the engine's `OpenPack`/ZipDir to mount +
  index, kcdx serves from the engine-built index) was rejected because the
  engine's ZipDir allocates + operates on the engine's CRT — a mounted-pak read
  would thread engine-CRT-owned structures back into kcdx's read path,
  reintroducing exactly the cross-runtime sharing the takeover eliminates. kcdx
  reading the pak itself is the only option with NO engine-CRT involvement in a
  pak read.
- **The pak-format READING is not engine-private** (front 5): both real Nexus
  paks are plain PKZIP, so kcdx's reader is a standard zip decoder, not a
  reverse-engineered engine format. The vanilla paks are the same format (the
  engine's own ZipDir parses standard PKZIP).

**`assumes` — vanilla pak format uniformity (CONFIRMED 2026-06-15, step 2.2):**
the recon verified 2 Nexus MOD paks are standard PKZIP; this clause asserted the
same of VANILLA game paks from the engine's ZipDir being a standard-PKZIP parser
(front 2) but had NOT read a vanilla pak's on-disk bytes. **Now DISCHARGED by a
fresh static on-disk read of real vanilla `<game>/Data/*.pak` files**
(`_research/probe-archive/vanilla-pak-format-confirmed.md`): 8 vanilla paks all
begin `PK\x03\x04` (no proprietary CryPak header prepended), GeomCaches.pak's
head + EOCD decode to standard PKZIP — `PK\x05\x06` EOCD, no zip64
(`PK\x06\x06`/`PK\x06\x07`), no encryption flag, STORED (method 0) + DEFLATE
(method 8) per entry, unsigned CDR. The design's standard-PKZIP reader holds; no
proprietary-format handling is needed. The `cap-110-pak-cdr-parse` test plugin's
format-uniformity check (scans several vanilla paks' head + EOCD + zip64-negative
at boot) makes this a STANDING falsifiable assertion — a future game version that
changes the pak format trips that regression row rather than silently corrupting
a read.

---

## §7 Backward-compatibility — author-facing contract unchanged

The takeover changes the engine-side seam, NOT the mod-authoring surface. Every
author-facing contract from `asset-replacement.md` is carried forward verbatim:

- **Loose-file overrides** (the `assets/` folder + sidecar `replaces`
  declarations) resolve exactly as designed — they become Loose byte-sources in
  the unified index.
- **Stock Nexus/Workshop pak mods** load unchanged — kcdx indexes their pak
  entries (kcdx's PKZIP reader) and resolves their vpaths to Pak byte-sources;
  the override wins where its load-order says. The mod author changes nothing
  (the compatibility contract: standard PKZIP, `<kcd_mod>` manifest with only
  `<modid>` required, entry-name = vpath — all VERIFIED, front 5).
- **Published names + cross-mod references** (`asset-replacement.md` §5) resolve
  against the same index. Unchanged.
- **The unified-index precedence** (overlay wins vanilla; load-order; cross-mod
  resolution) IS the `asset-replacement.md` §4.4/§5.3 precedence, now computed
  once at index-build rather than per-call.

The difference the author never sees: under the old seam kcdx owned the resolution
decision + the loose open and CALLED THROUGH to the engine for vanilla pak reads;
under the takeover kcdx reads the vanilla pak itself. Same result, no engine CRT
in the path.

---

## §8 Probes — the runtime mechanisms to prove before building

Per `.claude/rules/results-driven.md`, each clause asserting a runtime mechanism
is a probe target, provisional until observed. The design is provisional on these;
each is ordered BEFORE the build phase that rests on it
(`.claude/rules/incremental-delivery.md`). Probes are agent-written, built,
deployed; the user launches; the agent reads the log (`agent-builds-and-deploys.md`).

- **P1 — CCryPak construction timing (RESOLVED — outcome (c), static binary read
  2026-06-15).** *Probe was:* log `*(gEnv+0x50)` value (null vs non-null) at kcdx's
  ready-bracket entry, and log the first file-call timestamp. *Answered STATICALLY*
  by reading WHGame.dll instead of a live launch (static evidence precedes a live
  probe, `.claude/rules/results-driven.md` §4). *Outcome map:* (a) non-null at
  ready-bracket AND first file call is after → swap in the ready-bracket; (b) null
  at ready-bracket → the CCryPak ctor runs later; (c) first file call precedes
  ready-bracket → the swap must move earlier. **Outcome (c) HELD:** inside
  `CSystem::Init`, the CCryPak object is constructed + published to gEnv+0x50
  (`CSystem_pCryPak_construct_store`, id 158, RVA `0x9B3C0C`, called at
  `0x1807A71CA`), the first `*(gEnv+0x50)` file call fires at `0x1807A723A`, and
  the `ModManager_ctor` ready-bracket runs LAST at `0x1807A76FE` — so the first
  file call PRECEDES the ready-bracket. **Decision:** the swap seats at the
  construction site (id 158), NOT the ready-bracket; §4.1 + step 1.4 updated. The
  "swap in the ready-bracket" clause (outcome (a)) is FALSIFIED. Capture:
  `_research/probe-archive/p1-ccrypak-construction-order.md`; recon scripts
  `_research/ccrypak-init-order-recon/`.
- **P2 — swap acceptance (gates everything after seating).** *Probe:* after the
  swap, a kcdx marker in slot 36 (`FOpen`) logs on first fire. *Outcome map:* the
  marker fires on the first vanilla open → swap is live, engine dispatches into
  kcdx (proceed); no marker but file calls happen → swap did not take (engine
  cached the vtable / integrity check) → fall back to per-function detours (§4.2).
- **P3 — residual off-vtable raw-handle access (gates the handle representation).**
  *Probe:* read `asset-loadpath-map-recon/F5-streaming-engine-bypass.md` +
  `step2-directstorage-bypass-finding.md` (static leads), then a live probe: does
  any asset read reach bytes WITHOUT going through a vtable read slot? *Outcome
  map:* no off-vtable raw-handle access → a kcdx handle-id representation is safe;
  an off-vtable streamer operates a raw handle → kcdx's handle must be a real
  `FILE*`-shaped object operable on kcdx's CRT off-vtable. Settles §4.4's
  representation.
- **P4 — thunked-slot `this` compatibility (gates the thunk approach).** *Probe:*
  with the vtable swapped on the existing object, call one thunked internal slot
  (a pool accessor or CRC) and confirm it runs correctly against the kcdx-vtable'd
  object. *Outcome map:* runs correctly → thunks are sound (the object layout is
  preserved because kcdx swaps only the vtable pointer); crashes/misbehaves → a
  thunked original body cannot run against the swapped object → either own that
  slot too, or the swap model needs revisiting. Confirms §4.3's reversibility rests
  on a sound foundation.

P1 and P2 are the load-bearing seating probes. P1 is RESOLVED (static binary
read, outcome (c) — swap seats at the construction site, not the ready-bracket);
P2 still gates everything after seating (no build proceeds without it). P3 and P4
gate the handle-representation and thunk-correctness decisions respectively.

---

## §9 How this resolves KI-0019 and KI-0006 (the crash class)

[KI-0019](../known-issues/KI-0019-ccrypak-fopen-reentrancy-av-on-inventory-open.md)
(cross-CRT `fseek` on the inventory-open FSR2/DLSS path) and
[KI-0006](../known-issues/KI-0006-serve-execute-vehicle-not-found.md) (the
cross-CRT `fclose` sibling) are BOTH instances of the boundary-straddle: kcdx's
old HOOK 2 (`FOpenLooseOverlay`) mints a `FILE*` with kcdx's CRT and returns it to
the engine, whose `ucrtbase` then operates it (`fseek` for KI-0019, `fclose` for
KI-0006) on an fd that is invalid in the engine's CRT → AV.

**The takeover removes the class structurally, not by patch:** under total
ownership kcdx owns the read family (slots 38–59). A kcdx handle is operated ONLY
by kcdx's read slots, on kcdx's CRT. There is no point at which the engine's CRT
operates a kcdx handle — the boundary is never crossed, by construction. This is
why the fix is the takeover and not a targeted `fseek` patch: patching the one
crash leaves the straddle (any other engine CRT op on a kcdx handle is the next
crash); owning the read family removes the straddle entirely.

Both KIs are this design's home (the routing — "own track, this design is the
home" — supersedes the prior "bundled to Phase 11 / FIX A" routing in both KI
docs). They close when the takeover lands AND a launch confirms the KI-0019 repro
(inventory open) is clean — per `.claude/rules/test-discipline.md` +
`anti-patterns.md` AP17 (root cause = the mechanism paragraph above, not "no longer
crashes").

---

## §10 Scope, deferrals, and dependencies

**v1 IN:** the vtable swap (seating); the kcdx-owned file/resolution/read/mount/
existence slot set (§4.5); the unified asset index (§5); kcdx's own PKZIP/DEFLATE
reader (§6); the per-slot declarative table with thunk-to-original for internal
slots (§4.3); KI-0019/KI-0006 resolution (§9); the four probes (§8); preservation
of the `asset-replacement.md` author-facing contract (§7).

**Dependency — a DEFLATE inflater.** kcdx's pak reader needs DEFLATE. This is a
third-party dependency choice (`.claude/rules/dependencies.md` — stdlib-first,
license-checked, recorded). zlib (zlib license) and miniz (MIT, single-file) are
the candidates; the pick is a design decision the user owns, surfaced at the build
step that adds it, not pre-decided here. (kcdx may already vendor an inflater —
the build's first dependency step checks `vendor/` before adding one.)

**Deferred / OUT (surfaced, not silently dropped):**
- Reimplementing the thunked internal slots (pool, CRC/MD5, %USER%, dir-casing) in
  kcdx — explicitly deferred; the per-slot table makes each a future one-line flip
  (§4.3). Not v1 scope; no cornerstone forces it (those slots have no cross-CRT
  exposure and no author-facing behavior).
- Writing pak files (kcdx as a pak WRITER) — out of scope; the takeover is a read
  filesystem. The write slots (`FWrite` etc.) serve the engine's existing write
  paths (cache/save) and are kcdx impls operating on kcdx's CRT for consistency,
  but kcdx does not author new pak-writing capability in v1.

**Relationship to the restructure plan:** this is its OWN track (the settled
phase-relationship decision), decoupled from Phase 11 / FIX A (DllMain-timing +
Lua VM). Phase 11 keeps its DllMain/VM scope; the file-CRT class moves here. §11
captures the in-flight state this design must not orphan.

---

## §11 Relationship to in-flight work — preserve our place

This design is authored while other work is in flight. It does NOT touch that
work's code or trackers; this section records the relationships so nothing is lost
and a future reader (or `/plan` consuming this design) knows the exact state.

- **PROBE F is live in `src/asset_overlay.cpp`** (3 occurrences, the
  `LOG_DEBUG_KV("PROBE_F", …)` block ~L379–391, deployed to the live install at
  commit `04a77b5`). It confirmed HOOK 2 serves the UI `.dds` as a kcdx-CRT
  `FILE*` on the inventory path. It is OWED REMOVAL per the no-residue rule
  (`.claude/rules/working-artifacts.md`) — capture its finding to
  `_research/probe-archive/`, then remove from source. The takeover SUBSUMES the
  whole `asset_overlay.cpp` HOOK 1 + HOOK 2 seam (it becomes kcdx vtable slots +
  the unified index), so PROBE F's removal happens as part of that subsumption —
  but if the takeover build is not imminent, PROBE F removal is owed
  independently (it must not ship as residue). Tracked as the first build/cleanup
  item.
- **The `asset_overlay.cpp` two-hook seam is what §1/§7 subsume.** HOOK 1
  (`AdjustFileNameResolver`) → kcdx's slot-1 impl over the unified index; HOOK 2
  (`FOpenLooseOverlay`) → kcdx's slot-36 impl. The overlay map → the unified index
  (Loose byte-sources). This is a REPLACEMENT, not a parallel system — the
  takeover build removes the two AddCEngine hooks and the overlay-map globals.
- **KI-0019 + KI-0006 routing is corrected by this design** (own track, this is
  the home) — both KI docs' "bundled → Phase 11 / FIX A" routing is now stale and
  must be repointed at this design. That KI-doc edit is OUTSIDE the design tree, so
  it is the immediate next action after this design lands (not part of the design
  write).
- **Phase 10 (gameplay-event catalog) is UNAFFECTED.** The pinned-but-unconfirmed
  pilots (item_picked_up / perk / combat) and the cap-105/106/107 verification
  probes (committed `f115640`, removed from live) are a separate track. The
  takeover does not touch them. The separate deferred defect they surfaced — the
  raw-VA `address=` entry-hook registration rejection — is also independent.
- **The restructure plan (Phase 11 IN PROGRESS) is decoupled** — this design lifts
  the file-CRT class out of Phase 11. Phase 11's DllMain/VM scope is untouched.

The strongest preservation guarantee is that this section lives in the durable
design artifact: the in-flight state is recorded where the next phase of work will
read it, not held in conversation memory.
