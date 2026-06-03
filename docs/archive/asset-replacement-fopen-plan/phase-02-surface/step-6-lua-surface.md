# Step 6 — `kcdx.assets.*` Lua surface + docs

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 6.

## What

The `kcdx.assets.*` Lua verbs: `get_by_path(path)` / `get_by_name(name)` (resolve
an asset — own by bare path/name — to a game-loadable path), `replace(target,
with)` (runtime replacement), `declare(name, file)` (runtime publish), `register(
vpath, file)` (runtime-add an asset not in `assets/` at load). Cross-plugin: these
resolve off a step-5 plugin handle (`kcdx.plugin.<a>.<p>.assets.get_*`). Each verb
ships its `docs/lua/` entry + glossary term in this step.

## Scope

- A `kcdx.assets` domain sub-table (`lua-api-surface.md` rule 3); discrete verbs
  as listed (rule 4a sub-verbs); required args positional (rule 4).
- `get_by_path`/`get_by_name` return a game-loadable path (the path form settled
  by step-1's probe + step-2/4's resolution); a missing asset → teaching error.
- `replace`/`declare`/`register` are the programmatic equivalents of the step-3
  sidecar — feed the same overlay-map / published-name registries.
- Lua bridge: raw Lua C API, no static sentinel (`lua-bridge.md`, AP5); a returned
  path is a string (no `lua_Number` pointer-precision issue — `lua-precision.md`).
- `docs/lua/assets.md` per-call entries + `docs/lua/index.md` map row + glossary
  (`docs-discipline.md`); the C++ side gets its NYI mirror entry here, removed when
  step 7 lands.

## Test bar

Exercised at step 8. This step's own check: each verb is callable + registered
(verify the binder registers it, not just a doc example — `lua-api-surface.md`);
`get_by_path` on a known own asset returns a loadable path; `register` then
`get_by_path` resolves the registered asset. Falsifiable: a missing asset errors.

## Dependencies

**Steps 2–4** (the resolution + overlay/published-name registries these verbs read
+ write); **step 5** (the navigable namespace these resolve off for cross-plugin).
Ordered after — every verb is exercisable when it lands. (If step 5 cannot be
exercised without these verbs, merge 5+6 — see the plan's note.)

## Disassembler-test / author-burden

CLEAN — every verb takes a path or a name the author knows; no hex/offset/ABI.

## UX

Author-facing Lua surface (not UI). The "states" are the verb contracts +
teaching errors: a missing asset/plugin → named error; a successful resolve →
a usable path. Errors teach (file/name + why). Carried from design §5.

## Reference

[`../plan-spec.md`](../plan-spec.md); design authority
[`../../restructure/phase-08.5-asset-replacement/asset-design.md`](../../restructure/phase-08.5-asset-replacement/asset-design.md)
§5 (the verb table + shapes) + §3 (US-2/3/5/6 acceptance). Build to §5, not to this
summary.
