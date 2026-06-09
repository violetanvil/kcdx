# Phase 9.3 step 3 — `kcdx.functions.*` + `kcdx.dll.declare` + PDB auto-load

**Status: NOT STARTED.** Ledger rows: [`README.md`](README.md) → 3a, 3-probe, 3b.

**Split (2026-06-09, user-settled) — three ordered items.** The PDB-autoload half
rests on an unverified runtime mechanism (does `DbgHelp` `SymEnumSymbols` enumerate a
foreign plugin DLL's *non-exported internal* functions from a release-build sidecar
`.pdb`?). Per results-driven.md it is probed before built:
- **3a** — `kcdx.functions.*` reference namespace + `kcdx.dll.declare` (deterministic:
  DB read + author-declared map; no runtime unknown). Lands now; fully unblocks
  steps 4/5 with the reference-value type.
- **3-probe** — a minimal DbgHelp probe plugin shipping a `.pdb` with a known
  non-exported function; proves `SymEnumSymbols` enumerates it. Outcome→meaning:
  internal resolves → 3b buildable as designed; does NOT → re-design 3b's
  internal-address source before building (surface to user).
- **3b** — `plugin_pdb.{cpp,h}`, built ONLY after 3-probe confirms.

**Probe RESULT (2026-06-09) — 3b is buildable as designed, with one constraint.**
A falsify-then-confirm pair settled it (full finding:
`_research/probe-archive/pdb-autoload-symenum-internals.md`):
- 3-probe (default `/DEBUG` → FASTLINK PDB): Outcome B — `SymEnumSymbols` did NOT
  surface the plugin's own non-exported internal from the deployed PDB.
- 3-probe-2 (explicit `/DEBUG:FULL` PDB): Outcome A — `SymEnumSymbols` DID surface
  it (name + a real loaded VA). Only the PDB link-flag changed between the two.

So PDB-autoload-for-internals WORKS, but ONLY with a `/DEBUG:FULL` (self-contained)
PDB. A FASTLINK PDB (the VS2017+ default) is a build-machine-OBJ-indexing stub that
carries no private symbols when deployed — `SymLoadModuleEx` succeeds, the enumerate
returns CRT-privates only, the author's own internals are silently absent. **3b's
graceful-fallback path MUST therefore detect the FASTLINK/stub case and emit a
teaching log line** ("plugin X ships a FASTLINK PDB; rebuild with /DEBUG:FULL for
internal-function auto-load; falling back to exports + declared functions") — NOT
just the "no PDB → exports-only" fallback. Document the `/DEBUG:FULL` requirement in
the author-facing PDB-autoload doc. The "every internal, zero-friction" promise holds
for a FULL PDB; the constraint is "ship a FULL PDB," not "no auto-load."

## What

The author-self-declaration keystone — the proof a TC author extends without an
engine release. The `kcdx.functions.*` reference namespace + the `kcdx.dll.declare`
verb + PDB auto-load. A new, ADDITIVE surface (new files, new namespace; nothing
replaced). Lands BEFORE the hook (step 4) / statement (step 5) verbs that accept a
`kcdx.functions.*` reference value — so the consuming verbs are buildable against a
real reference type when they land.

## Scope

`src/lua_bind_functions.cpp`, `src/plugin_pdb.{cpp,h}` (new files):

- **`kcdx.functions.*` reference namespace — two structurally-disjoint populations:**
  - **Game-DLL functions, no-dot stem, from the reference DB.**
    `kcdx.functions.WHGame.IsInCombat` (stem = DLL filename minus extension).
    Eager-populated at engine startup from `refdb`; hash-tracked across game versions.
    `kcdx.functions.by_id[N]` is the stable-across-versions ID accessor (game-only).
  - **Plugin-DLL functions, dotted `<author>.<plugin>` stem, from the author.**
    `kcdx.functions["redmoon.outfit_mod"].SomeFn` (bracket-indexed — the stem has
    dots). AUTHOR-OWNED source — NOT through the reference DB, NOT hash-tracked.
  - The dotted-vs-undotted stem is structurally disjoint (game DLLs never have dotted
    stems) → the two populations never collide.
- **`kcdx.dll.declare(plugin_namespace, function_map)`** — the author declares their
  DLL's functions with signatures COPIED FROM THEIR OWN SOURCE (no disassembly).
  Populates `kcdx.functions["<author>.<plugin>"].*`. The primary, cornerstones-clean
  path. The `<author>.<plugin>` namespace is derived per
  `.claude/rules/naming-namespaces.md` (the engine stamps it; the author types the
  bare names).
- **PDB auto-load** (`src/plugin_pdb.{cpp,h}`, `DbgHelp` `SymLoadModuleEx` +
  `SymEnumSymbols`, links `dbghelp.lib`): a sidecar `.pdb` populates EVERY internal
  function's address (not just exports). Graceful fallback — no PDB → exports-only;
  stale/mismatched PDB → GUID/age mismatch detected, logs a teaching line, falls back
  to exports-only. Purely additive (an author who ships no PDB loses nothing).
- **C export table** — `GetProcAddress` for the address (address only, no signature).
- The function-reference value carries: address (+ for declared/DB functions, the
  verified signature). A callback hook needs the signature from a non-binary source
  (the DB, `kcdx.dll.declare`, or the consumer's own RE — a property of compiled C++,
  not a kcdx limit); a static op needs only address + range.

## Test bar (runs AT this step)

A `test-plugins/cap-NN-plugin-fn-declare/` (suite-gated, the declare half): plugin A
calls `kcdx.dll.declare("a.test", {DeclaredFn = {signature = "..."}})`; the declared
function is RESOLVABLE — `kcdx.functions["a.test"].DeclaredFn` returns a reference
value carrying the address + the declared signature (a row that FAILS if the declare
didn't populate the namespace or the resolved reference lacks the signature). *The
cross-plugin HOOK-and-FIRE half rides step 4* (the reference-accepting hook exists
there) — this step proves the declaration + resolution machinery.

A `test-plugins/cap-NN-pdb-autoload/` (suite-gated): a test plugin ships a `.pdb`;
`kcdx.functions["test.pdbmod"].InternalNonExportedFn` resolves to a NON-exported
function's address (a row that FAILS if the PDB internal didn't resolve, or if the
graceful-fallback path silently dropped on a present PDB). PROBE Q silent.

## Dependencies

Phase 9.1 (the reference DB + `refdb` — the game-DLL eager population reads it —
DONE). Independent of steps 1/2.

## Design authority

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "The `kcdx.functions.*`
reference namespace" + "How plugin-DLL functions get into the namespace (three
sources)" + "The signature is the one irreducible thing" +
`.claude/rules/naming-namespaces.md` (the `<author>.<plugin>` stamping). Build to
those §s, not this summary.

## Disassembler-test / author-burden note

`kcdx.dll.declare` is the disassembler test's strongest case: the author declares
from their OWN source (they have the types), no Ghidra. The consumer hooks by name,
no disassembly. PDB auto-load makes static ops on internals zero-friction. No author
hex on the common path. No new game-DLL DB rows (the game-DLL population reads
existing reference-DB rows; plugin functions are author-owned, not DB-tracked).

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "The `kcdx.functions.*`
reference namespace + the game-DLL vs plugin-DLL distinction".
