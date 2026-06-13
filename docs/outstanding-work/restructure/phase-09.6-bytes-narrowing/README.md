# Phase 9.6 — Lua-API rule update + tiered docs + C++ bytes wrapper

**Status: NOT STARTED.** Detail: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.6".

> **DESIGN REVERSAL (2026-06-12) — the `kcdx.bytes` narrowing is STRUCK.** A grounding pass + a cold architectural review overturned the original "narrow `kcdx.bytes` to non-function memory + reject function-internal targets + migrate to `kcdx.statement.replace_with`" plan on the evidence: `kcdx.statement.replace_with` emits only *named* ops (it cannot write an arbitrary byte string like cap-01's `45 31 F6`); cap-01's target is a `callsite` kind with no statement metadata (so `replace_with` can't even resolve it); EVERY existing `kcdx.bytes` site is function-internal and there are ZERO non-function bytes sites (the narrowing would reject 100% of the passing corpus for a region with no inhabitants); and forcing the migration is a UX regression to a dead end. **Corrected design:** `kcdx.bytes` STAYS the general low-level raw-byte-write primitive; `kcdx.statement.*` is the higher curated tier authors reach for by intent; the steer lives in docs (the tier ladder), never a binder reject. Full rationale: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.6" reversal note.

The cleanup phase. Rewrites the design rule that governs the surface, lands the
tiered author-model front door + the cross-plugin extensibility guide (incl. the
`docs/lua/bytes.md` tier-pointer up to `kcdx.statement.*`), and ships the
empowered C++ `kcdx::bytes::Replace` wrapper (bytes' next unshipped phase). No
`kcdx.bytes` binder change — the narrowing is struck.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| ~~[1 — narrow `kcdx.bytes` remit + final call-site migration]~~ | STRUCK | reversal 2026-06-12 — narrowing reversed; remnant (bytes.md tier-pointer) folded into step 3 |
| [2 — `lua-api-surface.md` rule 4 / 4a rewrite](step-2-rule-update.md) | DONE | (landed) — rules 4/4a already in final form (landed in the 2026-05-28 doc pass); no diff owed |
| [3 — docs: tiered front door + `extensibility.md` + bytes tier-pointer](step-3-docs.md) | DONE | (landed) |
| [4 — empowered C++ `kcdx::bytes::Write`/`TryWrite` wrapper + test](step-4-cpp-bytes-wrapper.md) | DONE | (landed) — wrapper+test shipped in 9.3/9.5 work; cap-63 live-verified 2026-06-13 (verb landed as Write/TryWrite, not Replace) |

## Verification gate (whole phase)

Full suite green; no plugin uses old surface forms; rule 4 + 4a documented;
`docs/lua/bytes.md` documents bytes as the low-level tier with a pointer up to
`kcdx.statement.*` (NOT a narrowed remit — bytes stays general); `docs/lua/index.md`
leads with the tier model; `docs/lua/extensibility.md` exists and covers both
directions; `kcdx.dll.declare` + `kcdx.functions.*` per-call docs landed; every
shipped capability has its per-call `docs/lua/` and `docs/cpp/` entry.
