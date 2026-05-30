# Step 6 — `apply` deprecate + supersede

**What.** Complete `apply` with the two names-side actions: deprecate an entity
(set `is_deprecated` + `deprecated_at_version`, optional
`deprecation_replacement`) and supersede X with Y (a rename — set X's
`superseded_by` + `superseded_at_version`, with Y added via step-4's add-entity
path). Both run the shared validator's acyclicity / pair-integrity checks before
any write. This closes the last of the action types `apply` must cover for the
phase gate. See `plan.md` §3 "Deprecate / supersede".

**Scope (commit-grain).**
- Delta classification extended to detect a changed deprecation pair / a new
  supersession edge on an `address_names` seed row.
- Deprecate: single `address_names` UPDATE (both DBs) —
  `is_deprecated = 1`, `deprecated_at_version`, optional
  `deprecation_replacement`.
- Supersede: predecessor `address_names` UPDATE (`superseded_by` +
  `superseded_at_version`); the successor entity Y lands via the step-4
  add-entity path (names + versions row, kind-class + baseline gate).
- The shared validator's supersession-acyclicity check and the
  deprecation/supersession pair-integrity check run in the pre-write validation
  gate (`seeds_shared/validators`) — no cycle or half-set pair can be written.
- Both DBs, user→dev, per-action `BEGIN; …; COMMIT;`. Run report extended to
  cover deprecate/supersede counts.

**Disassembler test.** Names-side metadata only; no offset/signature input. N/A.

**Test bar.** Oracle slice: deprecate an entity and supersede another, assert
both DBs' `address_names` rows match `--rebuild` from the same seeds.
Acyclicity: a CSV edit that would create a supersession cycle is refused by the
validation gate with no DB write. Full-phase oracle: an `apply` sequence
exercising re-verify + add-entity + add-versions-row + the survival-datum write
(step 5) + deprecate + supersede produces a DB row-set identical to `--rebuild`
(the phase-gate test).

**Dependencies.** Step 1 (validators), Step 3 (scaffold), Step 4 (add-entity
path that supersede's successor reuses). Independent of step 5 (survival datum).

**Reference.** [`../context.md`](../context.md);
[`data/maintainer-tool/plan.md`](../../../../data/maintainer-tool/plan.md) §3
(Deprecate / supersede).
