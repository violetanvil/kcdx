# KI-0028 — THE KCDX DIFF: kcdx's implementation laid against the vanilla init+FS map

**Date:** 2026-06-22 · **Trust:** kcdx source read THIS pass (every cited file:line read in body, AP19-clean
on the kcdx side). Vanilla side consumed from `VANILLA-MAP.md` (Phase 2) + its cited prior dumps; not re-derived.
**Apex question:** *what does the level-load abort TEST, and which kcdx slot-LOGIC deviation flips that
tested condition?* — the swap MECHANISM is SETTLED-innocent (PROBE-Z); this diff isolates the LOGIC deviation.

kcdx source root: `src/fs_takeover/`. Vanilla map: `VANILLA-MAP.md` §0–§8. RVA = VA − 0x180000000.

---

## The per-stage diff table — every vanilla step → IDENTICAL / THUNKS / DIFFERENT

| # | Vanilla step (VANILLA-MAP §) | kcdx verdict | What kcdx does (cite) |
|---|---|---|---|
| S0 | Boot → CSystem::Init reads pCryPak once @0x7A7225 before ModManager ctor | IDENTICAL | kcdx does not touch CSystem::Init's pCryPak read; the seat hook brackets the *construct-store helper*, lets it run verbatim, swaps after (`seating_hook.cpp:85-103`). |
| S1 | CCryPak construct / seat; vtable ptr reachable at `*(gEnv+0x50)` | DIFFERENT (mechanism — SETTLED innocent) | After the helper publishes, kcdx overwrites `[obj+0x00]` with `g_kcdxVtable` (`vtable_swap.cpp:206-213`). PROBE-Z: a no-op thunkswap renders → mechanism exonerated. **Not the LOGIC suspect.** |
| S2a | Search-path vector `[this+0x198..+0x1a0]` (slot 19 AddMod push_back; slot-1 iterates it) | THUNKS | Slot 19 AddMod = `Thunk` (`vtable_table.cpp:93`). The vector is the engine's, populated by the engine, intact (vtable-ptr-only swap). |
| S2b | Alias table `[this+0x1b0..+0x1b8]` (slot 22 SetAlias / 24 GetAlias) | THUNKS | Slots 22/24 = `Thunk` (`vtable_table.cpp:96,98`). **But kcdx re-implements two aliases ITSELF** in its index key fold (`FoldEngineAliasToIndexKey`, `asset_index.cpp:242-254`): `%engine%/`→strip, `data/gameshaders/`→`shaders/`. The engine's REAL alias table (slot 22/24) is NOT consulted by kcdx's resolve — kcdx's fold is a *hand-coded 2-entry subset* of whatever the engine alias table holds. **DIFFERENT in effect — see D5.** |
| S2c | Data root `[this+0x188]`; pakPriority cvar `[this+0x228→+0x20]` (mode 2 = PAK-first) | THUNKS / NOT-CONSULTED | Members intact (swap preserves them). kcdx's resolve does NOT read pakPriority — kcdx's index lookup is loose-overwrites-pak unconditionally (`asset_index.cpp:165-181`), a fixed precedence, not the engine's cvar-driven one. Benign for boot (default mode 2 = pak-first matches "index has the pak"), **flagged D6.** |
| S3 | Pak discovery + mount: OpenPacks→register→split→factory slot72; slot-1 AdjustFileName on the .pak path; ZipDir CDR; rank-insert into `[this+0x120..+0x128]`; MOUNT-ONCE | THUNKS (mount mechanism) **+** DIFFERENT (resolve gate) | **All mount slots THUNK:** 6/7 OpenPack, 9/10 OpenPacks, 71 mount, 72 TestArchive/factory, 100 ClosePakByIndex = `Thunk` (`vtable_table.cpp:74-75,77-78,168-169,199`). So the engine's OWN mount machinery runs — ZipDir CDR, rank-insert into `[this+0x120]` — all engine. **BUT factory STEP 1 calls slot-1 AdjustFileName (flag 0x10000) on the .pak path, and kcdx OWNS slot 1** (`vtable_table.cpp:68`; mount-dump §LOAD-BEARING). So a kcdx slot-1 mis-key on a `data/levels/<lvl>/<c>.pak` path reaches the MOUNT, not just serves. **DIFFERENT via the slot-1 chokepoint — D5.** |
| S4a | Slot 1 AdjustFileName `0x6205C` — the single resolution chokepoint; returns a STRING | DIFFERENT | `kcdx_AdjustFileName` (`open_slots.cpp:290-429`): loose hit→disk path; **pak hit→returns `pName` UNCHANGED** (`:359-392`); miss→thunks captured original (`:403-428`). The pak-hit-returns-pName-unchanged is the KI-0026 design; key fold applied first (`NormalizeVPath`+`FoldEngineAliasToIndexKey`, `:300-301` via `ResolveVPath`). |
| S4b | Slot 36 FOpen `0x4614A0` mints a tagged handle from its OWN pak lookup (not slot-1) | DIFFERENT | `kcdx_FOpen`→`OpenResolvedAndMint` (`open_slots.cpp:164-282,431-438`): re-resolves `pName` through the index itself (`ResolveVPath`, `:172`), opens on kcdx CRT, mints an odd handle. Does NOT use the engine's `FUN_180462ddc` pak test. |
| S5-1 | Vanilla FOpen acquires pak-mgr SRW lock `[this+0x200]` on EVERY open | DIFFERENT | kcdx uses its OWN `g_poolLock` leaf mutex (`file_handle.h:59-68`); never touches `[this+0x200]`. **D3 — low abort-relevance** (kcdx pool is private; no shared-state coupling). |
| S5-2 | Two handle arms: PAK→`index+1` into `[this+0x40]` (may GROW the vector, writes the entry); LOOSE→raw CRT `FILE*` | DIFFERENT | kcdx mints `(id<<1)|1` ODD handle for EVERY open (`file_handle.h:27-47`, `open_slots.cpp:114,152`). Never mints an `index+1`, never touches/grows `[this+0x40]`. **D2/D4 — benign IF every consumer is kcdx-owned (it is, by design).** |
| S5-3 | **Register side-effects: LOOSE open fires `(*this+0x268)` (register-handle) AND `(*this+0x2c8)` (post-open hook); PAK arm registers via the `[this+0x40]` vector write. `+0x2c8` fires on EVERY served arm.** | **DIFFERENT — D1 (LEADING on the handle axis)** | `OpenResolvedAndMint` mints via `MintLoose`/`MintPak` ONLY (`open_slots.cpp:181,197,240`). **kcdx fires NEITHER `+0x268` NOR `+0x2c8`, and never does the `[this+0x40]` pak-vector write.** Slots 77 (`+0x268` `%USER%`/register) = `Thunk` (`vtable_table.cpp:174`) but kcdx's open path never CALLS it. The swap captures ONLY slot-1 + the 8 metadata originals (`vtable_swap.cpp:169-186`) — `+0x268`/`+0x2c8` are NOT captured and NOT invoked. **The engine's registered-open-handle structure stays EMPTY under kcdx.** |
| S5-4 | Read dispatch: `handle-1 < pakEntryCount` → pak arm, else OS arm | IDENTICAL-by-avoidance | kcdx OWNS every read slot (38/39/40/41/43/44/46/47/53-59/66 = `Kcdx`, `vtable_table.cpp:118-141,159`), so the engine's index-vs-count test NEVER runs on a kcdx handle; kcdx dispatches on the odd tag (`file_handle.h:31-38`). No collision by construction. |
| S5-5 | Find-handle 63/64/65 vanilla FindClose object-derefs the handle (refcount@+8, vdtor through handle+0x0) | DIFFERENT (DIVERGENCE B — FALSIFIED) | Slots 63/64/65 = `Kcdx` (`vtable_table.cpp:156-158`); `find_slots.cpp` mints the SAME `(id<<1)|1` find-handle, kind=Find. Engine never operates it (53 consumers use `-1<h` + opaque pass-back). **Off the critical path (settled).** |
| S6 | Level load: C_Game::CreateInstance → CET_PrepareLevel; FS populates the level record one+ frames above | n/a (engine) | No kcdx frames on any thread at the abort (live cdb). kcdx's only role here is what its file slots SERVE/RESOLVE for the level-info read. |
| S7 | The TESTED CONDITION: `Game->[0x88]->[0x58]` current-level record / name@`[+0xc8]` length empty → MessageBoxA + RaiseException(0xD2) | n/a (engine) | The abort reads a downstream consequence of an FS read. The kcdx deviation that flips it must leave that record/name empty. |
| S8 | Which deviation flips it | see ranking below | Candidate 1 (resolve/serve miss on `levels/` key) + Candidate 2 (D1 skipped register hooks). |

