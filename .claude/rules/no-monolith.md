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
  - ".claude/**"
---

# No monolith — one file, one concern

A cross-cutting governance law for **file granularity**, code AND docs. A single file that mixes concerns is an anti-pattern even for documentation. This rule governs the FILE; the architecture above the file (how units/dirs/packages are carved + depend on each other) is `.claude/rules/structure-by-responsibility.md`; tracked-artifact lifecycle trees are `.claude/rules/doc-organization.md`.

## The law

**One file = one responsibility.** Decompose by concern into a folder tree; index every folder. Split a file the moment it spans two concerns, and update the folder index in the same change.

- **Code** — a file does one thing (per `structure-by-responsibility.md`'s coordinator/worker roles: a file either wires, or does one job). Past the repo's file-size threshold, review for a split; if a wiring/index file grows logic, extract the logic to a named file.
- **Docs / plans** — a design or reference doc is a folder tree of single-concern files (`<topic>/00-index.md` or `README.md` + numbered/named single-concern files), not one large file mixing concerns. Read the index, jump to the bucket.
- **Index every folder.** A folder of single-concern files carries a `README.md` / `00-index.md` so the set is navigable by bucket.

## What this is NOT

- NOT the unit/dependency architecture (that's `structure-by-responsibility.md` — units, core-leaf inversion, the coordinator/worker DAG, naming).
- NOT lifecycle-artifact trees (that's `doc-organization.md` — known-issues / tech-debt / plans, their `<TYPE>-NNNN` naming + index line shape + open→closed movement). **An index/ledger is purposefully EXEMPT** from this rule — it is meant to grow as one flat scannable list; never split a long index into multiple files.
- NOT a mandate to over-split a tiny file or churn a settled, well-decomposed layout. The bar is "one file, one concern," scaled to the work; a reorganization of existing structure is a design decision the user owns (`design-authority.md`).
