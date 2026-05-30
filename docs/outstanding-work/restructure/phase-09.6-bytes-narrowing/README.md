# Phase 9.6 — `kcdx.bytes` narrowing + Lua-API rule update + final migration

**Status: NOT STARTED.** Detail: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.6".

The cleanup phase. Narrows `kcdx.bytes` to its post-9.3 remit (no overlap with
`kcdx.statement.*`), rewrites the design rule that governs the surface, migrates
remaining call sites, and lands the tiered author-model front door + the
cross-plugin extensibility guide. Also ships the empowered C++ wrapper for
`kcdx.bytes` (bytes' next unshipped phase).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| [1 — narrow `kcdx.bytes` remit + final call-site migration](step-1-bytes-narrow-and-migrate.md) | NOT STARTED | — |
| [2 — `lua-api-surface.md` rule 4 / 4a rewrite](step-2-rule-update.md) | NOT STARTED | — |
| [3 — docs: tiered front door + `extensibility.md`](step-3-docs.md) | NOT STARTED | — |
| [4 — empowered C++ `kcdx::bytes::Replace` wrapper + test](step-4-cpp-bytes-wrapper.md) | NOT STARTED | — |

## Verification gate (whole phase)

Full suite green; no plugin uses old surface forms; rule 4 + 4a documented;
`kcdx.bytes` narrowed-remit doc landed; `docs/lua/index.md` leads with the tier
model; `docs/lua/extensibility.md` exists and covers both directions;
`kcdx.dll.declare` + `kcdx.functions.*` per-call docs landed; every shipped
capability has its per-call `docs/lua/` and `docs/cpp/` entry.
