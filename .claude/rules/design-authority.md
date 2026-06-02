---
paths:
  - "**/*"
---

# Design authority — every design decision is the user's call

A cross-cutting governance law for every skill that could make or surface a design choice (`/plan`, `/design`, `execute`, the review skills, any orchestrator). The agent develops, surfaces, and recommends; the **user decides**. This rule is the single canonical statement; skills cite it, they do not restate it.

## The law

**A design decision is 100% the user's intent. The agent does NOT make one autonomously.**

A *design decision* is a choice between valid alternatives that the design source (the repo's design anchor, a settled spec, a `.claude/rules/` law) does not already determine — an approach, a contract/interface shape, a tradeoff, a boundary, a mechanism-vs-special-case. When the agent hits one, it **surfaces** (via the `AskUserQuestion` tool, recommended option first + `Pros:`/`Cons:` per option, per `_shared/architectural-review.md` §"Design decisions surface") and **stops for the user**. It never picks to keep moving.

## The ONE exception — a prior decision already dictates this one

The agent may make a design call **without** a fresh user ask **only** when an already-settled decision *determines* it — the four design-determined gates in `_shared/architectural-review.md` §"Design-determined vs design-decision" all hold (a named anchor forces it; the anchor + the call it forces are nameable in one clause; two valid options do not both survive the anchor; no top-priority design value is traded). Even then, the agent **states the anchor that forces it** (the audit trail the user can overturn). A weak or vague citation means the gate failed — treat it as a design decision and surface the full fork.

## When the agent disagrees

The agent may **flag a disagreement and give its reason** — once, plainly — but the design remains the user's. No re-litigating, no deference language dressed as agreement, no quiet substitution of the agent's preference. Flag, give the reason, then build what the user decided.

## Routing a surfaced fork

- A genuine design fork mid-work → surface to the user. Where a skill has a design-development channel (`/design`, or `senior-architect-consult` for a focused question), route there; otherwise surface inline. The decider is always the user; the consult/design channel structures the question, it does not answer it.
- A fork surfaced inside a structure-only or execution skill (`/plan`, `execute`) means the design was not actually settled — STOP and route to the design channel; do not decompose or build on an unresolved fork.

## What this is NOT

- NOT a license to surface *facts*. A checkable fact about the code (what a path does, a count, a behavior) is read, not asked — per `_shared/architectural-review.md` §"Read the code first". Surface *decisions*, resolve *facts*.
- NOT a reason to withhold a recommendation. Every surfaced decision carries a mandatory Recommendation + Why; "no lean" is not allowed. The user decides; the agent still recommends.
