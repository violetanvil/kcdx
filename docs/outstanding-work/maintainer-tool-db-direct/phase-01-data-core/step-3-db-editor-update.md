# Step 3 — db_editor.py: validated atomic version-row UPDATE (audit-trio re-verify)

**What.** Add `data/refdata-extractor/python/seeds_shared/db_editor.py` as the headless
in-process entry point to the validated atomic applier (D13 — `db_editor` wraps the
refactored `import_to_sqlite.run_apply`/`apply_seeds`, never reimplements a rule). Its
first write shape: the **audit-trio re-verify** (Job 2 / US-3: bump
`last_verified_at_version` + `verified_by` + `verified_date` + `evidence_kind` on one
`address_versions` row). The identity key `valid_from_version` and the entity identity
(`kcdx_id`, `name`) are never mutated (R8, `policy.md`). The prospective state is gated
through the shared validator BEFORE any write; a validation failure aborts with NO write.
This is the DB-write unit the GUI field editor (Phase 2 step 10) + save-confirm (step 11)
call for re-verify.

**Scope narrowed mid-build (the full-column US-5 correction is now step 3c).** The
full-column correction (change an existing row's `module` / `kind` / `rva` / `signature`
/ survival columns) is split out: the existing applier's present-row branch updates ONLY
the audit trio, and extending it has undecided re-promote/survival semantics for a
kind/rva change (a probe-first design call). `db_editor` + the bridge accept a valid
full-column edit; only the applier sub-capability is missing — see
[`step-3c-db-editor-full-column.md`](step-3c-db-editor-full-column.md).

**Scope.** One new module `db_editor.py` + its UPDATE entry point (the INSERT and
lifecycle shapes are steps 4–5; they extend this same module). Reuses `validators.py`
(the gate), `row_builder.py` / `dict_codec.py` (the row shape + enum encoding). No
GUI, no Qt, no CSV write (export is step 1's unit; the GUI save chain sequences them).

**Test bar.** `data/refdata-extractor/tests/test_db_editor_update.py` (new). On the
mini-dump fixture: a valid audit-trio-only UPDATE lands atomically (the four trio
columns change, every other column incl. the read-only triple untouched); a valid
full-column UPDATE (e.g. correct an `rva` + `signature`) lands atomically; an INVALID
edit of either kind (malformed `verified_date`, out-of-enum `evidence_kind`/`kind`,
partial trio, `last_verified_at_version < valid_from_version`, unresolvable `module`
FK) aborts with NO write (the DB is byte-identical to pre-action). Runs headless.

**Dependencies.** None on steps 1–2 for the WRITE itself (db_editor only writes the
DB). Sequenced AFTER them so the phase's authoring path is coherent and the round-trip
(step 2) can confirm a written DB exports cleanly. The shared `validators.py` /
`row_builder.py` exist already. This step is the FIRST `db_editor` shape; steps 4–5
extend the same module and depend on its structure.

**Test bar runnable now?** Yes — the UPDATE oracle runs the moment this step lands
(the validator + the mini-dump fixture exist; no later step is needed to exercise it).
(`.claude/rules/incremental-delivery.md`.)

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§5 (`db_editor.py` responsibility: "apply a validated, atomic edit transaction to the
DB … runs the shared validator BEFORE any write; a validation failure aborts with no
write") + §6 US-3/US-4/US-5. Column invariants: `data/seeds/policy.md` §"Verification
audit trail" (the trio all-set-or-all-null), §"Required columns", §"Address kinds",
§"valid_from_version vs. last_verified_at_version".

**Disassembler-test / author-burden.** N/A for the audit-trio path (re-verify mutates
the trio only). The full-column path can correct an `rva`/`signature` — but it does
NOT make the maintainer hand-author a NEW game-function offset (that is a create flow,
step 4); a correction edits an already-resolved row's value. No name→address
resolution surface is added here.
