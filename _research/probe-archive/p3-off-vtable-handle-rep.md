# PROBE P3 — off-vtable raw-handle access: NONE on a FOpen-class handle (outcome 1) → kcdx handle-id

**Answered:** STATICALLY, from the primary-evidence asset-resolution recon already
on disk — NOT a live launch. P3 is a static call-graph question (does any consumer
operate a FOpen-returned handle without going through a vtable read slot?), and the
three body-read recon docs cited below answer it against the WHGame.dll binary.
Static evidence settles the question and precedes a live probe
(`.claude/rules/results-driven.md` §4); this discharges P3 (the step-3.1 Scope's
"then a live probe" is not needed — the static chain is conclusive). The earlier
live-probe framing is superseded the same way P1 and the DirectStorage step-2
question were resolved statically. No probe code was written into `src/`, so there
is no residue to remove (`.claude/rules/working-artifacts.md`).

**Trust:** the recon-cited facts are PRIMARY EVIDENCE — fresh-Ghidra body reads of
WHGame.dll (`release_1_5_1164953_841`, image base 0x180000000), each claim cited to
a recon doc + section/line. The handle-representation DECISION is
AGENT-AUTHORED SYNTHESIS — an interpretation of those facts against the design's
outcome map, never cited as a SOURCE.

## The question (design §8 P3 / step 3.1)

Does any engine code BYPASS the `CCryPak` vtable and operate a file handle DIRECTLY
off-vtable — a streamer / DirectStorage path holding a raw handle and seeking/
reading it itself, outside the vtable read slots? The answer decides the kcdx handle
representation (design §4.4): a lightweight kcdx **handle-id** (the engine treats
handles as opaque; only the vtable read slots operate them) vs a real
**`FILE*`-shaped object** kcdx must make operable on its own CRT off-vtable.

## Outcome→meaning map (pre-committed, design §8 P3 / step 3.1)

- **Outcome 1** — no off-vtable raw-handle access → a kcdx **handle-id**
  representation is safe → 3.2 mints handle-ids.
- **Outcome 2** — an off-vtable streamer operates a raw handle → kcdx's handle must
  be a real `FILE*`-shaped object operable on kcdx's CRT off-vtable → 3.2 mints that.

## The reading — Outcome 1 holds

**No off-vtable access to a `FOpen`-class (slot 36) handle exists.** Every consumer
that reads a `FOpen`-returned handle reaches it THROUGH the vtable read slots; the
one off-vtable raw-handle operation in the engine operates an ENGINE-minted pak-MOUNT
handle, never a per-file `FOpen` handle.

### 1. The read family dispatches purely on the handle tag — through the vtable

