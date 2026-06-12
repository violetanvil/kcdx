# tech-debt/

Deliberately-carried debt with a NAMED closure blocker — a known correctness
gap or compliance gap in shipped/working code that kcdx is carrying on purpose,
with a specific phase / fix / named source-mechanism that will close it. Per
`.claude/rules/doc-organization.md` (the typed-tree / naming / index / open-closed
convention) + `.claude/rules/tech-debt` (the qualifying bar — a vague blocker is
refused; that distinguishes TD from a `/report-bug` KI).

Distinct from the sibling trees:
- `docs/known-issues/` (`KI-NNNN`) — a live runtime defect with NO known fix.
- `docs/outstanding-work/` — designed-but-unbuilt capability (a new feature),
  not a gap in existing code.
- `docs/tech-debt/` (`TD-NNNN`, here) — deliberately-carried debt in working
  code, with a named blocker.

## File naming

`TD-NNNN-<slug>.md` with the ID zero-padded, allocated highest-existing + 1
(shared pool across `tech-debt/` + `tech-debt/closed/`, never reused). Open in
the type root; closed in `closed/` (the `git mv` + Resolution + reindex is one
move, per `doc-organization.md` §"Close → move to closed/").

## Closure-gate vocabulary

A TD's named blocker is phase- or fix-keyed (kcdx's `docs/outstanding-work/`
revisit-trigger vocabulary): `phase-N` / `post-phase-N-acceptance`, or a named
engine capability (`FIX-A`, `Phase-11`), or a named source-fix mechanism.

## Active

| id | reported | status | what it is | closure gate |
|----|----------|--------|------------|--------------|
| [TD-0001](TD-0001-declare-value-string-arena.md) | 2026-06-01 | Open | silent same-triple re-Declare use-after-free on cached `stringValue` | the value-string arena (`src/declared_targets.cpp`) source-fix |
| [TD-0002](TD-0002-lua-callback-main-thread-guard.md) | 2026-06-01 | Open | dynamic dispatchers `lua_pcall` with no main-thread check (AP13 gap) | the `GetCurrentThreadId()` guard in `src/scripting.cpp` |
| [TD-0003](TD-0003-engine-direct-hook-migration.md) | 2026-06-01 | Open | 5 engine-direct `MH_CreateHook` sites bypass `hook_chain` (AP4 gap) | migrate all 5 to `hook_chain::AddCEngine` (+ 10 test rows) |
| [TD-0005](TD-0005-high-level-lua-surface.md) | 2026-06-05 | Open | high-level Lua gameplay surface (player/inventory/world/dialogue/quest) deferred | a dedicated high-level-Lua-surface build phase |
| [TD-0006](TD-0006-statement-layer-in-user-db.md) | 2026-06-05 | Open | statement layer DEV-only — USER DB can't back statement-level named things + needs open kind model | the maintainer tool gaining the capability to own these kinds + project them to the USER DB |
| [TD-0007](TD-0007-unclassified-lua-loader-symbols.md) | 2026-06-05 | Open | 5 Lua C API fns unclassified (loadbuffer/loadstring/gsub unwired + newthread/cpcall fail-loud) — shim can't fully serve them | classify via /research-disassembly before Phase-11 P5 drops static Lua |
| [TD-0009](TD-0009-engine-browser-agreement-superset-kinds.md) | 2026-06-08 | Open | engine↔browser survival-agreement (D27) pinned for 4 algorithm-identical kinds; 3 superset kinds (string_anchor/instruction_anchor/data_slot) unpinned — the engine's checks are a documented superset of the browser/Python subset | reconcile (teach the browser the fuller checks) OR formalize per-kind agreement scope (annotate the fixture + amend D27) |
| [TD-0010](TD-0010-statement-replace-live-native-execution-readback.md) | 2026-06-09 | Open | cap-92 proves `replace_with` STRUCTURALLY (static-op-only, no callback can fire); the live native-execution readback (tight-loop, zero dispatch lines) is deferred | a maintainer-chosen boot-safe statically-neutralizable target + a cap-92 live-readback row |
| [TD-0011](TD-0011-stale-bulk-overlay-valid-through-column.md) | 2026-06-11 | Open | committed bulk overlay stale after D40 valid_through overlay→authored move (still carries a `valid_through` column the new code won't write) | the next expert from-dump rebuild + bulk re-export (D38 `--rebuild-from-dump`) re-exports the overlay without `valid_through` |
| [TD-0012](TD-0012-statement-carrier-conflict-engine-footprint.md) | 2026-06-11 | Open | statement carriers bypass the conflict engine (not in g_patches) — a statement-vs-statement clobber rejects loud but UNNAMED; conflict report omits statement writes (KI-0017 fork-3; the loud reject already prevents silent clobber) | the next statement-surface or conflict-engine cycle registers a WriteFootprint for the statement carrier |
| [TD-0013](TD-0013-lua-early-stop-window-law-fixture.md) | 2026-06-11 | Open | behavior window-law wall built (9.5 P1 s4) but its LUA early-stop trip has no caller — no Lua early stop exists; the Lua out-of-window fixture leg is bucket-2 deferred (C++ leg rides P2 s2) | Phase 11 P5's lua_before slot lands; then the Lua early-stop out-of-window fixture leg is written + this TD closed |

## Closed

| id | reported | status | what it is | closure gate |
|----|----------|--------|------------|--------------|
| [TD-0004](closed/TD-0004-rebuild-oracle-baseline-recapture.md) | 2026-06-02 | Closed 2026-06-08 | rebuild-oracle baseline stale; needed a deliberate inspected re-capture | inspected `--capture` of `oracle_baseline.json` + provenance note (via KI-0009) |
| [TD-0008](closed/TD-0008-stale-address-id-test-fixtures.md) | 2026-06-08 | Closed 2026-06-08 | 9 addr-library caps (CAP-20/28/33/34, COMP-12) referenced retired ids 1172/1124 after the 1–157 renumber | repointed fixtures to renumbered ids (97/49); pending-launch GREEN |
