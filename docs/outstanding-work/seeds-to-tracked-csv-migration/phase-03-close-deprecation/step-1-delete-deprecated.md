# 3.1 [CORE] Delete data/seeds - deprecated once no in-scope reference reads it

## What

Complete the deprecation: confirm no in-scope (toolchain / backend / test) code path reads
`data/seeds - deprecated/` or the retired `data/seeds/`, then delete the `data/seeds - deprecated/`
archival copy (`git rm`). With Phases 1–2 landed, the curated + bulk data lives wholly in the tracked
CSV export (`data/db-export/` + `data/db-export-bulk/`); the deprecated copy is the last residue, and
removing it closes the deletion-hygiene loop for the data layer.

## Scope

One commit: a final sweep (grep the toolchain + backend + tests for `data/seeds - deprecated` and the
retired `data/seeds/` read paths — confirm none remain in-scope), then `git rm -r "data/seeds -
deprecated/"`. Does NOT touch the deferred GOVERNANCE references (address-library.md's `paths:` glob +
prose, CLAUDE.md, policy.md links, the public-private carve-out) — those are the separate later
sweep, and they reference the conceptual path/policy, not reads of the archival files, so they do not
block this data-copy deletion.

## Test bar

The data-core + backend pytest stays GREEN after the deletion — the proof nothing in-scope depended
on the deprecated copy (if a test or toolchain path still read it, the suite goes red on the
deletion, catching the missed reference). Plus a grep assertion that no in-scope code reads
`data/seeds - deprecated/` or the retired `data/seeds/` rebuild path. Emits the canonical
`ACCEPT-RESULT`/`ACCEPT-SUITE`. FALSIFIABLE: the suite going red on the deletion (a surviving
in-scope read), or a remaining in-scope `data/seeds/` read-path literal, fails the row. Per
`.claude/rules/test-discipline.md`, `.claude/rules/deletion-hygiene.md`.

## Dependencies

- **Phases 1–2 (all steps)** — every in-scope reference migrated to db-export; the suite green
  WITHOUT `data/seeds/`. The deletion is verifiable only after nothing in-scope reads the path.

## Reference

[`../plan-spec.md`](../plan-spec.md) — the coverage map (the deprecated-deletion element + the
DEFERRED governance sweep, recorded as the follow-up).

## Design authority

`data/maintainer-tool/design.md` **D38** §"archived at `data/seeds - deprecated/` pending deletion" +
`.claude/rules/deletion-hygiene.md` (the removed surface leaves no surviving in-scope prescriptive
reference; the governance-prose survivors are the deferred sweep).

## Disassembler-test / author-burden

None — a file deletion + sweep; no author-facing input, no game-function target, no AP18 addition.
