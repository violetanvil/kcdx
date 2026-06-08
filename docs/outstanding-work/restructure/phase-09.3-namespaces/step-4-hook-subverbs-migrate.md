# Phase 9.3 step 4 — `kcdx.hook.*` sub-verb split + migrate the existing hook plugins (same commit)

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 4.

## What

Split `kcdx.hook` from mode-as-key (the Phase 2 shape) into mode-as-sub-verb —
callback-based interception, the industry-standard meaning of "hook" (per-call cost;
use when per-call Lua logic is needed). This step REPLACES `src/lua_bind_hook.cpp`
AND, in the SAME commit, migrates every existing hook test plugin to the new shape —
so the suite never goes red (the load-bearing re-decomposition fix; the old shape and
its consumers are deleted and replaced atomically, per
`.claude/rules/incremental-delivery.md`).

## Scope (`src/lua_bind_hook.cpp`, replaces existing — atomic with the migration)

- `kcdx.hook.before/after/around/replace(module, target, [locator], callback,
  [opts])`.
- `kcdx.hook.insert_before/insert_after(module, target, locator, callback, [opts])`
  — locator REQUIRED (no meaningful default for "insert before what?").
- `module` is the required positional first arg (no default — the phase rule).
- `target` accepts a canonical name / stable kcdx ID / Ghidra auto-name (Phase 9.1
  resolution) OR a `kcdx.functions.*` reference value (step 3). The engine dispatches
  by arg-1 type.
- `insert_before/after` callback receives captures as a named table; returning a
  table writes the named captures back to registers/memory (same shape as `before`
  mode's return flow). The captures-by-name thunk is built ONCE at install —
  native-speed register-to-table copies per call, no per-call lookup, no per-call
  SQLite query (`.claude/rules/memory.md` hot-path; `.claude/rules/logging.md` no
  per-call log).
- **Migrate the existing hook test plugins IN THIS COMMIT** (the suite-green
  invariant): `cap-03-hook-lua-callback`, `cap-04-midhook`, `cap-20-hook-modes`,
  `cap-21-mid`, `cap-22-callsite` (+ any other consumer of the old mode-as-key shape)
  → the new sub-verb shape. The old shape is removed and every caller is on the new
  API in the SAME deliverable (the CLAUDE.md "grep every caller, change atomically"
  rule).
- Survivor sweep (`.claude/rules/deletion-hygiene.md`): `docs/lua/hook.md` + any rule
  / CLAUDE.md prose describing the mode-as-key shape → repointed to the sub-verb
  shape; `docs/lua/` gets the per-sub-verb entries + glossary.

## Test bar (runs AT this step — suite stays GREEN)

The migrated `cap-03/04/20/21/22` PASS on the new sub-verb shape (the suite stays
21/21 — the migration is in this commit, so no red window). Each sub-verb
(`before/after/around/replace/insert_before/insert_after`) FIRES (a row per sub-verb
that FAILS if it never fires). **The cross-plugin keystone fires here:** the
`cap-NN-plugin-fn-declare` cross-plugin half — plugin B hooks
`kcdx.functions["a.test"].DeclaredFn` (declared by plugin A at step 3) BY NAME and
the hook FIRES (a row that FAILS if the reference-form hook doesn't resolve/fire) —
cross-plugin access without disassembly, the extensibility proof. PROBE Q silent.

## Dependencies

Step 1 (locators — `insert_before/after` require a locator value) + step 3 (the
`kcdx.functions.*` reference value the hook accepts; the cross-plugin test's declared
function). The migrated plugins' targets resolve via the existing Address Library /
reference DB (Phase 9.1, DONE).

## Design authority

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "`kcdx.hook.*` —
callback-based interception" + "`insert_before/insert_after` callback signature" +
the verification gate's "every existing hook test plugin migrates… suite stays 21/21
green". `.claude/rules/hook-engine.md` (the chain install path) +
`.claude/rules/lua-api-surface.md` (sub-verbs not table-keys, rule 4a). Build to
those, not this summary.

## Disassembler-test / author-burden note

`target = "<name>"` or a `kcdx.functions.*` reference resolves address AND ABI; the
author writes a name, never a signature (`.claude/rules/cornerstones.md`, AP12).
`module` required-positional is the honest multi-DLL surface, not a hex burden. No
new DB rows (targets resolve through the existing Address Library).

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "The two distinct
namespaces" + "`insert_before/insert_after` callback signature" + "Verification gate".
