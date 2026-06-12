# Phase 9.5 / P3 — the engine catalog

The `kcdx.behavior.*` tier: plain `.lua` files loaded as a builtin pack through a
catalog-aware loader path, then the shipped entries. Design:
[`../behavior-design.md`](../behavior-design.md) §7.

## Step ledger (step-grain)

| Step | Status | Commit |
|---|---|---|
| [1 — the catalog loader path](step-1-catalog-loader.md) | DONE | (landed) |
| [2 — the shipped entries (5–10)](step-2-shipped-entries.md) | NOT STARTED | — |

## Phase verification gate

Build green + the catalog fixture legs GREEN in a live launch: the pack loads
ahead of every user plugin (the pin-ahead observation from P1 s1 holding live);
entries stamp under `kcdx.behavior.*`; an early-stop set on a catalog name
resolves; a malformed catalog file surfaces the builtin-pack boot error; every
shipped entry declares cleanly AND applies against the live binary (its effect
observed via the entry's own §14 row); `list("kcdx.")` returns the catalog. The
user-facing acceptance: the canonical entry's in-game effect confirmed in the
phase launch.