---

## The DIFFERENT rows ranked by likelihood of flipping the level-load abort

Each ranked against the apex: *does this deviation leave `Game->[0x88]->[0x58]` / name@`[+0xc8]` empty?*

### RANK 1 — D5: slot-1 / index key-coverage gap on the `data/levels/` prefix (LEADING; live-observed)

**Deviation class:** *a resolve/serve answer the abort condition reads — kcdx returns a MISS for a level resource
file the level-info loader needs to populate the current-level record.*

- **Source shape:** `kcdx_AdjustFileName` + `ResolveVPath` key the lookup by `NormalizeVPath(pName)` then
  `FoldEngineAliasToIndexKey` (`open_slots.cpp:300-301`, `asset_index.cpp:256-265`). The fold handles ONLY
  `%engine%/` and `data/gameshaders/` (`asset_index.cpp:242-254`). **There is NO fold/normalization rule for a
  `levels/` ⇄ `data/levels/` key mismatch.** The recursive index walk (`asset_index.cpp:56`
  `recursive_directory_iterator`) stores each level pak's CONTENTS keyed by the pak's OWN central-directory
  entry name (`pe.name`, `:122`) — which is the pak-internal vpath, NOT necessarily the `data/levels/...` form
  the level load REQUESTS.
- **Live evidence (VANILLA-MAP §8 Candidate 1, `ROOT-CAUSE-existence-overreport.md` 20:49):** on a clean full
  swap, `loose_open_failed vpath="levels/kutnohorsko/auto_resourcelist.txt" errno=2` then `how=miss-original
  result=0` — kcdx tried a LOOSE disk open of `data\levels\...\auto_resourcelist.txt`, file-not-found, then
  thunked the original to a miss. **The file lives inside an indexed level pak yet the key does not resolve to
  the stored entry.** This is exactly the `open_slots.cpp` MISS arm (`:198-241`): index miss → original resolve
  → loose `_wfopen` of a non-existent disk path → `loose_open_failed` (`:106-112`).
