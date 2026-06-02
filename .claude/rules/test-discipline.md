---
paths:
  - "**/*"
---

# Test discipline — every implemented item has a test, same change

The test-bar and the exempt taxonomy a repo applies to decide what gets tested vs. legitimately excused. This rule is the language-agnostic floor; a repo names its concrete coverage tool, test layout, and build-gate layers in the relevant append. The exempt-registry mechanism (tree shape, schema, immutability, gate) is SYSTEM — `.claude/rules/exempt-registry.md` — not the repo's; only the coverage-tool fill is. The reviewer-side application of the bucket framework lives in `.claude/skills/_shared/architectural-review.md` §"Step 0" (cited, not restated); the AP it backstops is `.claude/rules/anti-patterns.md` §"bucket self-designation".

## The bar

**Every implemented item has a test, in the SAME change.** No grandfathering, no skip/ignore markers, no agent-controlled exemption. An implemented item without its test is incomplete — the same completeness standard a UX section or a decision record carries. `panic`/`assert`/`unwrap`/force-unwrap and equivalents are permitted ONLY in test code.

A change to behavior the test suite already covers updates that test in the same change. New behavior gets a new test that exercises the actual path end-to-end (not a wrapper around it — that is the extract-then-test-wrapper anti-pattern, `anti-patterns.md`).

## The coverage universe — first-party source only

The discipline judges **first-party source the repo authors** — nothing else. A dependency's interior lines, a vendored third-party tool's source, and code generated into the build are OUTSIDE the universe by definition: the repo did not write them, no bucket disposition can apply to them (you cannot author a bucket-1b source-fix or a bucket-2/3 exempt for code you do not own), and a coverage tool that attributes an uncovered region back to such a path reports a region the discipline never asked about.

The universe is **inclusion-based**: the repo declares its first-party source roots (named in the relevant append — the same roots the coverage gate already scans); a region is in scope ONLY if its normalized path is under a declared root. Inclusion is closed by default — a new dependency-cache location, a new vendoring scheme, or a new codegen output dir can never silently enter the universe, because it is not under a declared root. Scoping by excluding known dependency markers is the inverse failure (open by default: the next unanticipated path leaks back in); declare what is yours, not what is not.

This is the boundary the gate mechanism encodes — the coverage tool's uncovered set is scoped to the declared first-party roots BEFORE any bucket classification or exempt is considered (`.claude/rules/exempt-registry.md` §"The region-identity gate" applies the same scope to its forward/reverse checks). A gate that reaches past the declared roots into dependency or generated source is a scope defect in the gate, never a missing exempt.

## The bucket framework — every untested closure is bucket 1, 2, or 3

When code has an uncovered path, classify it. Read down — bucket 1 first, then 2, then 3. **Bucket 1 is the goal; buckets 2 and 3 are escape hatches requiring explicit per-entry user approval** (§"Approval"). The repo's coverage tool identifies the uncovered closures; this taxonomy decides what to do with each.

### Bucket 1 — test it in this change (default, no approval)

- **1a — write the test now.** The closure's input is constructible through the unit's public API today, even with a hostile fixture / synthetic input / environment setup. Write the test.
- **1b — source-fix, then test.** The closure exists because of a structural choice the repo controls — a redundant fallback arm, a structurally-unreachable cache-miss, an error mapping that flattens to a standard conversion, a defensive arm guarding a precondition the caller already enforces. Delete or consolidate the closure; test what remains. The source-fix rationale goes in the commit body. Source-fix is bucket 1, never an alternative to bucket 2/3.

### Bucket 2 — exempt + a tech-debt entry, future-testable (per-entry approval)

The closure becomes testable when a SPECIFIC named future thing lands — a delivery phase, an upstream dependency release exposing a constructor, or a named internal feature. The trigger must be nameable; vague triggers ("later", "eventually") are rejected at the approval surface. Carries two artifacts in lockstep: the exempt entry and a tech-debt entry naming the trigger (per `/tech-debt` + `.claude/rules/doc-organization.md`). When the trigger lands: test written + tech-debt closed + exempt removed, same change.