The read-family methods (FRead slot 40 / FSeek slot 38 / FEof+FTell slot 39 /
FWrite slot 41 / FClose slot 55) branch on the handle VALUE itself; there is no
per-read off-vtable handle access — the loose-vs-pak decision was already made at
`FOpen`-time and baked into the handle's tag
(`_research/phase8.5-pak-resolver/front3-handle-consume-read-path.md` §"THE
LOAD-BEARING FACT", lines 23–66). The handle is a tagged union: `handleIdx =
handle - 1`; `handleIdx < pak-vector count` → the **pak arm**; else → the **OS arm**,
which is a literal CRT `fread`/`fseek`/`fclose` on a genuine `FILE*` (the OS-arm leaf
`FUN_1804d7ab4` is "literally `fread(...)`", front3 line 53). The SAME
handleIdx-vs-count test appears verbatim across FRead/FSeek/FEof/FWrite/FClose
(front3 lines 56–61).

The loose-vs-pak decision "bites at FOpen→AdjustFileName (handle-minting), never at
FRead/CCryFile" (front3 line 201; §"WHERE the loose-vs-pak decision ACTUALLY BITES",
lines 101–113). `CCryFile` is a thin wrapper that forwards to `ICryPak::FOpen` (slot
36, vtable +0x120) and stores the returned handle — it "adds NO resolution logic"
and is "not a hook point" (front3 lines 69–97, 178–181). So every handle-consumed
read reaches its bytes through a vtable read slot operating on the tag `FOpen` set.

### 2. The ONE off-vtable raw-handle operation is on an ENGINE-MINTED mount handle, not a FOpen handle

The streaming engine performs its OWN Win32 `SetFilePointer`/`ReadFile` (not the
vtable FRead) — but ONLY on a handle `m_zipFile` that the ENGINE minted during the
CCryPak pak-MOUNT via `CreateFileA` (archive factory slot 72), to the pak the
resolver already chose
(`_research/asset-loadpath-map-recon/F5-streaming-engine-bypass.md` §VERDICT lines
20–28; §"THE EVIDENCE CHAIN" 1–2, lines 34–72). `ZipDir::ReadFileStreaming`
(`FUN_180464b88`) does `SetFilePointer`+`ReadFile` on the HANDLE at `zipDir+0x10`
(F5 lines 42–52); that handle is opened by `FUN_1804d6910`'s `CreateFileA(...,
FILE_FLAG_NO_BUFFERING, ...)` inside the mount path, whose sole caller is archive
factory slot 72 (F5 lines 54–72, 136–139). This is the "one nuance": the streamer
"bypasses FOpen" at the literal slot-36 level (its handle is a `CreateFileA`, not a
`FOpen` call) but does NOT bypass RESOLUTION — "no asset class reaches its bytes
through a file the CCryPak resolver did not pick" (F5 §"The one nuance", lines
120–126). The off-vtable raw handle is an engine pak-mount handle, never a `FOpen`
per-file handle, and kcdx does not replace the engine's streaming at the per-file
level — it owns the resolution/mount that picks the pak the streamer reads.

### 3. The DirectStorage arm — the only other candidate off-vtable open — is dead at the shipped default

DirectStorage (the variant whose `dstorage.dll` would open its own handles outside
`CCryPak::FOpen`) is default-OFF: `wh_sys_streaming_directstorage_enabled` registers
with default value 0, the gate `DAT_1849272d4` reads that same storage, so the DS
StreamEngine is never constructed and control falls to the normal `CStreamEngine`
(`_research/asset-loadpath-map-recon/step2-directstorage-bypass-finding.md` §(a)–(b),
lines 19–60). Confirmed both statically and live (`KCDX_DSCVAR=0` at `input_loaded`,
step2 §"Live confirmation", lines 113–131). The DS arm is dead code at the shipped
default and is a documented v1 limitation, not a common-path bypass (step2 §(c),
lines 84–110). The normal arm reads only the CCryPak-resolved pak (per F5, above).

## The decision (design §4.4) — a kcdx handle-id, honoring the tagged-union contract

**A kcdx `FOpen` handle is a lightweight kcdx handle-id** — opaque to the engine,
operated only by kcdx's own read slots (Phase 3.3 owns the read family). This is the
safe representation because Outcome 1 holds: no engine code operates a `FOpen`-class
handle off-vtable, so the engine never needs to inspect or seek a kcdx handle
directly.

The handle-id MUST honor the engine's tagged-union dispatch contract so any reused or
thunked read slot still dispatches correctly. The engine's contract
(front3 lines 160–163): a small `index+1` value = a pak entry in `[this+0x40]`;
anything else = a real-`FILE*`-class handle (the OS arm). A kcdx handle that is
neither tag "would crash FRead's dispatch — it would index the pak vector out of
bounds or `fread` a non-FILE*" (front3 line 162). So the representation kcdx mints
must be distinguishable in the same way the engine's is, so the dispatch test routes
it deterministically to a kcdx-owned arm.

**Why it is safe — the cross-CRT invariant (design §9).** A handle-id is safe ONLY
because kcdx owns the read family (Phase 3.3): kcdx's own read slots map the
handle-id to its open byte-source state (a loose `FILE*` on kcdx's CRT, or a pak read
cursor into kcdx's PKZIP reader) and operate it entirely on kcdx's CRT. No engine
`ucrtbase` ever operates a kcdx handle, because the engine only HOLDS the handle and
passes it back to kcdx's slots. The streamer's one off-vtable raw-handle access stays
confined to the engine's OWN mount handles (`m_zipFile`), which kcdx does not mint —
so it never touches a kcdx handle.

### The load-bearing constraint this decision imposes on 3.2 / 3.3 / 3.5

**The read family (the slots that operate a handle) MUST be kcdx-owned, not thunked.**
The handle-id is safe ONLY under this constraint. If a read slot were left THUNKED to
the engine original, that thunked original's OS arm would `fread`/`fseek`/`fclose` the
kcdx handle-id on the ENGINE's CRT — the exact cross-CRT straddle that causes
KI-0019/KI-0006 (front3 lines 53, 160–163; design §9). The two ways to keep the
representation correct are mutually exclusive in practice:

- kcdx owns the read family (slots 38/39/40/41/55 + variants) → a kcdx handle-id is
  operated only by kcdx, on kcdx's CRT — SAFE; **or**
- the handle for any thunked-read path must be a real engine-CRT-operable `FILE*` —
  which REINTRODUCES the cross-CRT risk the takeover exists to remove.

Therefore: **kcdx owns the read family, full stop** (it already does in design §4.4
— the read family is all `KCDX(&…)`). 3.2 mints the kcdx handle-id; 3.3 builds the
kcdx-owned read family that operates it; 3.5's per-slot table must keep every
handle-operating slot `KCDX`, never `THUNK`. A future one-line flip of a read slot to
`THUNK` (the §4.3 reversibility property) is the one flip this constraint forbids for
any handle-operating slot while the handle is a kcdx-minted id.

## Reading against the outcome map — verdict

**Outcome 1 holds → a kcdx handle-id representation is safe → step 3.2 mints
handle-ids** (honoring the tagged-union contract; the read family kcdx-owned per the
constraint above). Outcome 2 does NOT hold: the only off-vtable raw-handle operation
is on an engine-minted pak-mount handle, never a `FOpen`-class per-file handle, so
kcdx is not forced into a real-`FILE*`-shaped representation.

## Seed-row candidates (AP18 — FLAGGED, NOT written; need user approval)

The recon's seed candidates stay AP18-flagged, not written (only the user approves a
DB addition). For completeness, the candidates the cited recon already flagged:

- From front3 (read path): `CCryPak::FRead` (`FUN_18051cd00`, slot 40), `CCryFile::Open`
  (`FUN_1804605bc`), the read-family siblings FSeek/FEof and the pak/OS read leaves.
- From F5 (streaming): `ZipDir::ReadFileStreaming` (`FUN_180464b88`), the ZipDir
  uncached opener (`FUN_1804d6910`), the non-streaming read fallback (`FUN_180461a5c`).

None are needed for the P3 resolution — the representation decision rests on the
already-curated FOpen id 131 + the read-path contract, not on owning the streaming
leaves. No seed row is authored here.

## Reusable wiring — the cited recon (co-located, reuse-first)

P3 was answered by reuse, not fresh disassembly. The body-read recon that settles it
lives at:

- `_research/phase8.5-pak-resolver/front3-handle-consume-read-path.md` — the
  handle-tag read-path map (FRead/FSeek/FEof/FWrite/FClose dispatch; the tagged-union
  contract; the loose-vs-pak decision bites at FOpen).
- `_research/asset-loadpath-map-recon/F5-streaming-engine-bypass.md` — the streaming
  engine's off-vtable `ReadFile` operates an engine-minted mount handle, not a FOpen
  handle.
- `_research/asset-loadpath-map-recon/step2-directstorage-bypass-finding.md` — DS
  default-off (static + live), its arm dead at the shipped default.

A future re-verification reconstructs the reading from those docs + their co-located
producer scripts; no probe code need return to `src/`.
