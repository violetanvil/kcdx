# Phase 9.3 — `kcdx.hook.*` / `kcdx.statement.*` split + value namespaces + author-self-declaration + multi-region trampoline

**Status: NOT STARTED.** Design source: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3".

The biggest author-surface phase. Lands the two distinct site-modification
namespaces (`kcdx.hook.*` callback-based interception vs `kcdx.statement.*`
static-bytes modification — genuinely different mechanisms, not aliases), the value
namespaces they consume (`kcdx.locator.*`, `kcdx.op.*`), the function-reference
namespace + author-self-declaration (`kcdx.functions.*` + `kcdx.dll.declare` + PDB
auto-load — the proof a TC author extends without an engine release), the
multi-region trampoline pool for TC scale, and full C++ parity.

`module` is a REQUIRED positional first arg on every hash-checked verb (no default)
— honest about multi-DLL coverage. The split is honest about a real mechanism
difference: callbacks pay per-call dispatch; static bytes execute natively forever
after install. Authors pick by intent.

**Re-decomposed (2026-06-07)** for incremental-delivery: every step lands GREEN +
independently verified. The function-reference namespace (now step 3) lands BEFORE
the hook/statement verbs that consume it; the hook step that REPLACES the existing
surface migrates the existing hook test plugins IN THE SAME COMMIT (the suite never
goes red); each new `cap-NN` row lands with its feature step, not piled at the end.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| [1 — `kcdx.locator.*` value namespace](step-1-locator-namespace.md) | DONE | 9802a5e |
| [2 — `kcdx.op.*` static-op value namespace](step-2-op-namespace.md) | DONE | dce5c35 |
| [3a — `kcdx.functions.*` + `kcdx.dll.declare` (deterministic half)](step-3-functions-and-declare.md) | DONE | b290d9e |
| [3-probe — DbgHelp `SymEnumSymbols` enumerates a foreign DLL's non-exported internals](step-3-functions-and-declare.md) | DONE | f51b847 — Outcome B (FALSIFIED) |
| [3-probe-2 — does `/DEBUG:FULL` (not the FASTLINK default) make the plugin's own internals enumerable?](step-3-functions-and-declare.md) | DONE | ac12caa — Outcome A (CONFIRMED) |
| [3b — PDB auto-load (`plugin_pdb.{cpp,h}`) — internal addresses from a `/DEBUG:FULL` PDB, resolved by the author's bare name](step-3-functions-and-declare.md) | DONE | 54d004d + a29bf8f — cap-90 acceptance RED at first launch surfaced two defects (wrong function-kind filter field + decorated-name keying), both fixed under KI-0014 (`a29bf8f`); cap-90-pdb-internal-address user-confirmed GREEN (resolves by bare name, suite 206/229) |
| [4 — `kcdx.hook.*` sub-verb split + migrate the existing hook plugins (same commit)](step-4-hook-subverbs-migrate.md) | DONE | 4e901db + gov debecf6 + fixes — all 15 consumers migrated; launch-confirmed GREEN after 3 fixes: (1) a `p->address` clobber (an unconditional opts-address read zeroed a positional raw-VA target → hooks installed but never fired; 13 rows) fixed; (2) COMP-03 was a stale deploy (comp-03-A/comp-12-A not redeployed), not a code bug; (3) cap-86-insert's SaveGame sig parse-fail was a seed bug (`char`→`i8`, user-fixed in the maintainer tool) + a test-timing relax (Pending OR Failed-deferred both honest). suite 223/246; only pre-existing KI-0010/0011 remain red |
| [5 — `kcdx.statement.*` static-bytes namespace](step-5-statement-namespace.md) | DONE | cca4c1c — `kcdx.statement.replace_with` (resolve + op-kind-check + determinate-op emit + same-size `patch::ApplyPatch` write, registered as `Kind::Statement`) + `insert_*` defer; cross-binder seams `ReadOp`/`ReadLocatorDescriptor`; cap-92 (4 structural rows) + docs (lua + cpp-NYI). Live native-execution readback deferred to [`TD-0010`](../../../tech-debt/TD-0010-statement-replace-live-native-execution-readback.md) (user-approved, maintainer owns the boot-safe-target choice) |
| [6 — multi-region trampoline-pool expansion](step-6-multi-region-trampoline.md) | DONE | 879b4c7 — scope corrected (the pool was ALREADY multi-region; the real delta is proactive per-reservation 80% expansion + an N-region-per-anchor cap + a teaching exhaustion error). Built into `Allocate()`; rel32-reach reuse preserved exactly; engine self-test cap-93 (expansion + exhaustion rows). Gated step-review PROCEED |
| [7a — `kcdxFunctionsInterface` + `kcdxDllInterface` (C++ function-reference + declare)](step-7a-cpp-functions-declare.md) | DONE | 3c3b473 — two new interfaces (QI 12/13, append-only): `kcdxFunctionsInterface` mints the by-value 8-field `kcdxFunctionRef` (GameByName/GameById/PluginByName, strings interned process-lifetime); `kcdxDllInterface::Declare` shares the `g_declared` store with the Lua `kcdx.dll.declare` via one seam (`DeclareFunction`). `K.functions`/`K.dll` wrapper; survivor-comment fix; docs/cpp NYI→built. cap-94 (declare-resolve / game-resolve / miss-reason). Gated step-review PROCEED |
| [7b — `kcdxStatementInterface` (C++ static-bytes mirror)](step-7b-cpp-statement.md) | NOT STARTED | — |
| [7c — `kcdxHookInterface` insert methods + `targetRef` opts field](step-7c-cpp-hook-insert.md) | NOT STARTED | — |

## Ordering note (the re-decomposition's dependency order)

Value namespaces (1 locators, 2 ops) land first — independent, additive, each
self-verifies. The function-reference namespace + author-declaration (3a) lands
BEFORE the hook (4) and statement (5) verbs that accept a `kcdx.functions.*`
reference value — its own declare test runs at 3a; the cross-plugin
hook-by-reference test fires at step 4 where the reference-accepting hook exists.

**Step 3 split (2026-06-09, user-settled).** The original step 3 bundled three
surfaces; the PDB-autoload half rests on an UNVERIFIED runtime mechanism (does
`DbgHelp` `SymEnumSymbols` enumerate a foreign plugin DLL's *non-exported internal*
functions from a release-build sidecar `.pdb`? — `src/crash_guard.cpp` uses
`DbgHelp` only to symbolize its own backtrace, never a foreign DLL's internals).
Per results-driven.md (probe the asserted mechanism before the phase builds on it)
the step is ordered: **3a** deterministic `kcdx.functions.*` + `kcdx.dll.declare`
(DB read + author-declared map — no runtime unknown; fully unblocks steps 4/5 with
the reference-value type) → **3-probe** a minimal DbgHelp probe plugin shipping a
`.pdb` with a known non-exported function, proving `SymEnumSymbols` enumerates it
(outcome→meaning: internal resolves → 3b is buildable as designed; does NOT →
re-design 3b's source-of-internal-addresses before building) → **3b** `plugin_pdb`
built only after 3-probe confirms. 3a's resolvability is independent of the probe.
The hook split (4) REPLACES `lua_bind_hook.cpp` AND migrates the existing hook test
plugins in the SAME commit (suite stays green). Statement (5) consumes 1+2+3.
Trampoline (6) is independent (the single-target tests of 4/5 don't need it; it is
required at TC scale). C++ parity (7) follows the settled Lua shapes. Each step ends
buildable + its same-change test runnable.

**Step 7 split (2026-06-09, user-settled).** The original step 7 bundled THREE
independent C++ interface surfaces (each its own ABI struct + binder + C++ test
plugin + docs = its own commit), so it was re-decomposed into three commit-grain
sub-steps after a `/design` pass settled the C++-parity shape
(`../00-original-plan.md` §9.3-C, committed `4445cb9` — both design gates PROCEED):
**7a** `kcdxFunctionsInterface` + `kcdxDllInterface` (the new function-reference +
declare interfaces; the passable by-value `kcdxFunctionRef` 7b/7c's `targetRef`
opts field consumes — so 7a is the producer, ordered first; independently
verifiable via its own declare-then-resolve C++ test) → **7b**
`kcdxStatementInterface` (the C++ mirror of `kcdx.statement.*`; depends on 7a's
`kcdxFunctionRef`) → **7c** `kcdxHookInterface` insert methods + the `targetRef`
opts-field append (depends on 7a's `kcdxFunctionRef`). The original
`step-7-cpp-parity.md` is superseded by these three; its premise that the insert
apply-paths "fire" was corrected (both Lua insert apply-paths are unwired/
fail-loud-deferred today, so the C++ inserts mirror the register-and-DEFER
contract — wiring them to fire is separate deferred work on both surfaces, OUT of
this phase). Each sub-step ships its own C++ test plugin + `docs/cpp/` NYI→built
resolution + the AP11 append-only InputLoaded-listener-count ABI check.

## Verification gate (whole phase)

- **The suite stays green at EVERY step** (the re-decomposition's load-bearing
  property): no step ships a red suite. Step 4 migrates the existing hook plugins
  (`cap-03`, `cap-04`, `cap-20`, `cap-21`, `cap-22`) to the sub-verb shape in its own
  commit — suite stays 21/21 (+ the new rows) green throughout.
- **`cap-NN-plugin-fn-declare`** — plugin A declares a function via `kcdx.dll.declare`;
  plugin B (depending on A) hooks it by name and the hook FIRES — cross-plugin access
  without disassembly (the extensibility proof). Declare machinery + resolvability at
  step 3a; the cross-plugin hook fires at step 4.
- **`cap-NN-pdb-autoload`** (step 3b) — a PDB-shipped plugin's non-exported internal
  resolves to its address; a static `replace_with_noop` applies — PDB-sourced
  addresses work without declaration. Gated upstream by **3-probe**: runs only after
  the probe proves `SymEnumSymbols` enumerates internals (a red row here is then a
  build defect, not a disproven assumption — the assumption was proven before 3b built).
- **`cap-NN-statement-replace`** (step 5) — a `kcdx.statement.replace_with(...)`
  produces ZERO per-call Lua dispatch (verified by absence of dispatch log lines
  during a tight loop hitting the target).
- **Full Lua+C++ parity** (`.claude/rules/lua-api-surface.md`) — every capability
  reachable + tested from BOTH surfaces (step 7's C++ interfaces + their C++ test
  plugins).
- **Fixture currency — every new `cap-NN` plugin references a CURRENT curated id/name,
  resolved against the shipped `reference.sqlite` at author time, never a hardcoded id
  from an older curated set.** The curated entity set was renumbered to a contiguous
  1–157 scheme (the DB↔Address-Library unification); a fixture written against a retired
  id (the [`TD-0008`](../../../tech-debt/TD-0008-stale-address-id-test-fixtures.md)
  class — `address_id` 1172/1124 etc.) ships a red row. Each step's plugin verifies its
  `target` / `address_id` / `kcdx.functions.*` reference resolves in the shipped DB
  before the step lands (a name resolves to address AND ABI per
  `.claude/rules/cornerstones.md`; the engine carries the curated detail).
- Confirmed by the user's launch + the agent's `kcdx-dev.log` read at each step.
