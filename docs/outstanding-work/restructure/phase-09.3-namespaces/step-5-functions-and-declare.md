# Phase 9.3 step 5 — `kcdx.functions.*` + `kcdx.dll.declare` + PDB auto-load

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 5.

## What

The `kcdx.functions.*` reference namespace — both hook and statement verbs accept
a function as EITHER a reference value (`kcdx.functions.<stem>.<name>`) OR a
`(module, target)` string pair. The reference form is the documented common path.

`kcdx.functions.*` carries two populations, the stem makes the source visible:
- **Game-DLL functions — no-dot stem, from the reference DB.**
  `kcdx.functions.WHGame.IsInCombat`. Eager-populated at startup; hash-tracked
  across versions (this is what the whole reference-DB apparatus exists for —
  WHGame is stripped/obfuscated). `kcdx.functions.by_id[N]` is the
  stable-across-versions accessor.
- **Plugin-DLL functions — dotted `<author>.<plugin>` stem, from the author.**
  `kcdx.functions["redmoon.outfit_mod"].SomeFn`. AUTHOR-OWNED source — NOT
  through the reference DB, NOT hash-tracked; cross-version is the plugin's own
  semver.

The two stem shapes are structurally disjoint, so they never collide.

## Three sources for plugin-DLL functions (descending author cooperation)

1. **`kcdx.dll.declare(plugin_namespace, function_map)`** — the author declares
   their DLL's functions with signatures COPIED FROM THEIR OWN SOURCE. No
   disassembly. Primary path; the cornerstones-clean answer to "expose my
   internals for other mods to extend."
2. **PDB auto-load** (`src/plugin_pdb.{cpp,h}`, `DbgHelp` `SymLoadModuleEx` +
   `SymEnumSymbols`, links `dbghelp.lib`) — a sidecar `.pdb` populates EVERY
   internal function's address (not just exports), making static ops on any
   internal zero-friction (address from PDB; static ops need no signature).
   Graceful fallback on missing/mismatched PDB (GUID/age check → exports-only +
   log line). Purely additive.
3. **C export table** — `GetProcAddress`; address only, no signature.

## The signature is the one irreducible thing

A callback hook that marshals args needs the ABI; compiled C++ carries no
runtime-queryable signature. So a callback hook ALWAYS needs the signature from a
non-binary source: the reference DB (game functions), `kcdx.dll.declare` (plugin
functions), or the consumer's own RE. Static byte ops need only address + range —
no signature. This is a property of compiled C++, not a kcdx limitation.

## Files

`src/lua_bind_functions.cpp` (the namespace + the `kcdx.dll.declare` verb),
`src/plugin_pdb.{cpp,h}`.

## Test bar

`cap-XX-plugin-fn-declare`: plugin A declares a function via `kcdx.dll.declare`;
plugin B (depending on A) hooks `kcdx.functions["a.test"].DeclaredFn` and the hook
fires — cross-plugin access without disassembly. `cap-XX-pdb-autoload`: a
PDB-shipped plugin's non-exported internal resolves; a static `replace_with_noop`
applies.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "The
`kcdx.functions.*` reference namespace" + the cross-plugin extension table.
