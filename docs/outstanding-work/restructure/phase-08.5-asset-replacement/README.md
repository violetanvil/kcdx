# Phase 8.5 — asset replacement (kcdx absorbs pak mods)

**Status: NOT STARTED** (8.5a partial). Detail: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 8.5" + §"kcdx replaces pak mods".

This phase is **OVERLAY**: a kcdx plugin shipping a single loose file that
OVERRIDES a pak-resident asset by virtual path. (The pak-mod ABSORB path —
landing pak mods into MOUNT verbatim — is a SEPARATE, already-complete feature,
NOT this phase.) One `/feature` cycle; independent of the other live phases; high
user-visible leverage.

After this phase, `pak-mods.md` is rewritten to "pak mods are deprecated; use
`[entrypoints].assets`". Existing pak mods keep working; kcdx is the path forward
for new TC asset work.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| [1 — hook the game's pak resolver (production overlay hook)](step-1-pak-resolver-hook.md) | PARTIAL — resolver named + observe-only probe; production hook not installed | — |
| [2 — parse `[entrypoints].assets` + build overlay map](step-2-parse-assets-overlay-map.md) | NOT STARTED | — |
| [3 — overlay-map check in the resolver hook](step-3-resolver-overlay-check.md) | NOT STARTED | — |
| [4 — `kcdx.assets.*` Lua surface + `kcdxAssetInterface`](step-4-assets-surface.md) | NOT STARTED | — |
| [5 — `cap-XX-asset-replace` test plugin](step-5-test-plugin.md) | NOT STARTED | — |

## Verification gate (whole phase)

A TOML-only plugin with `[entrypoints].assets = "assets/"` containing a
known-safe replacement (e.g. a UI string in a menu) loads; the in-game UI shows
the replacement; the engine log emits the overlay-hit line; a second plugin
replacing the same file gets a "lost to plugin X" log line per the existing
conflict-report shape.
