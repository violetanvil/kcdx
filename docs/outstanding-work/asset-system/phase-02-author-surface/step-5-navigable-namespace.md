# Phase 2 step 5 — navigable namespace `kcdx.plugin.<author>.<plugin>.*` (`__index` chain)

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 5.

## What

The general cross-plugin navigation primitive: `kcdx.plugin` gains an `__index`
metamethod resolving the `<author>` segment to an author resolver, whose `__index`
resolves the `<plugin>` segment to a plugin handle, on which `.assets.*` (and any
future cross-plugin surface) resolves — each dot a resolution hop against
engine-side namespace data (design §6). This is the SAME chained-`__index`
mechanism `kcdx.hook.<name>` already uses (verified in `src/lua_bind_hook.cpp`),
NOT a new architecture. It reads as native dotted Lua
(`kcdx.plugin.redmoon.outfit_swap.assets.get_by_name("shirt")`), no quoted-namespace
ceremony, all under the one `kcdx` global. This is the US-3 enabler (the
`.assets.*` surface itself lands in step 7).

## Scope

- `src/lua_bind_plugin.cpp`: add the `__index` resolver chain on `kcdx.plugin` and
  its returned handles. ADDITIVE — the existing `is_rejected("author.plugin")`
  function-call surface stays (a query, not a navigation; design §6). Resolution
  honors self > engine > other (`naming-namespaces.md`).
- A non-existent author/plugin segment resolves to a teaching error (names what
  was not found), never a silent nil that surfaces confusingly downstream (AP14).
- This step ships the navigation primitive + a probe/test plugin proving the chain
  resolves; the `.assets.*` leaf it fronts is step 7 (a handle exposing
  `.assets.*` returns the surface step 7 builds — until then a handle resolves but
  `.assets` is the step-7 deliverable).

## Test bar

A `cap-NN` suite-gated test plugin (`test-suite.md`; permanent home coordinated
with step 9) navigates `kcdx.plugin.<author>.<plugin>` and asserts the `__index`
chain resolves each segment to the expected engine-side handle, and that a
non-existent segment yields the teaching error (a falsifiable claim — FAILS if a
bad segment returns a silent nil, AP15). Build green; live-confirmed via the launch.

## Dependencies

The landed plugin registry / namespace data (`kcdx.plugin.is_rejected` already
reads it). No prior step in THIS plan blocks it (it is the surface layer's first
step). Ordered before step 7 because the `kcdx.assets.*` cross-plugin form
(`kcdx.plugin.<a>.<p>.assets.*`) resolves THROUGH this chain
(`.claude/rules/incremental-delivery.md`).

## Reference

Design authority: [`../../../design/asset-replacement.md`](../../../design/asset-replacement.md)
§6 (the navigable namespace) + §3 US-3. Shared spec: [`../plan-spec.md`](../plan-spec.md).
The mirror mechanism: `src/lua_bind_hook.cpp` `__index` smart-resolver (~line 1106).

## Disassembler-test / author-burden

The author writes `kcdx.plugin.redmoon.outfit_swap.*` — plain dotted Lua, no engine
internal, no string-delimiter ceremony (design §6; `lua-api-surface.md`
surfaces-mirror). The string-key escape (`kcdx.assets.replace("a.p.asset", …)`) for
dynamic/quoted keys is step 7's surface. No hand-written hex/ABI.
