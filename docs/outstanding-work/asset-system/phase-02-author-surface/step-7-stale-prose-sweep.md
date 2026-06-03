# Phase 2 step 7 — stale-prose sweep (dotted-`__index` prose + the falsified id-152 seed prose)

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 7.

## What

Correct TWO bodies of stale prose the design names (design §10.2):

1. **Dotted-`__index` prose.** During the asset-system design, research/design
   notes (and any rule prose implying cross-plugin dotted access is impossible)
   obscured that `kcdx.hook.<name>` resolves dotted segments dynamically via a
   chained `__index` smart-resolver — briefly producing a wrong "you can't
   dereference a namespace with dots in Lua" conclusion. Scope: the stale prose
   that misstates dotted dynamic resolution — NOT the production binder comments
   (`src/lua_bind_hook.cpp` ~1106), which are CORRECT.
2. **The falsified id-152 seed prose.** The `data/seeds/address_names_seed.csv` row
   for `CCryPak_AdjustFileName` (id 152) still asserts the v1 claim — "every by-name
   consumer (FOpen, …) calls `*(*pCryPak+8)` … the single chokepoint … independent
   of sys_pakPriority." The gated load-path map (`3193e84`) FALSIFIED this: FOpen
   does NOT call slot 1, and the resolver IS `sys_pakPriority`-gated (pak-only at
   the default). Correct the id-152 prose to the verified two-lane / two-hook model
   (slot 1 is the resolution DECISION; FOpen mints the handle independently; the
   seam is HOOK 1 + HOOK 2, not slot-1-alone). This is an AP19/AP2 correction — a
   falsified inference left in an authoritative record.

## Scope

- **Target 1:** grep the research/design layer + `.claude/rules/` prose for claims
  that dotted dynamic resolution (`kcdx.hook.<name>`, `kcdx.plugin.<a>.<p>.*`) does
  not work / is impossible / needs a quoted string; correct each to state it
  resolves via `__index` metamethods against engine-side data, citing the as-built
  resolver (step 6 just proved it live).
- **Target 2:** correct the id-152 row prose in `data/seeds/address_names_seed.csv`
  to the verified mechanism. (Editing the seed CSV is the working flow; the
  correction is prose-only — NOT a new entity/version row, so NOT AP18-gated; an
  UPDATE to an existing row's notes.) Re-run the DB rebuild round-trip if the
  importer derives anything from the prose (it should not — `notes` is commentary).
- Both targets are PROSE corrections (notes, rule prose, seed commentary) — no
  behavior changes. A public-facing doc correction states the fact self-contained
  (`public-private-boundary.md`); the id-152 prose lives in a private seed CSV.
- Distinct concern from step 6 (correcting prose vs building a resolver) — its own
  commit per `no-monolith.md`.

## Test bar

A doc/prose correction, not code behavior — verification is a re-grep + a review:
(1) no surviving prose claims dotted dynamic resolution is impossible, and the
corrected statements read true against the as-built step-6 resolver; (2) the
id-152 seed prose no longer asserts "FOpen calls slot 1 / single chokepoint /
independent of sys_pakPriority" and instead states the two-lane / two-hook model.
A `step-review`/`code-review` confirms each corrected claim matches the verified
mechanism (`.claude/rules/spec-conformance.md` — the claim matches the as-built /
gated-RE, not an intended, mechanism). No test plugin (no behavior changed).

## Dependencies

**Step 6** (the `__index` chain — target 1 corrects prose to match the AS-BUILT,
verified resolver). Target 2 (the seed prose) depends only on the already-gated
load-path research (`3193e84`), not on a build step. Ordered after step 6 so
target 1's correction cites a mechanism that exists and was proven
(`.claude/rules/incremental-delivery.md`).

## Reference

Design authority: [`../../../design/asset-replacement.md`](../../../design/asset-replacement.md)
§10.2 (the stale-prose sweep deliverable — its two targets). Shared spec:
[`../plan-spec.md`](../plan-spec.md). Gated RE that falsified target 2:
`_research/asset-loadpath-map-recon/` (commit `3193e84`).

## Disassembler-test / author-burden

None — a prose correction adds no author-facing surface.
