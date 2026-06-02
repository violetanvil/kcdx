# Step 5 — navigable `kcdx.plugin.<author>.<plugin>.*` namespace (`__index` resolvers)

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 5.

## What

Give `kcdx.plugin` (today a plain function table) an `__index` metamethod that
resolves the `<author>` segment to a resolver, whose `__index` resolves the
`<plugin>` segment to a plugin handle, on which `.assets.*` (this plan) and future
cross-plugin surfaces resolve. Each dot is a resolution hop against engine-side
namespace data — so a cross-plugin reference reads as native dotted Lua
(`kcdx.plugin.redmoon.outfit_swap.assets.get_by_path("…")`), no quoted-namespace
ceremony, all under the one `kcdx` global. A GENERAL primitive (not asset-only),
the established `kcdx.hook.<name>` `__index` pattern extended.

## Scope

- Add the chained `__index` resolvers to `kcdx.plugin` + its returned per-author /
  per-plugin handles (mirror the `kcdx.hook` smart-resolver shape in
  `lua_bind_hook.cpp` / `lua_bind_plugin.cpp`).
- Resolution honors self > engine > other (`naming-namespaces.md`); a non-existent
  plugin / asset resolves to a teaching error, not nil-surprise (AP14).
- This step lands the namespace SKELETON + its asset accessor wiring; the asset
  verbs themselves (`get_by_path` etc.) are step 6 (a handle's `.assets` resolves
  to the step-6 surface, scoped to the named plugin).
- Lua bridge discipline: raw Lua C API, no static-const sentinel (`lua-bridge.md`,
  AP5).

## Test bar

Exercised at step 8 (a cross-plugin reference resolving). This step's own check: a
test resolves `kcdx.plugin.<a>.<p>` to a handle and reads a known asset off it
(once step 6's accessor lands — if step 6 is ordered after, this step's check is a
handle-resolves probe + the full cross-plugin read is step 6's). State the
falsifiable claim: a bad `<a>.<p>` errors; a good one returns a handle.

## Dependencies

The landed namespace model (`naming-namespaces.md`) + the overlay/published-name
registries (steps 2–4 — what a handle's `.assets` reads). The asset accessor it
exposes is completed by **step 6**; if a handle's `.assets.get_*` can't be
exercised until step 6, this step + step 6 are tightly coupled — consider whether
step 5 lands the resolver and step 6 lands the verbs it dispatches to (verifiable
together at step 6). Reorder into one step if the split leaves step 5 unexercisable
(`.claude/rules/incremental-delivery.md`).

## Disassembler-test / author-burden

CLEAN — the author writes a plugin namespace they know (`redmoon.outfit_swap`),
bare-dotted; the engine resolves it. No hex.

## Reference

[`../plan-spec.md`](../plan-spec.md); design authority
[`../../restructure/phase-08.5-asset-replacement/asset-design.md`](../../restructure/phase-08.5-asset-replacement/asset-design.md)
§6 (the navigable namespace mechanism). Build to §6, not to this summary.
