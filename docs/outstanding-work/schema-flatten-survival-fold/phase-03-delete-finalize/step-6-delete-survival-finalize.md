# Step 6 — delete the survival table + finalize

**What.** With the av columns proven equal to the survival table (step 2) and every consumer
migrated to read them (steps 4, 5), delete the `survival` sibling: drop the `survival` SCHEMA
entry + its `USER_COLUMNS` allowlist + the dual-write, delete the `kind_form` discriminator, drop
the redundant `content_hash`/`length` survival dupes (the av columns already carry them). Reduce
`survival_builder.py` to the `_KIND_TO_FORM` dispatch the av-row build uses (or delete it if the
dispatch inlined into the row build). Sweep the deletion-hygiene survivors (tests/docs naming the
`survival` table). Populate 156/157's `vtable_slot`/`struct_offset` into the now-first-class
columns. Re-capture the rebuild-oracle baseline with `survival` gone. End state: one flat table,
the sibling deleted, all oracles green.

**Scope.**
- `seeds_shared/schema.py` — remove `SCHEMA["survival"]`, `USER_COLUMNS["survival"]`, the
  `survival` entry in the table-sets-per-db; remove `kind_form` everywhere.
- `import_to_sqlite.py` — drop the `survival`-table write (the dual-write's second target);
  `_apply_one_db` + rebuild write ONLY the av columns now; drop the restore-point's `survival`-row
  capture (the touched-rows set no longer includes a survival table).
- `survival_builder.py` — reduced to `_KIND_TO_FORM` (the kind→form dispatch the av build needs)
  or deleted if inlined; no survival-row constructor.
- `csv_exporter.py` / `round_trip.py` — drop any `survival`-table handling (the folded columns are
  av columns now). SPECIFICALLY (P1.3 step-review forward-pointer): remove `survival` from
  `schema.USER_TABLES` (the table-set the round-trip + write_db iterate) so the round-trip stops
  hashing a table that no longer exists, AND confirm `round_trip.py`'s present-set guard
  (`round_trip.py:97-98`) handles `survival`'s absence cleanly. The exporter itself is already
  survival-table-independent (P1.3, `aad…`/`ad0…` — proven by the survival-DROP export test), so
  only the `USER_TABLES` removal + the round-trip present-set are the remaining survival touchpoints.
- Deletion-hygiene (`.claude/rules/deletion-hygiene.md`): `tests/test_survival_table.py`
  removed/repointed to the av columns; sweep any doc/comment naming the `survival` table as a
  current structure (the design §11 records the fold — repoint references to the av columns /
  §11). `read_api.py` drops any survival-table read.
- The 156/157 `vtable_slot`/`struct_offset` populate — a small seed UPDATE (id156 slot 2 / +0x10;
  id157 slot 4 / +0x20, the RE-handoff values) into the structured columns; the notes prose keeps
  the narrative (the §11 convention: a resolvable fact lives in its column). Not a new entity (an
  UPDATE — no AP18 gate).
- Re-capture `tests/oracle_baseline.json` (deliberate + inspected, a BASELINE PROVENANCE entry
  documenting the `survival` table removal + the 156/157 cells).
One commit (the delete is cohesive — the table + its discriminator + dupes + the survivor sweep go
together, per deletion-hygiene "surface + every prescription of it go together").

**Test bar.** The full data-core suite green with `survival` gone: `test_rebuild_oracle`
re-captured (the `survival` table absent from the baseline, no other unexplained drift);
`test_round_trip` / `test_csv_exporter` green (the folded av columns round-trip, no survival
table); the convergence pin (direct-write == seed-rebuild byte-identity) green; `test_read_api` /
backend green (reading the av columns). No test references the `survival` table as a current
structure. The 156/157 cells assert their RE-verified values in the columns. The whole-feature
checkpoint: a game launch confirms no resolve-path regression (the matrix stays green — the engine
reads the av columns, the survival pass behavior preserved).

**Dependencies.** Steps 2 (equivalence proven), 4 (engine reads av columns), 5 (read seam reads av
columns) — every consumer migrated BEFORE the delete. This is the last step; the delete is gated
on all prior consumers being off the sibling.

**Reference.** [`../plan-spec.md`](../plan-spec.md) §"The survival fold mapping" (content_hash/
length dropped as redundant; kind_form deleted) + §"Cross-step invariants" (dual-write → prove
equal → delete; the delete is the last act).

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§11.2 (the delete — survival folded, kind_form deleted, content_hash/length dropped) + §11.5 (the
migration checklist's final baseline re-capture step). The 156/157 values:
`_research/icvar-getival-recon/MAINTAINER-TOOL-HANDOFF.md` (the RE-verified slot/offset).

**Disassembler-test / author-burden.** N/A — the 156/157 slot/offset are already-RE-verified
values moved from notes prose into their structured columns (an UPDATE of authored data); no new
game-function resolution. The delete resolves no game input.
