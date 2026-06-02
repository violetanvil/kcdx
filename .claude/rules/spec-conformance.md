---
paths:
  - "**/*"
---

# Spec conformance — the design is the authority; a plan is a pointer, not the spec

A cross-cutting governance law for every skill that builds or reviews a surface against a settled design (`/plan`, `execute`, `/feature`, any orchestrator, the review skills). When a surface has a settled design artifact, that artifact — not a plan/step doc's prose summary of it — is the authority the executor builds against and the reviewer checks against. This rule is the single canonical statement; skills cite it, they do not restate it.

## The law

**A plan/step doc is a pointer to the design, not a replacement for it.** A plan summarizes scope to make work trackable; it is lossy by construction. When a step builds a surface that a settled design artifact specifies — a UI screen, an API/interface contract, a data schema, a wire/protocol format, a state machine — the executor reads that design artifact's relevant section as the load-bearing authority BEFORE writing line 1, and builds to it. Building to the step doc's summary alone is the defect that ships off-spec work past a green gate.

A *settled design artifact* is the durable specification of a surface's shape: a design doc / TRD section, a screen spec, an interface/schema/protocol definition, the decision record a design dialogue produced. The step doc's prose, the plan ledger, and the conversation are NOT it — they point at it.

## Re-ground at build time — dereference the pointer

- **The design artifact is a mandatory reading-list item**, not optional context. A build brief for a surface step names the specific design artifact + its section (`path §section`) as a load-bearing authority — alongside the rule files, never in place of them. Reading the step doc's summary is necessary, not sufficient.
- **A step doc carries an explicit back-pointer to its design authority** — the exact design artifact + section the surface is built against — so the executor can dereference from the step doc to the spec without hunting. A surface step doc with no design back-pointer is incomplete (the same class as a missing test bar).
- **No filling a spec gap with a plausible default.** When the design artifact does not specify a detail the build needs, that is an unsettled-design gap — surface it (`.claude/rules/design-authority.md`), never invent a generic default and build it. A reasonable-but-unspecified choice built silently is how off-spec work enters; the design's silence is a question for the user, not a license.

## A divergence is surfaced, never silently built

When the design artifact and the step doc's summary disagree, or the design specifies something the plan omitted, the design wins — and the discrepancy is surfaced to the user (`.claude/rules/design-authority.md`), never silently resolved in the plan's favor. A plan that drifted from the design at authoring time is a defect in the plan, caught by building to the design; do not propagate the drift into the build.

## The review verifies the diff against the spec, not the brief

A green build proves the code runs; it does NOT prove the code matches the design. For a surface with a settled design artifact, the review reads that specific artifact's section and verifies the diff IMPLEMENTS it — the screen's state matrix, the contract's signature, the schema's fields, the protocol's framing — not merely that the diff aligns with the brief's framing of it. "Matches the plan summary" is not "matches the design"; the gate that checks only the former is blind to exactly the drift this rule exists to stop. (The reviewer-side application is `.claude/skills/_shared/architectural-review.md` §"Design fit"; this rule is the standalone obligation.)

## What this is NOT

- NOT `design-authority.md` — that owns WHO decides a design (the user, never the agent autonomously); this owns WHAT a settled design binds (the executor builds to it, the reviewer checks against it). They meet where a spec gap becomes a surfaced decision.
- NOT `ux-first-class.md` — that owns UX completeness (the states/flows/a11y a UI deliverable must address); this owns conformance to the settled spec of ANY surface (UI, API, schema, protocol). A UI step clears both: it addresses UX *and* builds to the screen spec.
- NOT `decision-capture.md` — that owns recording a settled decision durably; this owns building/reviewing against the record. Capture writes the authority; conformance obeys it.
- NOT the repo's concrete design-artifact trees, section-locators, or anchor labels — those are the repo's (named in the relevant append / its layout doc). This rule is the language-agnostic floor; the surfaces it names are examples, not an exhaustive list.
