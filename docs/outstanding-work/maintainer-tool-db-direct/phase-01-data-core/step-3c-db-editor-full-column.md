# Step 3c — full-column correction (applier present-row extension, probe-first)

**What.** Complete US-5: editing an EXISTING `address_versions` row's full columns
(`module` / `kind` / `rva` / `signature` / the six survival columns), not just the audit
trio. `db_editor` + the prospective-seed bridge already accept a valid full-column edit
(step 3); the gap is in the applier: `import_to_sqlite._apply_one_db`'s present-row branch
updates ONLY the four audit-trio columns ("the audit trio is the only mutable part",
import_to_sqlite.py ~:1516/1527) — a non-trio change to an existing row is a silent no-op.
This step extends that branch to a full-column UPDATE, through the same shared applier
(D13 — never in `db_editor`).

**Probe FIRST (the undecided semantics — `results-driven.md`).** Extending the present-row
branch is NOT a plain column copy: changing a row's `kind` or `rva` has undecided
re-promote / survival-rebuild semantics that affect apply==rebuild correctness:
- Does a new `rva` that now matches a DIFFERENT bulk function re-promote a new
  body-hash fingerprint, or keep the row's existing one?
- Does a `kind` change rebuild the survival datum (the per-kind `kind_form` changes —
  `survival_builder.py`)?
- Does a `signature`-only change leave the fingerprint untouched?

These are checkable against the rebuild oracle: author each change-case in a seed, rebuild,
observe what a from-scratch rebuild produces for that row, and make the present-row UPDATE
match it (apply==rebuild is the contract). The probe's outcome→meaning map settles the
semantics BEFORE the applier extension is written; the design call (if any survives the
probe) surfaces to the user (`design-authority.md`).

**Scope.** A probe (a `_research/`-style captured finding or a throwaway test asserting
what rebuild produces for each change-case) → settle the semantics → extend
`_apply_one_db`'s present-row branch to the full-column UPDATE (rebuilding the curated row
via `build_curated_row` + the kind-class gate + the survival-row rebuild, the same
machinery the ADD path uses) in the same BEGIN/COMMIT → flip `test_db_editor_update.py`'s
SKIP'd `test_full_column_update_lands_atomically` to a positive oracle. The apply==rebuild
oracle stays byte-identical for the trio-only path (an idempotent rewrite).

**Test bar.** The SKIP'd case in `test_db_editor_update.py` flips to PASS: a valid
full-column UPDATE (e.g. correct an `rva` + `signature`) lands atomically with the correct
re-promote/survival result the probe settled; the trio-only path is unchanged; the existing
apply oracles stay green. Plus the probe's captured finding.

**Dependencies.** Step 3 (the `db_editor` UPDATE entry + the bridge + the refactored
`apply_seeds` — all landed). This step extends the applier the bridge already drives.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-5 + §10 D13 (the applier is the single path). `data/seeds/policy.md` §"Address kinds"
+ the survival design (`data/maintainer-tool/fingerprint-per-kind.md`) for the re-promote
semantics.

**Disassembler-test / author-burden.** N/A — a correction edits an already-resolved row's
values; no NEW game-function offset is authored (that is the create path, step 4).
