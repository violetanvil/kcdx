# Phase 9.5 — `kcdx.behavior.*` named-behavior catalog (two-tier)

**Status: NOT STARTED.** Detail: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.5".

The simple-modder surface. Author writes one line; never sees a function name,
statement, or op. Two tiers coexist through the same `set`/`get`/`list`/`declare`
verbs: engine-shipped (reserved `kcdx.behavior.*` namespace) and plugin-declared
(standard `<author>.<plugin>.<bare>` namespace). This is the contribution surface
that scales the simple-modder UX organically — each TC plugin grows the named-behavior
pool for downstream consumers.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| [1 — `kcdx.behavior.*` verbs (declare/set/get/list)](step-1-behavior-verbs.md) | NOT STARTED | — |
| [2 — engine-shipped catalog (5–10 entries)](step-2-engine-catalog.md) | NOT STARTED | — |
| [3 — test plugin (engine + cross-plugin behaviors)](step-3-test.md) | NOT STARTED | — |

## Verification gate (whole phase)

`kcdx.behavior.set("test_behavior", true)` against a catalog entry → underlying
byte rewrite applies. A behavior-only plugin without `authored_against_game_version`
still loads (exempt). `kcdx.behavior.list()` returns engine + plugin behaviors;
`list("redmoon.")` filters. Cross-plugin: plugin A declares `a.test.foo`; plugin B
calls `set("a.test.foo", "value")`; A's implementation fires with the value.
