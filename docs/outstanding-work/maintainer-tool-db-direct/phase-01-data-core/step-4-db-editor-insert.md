# Step 4 — db_editor.py: validated atomic INSERT (new version + new entity)

**What.** Extend `db_editor.py` with the **INSERT** write shapes: a new
`address_versions` row (Job 6 / US-6 — a new game-version row for an existing entity)
and a new entity (Job 1 / US-7 — a new `address_names` row + its first
`address_versions` row). Both run the shared validator over the prospective state
BEFORE any write and land as one atomic transaction. The new-entity path assigns the
next free `kcdx_id` (append-only, no autoincrement — `policy.md` §"ID assignment");
the new-version path takes the maintainer-supplied `valid_from_version`. Both enforce
`(kcdx_id, valid_from_version)` tuple-uniqueness (a duplicate is a HARD ERROR) and
**surface the AP18 new-row approval flag** (the data-core marks the write as a
DB-addition; the GUI confirm step gates it — step 11/13/14, D11). The "nothing
changed" detection for a new version (D12) is computed here (is the prospective new
row identical to its source except `valid_from_version`?) so the GUI can steer the
maintainer to re-verify instead.

**Scope.** The two INSERT entry points on the existing `db_editor.py` (step 3's
module). Reuses `validators.py` (FK resolution, tuple-uniqueness, required-column,
enum, trio integrity) + the id-assignment helper (next-free-integer from the DB). No
GUI, no Qt. The approval GATE itself is the GUI's (step 11); this step SURFACES the
flag + the nothing-changed signal the GUI consumes.

**Test bar.** `data/refdata-extractor/tests/test_db_editor_insert.py` (new). On the
mini-dump fixture: a valid new-version INSERT lands atomically with the supplied
`valid_from_version` + prefilled columns; a valid new-entity INSERT assigns the next
free id + lands both rows atomically; a duplicate `(kcdx_id, valid_from_version)`
tuple aborts with NO write; a missing required column aborts; the AP18 flag is set on
both INSERT shapes; the nothing-changed signal fires when a new version equals its
source except `valid_from_version`. Runs headless.

**Test bar runnable now?** Yes — the INSERT oracle runs when this step lands (the
validator + id-assignment + fixture exist).

**Dependencies.** Step 3 (the `db_editor.py` module + its validate-then-write
structure this step extends). The shared `validators.py` (FK + tuple-uniqueness +
required-column rules exist). Sequenced after step 3.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-6 (new version: prefill, the nothing-changed guard) + US-7 (new entity: id
assignment, first row) + §10 D11 (AP18 approval-gated) + D12 (nothing-changed steers
to re-verify). `data/seeds/policy.md` §"ID assignment" (append-only, next-free) +
§"DB additions require explicit approval" (AP18) + §"Required columns" + the
`(kcdx_id, valid_from_version)` tuple-uniqueness rule.

**Disassembler-test / author-burden.** The new-entity / new-version flows author a row
that may carry an `rva` + `signature` for a game function. The tool ASSIGNS the
`kcdx_id` (the maintainer never types it) and, when a DLL is linked, RESOLVES the
`valid_from_version` from the binary (step 12, R12) — the engine carries identity +
version. The maintainer DOES supply the `rva`/`signature` for a genuinely new target
(the expert-only authoring case the cornerstones rule labels — `policy.md`); this is
the create-a-new-curated-target path, not a common per-hook task, and it is the
AP18-gated deliberate act. No name→address auto-resolution is owed here (this IS the
step that mints the name→address row).