### Bucket 3 — exempt only, permanently untestable (per-entry approval, most rigor)

The closure can NEVER be exercised through the public API without compromising the defense itself (a test-only-shim / DI-for-tests anti-pattern) or relying on OS/kernel/hardware/third-party fault state no public API exposes deterministically. No tech-debt entry (nothing will ever change it). The most exclusive category — never a convenience escape hatch.

### The bucket-1 challenge — run it BEFORE accepting a bucket-2/3 label

A pre-labeled bucket-2/3 proposal is one input, not the answer. All three must be **no** before bucket 3 applies (this is the same challenge `architectural-review.md` §"Step 0" runs):

1. **Bucket-1a?** Input constructible through the public API today, even hostile? → bucket 1a.
2. **Bucket-1b?** A source-fix the repo controls removes the closure (delete, consolidate, lift to caller-precondition, standard-conversion)? → bucket 1b.
3. **Bucket-2 trigger?** Any named future feature/phase/upstream release exposes a constructor? → bucket 2.

A `bucket: 1b-deferred` transient state exists when the 1b source-fix is real but queued for a follow-up cycle rather than this change — exempt temporarily, with a tech-debt entry naming the source-fix mechanism, defer only when the in-change fix would exceed the cycle's brief.

## Approval — buckets 1b-deferred / 2 / 3 require explicit per-entry user approval

**No autonomous bucket designation** (the AP8 "bucket self-designation" floor). The agent encountering the gap is not the audit authority on its own framing. Two steps:

1. **Independent audit first.** Before surfacing any exempt proposal, an INDEPENDENT reviewer runs the bucket-1 challenge cold + an anti-pattern self-check on the proposal and returns a structured verdict (approve-as-bucket-N / reject-reclassify / reject-AP-hit). Dispatch this per `.claude/skills/_shared/verification-contract.md` (a gated verifier — `architect-review` carries the Step-0 bucket challenge). A reject ends the surface — apply the reclassification, do not surface the original.

2. **Surface to the user, per entry.** On an approve verdict, present: what needs exempting (closure location + body), why (the structural argument — why no public-API path and no source-fix), the independent audit's findings verbatim, and the exact exempt artifact. For bucket 3, the user verbatim-confirms "no bucket-1b source-fix and no bucket-2 future path exists." The user lands the exempt file (a net-new exemption is operator-applied — the system hook `hooks/guard-exempt-mutation.py` blocks an agent creating/deleting an entry or changing its `bucket`/`source`/`symbol`/`approval`, while letting the agent maintain an existing entry's `regions:` line-drift; the full field-scoped model is `.claude/rules/exempt-registry.md` §"What the agent may change"); the agent then commits, quoting the user's approval phrase verbatim in the commit footer (the audit trail).

The exempt artifact carries an `approved_by_user: <YYYY-MM-DD>` + `bucket:` marker in its own frontmatter — never an in-source annotation escape (`// test-ok:`, `// bucketN-ok:`, coverage-ignore comments). A self-designation marker absent the user-approval frontmatter is an AP8 violation.

## What this is NOT

- NOT the coverage tool, test layout, or build-gate layering — those are the repo's (the append names them). The exempt-registry MECHANISM (the tree shape, the frontmatter schema, the field-scoped agent-immutability, the region-identity gate algorithm) is SYSTEM — `.claude/rules/exempt-registry.md`; only the coverage tool that produces the missed regions + the repo's concrete unit-grain/paths are the repo's fill. This rule owns the exempt TAXONOMY (the bucket framework + the approval flow below); `exempt-registry.md` owns where those decisions are RECORDED.
- NOT the anti-pattern catalog (`.claude/rules/anti-patterns.md`) — this rule is the test-bar + bucket framework; that file enumerates the gamed-gate shapes (cfg-test-shim, DI-for-tests, extract-then-test-wrapper, coverage-gaming, bucket self-designation) an exempt proposal is audited against.
- NOT the reviewer framework (`.claude/skills/_shared/architectural-review.md`) — that file runs the bucket-1 challenge as Step 0; this rule is the standalone discipline every change carries.
