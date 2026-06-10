# 1.2 [CORE] Git LFS tracking for data/db-export-bulk/ (.gitattributes + LFS init)

## What

Track the bulk CSV bundle (`data/db-export-bulk/`, produced by 1.1) via **Git LFS** — add the
`.gitattributes` LFS pattern(s) for the bulk CSV files + initialize LFS so the heavy bulk content
lives in LFS storage, not the git object store as diffable text (D38: bulk → Git LFS; the curated
`data/db-export/` stays plain git text). This is what makes "track as CSV" work at the bulk's scale
(~5.24M-row `statements`) without bloating git.

## Scope

One commit: the `.gitattributes` entry (e.g. `data/db-export-bulk/*.csv filter=lfs diff=lfs
merge=lfs -text`) + LFS init for the repo if not already initialized. The bulk CSVs from 1.1 become
LFS-tracked pointers. Does NOT change the exporter (1.1) or run_rebuild (1.3). NOTE: this introduces
Git LFS as a repo prerequisite (D18 revised — `git lfs` on every clone/CI; the publish-public
allowlist's LFS interaction is the DEFERRED governance sweep, not this step).

## Test bar

A check (a small test or a documented verification command run by the executor) that the bulk CSVs
are LFS-tracked: `git check-attr filter -- data/db-export-bulk/<f>.csv` reports `filter: lfs`, AND
`git lfs ls-files` lists them. Emits the canonical signal to the DB-pipeline sink where a test
harness applies; else the executor runs the `git check-attr`/`git lfs ls-files` verification and
records it (a config-correctness check, not a runtime behavior). FALSIFIABLE: a bulk CSV that is NOT
LFS-tracked (tracked as plain git text) fails the check. Per `.claude/rules/test-discipline.md`.

## Dependencies

- **1.1** — the bulk CSVs exist at `data/db-export-bulk/` (the files LFS tracks).

## Reference

[`../plan-spec.md`](../plan-spec.md) — the coverage map (the db-export-bulk + Git LFS element).

## Design authority

`data/maintainer-tool/design.md` **D38** §"a CSV bundle under **Git LFS** at `data/db-export-bulk/`"
+ the rejected-alternatives (raw-CSV-in-git / binary-sqlite / release-page — why LFS) + the revised
**D18** (LFS as a container/clone prerequisite). Build to D38's LFS choice.

## Disassembler-test / author-burden

None — git config / LFS setup; no author-facing input, no game-function target, no AP18 addition.
