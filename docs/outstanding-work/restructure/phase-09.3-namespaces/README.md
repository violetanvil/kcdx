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
| [1 — `kcdx.locator.*` value namespace](step-1-locator-namespace.md) | NOT STARTED | — |
| [2 — `kcdx.op.*` static-op value namespace](step-2-op-namespace.md) | NOT STARTED | — |
| [3 — `kcdx.functions.*` + `kcdx.dll.declare` + PDB auto-load](step-3-functions-and-declare.md) | NOT STARTED | — |
| [4 — `kcdx.hook.*` sub-verb split + migrate the existing hook plugins (same commit)](step-4-hook-subverbs-migrate.md) | NOT STARTED | — |
| [5 — `kcdx.statement.*` static-bytes namespace](step-5-statement-namespace.md) | NOT STARTED | — |
| [6 — multi-region trampoline-pool expansion](step-6-multi-region-trampoline.md) | NOT STARTED | — |
| [7 — C++ parity (`kcdxStatementInterface` / `kcdxFunctionsInterface` + hook insert methods)](step-7-cpp-parity.md) | NOT STARTED | — |

## Ordering note (the re-decomposition's dependency order)

Value namespaces (1 locators, 2 ops) land first — independent, additive, each
self-verifies. The function-reference namespace + author-declaration (3) lands
BEFORE the hook (4) and statement (5) verbs that accept a `kcdx.functions.*`
reference value — its own declare + PDB tests run at step 3; the cross-plugin
hook-by-reference test fires at step 4 where the reference-accepting hook exists.
The hook split (4) REPLACES `lua_bind_hook.cpp` AND migrates the existing hook test
plugins in the SAME commit (suite stays green). Statement (5) consumes 1+2+3.
Trampoline (6) is independent (the single-target tests of 4/5 don't need it; it is
required at TC scale). C++ parity (7) follows the settled Lua shapes. Each step ends
buildable + its same-change test runnable.

## Verification gate (whole phase)

- **The suite stays green at EVERY step** (the re-decomposition's load-bearing
  property): no step ships a red suite. Step 4 migrates the existing hook plugins
  (`cap-03`, `cap-04`, `cap-20`, `cap-21`, `cap-22`) to the sub-verb shape in its own
  commit — suite stays 21/21 (+ the new rows) green throughout.
- **`cap-NN-plugin-fn-declare`** — plugin A declares a function via `kcdx.dll.declare`;
  plugin B (depending on A) hooks it by name and the hook FIRES — cross-plugin access
  without disassembly (the extensibility proof). Declare machinery + resolvability at
  step 3; the cross-plugin hook fires at step 4.
- **`cap-NN-pdb-autoload`** (step 3) — a PDB-shipped plugin's non-exported internal
  resolves to its address; a static `replace_with_noop` applies — PDB-sourced
  addresses work without declaration.
- **`cap-NN-statement-replace`** (step 5) — a `kcdx.statement.replace_with(...)`
  produces ZERO per-call Lua dispatch (verified by absence of dispatch log lines
  during a tight loop hitting the target).
- **Full Lua+C++ parity** (`.claude/rules/lua-api-surface.md`) — every capability
  reachable + tested from BOTH surfaces (step 7's C++ interfaces + their C++ test
  plugins).
- Confirmed by the user's launch + the agent's `kcdx-dev.log` read at each step.
