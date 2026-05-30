---
name: plan
description: Use this skill to turn an ALREADY-SETTLED goal into a navigable work-plan tree under docs/outstanding-work/<slug>/ — a context.md shared-spec doc, a top README with a phase-grain status ledger, one phase-NN subdir per phase (each with a step-grain ledger), and one document per shippable (commit-grain) step. Structure-only: it decomposes and structures a decided goal; it does NOT make design decisions (forks route to /senior-architect-consult) and does NOT build (the tree is consumed later by /feature or /execute, which read a step doc as their Source work-item). For a settled spec that should be built now in one motion use /feature; for a single-commit change use /execute; to file a bug use /report-bug.
---

# Plan — author a work-plan tree from a settled goal

User has a decided goal and wants it structured into a trackable plan, NOT built now. Decompose into phases + commit-grain steps, write the tree + ledger under `docs/outstanding-work/<slug>/`, commit, stop. The sibling of `/report-bug` for outstanding work: pure capture + structure, halting before execution.

The tree is the source-of-truth a later `/feature` / `/execute` cycle reads — each step doc is a `Source work-item` per `_shared/orchestrator-loop.md` "Caller-injected parameters"; landing a step flips its ledger row.

## Scope

**In:** writing `docs/outstanding-work/<slug>/` (context.md + top README ledger + per-phase README ledgers + per-step docs), appending one entry row to `docs/outstanding-work/README.md` "Current entries", `/commit` to land.

**Out — refuse and route:**
- A design fork (which approach, unsettled schema/contract) → `/senior-architect-consult` (you are not the decider). Surface it, route, resume when settled.
- Building / deploying / launching → `/feature` or `/execute` consume the tree later. `/plan` writes no code.
- A single-commit change with no multi-step structure → `/execute` (no plan tree needed).
- A bug → `/report-bug`.
- Editing any file outside `docs/outstanding-work/` → boundary violation, stop.

## Precondition — the goal must be settled

`/plan` structures a DECIDED goal. Before writing a step doc, every design fork it depends on must already be resolved (a prior `/senior-architect-consult`, a clear user spec, or a settled `docs/design.md` / rule section). If decomposition surfaces an unsettled fork, STOP and route:

> *"Structuring this plan surfaced an undecided design fork: <fork in one line>. That's a `/senior-architect-consult` decision, not mine to make. Resolve it there, then re-invoke `/plan` — I'll fold the decision in. Stopping."*

Do NOT invent the answer to keep moving. A step doc resting on an unverified assumption is a plan that misdirects the cycle that reads it.

## Procedure

1. **Intake from conversation context.** Draft:
   - **Goal** — one sentence, plain English: what the finished work delivers.
   - **Slug** — `<kebab-case>` for the dir name (`docs/outstanding-work/<slug>/`).
   - **Phases** — the ordered phase split (1+; one phase is valid). Each phase ends in a buildable state.
   - **Steps per phase** — one per shippable, commit-grain unit (one `/execute`-or-`/feature`-step-sized change = one step doc).

2. **Confirm scope + decomposition with the user.** Present and wait for a go:

   ```
   Plan: <goal — one sentence>
   Slug: <slug>  (-> docs/outstanding-work/<slug>/)
   Settled decisions folded in: <bullets, or "none surfaced">

   Phases / steps (each step = one commit):
     Phase 1 — <name>
       1. <step> — <one-line what>
       2. <step> — <...>
     Phase 2 — <name>
       1. <step> — <...>
     ...

   Confirm to write the tree, or refine.
   ```

   On confirm → step 3. On a surfaced fork → route per Precondition, do not proceed. On refine → revise, re-present.

3. **Write the tree** under `docs/outstanding-work/<slug>/`:

   - **`context.md`** — the shared spec every step leans on: the goal, the settled design decisions (verbatim, with their source — consult thread / design.md section / rule), cross-step invariants, and any preserved source material. Steps cross-link here rather than each restating shared context.
   - **`README.md`** — index + the **phase-grain status ledger** per `docs/outstanding-work/README.md` "Status ledger" (one row per phase, `Step | Status | Commit`, every row `NOT STARTED` / `—` at authoring; `BLOCKED` rows name the blocker in the label). Plus a one-line intent and a link to `context.md`.
   - **`phase-NN-<slug>/README.md`** per phase — phase intent + a **step-grain ledger** (one row per step doc) + the phase's verification gate.
   - **`phase-NN-<slug>/step-M-<slug>.md`** per shippable step — the doc a future cycle reads as its `Source work-item`. Each carries: **What** (one paragraph), **Scope** (settled, commit-grain), the **disassembler-test note** on any author-facing input it adds (per `cornerstones.md`), **Test bar** (the `test-plugins/` plugin/sub-test per `test-suite.md`), **Dependencies** (prior steps), **Reference** (link into `context.md` / a cited doc).

   A single-phase plan still gets one `phase-01-<slug>/` subdir — uniform structure.

   Relative-link depth from a step doc (`<slug>/phase-NN/step-M.md`) to repo root is FOUR levels (`../../../../`); to `<slug>/README.md` is `../README.md`; to `context.md` is `../context.md`. Verify links resolve before commit.

4. **Append the index row** to `docs/outstanding-work/README.md` "Current entries":
   `- [<slug>/](<slug>/README.md) — **active, not started.** <one-line goal>. Phase tree authored by /plan; canonical phase-grain ledger in [<slug>/README.md](<slug>/README.md).`

5. **Commit.** Invoke `/commit`. Docs-only under `docs/outstanding-work/**` is one cohesive chunk → auto-commits. Capture the short-hash.

6. **Stop.** Report: `Authored plan at docs/outstanding-work/<slug>/, committed <short-hash>. /plan does not build — run /feature (multi-step) or /execute (per step) with the step doc as the Source work-item; landing a step flips its ledger row.`

## Hard rules

- **Structure-only — never decide a design fork.** A surfaced fork routes to `/senior-architect-consult` (Precondition). The user (via consult) decides; `/plan` folds the decision in.
- **Never build / deploy / launch.** `/plan` writes docs. Execution is `/feature` / `/execute` reading the tree later.
- **Every step doc is commit-grain** — one shippable `/execute`-or-`/feature`-step-sized unit, so a step doc IS a valid `Source work-item`. A step that's really three commits gets split into three step docs.
- **One ledger per doc, fixed shape** (`docs/outstanding-work/README.md` "Status ledger"). Top README = phase-grain; phase README = step-grain. No status prose alongside a ledger.
- **All rows start unstarted.** `/plan` authors; it does not mark anything `DONE` (nothing is built). A `BLOCKED` row at authoring is valid (names the blocker); `DONE` is not.
- **Edits limited to `docs/outstanding-work/`.** Any Edit/Write outside is a boundary breach — stop and surface.
- **Invoke only on explicit user request.** A plan-shaped finding surfaces to the user, who decides to `/plan`.

## Anti-patterns

- Inventing a design decision to finish the tree. An unsettled fork is a STOP-and-route, not a guess (Precondition).
- A step doc that spans multiple commits — it can't be a clean `Source work-item`. Split it.
- Status prose ("mostly done, just X left") alongside the ledger — the ledger is the single source of truth; prose drifts.
- Marking a row `DONE` at authoring time. `/plan` builds nothing; completion is the executing cycle's job.
- A monolithic single doc instead of the tree — that's the shape the restructure split apart.
