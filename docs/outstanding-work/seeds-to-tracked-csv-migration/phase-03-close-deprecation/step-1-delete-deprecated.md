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

## Deferred governance sweep — the precise survivor set (NOT this step; the user-decided follow-up)

The data deletion landed (the 3 `data/seeds/*.csv` `git rm`'d + the untracked `data/seeds - deprecated/`
filesystem-deleted). `data/seeds/policy.md` was deliberately KEPT (restored from HEAD, still tracked) —
it is the AP18 authoring-law authority, and deleting it would dangle the references below. The user
decided (2026-06-10): delete the data CSVs now, keep `policy.md` until the governance sweep relocates it
+ repoints its references atomically. The concrete survivor set the deferred sweep must handle:

- **`data/seeds/policy.md` relocation** — move it to its new home (e.g. `data/maintainer-tool/` or
  `data/db-export/`) and repoint the 9+ prescriptive references in the SAME change (deletion-hygiene):
  `.claude/hooks/guard-seed-approval.py` (2 refs), `.claude/rules/address-library.md` (3 markdown links),
  `.claude/rules/anti-patterns.md`, `.claude/rules/no-hardcoded-addresses.md`, `CLAUDE.md` (the AP18 hard
  rule + link), `data/maintainer-tool/design.md` (15+ §-citations).
- **`data/refdata-extractor/tests/make_mini_dump.py:31`** — an ACTIVE code literal
  `os.path.join(HERE, "..", "..", "seeds", "address_versions_seed.csv")` that now dangles on the deleted
  `data/seeds/`. A run-once mini-dump slicer (not a data-core test), so it did not break the suite, but it
  will fail the next time the mini-dump is regenerated → repoint to `data/db-export/`.
- **`data/refdata-extractor/README.md`** (lines ~13, 150, 154) — prose still prescribing `data/seeds/` as
  the curated-CSV location; now stale (the CSVs are at `data/db-export/`). Rewrite to db-export.
- **`address-library.md`'s `paths:` glob + the public-private carve-out + the seed-CSV path prose** in
  `CLAUDE.md` / the rules — the remaining conceptual `data/seeds/` references named in the §Scope defer.

## Disassembler-test / author-burden

None — a file deletion + sweep; no author-facing input, no game-function target, no AP18 addition.
