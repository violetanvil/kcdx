# Step 4 — save API (the six job shapes — validate → write → export → round-trip)

**What.** Add the save endpoint(s) that drive the data-core save spine for **all six job
shapes**: re-verify / full-column UPDATE (`update_version_row`), create version
(`create_version`), create entity (`create_entity`), supersede (`supersede_entity`),
deprecate (`deprecate_entity`). Each runs the data-core's atomic spine (validate → write DB
→ export the 3 CSVs → round-trip oracle) and returns the result — the AP18 new-row flag
(D11) and the D12 nothing-changed signal where applicable. A validation failure aborts with
NO write (the data-core's gate, law 6/D13). **This step does NOT yet commit** — it lands the
prospective change through the data-core and reports; the commit + push is step 5 (so the
save mechanism is testable in isolation before the git layer).

**Scope.** The save endpoint(s) mapping each job shape to its data-core entry point (via the
step-1 version-tag adapter for the `dll_path` param). Returns the save result + the AP18/
nothing-changed flags the frontend confirm gate consumes. No git commit/push (step 5), no
frontend.

**Test bar.** A backend test (`pytest`) on the mini-dump fixture, real API → real data-core
→ real DBs: each of the six job shapes lands atomically (the correct rows change); an invalid
edit (per shape — malformed date, partial trio, duplicate tuple, supersession cycle,
missing required column) aborts with NO write (the DB is byte-identical); the AP18 flag is
set on create-version/create-entity; the nothing-changed verdict fires for an identical new
version. Runnable now (the data-core write shapes are all landed Phase 1).

**Dependencies.** Step 1 (the backend + the version-tag adapter). Phase 1 steps 3/4/5
(`db_editor` UPDATE/INSERT/lifecycle — landed). Sequenced after step 1 (and after step 3 so
the frontend's review→save flow has both the delta + the save endpoints, though they are
independent backend-side).

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-3…US-8 (the six jobs) + §7 (the save spine + the write-failure state) + §10 D11
(AP18) + D12 (nothing-changed) + D13 (the applier path). `policy.md` (the column invariants
the data-core validator enforces — not reimplemented here).

**Disassembler-test / author-burden.** N/A — the save API drives already-authored edits;
the create flows' expert RVA/signature authoring is the frontend's (s05) + AP18-gated; the
backend just persists what the validator accepts.
