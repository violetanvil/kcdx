# Phase 2 step 8 — `kcdx.assets.*` Lua surface (reference / publish / register / replace)

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 8.

## What

The `kcdx.assets.*` Lua surface — the author's code-side entry to the asset system
(design §5). Five verbs: `get_by_path(path)` (resolve own asset → loadable path,
US-2), `get_by_name(name)` (resolve own published asset, US-5), `replace(target,
with)` (runtime replacement, US-6), `declare(name, file)` (runtime publish, US-5),
`register(vpath, file)` (runtime add, US-6). Cross-plugin reference uses the step-6
navigable namespace (`kcdx.plugin.<a>.<p>.assets.get_by_path(...)`); the string-key
form `kcdx.assets.replace("author.plugin.asset", with)` is the equivalent for a
dynamic/quoted key the dotted form can't express (design §6). Own-asset calls take
no owner prefix (the engine knows the calling plugin).

## Scope

- `src/lua_bind_assets.cpp` (NEW unit, `no-monolith.md` / `structure-by-responsibility.md`):
  the `kcdx.assets.*` domain sub-table (a grouped capability domain, `lua-api-surface.md`
  rule 3) with the five sub-verbs; required args positional. Each verb returns a
  loadable path (resolution) or applies (register/replace/declare); a path to a
  file not in the plugin's `assets/`, or a non-existent published name, returns a
  teaching error naming the missing thing (AP14).
- Wire the `.assets.*` leaf on the step-6 plugin handle (cross-plugin form).
- Register the table in `RegisterKcdxTable` (`src/lua_bind.cpp`).
- **Docs move with the surface** (`docs-discipline.md`): each verb ships its
  `docs/lua/` entry (call shape, args, return, error behavior, runnable snippet) +
  any new glossary term + a matrix row, SAME step. The C++ mirror's `docs/cpp/`
  entry lands in step 9 (or a NYI marker here per `docs-discipline.md`).

## Test bar

A `cap-NN` suite-gated test plugin (`test-suite.md`): a plugin resolves its own
asset (`get_by_path` returns a loadable path), references another mod's published
asset via the namespace (`kcdx.plugin.<a>.<p>.assets.get_by_name` resolves),
publishes (`declare`) and registers (`register`) at runtime — each a falsifiable
claim (FAILS if the verb returns nil/a wrong path, AP15); a missing asset yields the
teaching error. Build green; live-confirmed via the launch (`acceptance-signal.md`).

## Dependencies

**Step 6** (the navigable namespace — the cross-plugin `kcdx.plugin.<a>.<p>.assets.*`
form resolves through it) and **Phase 1 steps 3 + 4** (HOOK 1 + HOOK 2 — the
two-hook seam that resolves AND serves; what `get_by_path`/`get_by_name` hand back
is a path/handle the seam serves). Ordered after the seam + the namespace so the
verbs return something that actually resolves and serves
(`.claude/rules/incremental-delivery.md`).

## Reference

Design authority: [`../../../design/asset-replacement.md`](../../../design/asset-replacement.md)
§5 (the surface table) + §6 (the string-key escape) + §3 US-2/US-3/US-5/US-6.
Shared spec: [`../plan-spec.md`](../plan-spec.md).

## Disassembler-test / author-burden

The author calls `kcdx.assets.get_by_path("icons/my_icon.dds")` — a path, no engine
internal, no asset class, no RVA (the disassembler test, `cornerstones.md`). Errors
teach (name the missing path/name, AP14). Reads idiomatically per `lua-api-surface.md`.
No hand-written hex/ABI.
