# Step 4c — data-core DIRECT-WRITE path (D19; reuse `_apply_one_db`'s helpers)

**What.** Rework the data-core's six write functions (`db_editor.update_version_row` /
`create_version` / `create_entity` / `supersede_entity` / `deprecate_entity` +
`_drive_names_lifecycle_edit`) from the **seed-CSV-rebuild bridge** (export the DB → edit a
temp seed CSV → re-apply by diffing the prospective seed against the DB) to **DIRECT-DB
INSERT/UPDATE** — the DB is the originator (design D19). A ground-truth probe established that
`import_to_sqlite._apply_one_db` ALREADY runs the real `INSERT`/`UPDATE` statements; the
seed-rebuild was only the wrapper. So this step feeds those EXISTING write helpers **edit
parameters** instead of CSV-diff-derived actions, preserving the 8 load-bearing behaviors, runs
the same validator gate re-targeted to the **prospective DB state**, executes inside the **4a
deferred-commit transaction** (reused verbatim), and exports the DB → **`data/db-export/`** (D20)
after a successful write. **`create-version`-at-a-new-game-tag now works** — a direct INSERT
bypasses the seed-rebuild's `GAME_VERSION_TAG`/baseline-matcher gate that materialised zero rows.

**Scope.**
- **A direct-write entry in the data-core** that builds the action(s) `_apply_one_db`'s write
  helpers consume from EDIT PARAMETERS (the maintainer's edit), not from a CSV diff. Reuse —
  do NOT reimplement — the helpers that carry the 8 load-bearing behaviors:
  1. the 1:1 `survival` sibling INSERT on every add;
  2. the interval-close (`UPDATE … SET valid_through=? WHERE kcdx_id=? AND valid_through IS NULL`)
     BEFORE an add-versions-row INSERT (the `ix_av_open_unique` partial-unique constraint);
  3. the function-kind PROMOTE-vs-mint (USER inserts a projected curated row from the DEV bulk
     base carrying the fingerprint; DEV does the in-place `UPDATE` promote) + the `BaselineRefusal`
     gate (a function-kind add with no bulk row refuses);
  4. the per-DB column projection (`USER_COLUMNS` for USER, full for DEV);
  5. FK-id resolution against the DB (`_db_tag_to_id` / `_db_dict_id` / `_db_evidence_kind_id` —
     look up EXISTING ids, never mint; an unseen value refuses);
  6. the two-DB USER-first commit ordering (the 4a seam already owns this);
  7. the D12 nothing-changed + AP18 markers, now computed from the **prospective DB state**
     (compare the new row against the entity's existing DB rows, not a prospective seed);
  8. diff-preserved export (the export's job — preserved at the `data/db-export/` write).
- **Re-target validation to the prospective DB state.** The whole-state validator
  (`_validate_full_seed_state` / the `validators.py` cross-row checks — tuple-uniqueness,
  audit-trio integrity, supersession pair-integrity + acyclicity, FK closure, enum/required)
  today reads seed-CSV rows. Re-target it to run against the **prospective DB rows** (the DB as
  it would be after the edit). The validators are pure (row-dicts + lookups), so feed them
  DB-shaped dicts — same invariants, DB source. A validation failure aborts with NO write (the
  txn never commits).
- **Run inside the 4a deferred-commit transaction** (reused verbatim — connection-level). The
  direct writes land uncommitted; `commit(handle)` / `rollback(handle)` are the confirm/cancel.
  The `ROLLBACK` discards the whole txn INCLUDING `sqlite_sequence`/PK-autoincrement bumps — the
  robust post-failure rollback (the user's explicit requirement; it comes for free from staying
  in one transaction).
- **The export target is `data/db-export/`** (D20), NOT `data/seeds/`. `export_seeds(db_path,
  seed_dir)` is unchanged (it already exports DB→CSV diff-preserved); only the `seed_dir` it is
  pointed at changes — the backend (step 5) passes `data/db-export/`. `data/seeds/` is the frozen
  bootstrap input (`run_rebuild` reads it once; the maintainer tool never writes it).
- **`create-version`-at-a-new-tag**: the direct path INSERTs the new `game_versions` row (the
  tag now exists), closes the entity's prior open interval, and INSERTs the new `address_versions`
  row — none of which consult `GAME_VERSION_TAG` or the unbuilt cross-version matcher. (The
  matcher's bulk re-identification job is a separate concern; a maintainer hand-authoring a single
  curated row supplies the resolve facts directly.)

**Out of scope.** No backend endpoint changes (4b-rework + step 5). No git/export-location wiring
in the backend (step 5 passes `data/db-export/`). The `run_rebuild` bootstrap is UNCHANGED. The
seed-rebuild bridge code is REMOVED for the incremental path (deletion-hygiene: sweep
`apply_seeds`-as-the-maintainer-write-mechanism references; `apply_seeds` itself stays for the
bootstrap/`run_apply` CLI + any non-maintainer caller — confirm at build time what still needs it).

**Test bar (same change; the data-core test tree + the mini-dump fixture exist):**
a new `data/refdata-extractor/tests/test_direct_write.py`:
- **CONVERGENCE (the load-bearing proof):** for each of the existing job shapes (re-verify,
  full-column UPDATE, create-version-at-the-CURRENT-tag, create-entity, supersede, deprecate),
  the DIRECT write produces a DB **byte-identical** to the SAME edit via the old seed-rebuild
  path — the direct write changes the MECHANISM, not the result (pin to the seed-rebuild output,
  the 1b/4a convergence pattern). Assert the whole-DB per-table content fingerprint over both DBs.
- **The 8 behaviors each exercised:** a create-version asserts the 1:1 survival row landed + the
  prior interval closed; a function-kind add asserts the promote-vs-mint + the fingerprint carried
  + the `BaselineRefusal` on a missing baseline; an add asserts per-DB projection (USER vs DEV
  columns) + FK-id resolution (no minted id).
- **create-version-at-a-NEW-tag (the new capability):** a direct create-version at a tag the DB
  has no baseline for LANDS the new `game_versions` row + the closed prior interval + the new
  `address_versions` row (the old seed-rebuild path materialised ZERO rows here — assert the NEW
  path materialises them).
- **Prospective-DB-state validation:** an invalid edit per shape (malformed date, partial trio,
  duplicate tuple, supersession cycle, missing required) raises the validator's verdict with NO
  write (the DB byte-identical), validated against the prospective DB state (not a seed CSV).
- **Robust rollback:** a direct write under the deferred txn, then `rollback(handle)`, leaves the
  DB byte-identical INCLUDING the `sqlite_sequence` values (the PK-autoincrement reset proof) — a
  subsequent add reuses the same next id, proving the sequence was not advanced.
- **The landed oracles stay green** (additive/oracle-preserving where it touches landed code; the
  `apply_seeds` bootstrap/immediate path untouched). Capture the data-core suite on HEAD first
  (the TD-0004 rebuild-baseline-drift red is pre-existing); confirm 4c turns no green oracle red.
Run: `python -m pytest data/refdata-extractor/tests/ -q`.

**Dependencies.** Step 4a (the deferred-commit seam, reused verbatim) + step 1b (the `version=`
seam) + Phase 1 (`db_editor`, `_apply_one_db`'s write helpers, the validator, `export_seeds`,
the survival/row builders — all landed). This is the PRODUCER; the reworked 4b-validate +
step 5 consume it (`.claude/rules/incremental-delivery.md`).

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§10 **D19** (the direct-write mechanism + the 8 load-bearing behaviors + the prospective-DB-state
validation + the deferred-commit ROLLBACK rollback) + **D20** (the `data/db-export/` target;
`data/seeds/` frozen) + §5 (the `db_editor` mechanism paragraph). [`../plan-spec.md`](../plan-spec.md)
§"Cross-step invariants" (the D19/D20 model). `data/seeds/policy.md` (the validator invariants —
not reimplemented; re-targeted to the DB).

**UX.** N/A — a data-core write path; no user-facing surface.

**Disassembler-test / author-burden.** N/A — internal Python write path; no author-facing
game-function input (the maintainer authors values; the engine carries no new hex burden).
