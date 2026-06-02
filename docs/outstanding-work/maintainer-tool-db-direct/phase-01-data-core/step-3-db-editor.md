# Step 3 — db_editor.py (validated atomic audit-trio UPDATE)

**What.** Add `data/refdata-extractor/python/seeds_shared/db_editor.py` — the
headless, Qt-free unit that applies the Job-2 edit to the reference DB: a
validated, atomic audit-trio UPDATE on one `address_versions` row
(`last_verified_at_version`, `verified_by`, `verified_date`, `evidence_kind`).
It runs the shared validator (`validators.py`, R3) over the prospective
post-action state BEFORE any write; a validation failure aborts with NO write.
The write is a single atomic transaction; the three read-only fields (`kcdx_id`,
`name`, `valid_from_version`) are never mutated (R8, `policy.md`). This is the
DB-write half of the Job-2 save chain the GUI (Phase 2 step 7) calls.

**Scope.** One new module in `seeds_shared/`. The Job-2 audit-trio UPDATE only —
NOT Job-1 add-entity, NOT supersede/deprecate (those are deferred R7 jobs). Reuses
`validators.py` (the gate) + `row_builder.py` / `dict_codec.py` (the row shape +
`evidence_kind` encoding). No GUI, no Qt, no CSV write (the export is step 1's
concern, sequenced by the GUI save chain later).

**Test bar.** `data/refdata-extractor/tests/test_db_editor_reverify.py` (new). On
the mini-dump fixture: a valid audit-trio UPDATE lands atomically and the four
trio columns change while every other column (incl. the read-only triple) is
untouched; an INVALID edit (malformed `verified_date`, out-of-enum
`evidence_kind`, partial trio) aborts with no write (the DB is byte-identical to
pre-action). Runs headless. (The full DB→CSV→commit chain is exercised end-to-end
by the GUI save step; this oracle proves the DB-write unit in isolation.)

**Dependencies.** None on steps 1–2 for the WRITE itself (db_editor only writes
the DB). But it is sequenced AFTER them so the phase's headless re-verify path is
complete and the round-trip (step 2) can confirm the written DB exports cleanly —
ordering keeps the phase coherent. The shared `validators.py` / `row_builder.py`
exist already.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§5 (`db_editor.py` responsibility: "apply a validated, atomic edit transaction to
the DB … runs the shared validator BEFORE any write; a validation failure aborts
with no write") + §6 US-3/US-4. Column invariants: `data/seeds/policy.md`
§"Verification audit trail" (the trio is all-set-or-all-null) + §"Required
columns".

**Disassembler-test / author-burden.** N/A — re-verify mutates the audit trio
only; no game-function address/ABI is authored here (the RVA/signature are
unchanged on a re-verify, `policy.md` §"New game version workflow").
