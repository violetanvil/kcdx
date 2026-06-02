---
paths:
  - "Documentation/**"
  - "docs/**"
  - "doc/**"
  - ".claude/**/*.md"
  - "**/known-issues/**"
  - "**/tech-debt/**"
  - "**/plans/**"
  - "**/outstanding-work/**"
---

# Documentation organization — structure, indexes, and open→closed lifecycle

A cross-cutting governance law for every skill that creates, updates, or closes a **tracked-artifact document** — a known-issue, a tech-debt entry, a plan, a design doc, a review finding. Documentation is a first-class deliverable: tracked docs are organized, indexed, and lifecycle-managed with the same discipline as code. This rule is the single canonical statement; skills cite it, they do not restate it.

A repo names its concrete artifact types, paths, ID prefixes, and index columns in the relevant skill's append; this rule is the language-agnostic floor.

## The law

A **tracked artifact** (a known-issue, tech-debt entry, plan, or any numbered/indexed lifecycle document) MUST live in a typed directory with an index, follow the naming + open/closed convention below, and keep the index synchronized with the directory in the SAME change that touches it. A flat dump of files, an orphaned index, or a "closed" artifact still sitting among the open ones is a **defect**, not a stylistic choice.

## Structure — the typed directory

```
<artifact-root>/<type>/            e.g. Documentation/known-issues/, docs/tech-debt/
  README.md                        the INDEX — the canonical status surface
  <TYPE>-NNNN-<slug>.md            one file per OPEN artifact (in the type root)
  closed/                          resolved / completed artifacts
    <TYPE>-NNNN-<slug>.md          the SAME file, moved here on closure
```

- **One file per artifact.** Never multiple artifacts in one doc; never a monolith.
- **Naming: `<TYPE>-NNNN-<slug>.md`** — a short type prefix (the repo names it: `KI`, `TD`, …), a zero-padded sequential ID, and a kebab-case slug. The ID pool is shared across open + closed; allocate highest-existing + 1; never reuse an ID.
- **Open lives in the type root; closed lives in `closed/`.** Open vs closed is STRUCTURAL (which directory), never a status word buried in prose.

## The index (README.md) — the canonical status surface

The index is a markdown table, in two sections — **Active** and **Closed** — NOT prose, NOT per-file `— DONE` tags.

**Every entry is ONE line** with this fixed shape: **name (linked)** · **date reported** · **status** · **what it is (~10 words, aim short)**. Detail lives in the artifact file, never in the index line — keep the line scannable.

```
## Active
| id | reported | status | what it is |
|----|----------|--------|------------|
| [<TYPE>-NNNN](<TYPE>-NNNN-slug.md) | YYYY-MM-DD | Open / In-progress / Blocked | one short line (~10 words) |

## Closed
| id | reported | status | what it is |
|----|----------|--------|------------|
| [<TYPE>-NNNN](closed/<TYPE>-NNNN-slug.md) | YYYY-MM-DD | Closed YYYY-MM-DD | one short line (~10 words) |
```

The **status** field is explicit on every line (Open / In-progress / Blocked / Closed-with-date); the Active/Closed sections group by lifecycle and the link path reflects the open-vs-closed directory. A repo may ADD columns (e.g. tech-debt adds `closure gate | owner`); it never drops the four base fields — the table columns above: `id` (the linked `<TYPE>-NNNN`) · `reported` · `status` · `what it is`. The table cell is the single source of truth for an artifact's state — no status prose alongside it (prose drifts from the table).

**The index is purposefully EXEMPT from the no-monolith rule** (`.claude/rules/no-monolith.md`). It is meant to grow as a single flat list — one line per artifact, open + closed — so the whole tracked set is scannable in one place. Do NOT split a long index into multiple files; a growing index is working as intended, not a monolith to decompose. (`no-monolith.md` governs the artifact FILES, not their index.)

## The two enforced disciplines

These are mandatory and checkable.

1. **Touch a tracked artifact → update its index in the SAME change.** Filing, renaming, re-scoping, or closing an artifact without updating its index row is INCOMPLETE. The directory and the index agree at all times; a change that desynchronizes them is not done.

2. **Close → move to `closed/` + reindex, in the SAME change (one commit).** Closing an artifact is three coupled edits that land together:
   - append a **Resolution** section to the artifact body (what fixed it / what gate resolved it),
   - **`git mv <type>/<TYPE>-NNNN-slug.md <type>/closed/<TYPE>-NNNN-slug.md`**,
   - move the index row Active→Closed and repoint its link to the `closed/` path.

   A close that leaves the file in the type root, or flips the index without moving the file (or vice versa), is a **broken closure** — the orphaned-state defect this rule exists to prevent. (The closing skill's body is the primary enforcer; a post-commit hook — `hooks/tripwire-closure-integrity.py` — WARNs as a backstop when a commit claims a closure without the coupled move + reindex + Resolution. It is a recoverable-orphan notice, not a hard block — the commit has already landed.)

## Multi-step plan ledgers

A plan/outstanding-work tree tracks step completion with a status ledger (`| Step | Status | Commit |`), not the open/closed file move — the plan doc stays put while its rows flip. The ledger is the canonical completion surface (no per-header `— DONE` tags). A row's Status is one of `NOT STARTED` (authored, unbuilt) · `BLOCKED` (named blocker) · `DONE` (landed) · `NEEDS REWORK` (a previously-`DONE` step a later check found deficient — a transient state that returns to `DONE` when the correcting work lands). `NEEDS REWORK` is never an authoring-time value (`/plan` authors only `NOT STARTED` / `BLOCKED`); it is written only by the orchestrator on a post-landing rejection. Row-flip + hash-backfill discipline (incl. the `DONE → NEEDS REWORK → DONE` transitions) lives in `_shared/orchestrator-loop.md` §C.3 — the orchestrator writes the rows, not a human.

This ledger is the persistence target `.claude/rules/plan-persistence.md` mandates: any orchestrator that forms a multi-item plan writes one of these (one row per item, a step doc per item too large for a row) before acting and flips it as work lands. This rule owns the ledger's structure; `plan-persistence.md` owns the behavioral law (persist, flip, close, never run a plan off in-conversation memory).

## What this is NOT

- NOT a mandate to over-structure. A repo with two known-issues still gets the index + the `closed/` convention — but a one-off note that is not a tracked lifecycle artifact does not need a numbered ID and an index.
- NOT triggered by non-doc work. A pure code change that touches no tracked document does not invoke this rule.
- NOT a substitute for the skill that owns each artifact type. This rule is the shared structure; `/report-bug`, `/tech-debt`, `/plan`, `/design` (and the repo's appends) name the concrete types, paths, prefixes, and columns.
- NOT a reshaping of an artifact tree that legitimately uses a different structure. **Code-review findings** (`.claude/skills/code-review/<branch>/<short-hash>/` — numbered-by-severity `00-index.md` / `0N-*.md` files, an immutable per-commit snapshot) are NOT open/closed lifecycle artifacts; a fixed finding is tracked by the NEXT review's fresh snapshot, never by a move-to-`closed/`. They are explicitly OUTSIDE this rule's open/closed + `<TYPE>-NNNN` convention. Any tree a skill defines with its own documented immutable/snapshot structure is governed by that skill, not retroactively by this rule.
