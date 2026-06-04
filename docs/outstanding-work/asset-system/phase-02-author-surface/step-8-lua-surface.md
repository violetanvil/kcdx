# Phase 2 step 8 — `kcdx.assets.get_by_path` (the pure-read verb) + the `kcdx.assets.*` table

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 8.

> **Scope narrowed 2026-06-04.** This step originally built all five
> `kcdx.assets.*` verbs. The four RUNTIME verbs (`get_by_name` / `declare` /
> `register` / `replace`) need a runtime store the v2 design left unspecified —
> a genuine unsettled-design gap surfaced at build time, confirmed by
> architect-review, settled by a focused consult in design §5.1 (2026-06-04).
> Per `incremental-delivery.md` + `spec-conformance.md`, this step now ships ONLY
> `get_by_path` (a pure read, depends on none of the store mechanism); the four
> runtime verbs move to [step 8b](step-8b-runtime-store-verbs.md), built against
> the settled §5.1 mechanism. The verb SHAPES were always settled (§5); only the
> store was the gap. This is dependency ordering, NOT a capability cut — end state
> is all five verbs at full Lua↔C++ parity.

## What

The `kcdx.assets.*` domain sub-table + the FIRST verb, `get_by_path(path)` — the
author's code-side read of their OWN asset → a loadable path (design §5, US-2).
`get_by_path` is a PURE READ: it resolves the calling plugin's identity + its
`assets/` root → a loadable disk path the HOOK-2 open already serves. It mutates
no store and depends on none of the §5.1 runtime store. The cross-plugin form
`kcdx.plugin.<a>.<p>.assets.get_by_path(...)` resolves through the step-6
namespace. Own-asset calls take no owner prefix (the engine knows the calling
plugin).

## Scope

- `src/lua_bind_assets.cpp` (NEW unit, `no-monolith.md` / `structure-by-responsibility.md`):
  the `kcdx.assets.*` domain sub-table (a grouped capability domain, `lua-api-surface.md`
  rule 3). Build `get_by_path` (required arg positional). It returns a loadable
  path; a path to a file not in the plugin's `assets/` returns a teaching error
  naming the missing path (AP14). **CHECKABLE FACT to resolve by reading code (not
  a design call):** confirm the calling-plugin-identity seam
  (`OwningPluginForCurrentCall` / the lua_registry owner resolution) exists and is
  callable from the Lua-bind layer; if absent, `get_by_path` gains a dependency
  and reorders behind that seam (`incremental-delivery.md`).
- Wire the `.assets.get_by_path` leaf on the step-6 plugin handle (cross-plugin
  form).
- Register the `kcdx.assets` table in `RegisterKcdxTable` (`src/lua_bind.cpp`).
- **The four runtime verbs ship as NYI doc entries + deliberately-failing matrix
  rows (design §5.2):** `get_by_name` / `declare` / `register` / `replace` each
  get a `docs/lua/assets.md` entry marked NYI (the planned shape) + a matrix row
  that FAILS until step 8b builds it — pinning the contract so it is not lost
  (`docs-discipline.md`, `test-suite.md`).
- **Docs move with the surface** (`docs-discipline.md`): `get_by_path` ships its
  `docs/lua/assets.md` entry (call shape, args, return, error behavior, a
  copy-paste-runnable snippet, common-path-first) + any new glossary term + a
  matrix row, SAME step. The C++ mirror's `docs/cpp/` entry lands in step 9 (an
  NYI marker here per `docs-discipline.md`).

## Test bar

A `cap-NN` suite-gated test plugin (`test-suite.md`): a plugin resolves its OWN
asset (`get_by_path` returns a loadable path) and the cross-plugin
`kcdx.plugin.<a>.<p>.assets.get_by_path` form resolves through the step-6
namespace — each a falsifiable claim (FAILS if the verb returns nil/a wrong path,
AP15); a path not in the plugin's `assets/` yields the teaching error (a row that
FAILS if a bad path returns a silent nil). The four runtime verbs carry
deliberately-failing rows (FAIL until step 8b — the contract pinned). Build green;
live-confirmed via the launch (`acceptance-signal.md`).

## Dependencies

**Step 6** (the navigable namespace — the cross-plugin `kcdx.plugin.<a>.<p>.assets.get_by_path`
form resolves through it) and **Phase 1 steps 3 + 4** (HOOK 1 + HOOK 2 — the
two-hook seam that resolves AND serves; what `get_by_path` hands back is a path
the seam serves). Ordered after the seam + the namespace so `get_by_path` returns
something that actually resolves and serves (`.claude/rules/incremental-delivery.md`).
The four runtime verbs are deferred to [step 8b](step-8b-runtime-store-verbs.md)
(they depend on the §5.1 store mechanism).

## Reference

Design authority: [`../../../design/asset-replacement.md`](../../../design/asset-replacement.md)
§5 (the surface table) + §5.2 (the build split — `get_by_path` first) + §6 (the
cross-plugin form) + §3 US-2/US-3. Shared spec: [`../plan-spec.md`](../plan-spec.md).

## Disassembler-test / author-burden

The author calls `kcdx.assets.get_by_path("icons/my_icon.dds")` — a path, no engine
internal, no asset class, no RVA (the disassembler test, `cornerstones.md`). Errors
teach (name the missing path, AP14). Reads idiomatically per `lua-api-surface.md`.
No hand-written hex/ABI.
