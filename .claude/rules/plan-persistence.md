---
paths:
  - ".claude/skills/**"
  - ".claude/rules/**"
---

# Plan persistence — a plan is a persisted artifact, never in-conversation memory

A cross-cutting governance law for every orchestrator / executor / manager that forms a plan (`/execute`, `/feature`, `/debug`, the reconciliation orchestrator, any skill wrapping `_shared/orchestrator-loop.md` or `_shared/reconcile-loop.md`). When an agent decides on a multi-item plan of work, that plan is a **tracked document on disk**, written before the work starts and updated as it proceeds — not a list held in the conversation. This rule is the single canonical statement; skills cite it, they do not restate it. It states the BEHAVIORAL law; the artifact's structure, naming, index, and ledger mechanics are `.claude/rules/doc-organization.md` (cited, not restated).

## The law

**The moment an agent forms a plan with 2+ action items, it persists that plan as a tracked artifact BEFORE acting, flips each item's status as the item lands, and closes the artifact out when the plan completes.** The artifact is the source of truth for what is done and what remains — NOT the conversation. An agent never runs a multi-item plan off in-context memory when it could write it down.

A *plan* is any ordered or unordered set of action items an agent commits to executing: a decomposition into steps, an aggregated set of findings to apply, an investigation's probe sequence, a migration's move-groups. If you would otherwise "keep track of" more than one pending action in your head, that is a plan, and it goes on disk.

## When it triggers (the floor)

**Triggers — persist before acting:**
- A plan with **2+ action items**, OR
- an item that **does not fit one commit** — it decomposes into multiple commit-grain sub-items, which IS a multi-item plan (persist it; each sub-item is a row).

**Exempt — the commit or the item's own doc is record enough:**
- A **single `/execute` cycle** (one brief → one commit) — the commit message is the record.
- A **trivial `/commit`** — one cohesive chunk, no plan.
- A **`/report-bug` or `/tech-debt`** — it already writes one tracked doc (the KI / TD), which IS the artifact; no separate plan needed.
- A purely **conversational** answer with no execution.

When in doubt at the boundary (is this 2 items or 1?), persist — the cost of a small ledger is trivial; the cost of a lost plan is a re-audit from memory.

## The artifact is a written file — a plan rendered in the conversation is NOT persisted

**Persisting a plan means writing the ledger to a file on disk BEFORE the first action — rendering the plan as a table in your message does NOT satisfy this rule.** A markdown table typed into the conversation, however complete its columns, is in-conversation memory — the exact thing this rule forbids. It does not survive a compaction, a context reset, or a re-entry; the plan vanishes with the window. The tells that the rule was misread this way:

- "Here's the tracked plan I'll execute…" followed by a table in the message and no file Write.
- Calling an in-chat table "the source of truth" or "tracked" without a path to a file under the plans root.
- Beginning to act (reading files for step 1, editing) when no ledger file exists on disk for this run.

Before the first action: **Write the ledger to a file** under the repo's plans / outstanding-work root (the location + naming + index per `.claude/rules/doc-organization.md`), then act, then flip each row in that file as its item lands. The file is the source of truth; the conversation is not. If you would have typed the plan into a message, write it to disk instead and name the path.

## What the artifact must carry

Per `.claude/rules/doc-organization.md` (the structure / typed-tree / naming / index / ledger mechanics — read it; do not restate it here):

1. **A status-ledger table** — one **row per plan item**: the item, its status, its commit (the `| Step | Status | Commit |` shape from `doc-organization.md` §"Multi-step plan ledgers", or that artifact type's documented columns). The ledger is the canonical completion surface — no per-item `— DONE` prose alongside it.
2. **A step doc per item too large for one ledger line** — an item that does not fit a single scannable row gets its own document (the step-doc shape the repo's plan convention defines), and the ledger row links to it. Small item → a row; large item → a row that links to its doc. (This is the user-facing distinction: "docs for each step when they're large enough to not fit on a single line in the table.")
3. **Written under the repo's plans / outstanding-work root** — the typed location `doc-organization.md` + the repo's plan convention name; correctly named and indexed, never an ad-hoc dump beside the code.

## Update as you go — the flip + close-out discipline

- **Flip each row as its item lands** — status → `DONE`, commit cell per the row-flip + `(landed)`→hash-backfill recipe in `_shared/orchestrator-loop.md` §C.3 / §F.4. Reuse that recipe; do NOT fork a parallel convention. The orchestrator writes the rows, not a human.
- **Close out** — when the plan completes, the artifact reflects it: every row flipped, and the doc moved per `doc-organization.md`'s open→closed convention if its artifact type uses one. **A half-flipped ledger or an un-closed completed plan left behind is a defect** — the agent always returns to close items and the document as it proceeds, before reporting done.
- **Resume from the artifact, not the conversation** — an interrupted, compacted, or re-entered run reads the ledger to learn what is done vs pending. The plan surviving a context reset is the entire point of persisting it.

## What this is NOT

- NOT a mandate to persist trivial or single-item work (see the floor) — a one-commit cycle's record is its commit.
- NOT a second planning system. It generalizes `/plan`'s discipline (which already persists a tracked tree) to every orchestrator that forms a plan *without* first calling `/plan`. An orchestrator may invoke `/plan` to author the artifact, OR write a lighter per-run ledger itself when a full `/plan` tree is overweight for a one-pass job — either way the artifact obeys `doc-organization.md`.
- NOT a replacement for `doc-organization.md` — that rule owns the structure / naming / index / ledger mechanics; this rule owns the behavioral law (persist, flip, close, never run off memory) and cites those mechanics.
- NOT triggered by a skill that already writes its tracked doc as its product (`/report-bug`, `/tech-debt`) — that doc is the artifact.