- **Why it flips the abort:** if the level-info loader reads `resourcelist.txt` to fill `Game->[0x88]->[0x58]`
  / the name@`[+0xc8]`, a kcdx miss on it leaves that record empty → S7 gate fires → `0xD2`.
- **Mount AND serve, one gate:** because factory STEP 1 calls slot-1 on the `.pak` path (mount-dump §LOAD-BEARING),
  the SAME key gap can fail the level-pak MOUNT, not only the resource-file serve. Both route through the
  kcdx-owned slot-1 (`vtable_table.cpp:68`).
- **`contradictsSettledFact`: false.** Consistent with every settled fact: kcdx serves indexed bytes correctly
  (this is a MISS, not a wrong serve); the over-report/IsFileExist facts are about DIFFERENT files; the abort is
  a deliberate engine test of an empty record.
- **The un-pinned half:** WHICH normalization (a `levels/`↔`data/levels/` prefix, or the pak-CD entry name not
  matching the requested form) is the gap is NOT yet body-pinned — the loader that writes the record from
  `resourcelist.txt` was not traced (VANILLA-MAP §"Still-unverified" #1). Leading-but-unverified.

### RANK 2 — D1: kcdx skips `+0x268` (register-handle) and `+0x2c8` (post-open hook) on every open (structural)

**Deviation class:** *a state mutation / side effect a vanilla slot body had that kcdx's reimpl dropped — a
structure a downstream validation/teardown walks that kcdx leaves empty.*

- **Source shape (CONFIRMED this pass):** vanilla FOpen fires `(*this+0x268)` (loose register) + `(*this+0x2c8)`
  (post-open, every arm) and writes the `[this+0x40]` pak vector (pak arm) — `raw-handle-lifecycle.txt:40-46`.
  kcdx's `OpenResolvedAndMint` fires NONE of them (`open_slots.cpp:164-282` — mint-only). The swap does not even
  capture `+0x268`/`+0x2c8` (`vtable_swap.cpp:169-186` captures slot-1 + 8 metadata only).
- **Why it COULD flip the abort:** if the level-load / CET path walks the engine's registered-open-handle table
  (populated by `+0x268`) or depends on a `+0x2c8` post-open side effect to record the level files it opened,
  and that record feeds the current-level-record population, the same empty-record state results.
- **Why RANK 2, not 1:** the `+0x268` BODY is UNREAD (VANILLA-MAP §"Still-unverified" #3) — WHAT structure it
  populates and whether ANY level-load path WALKS it is unverified. Structurally-plausible, not live-observed.
  There is also a NAME/ROLE conflict: `front1` maps slot 77 (`+0x268`) to `%USER%` expansion, yet the slot-recon
  asserts "register the handle" — the role itself is asserted, not body-confirmed.
- **`contradictsSettledFact`: false.** Does not touch any settled serve/enumerate fact.

### RANK 3 — D6: kcdx ignores the pakPriority cvar; fixed loose-over-pak precedence

- **Source:** `asset_index.cpp:165-181` overwrites a pak source with a loose source unconditionally; kcdx never
  reads `[this+0x228→+0x20]`. The engine's default mode 2 (pak-first) is matched only incidentally (an indexed
  pak entry IS served). For a level file that exists ONLY in a pak (no loose override), kcdx serves the pak — so
  the precedence difference does not change the boot serve. **Low abort-relevance**; included for completeness.
- **`contradictsSettledFact`: false.**

### RANK 4 — D2 / D4: odd-handle representation + skipped pak-vector grow

- kcdx never mints `index+1`, never grows `[this+0x40]` (`file_handle.h:27-47`). Benign because every read/find
  consumer is kcdx-owned (S5-4/S5-5). A residual flip would need a NON-read-family vanilla consumer reading the
  handle as a `[this+0x40]` index OR reading `[this+0x40]/[this+0x48]` as an open-file COUNT the abort path
  consults — none found in priors (straddle FALSIFIED). **Lowest.**
- **`contradictsSettledFact`: false.**

### RANK 5 — D3: private `g_poolLock` vs engine `[this+0x200]` SRW lock

- No shared-state coupling (kcdx pool is private). No path by which a missing `[this+0x200]` acquire flips a
  level-record test. **Lowest; included for completeness.**

---

## The two surviving live candidates (VANILLA-MAP §8), restated against the source

Both produce the SAME observable (empty current-level record → `0xD2`); distinguished only by WHERE population fails:

- **Candidate 1 = D5 (RANK 1, LEADING, live-observed):** kcdx slot-1/index key-coverage gap on the `data/levels/`
  prefix → `resourcelist.txt` resolves to a MISS → loose open of a non-existent disk path → `result=0` → the
  current-level record never populated. Directly observed in the failing-run log.
- **Candidate 2 = D1 (RANK 2, structural):** kcdx skips `+0x268`/`+0x2c8` → the engine's registered-open-handle
  structure stays empty → a level-load validation/teardown that walks it leaves the record empty. The `+0x268`
  body is unread, so structurally-plausible, not confirmed.

**The decisive missing read (both candidates):** the level-info loader that writes `Game->[0x88]->[0x58]` /
name@`[+0xc8]` from a file read — does it consume `resourcelist.txt` (→ D5) and via which slot (FOpen vs an
existence probe), or does it walk the `+0x268`-registered structure (→ D1)? That loader body was NOT traced.

---

## Key reconciliation notes (where this diff sharpens or corrects the map)

1. **The alias fold is kcdx's, NOT the engine alias table (new in this pass).** kcdx re-implements `%engine%`
   and `data/gameshaders` aliases by hand (`asset_index.cpp:242-254`) and never consults slot 22/24 (THUNK but
   un-called on the resolve path). So any alias the engine's REAL table holds that kcdx's 2-entry fold does NOT
   replicate is a silent resolve gap — a concrete mechanism for the `levels/` key miss (D5). The level load may
   request a path under an engine alias kcdx's fold doesn't cover.
2. **Mount machinery is the engine's, gated by kcdx slot-1.** Every mount slot (6/7/9/10/71/72/100) THUNKS, so
   ZipDir/rank-insert is all engine — but the slot-1 call inside the factory (kcdx-owned) means a kcdx key gap
   reaches the mount. This confirms VANILLA-MAP §3's "slot-1 chokepoint even during mount."
3. **D1 confirmed at source.** The map flagged D1's `+0x268` body as unread; this pass confirms the kcdx SIDE
   definitively — `OpenResolvedAndMint` fires neither hook and the swap captures neither original. The
   engine-side body remains the un-bisected half.
4. **No diff row contradicts a settled fact.** Every DIFFERENT row is a MISS / skipped-side-effect / different-
   precedence, none a wrong-serve or a real enumeration drop.

---

## DIFF_SCHEMA (returned object)
</content>
</invoke>
