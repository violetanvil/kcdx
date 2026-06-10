# Phase 3 — Close the deprecation (delete data/seeds - deprecated)

**Intent:** complete the deletion-hygiene close — once the toolchain + backend + tests run on
db-export (Phases 1–2) and no IN-SCOPE reference to `data/seeds/` remains, delete the archival
`data/seeds - deprecated/` copy. The DEV/curated data now lives wholly in the tracked CSV export;
the deprecated copy is the last residue.

**Design authority:** `data/maintainer-tool/design.md` **D38** ("the data is archived at
`data/seeds - deprecated/` pending deletion") + `.claude/rules/deletion-hygiene.md` (a removed
surface leaves no surviving prescriptive reference).

**Precondition:** Phases 1–2 green (every toolchain/backend/test reference migrated to db-export).
NOTE the GOVERNANCE references (address-library.md, CLAUDE.md, policy.md links, the public-private
carve-out) are the DEFERRED sweep — they are NOT a blocker for deleting the deprecated DATA copy
(they reference the conceptual `data/seeds/` path/policy, not the deprecated archival files), but the
deletion step confirms no in-scope CODE path reads the deprecated copy before removing it.

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [3.1 [CORE] Delete data/seeds - deprecated once no in-scope reference reads it](step-1-delete-deprecated.md) | NOT STARTED | — |

## Phase verification gate

Phase 3 is done when: a sweep confirms no in-scope (toolchain/backend/test) code reads
`data/seeds - deprecated/` or the retired `data/seeds/`, and the `data/seeds - deprecated/`
directory is deleted (`git rm`). The data-core + backend pytest stays green after the deletion (the
proof nothing in-scope depended on the deprecated copy). The deferred governance-prose sweep is
recorded as the remaining follow-up (it does not block this deletion — those refs are conceptual,
not reads of the archival files).
