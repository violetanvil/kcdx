# Step 3 — `apply` scaffold + re-verify path

**What.** Add the `apply` subcommand to `import_to_sqlite.py` and wire its full
spine for the SIMPLEST action — re-verify (audit-trio UPDATE). The spine is:
resolve the target version (step 2 resolver, or the `whdlversions.json`
fallback) → validate the full seed CSV state through `seeds_shared/validators`
→ compute the seed-vs-DB delta → apply the delta to both DBs. Re-verify is the
right first action because it is a single idempotent UPDATE that needs no dev DB
and no row-builder INSERT — it exercises the scaffold without the kind-class
complexity that steps 4–5 add. See `plan.md` §3 "Re-verify" + "Running an
incremental build".

**Scope (commit-grain).**
- `apply` subcommand parsing (`apply --dll <path>`; `--dll` optional if the
  link cache resolves it — link cache itself is Phase 2, so MVP takes `--dll`).
- Version resolve (step-2 resolver) with the `whdlversions.json` fallback.
- Full-CSV validation gate via `seeds_shared/validators` — abort, no writes, on
  any failure (`../context.md` invariant).
- Delta computation: for each `address_versions` seed row, look up the DB row by
  `(kcdx_id, valid_from)`; classify present-unchanged (no-op) vs present-with-
  changed-audit-trio (re-verify).
- The re-verify UPDATE (both DBs, user→dev, each `BEGIN; …; COMMIT;`):
  audit-trio columns only, `evidence_kind` via `seeds_shared/dict_codec`.
- A run report: counts of re-verified / no-op, nothing silent.
- Out of scope for this step: add-entity, add-versions-row, deprecate,
  supersede (steps 4–5); the baseline-present gate (step 4, since re-verify
  never reads a bulk row).

**Disassembler test.** Author supplies a DLL path + hand-edits CSV cells; no
offset/signature burden. Compliant.

**Test bar.** Oracle slice: hand-edit a row's audit trio, run `apply`, assert
the row's audit-trio columns in both DBs match what `--rebuild` produces from the
same edited seeds; all other columns unchanged. Idempotence: a second `apply` is
a no-op. Validation: a CSV broken elsewhere aborts `apply` with no DB write.

**Dependencies.** Step 1 (validators + dict_codec + schema), Step 2 (resolver).

**Reference.** [`../context.md`](../context.md);
[`data/maintainer-tool/plan.md`](../../../../data/maintainer-tool/plan.md) §3
(Re-verify; Running an incremental build), §6.
