---
paths:
  - "**/*"
---

# Working artifacts — produced non-source things are classified, placed, and reused

A **working artifact** is a non-source, non-lifecycle-doc thing produced by a skill's process, or created or acquired as dev tooling — a verification probe, an investigation dump, a generated fixture, a benchmark or maintenance instrument, a vendored analysis tool. It is governed before it lands. Placement-by-responsibility is `.claude/rules/structure-by-responsibility.md`; tracked lifecycle docs (KI/TD/plans) are `.claude/rules/doc-organization.md`; this rule owns the artifact's KIND + lifecycle + provenance. The repo declares its concrete trees, markers, and per-axis policy in its own layout doc (its `CLAUDE.md` or a repo-local layout rule that auto-loads alongside this floor); this rule is the language-agnostic floor.

## Every artifact is one of four kinds

Classify by the artifact's intrinsic question, not by example. An artifact answering none of these is not a working artifact — it is source, or it is undeclared cruft to delete. Generated source that compiles into the build IS source (`.claude/rules/structure-by-responsibility.md`) — the generator is a durable instrument, its committed output is not a working artifact.

- **Scratch / verification** — produced to answer a question that expires (a probe, a one-off analysis script).
- **Durable process-output** — a finding/record a FUTURE run reuses (a research dump, a captured fact, a recon note).
- **Durable instrument** — a kept tool re-run over time (a benchmark, a generator, a maintenance script).
- **Vendored tool** — a third-party analysis tool the work uses, not produced here.

## The five property axes — declared before the artifact lands

Each kind declares its value on these axes; the repo sets the policy per axis (the append). Declaring them is the floor; an artifact in a location whose purpose is undeclared is the defect (`structure-by-responsibility.md`).

- **Committed or gitignored** · **In-build or isolated** (a standalone artifact never joins the build/workspace) · **End-of-life** (when, and how, its life ends) · **Trust level** · **Visibility** (where the repo splits public/private).

## Trust level — an agent-authored artifact is never a SOURCE

A durable process-output artifact declares whether it is **primary evidence** (a live-fetched doc, a fresh tool run) or **agent-authored hypothesis** (an interpretation). Hypothesis is NEVER cited as a `SOURCE:` (`.claude/rules/skeptical-expert.md`, `.claude/rules/dependencies.md`) — it is a lead to re-verify, not the answer.

## Durable output is reuse-first; its producer is co-located

A producing skill reads the artifact tree for an existing answer BEFORE regenerating — a write-only artifact tree is the anti-pattern. The script/tool that generated an artifact lands beside its output (reproducibility).

## A scratch probe leaves NO residue in live source

When a probe's question is answered, capture the finding + the probe's reusable wiring (its script or instrumentation recipe) into the artifact tree as durable process-output, THEN remove the probe from source. The live source returns to pure production logic — no dormant branch, no conditional-compile block, no commented-out corpse, no runtime-disabled flag. **A probe adds ZERO cost to live code** (`.claude/rules/memory.md`, `.claude/rules/logging.md`, `.claude/rules/polling.md`): an "off" probe still costing a call/branch/allocation is forbidden, archived-in-place or not. The next investigation reconstructs the probe from the artifact tree, never from source.

## What this is NOT

- NOT placement-by-responsibility (`structure-by-responsibility.md`) or file granularity (`no-monolith.md`) — cited for where an artifact sits; this rule owns its kind + lifecycle.
- NOT a tracked lifecycle doc (`doc-organization.md`) — a working artifact has no `<TYPE>-NNNN` id and no open→closed movement.
- NOT a tree a skill already owns with its own documented structure (a code-review snapshot, the exempt registry) — those are governed where they are defined.
- NOT the repo's concrete trees, marker syntax, clutter threshold, or per-axis policy — those are the repo's, declared in its own layout doc / a repo-local layout rule.
