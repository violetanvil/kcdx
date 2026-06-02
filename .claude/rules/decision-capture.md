---
paths:
  - "src/**"
  - "lib/**"
  - "crates/**"
  - "packages/**"
  - "app/**"
  - "apps/**"
  - "services/**"
  - "internal/**"
  - "pkg/**"
  - "cmd/**"
  - "Documentation/**"
  - "docs/**"
  - "doc/**"
---

# Decision capture — a settled design decision gets a durable record, same change

A cross-cutting governance law for every skill that lands code or edits a design doc (`/execute`, `/feature`, `/debug`, `/design`): when the user **settles a design decision** that the code will encode, that decision is captured in a durable, organized record **in the same change** that implements it. An ad-hoc decision settled in conversation and then forgotten is the defect this rule prevents — the design rationale must outlive the chat. This is the single canonical statement; skills cite it, they do not restate it.

## The law

A code change that encodes a **settled design decision** is NOT complete until that decision has a durable record landing in the SAME change. An undocumented settled decision is incomplete.

This enables ad-hoc design (the user settles a real call mid-work without a full `/design` dialogue) while keeping it documented — capture is mandatory, the *weight* of the record scales to what was decided (§"Where it goes").

## What triggers capture (recognition)

Capture fires when the user **settles a choice between valid alternatives that the code now encodes** — a contract/interface shape, a mechanism, a boundary, a tradeoff, an approach picked over another. It is a *settled* decision (already the user's call per `.claude/rules/design-authority.md`); this rule is about recording it, not making it.

It does NOT fire on:
- a pure implementation detail the design already determines (a variable name, an obvious loop) — no decision was made;
- a fact read from the code (not a decision);
- a decision already captured (in the design doc, a plan's `plan-spec.md`, a KI/TD, or a prior decision record) — cite it, do not duplicate.

## Where it goes (the routing test — scales to what was decided)

Route by what the decision touches:

1. **Amends or refines an existing design doc / section** → record it THERE. Edit the affected §section (or append to the doc) + its changelog, via `/design`'s revision ceremony for a substantive change, or a direct §-edit + changelog entry for a small addendum. One source of design truth; the decision lives where the design lives.

2. **Stands on its own** (a cross-cutting call, an ADR-style decision not owned by one doc, a deviation from a default) → its own **decision record** in the repo's decisions tree, governed by `.claude/rules/doc-organization.md` (a `<DD>-NNNN-<slug>.md` doc + an index row — the same tracked-artifact family as known-issues / tech-debt; `DD` is the conventional example prefix, the actual prefix is repo-named per `doc-organization.md`). The record names: the decision, the alternatives considered, why this one, and the change that encodes it.

3. **Trivial — a genuine call but with no standalone weight** (the design already strongly implied it; a small mechanical pick among near-equivalent options) → the **commit-message body** suffices. The why travels with the change in history; no separate record. Reserve this for decisions a future reader can fully reconstruct from the commit body — when in doubt, write the record.

## Same-change enforcement

The record lands in the SAME change as the code that encodes the decision — not a follow-up. A commit that encodes a settled non-trivial decision without its record (an amended §, a new decision doc, or a sufficient commit body) is incomplete. (A hooks-stage guard may later assert this on commits that touch load-bearing code; until then, the orchestrators that land code — `/execute`, `/feature`, `/debug` — enforce it as a pre-commit step.)

## What this is NOT

- NOT a mandate to over-document. A trivial call goes in the commit body; only a genuine choice among real alternatives needs a durable doc/addendum. The bar is "a future reader can recover the decision and its why," scaled to the decision.
- NOT a place to MAKE design decisions — those are the user's (`design-authority.md`). This rule records what the user already settled.
- NOT the design dialogue itself (that's `/design`) and NOT the work-plan (that's `/plan`). This is the capture of a decision settled ad hoc, routed to the right durable home.
- NOT lifecycle-artifact structure (`doc-organization.md` owns the `<DD>-NNNN` tree + index shape) and NOT the design-doc structure (`structure-by-responsibility.md` / `/design`). This rule is the capture obligation + the routing test.
