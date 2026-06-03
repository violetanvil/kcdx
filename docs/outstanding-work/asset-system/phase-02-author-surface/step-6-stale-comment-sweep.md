# Phase 2 step 6 — stale-comment sweep (research/design prose on dotted `__index`)

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 6.

## What

Correct the stale prose that misstated how dotted dynamic resolution works. During
the asset-system design, research/design notes (and any rule prose implying
cross-plugin dotted access is impossible) obscured that `kcdx.hook.<name>` resolves
dotted segments dynamically via a chained `__index` smart-resolver — briefly
producing a wrong "you can't dereference a namespace with dots in Lua" conclusion
(design §10.2). This step sweeps and corrects that prose so the next reader is not
misled. Scope: the stale prose that misstates dotted dynamic resolution — NOT the
production binder comments (`src/lua_bind_hook.cpp` ~1106), which are CORRECT and
document the mechanism well.

## Scope

- Grep the research/design layer + `.claude/rules/` prose for claims that dotted
  dynamic resolution (`kcdx.hook.<name>`, and the new `kcdx.plugin.<a>.<p>.*`) does
  not work / is impossible / needs a quoted string; correct each to state it
  resolves via `__index` metamethods against engine-side data, citing the as-built
  resolver (step 5 just proved it live).
- Targets are prose only (research notes, rule/doc prose) — the production binder
  comments stay (they are correct). A private-tree note may reference anything; a
  public-facing doc correction must state the fact self-contained
  (`public-private-boundary.md`).
- Distinct concern from step 5 (correcting prose vs building a resolver) — its own
  commit per `no-monolith.md`.

## Test bar

This is a doc/prose correction, not a code behavior — its verification is a
re-grep: no surviving prose claims dotted dynamic resolution is impossible, and the
corrected statements read true against the as-built step-5 resolver. No test plugin
(no behavior changed). A `step-review`/`code-review` confirms the corrected prose
matches the verified mechanism (`.claude/rules/spec-conformance.md` — the claim
matches the as-built, not an intended, resolver).

## Dependencies

**Step 5** (the `__index` chain — the sweep corrects prose to match the AS-BUILT,
verified resolver, not a planned one). Ordered after step 5 so the correction cites
a mechanism that exists and was proven (`.claude/rules/incremental-delivery.md`).

## Reference

Design authority: [`../../../design/asset-replacement.md`](../../../design/asset-replacement.md)
§10.2 (the stale-comment sweep deliverable). Shared spec: [`../plan-spec.md`](../plan-spec.md).

## Disassembler-test / author-burden

None — a prose correction adds no author-facing surface.
