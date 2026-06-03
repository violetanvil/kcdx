# plan-spec — maintainer-tool-db-direct (web app)

The shared spec every step in this plan leans on. Steps cross-link here rather than
restating context.

## Goal

Build the maintainer tool — DB-direct authoring of the Address Library with CSV
auto-export — as **the complete six-job web app** (v1). A small set of trusted
maintainers manages the entire reference DB from any browser (including a phone):
browse/search/filter, view any entity's full record + all game-version rows, compare
versions side-by-side, and author all six jobs (create entity, re-verify, supersede,
deprecate, create version, plus edit any existing version's full columns). Every
mutation validates, writes the DB, auto-exports the three seed CSVs (byte-identity
round-trip), shows a plain-language field delta, and commits + pushes server-side as
one atomic transaction — no CSV hand-edit, git invisible.

## The settled design — the authority every step builds to

Three artifacts, all settled:

- **[`data/maintainer-tool/design.md`](../../../data/maintainer-tool/design.md)** — the
  TRD (WHAT the tool does). Web-pivoted: §5 (the backend + frontend units over the
  data-core), §6 (US-1…US-10), §8 (R9 Docker, R12 client resolver, the server commit),
  §9 (scope), §10 (D1–D18). Committed at the web pivot (`32df16d`).
- **[`data/maintainer-tool/ui/design.md`](../../../data/maintainer-tool/ui/design.md)**
  + **[`ui/screens/`](../../../data/maintainer-tool/ui/screens/)** — the UI design layer:
  the React + Mantine responsive app shell, the 9 interaction laws, the token system, and
  the 6 screen specs (s01–s06). Committed at the web re-expression (`8c170c8`).

Every step that builds a surface dereferences to the named §section / screen spec — the
step doc is a pointer, not a replacement (`.claude/rules/spec-conformance.md`). Supporting
authority: `data/seeds/policy.md` (column-level invariants), `requirements.md` R2–R5/R7–R12.

### Settled decisions (verbatim, from design.md §10 — the web-relevant set)

- **D1 — Source of truth:** DB authoritative; CSVs auto-exported as the git-tracked diff
  layer (derived, never hand-edited).
- **D2 — Round-trip contract:** bidirectional byte-identity (`import(export(DB))==DB` AND
  `export(import(CSVs))==CSVs`).
- **D5/D8 — Save/confirm UX:** validate → write → auto-export → round-trip → a
  **plain-language field delta** (`field: old → new`) → commit. The field delta is the
  human's acceptance signal; the CSV diff is oracle-verified, not shown.
- **D11 — New-row approval (AP18):** create entity (Job 1) or version (Job 6) is
  approval-gated in the confirm step; an UPDATE is not.
- **D12 — New-version "nothing changed":** a new version identical to its source is blocked
  with steering copy → re-verify instead.
- **D13 — db_editor reuses the applier:** `db_editor` is the in-process entry point to
  `import_to_sqlite.apply_seeds` (the single validated atomic applier); zero rule logic in
  the tool.
- **D14 — Delivery:** a Dockerized web app — a Python (FastAPI/Flask) backend over the
  data-core + a **React + Mantine** frontend. (Component lib = Mantine; image packaging
  single-vs-compose resolved in P5.)
- **D15 — DLL resolver client-side:** the `.rdata` scan is a small **JS port** running in
  the browser on a locally-picked DLL (File API, no upload — only the version tag reaches
  the server); `version_resolver.py` is the test-of-record (a cross-impl agreement test). A
  version **dropdown** is the phone-friendly default.
- **D16 — Server commit + push:** the backend commits to its volume-mounted checkout AND
  pushes to GitHub on confirm (exact-path staging, live-lock respect, self-authored message
  — `concurrency-git.md`).
- **D17 — Auth out of scope, auth-ready seams:** the commit identity comes from the request
  context the operator's login supplies; the push credential is env-injected. No
  login/auth/hosting/portal from the build — only the seams + a dev default for local
  testing.
- **D18 — Container data layout:** the git checkout (`data/seeds/` + the reference DB) on a
  mounted volume at a configured path; the image carries only app code.

## Cross-step invariants

- **The data-core is the single authority, reached through the API** (design §5, R3/law 6):
  the backend imports `seeds_shared` and calls its public surface; the frontend calls the
  API. NO validation/SQL/export/rule logic in the backend or the frontend — every invariant
  is the data-core's. The data-core public surface (LANDED, Phase 1):
  `export_seeds`, `round_trip`, `update_version_row`, `create_version`, `create_entity`,
  `supersede_entity`, `deprecate_entity`, `field_delta`, `is_new_version_nothing_changed`.
  (These take `out_dir, dll_path, …` and resolve the version FROM the DLL themselves. The
  web app has no DLL server-side — so P2 step 1b adds an OPTIONAL pre-resolved `version=(tag,
  ordinal)` param to `apply_seeds` + the five `db_editor` write functions: supply `version`
  → the data-core skips the DLL read; supply `dll_path` → unchanged (desktop + every landed
  test). The backend's adapter passes the browser-resolved `version`, no DLL server-side. **A2,
  settled 2026-06-02** — additive + oracle-preserving, NOT a `dll_path`→`version` replacement;
  implements the D13/D15 "thin adapter maps a chosen version tag → the data-core's params"
  contract.)
- **The READ-for-display surface lives in the data-core, not the backend** (design section 5,
  R3/law 6/D13): the LANDED data-core surface is write + validate + export + round-trip -- it
  has NO read/query function for the display set, and the status derivation
  (DEPRECATED/SUPERSEDED/VERIFIED/UNVERIFIED) existed ONLY as prose in `data/seeds/policy.md`
  section "Status is NOT an authored column" (no function anywhere). So P2 step 2a adds a
  read+derive module to `seeds_shared` -- `read_curated_set` / `read_entity_detail` /
  `read_version_rows` (read the reference DB) + the single
  `derive_status(current_version, row, entity)` implementing policy.md's 4-rule precedence ONCE
  -- and the backend (step 2b) just calls it + serializes JSON. **Read-seam decision, settled
  2026-06-02** -- the rule logic (status derivation) belongs in the data-core where every
  invariant lives (D13/law 6); the backend reimplementing it (or the React frontend deriving it
  in JS) was rejected as a law-6 violation that drifts. Mirrors the 1b precedent: the data-core
  was built for write; the read-for-display surface didn't exist, so it is built IN the
  data-core. Status logic lives once, tested against policy.md, reusable by any consumer.
- **The save flow is Save-previews / Confirm-transacts -- NOTHING is held across the user's
  think-time** (design §7 save spine; user-settled 2026-06-03, REVISED 2026-06-03 from a
  held-transaction model to a Confirm-time transaction model). The maintainer edits an entity's
  fields locally, hits **Save** -> the backend VALIDATES the prospective edit + returns the
  field-delta (`field: old -> new`) for review (NO DB write, NO transaction, NO held state);
  reviews the diff, hits **Confirm** -> the backend runs the WHOLE queued transaction
  SYNCHRONOUSLY in the one request while the page waits: start txn -> DB ops -> CSV export ->
  commit DB -> git commit/push -> return success/failure; ANY failure rolls back everything
  (the page gets a failure status, nothing lands). The transaction opens AND closes inside the
  single Confirm request -- there is NO transaction held open between Save and Confirm, so no
  registry, no executor-per-save, no leak, no reaper, no cross-request thread-affinity problem.
- **The deferred-commit seam (step 4a, landed 63a2a92) makes Confirm's transaction atomic
  WITHIN the one request.** A GROUND-TRUTH probe confirmed the landed apply_seeds opens both
  DBs, commits each action INTERNALLY, then closes both before returning -- so to make
  DB-ops + CSV-export + commit atomic, 4a added a deferred-commit MODE (additive +
  oracle-preserving): run the writes under ONE outer transaction per DB, skip the per-action
  COMMIT, return the open connections + commit(handle)/rollback(handle). Confirm (step 5) opens
  this transaction, does the work, COMMITs on success / ROLLBACKs on any failure -- all inside
  the one Confirm request (NOT held across think-time, the revision above). The two-DB commit
  ORDERING is SETTLED (user-confirmed 2026-06-03, landed in 63a2a92): commit() does USER-first
  then DEV, re-raising on a DEV-COMMIT failure. user + dev are two separate SQLite files with no
  cross-file atomic commit; user-first makes the only possible split "USER (shipped curated)
  committed, DEV (bulk) lagging" -- the more-recoverable split (a re-apply diffs only the DEV
  side). A truly-atomic two-file commit (ATTACH/single-file) was deferred as a follow-on.
- **The WRITE is DIRECT-DB, not a seed rebuild (D19/D20, settled 2026-06-03)** — the DB is the
  ORIGINATOR. A maintainer edit is a DIRECT INSERT/UPDATE through the applier's EXISTING
  `_apply_one_db` write helpers (fed edit parameters, not CSV-diff-derived actions), NOT the
  export-seed→edit→`apply_seeds`-rebuild bridge the original D13 recorded. A ground-truth probe
  established that `_apply_one_db` ALREADY runs the real INSERT/UPDATE — the seed-rebuild was only
  the wrapper — so P2 step 4c reuses those helpers directly, preserving the 8 load-bearing
  behaviors (the 1:1 survival INSERT, the interval-close-before-add, the function-kind
  promote-vs-mint + fingerprint + `BaselineRefusal`, the per-DB column projection, FK-id
  resolution-never-minting, the two-DB USER-first ordering, the D12/AP18 markers). The SAME
  validator gate runs, re-targeted to the **prospective DB state** (step 4b's preview re-points to
  it too — step 4b-rework). The direct write runs inside the 4a deferred-commit txn; a PRE-commit
  failure (validation) `ROLLBACK`s the held txn (discarding the change incl. `sqlite_sequence`/PK
  bumps — nothing committed). A POST-commit failure (export/integrity/git) is undone by the 4d
  **scoped restore-point** (D21) — the deferred rollback is gone once the txn commits; together
  they give the robust rollback on ANY failure (D21). After a successful write, `export_seeds`
  exports the DB → the derived record at **`data/db-export/`** (D20), NOT `data/seeds/` (the frozen
  one-time `run_rebuild` bootstrap input). **`create-version`-at-a-new-game-tag now works** — a direct
  INSERT (new `game_versions` row + interval-close + new `address_versions` row) bypasses the
  seed-rebuild's `GAME_VERSION_TAG`/baseline-matcher gate that materialised zero rows. The
  `run_rebuild` bootstrap is UNCHANGED.
- **DB↔CSV information-equivalence + the integrity check** (design §4): the full bidirectional
  round-trip (`import(export(DB))==DB` AND `export(import(CSVs))==CSVs`) is the BUILD-time oracle
  (it rebuilds the 1.3GB bulk DB + needs the dump — not run per save). The maintainer tool's
  per-save integrity check is the cheap `export(DB)`-is-deterministic direction (re-export the
  committed DB, assert the `data/db-export/` record matches). The export+check runs AFTER the DB
  commit (the export-visibility constraint: `export_seeds` opens its own fresh connection and
  cannot read the uncommitted held txn). A divergence there is a POST-commit failure → the 4d
  scoped restore-point undoes the committed write (the DB rows + `sqlite_sequence` + the
  `data/db-export/` CSVs restored, nothing lands — D21), NOT the deferred ROLLBACK (gone after
  commit).
- **The robust rollback is TWO mechanisms split at the irreversible commit (D21, settled
  2026-06-03)** — `commit(handle)` is one-way (COMMITs + closes both connections; the deferred
  rollback raises after it). (a) PRE-commit failure (validation) → the deferred-commit `ROLLBACK`
  (4a). (b) POST-commit failure (export/integrity/git) → a SCOPED restore-point (P2 step 4d), a
  DATA-CORE capability (D13/law 6 — it owns the write semantics + the open connections + knows the
  touched rows): captured BEFORE the commit (only the touched rows across
  `address_versions`/`survival`/`game_versions`/`address_names` in both DBs + `sqlite_sequence` +
  the `data/db-export/` CSVs — a few KB, never the ~1.3GB DEV DB), restored on a post-commit
  failure (the rows + the sequence + the CSVs). Step 5 calls it; the full-file snapshot the step-5
  WIP built is dropped.
- **The 9 interaction laws bind on every frontend step** (`ui/design.md` §"Global
  interaction laws"): layout stability (law 1), the responsive navigation shell (law 2),
  user-driven navigation (law 3), advisory verification + the override (law 4), the atomic
  confirmed transaction (law 5), the single-validator gate (law 6), read-only identity
  (law 7), AP18 approval-gated new rows (law 8), no raw values at a call site (law 9). Each
  frontend step cites the laws it obeys.
- **Verification is advisory** (D9/D15, law 4): a picked-or-resolved version (or the
  newest-row default — D10) always works; an unverified state warns + is overridable ("I
  accept — save anyway"). The client DLL check uploads nothing.
- **Incremental order** (`.claude/rules/incremental-delivery.md`): the data-core (P1) before
  the backend; the backend API before the frontend that calls it; the frontend spine
  (edit-an-existing-version) before the full jobs (create/compare/lifecycle); the client
  resolver before the create flows that prefill from it; Docker packages a working app.

## Reuse — what already exists (do not rebuild)

- **Phase 1 — the data-core (BUILT + landed, all oracles green):**
  `seeds_shared/{csv_exporter, round_trip, db_editor, field_delta, version_resolver,
  validators, row_builder, dict_codec, schema}.py` + `import_to_sqlite.apply_seeds` (the
  validated atomic applier db_editor drives). Delivery-agnostic — the web backend calls it
  unchanged. (Open Phase-1 follow-ons, already filed, NOT re-planned here: step 3c —
  full-column correction, probe-first; TD-0004 — the rebuild-oracle baseline re-capture.)
- `version_resolver.py` — the Python `.rdata` resolver; the **test-of-record** for the JS
  port (P4 step 11), not rebuilt.
- `tests/` — the mini-dump fixture + the existing oracle tree.
- Privacy: `data/maintainer-tool/` is already a private carve-out (R10 — done).

## Coverage map — every design element → its step (or deferral)

| Design element | Covered by | Notes |
|---|---|---|
| Data-core (csv_exporter / round_trip / db_editor / field_delta) | **Phase 1 — DONE** | landed; delivery-agnostic, the backend calls it |
| Backend skeleton + version-tag→data-core-params adapter (D14/§5) | P2 step 1 — DONE (c0b270c) | no DLL server-side; a health/load endpoint |
| Data-core tag seam — optional `version=` on `apply_seeds` + 5 db_editor writes (A2, D13/D15) | P2 step 1b | additive + oracle-preserving; the producer the save API (step 4) consumes |
| Data-core read seam — read_curated_set/read_entity_detail/read_version_rows + the single derive_status (policy.md 4-rule) | P2 step 2a | the rule logic lives in the data-core (D13/law 6), not the backend; the producer the read endpoints consume |
| Read API — curated set + entity detail + version rows + derived status | P2 step 2b | the backend calls step 2a + serializes JSON; feeds s01/s02/s03 |
| Field-delta API (D8) | P2 step 3 | wraps `field_delta` |
| Data-core deferred-commit seam — apply_seeds returns open uncommitted connections + commit/rollback (makes Confirm's txn atomic within one request) | P2 step 4a | additive + oracle-preserving; Confirm (step 5) opens+commits it in one request |
| Save (preview) API — the six job shapes: validate the prospective edit + return the field-delta (NO write, NO held txn) | P2 step 4b (landed f348857) | the maintainer reviews the diff before Confirm; **consumes step 1b's tag seam + step 3's field-delta**; its validate re-targets to prospective DB state in step 4b-rework |
| Data-core DIRECT-WRITE path (D19) — db_editor reworked to direct-DB INSERT/UPDATE reusing _apply_one_db's helpers (8 behaviors); validate prospective DB state; inside the 4a txn (ROLLBACK resets PK auto-increments); create-version-at-a-new-tag works; export → data/db-export/ (D20) | P2 step 4c | the PRODUCER the Confirm consumes; reworks the landed db_editor from the seed-rebuild bridge; **consumes 4a + 1b**; additive/oracle-preserving where it touches landed code |
| Preview Save validate re-targeted to prospective DB state (D19) | P2 step 4b-rework | re-points 4b's preview validate from the seed-validate path to 4c's prospective-DB-state validate (one gate, DB-targeted); **consumes 4c** |
| Data-core SCOPED restore-point (D21) — capture touched rows + sqlite_sequence + db-export CSVs before commit; restore on a post-commit failure (a few KB, never the 1.3GB DEV DB; a data-core capability per D13/law 6) | P2 step 4d | the post-commit-rollback half of D21 (the deferred ROLLBACK is the pre-commit half); the PRODUCER step 5 calls; **consumes 4c + 4a** |
| Robust rollback — deferred ROLLBACK (pre-commit) + scoped restore-point (post-commit), incl. PK reset (D21) | P2 step 4a (pre-commit) + 4d (post-commit) + 5 (orchestrates) | the two-mechanism rollback split at the irreversible commit |
| Confirm transaction (D16/D19/D20/D21) + auth-ready seams (D17) | P2 step 5 | ONE synchronous request: open 4a txn → 4c DIRECT-write → commit DB → export to data/db-export/ → cheap integrity check → git commit/push; ROBUST rollback on any failure (deferred ROLLBACK pre-commit; the 4d scoped restore-point post-commit, incl. PK reset); EVENT-DRIVEN index.lock (git's exit, no poll); injected identity + env credential + dev default; **reuses the kept step-5 WIP git/auth machinery (drops its full-file snapshot → calls 4d)**; **consumes 4c + 4b-rework + 4d + 4a + 1b** |
| Container data layout — backend reads the checkout (D18) | P2 step 1 (consumes) + P5 step 16 (provides) | configured checkout path |
| Frontend skeleton + Mantine theme (tokens) + responsive app shell (D14, laws) | P3 step 6 | the API client; the two-pane ↔ drill-down shell |
| s01 navigator (search/filter/list/chips) | P3 step 7 | `ui/screens/s01` |
| s02 entity detail (read) + version dropdown + default-row | P3 step 8 (read) + P4 step 14 (lifecycle edit) | `ui/screens/s02` |
| s04 field editor (dirty/was/validation) | P3 step 9 | `ui/screens/s04` |
| s06 save-confirm (field-delta modal/sheet) + toast + atomic save→commit | P3 step 10 | `ui/screens/s06`; calls P2 s4+s5 |
| Client-side JS `.rdata` resolver + cross-impl test (D15) | P4 step 11 | the "check against a local DLL" control in s02 |
| s05 create new version (Job 6) | P4 step 12 | `ui/screens/s05`; AP18 + nothing-changed |
| s05 create new entity (Job 1) | P4 step 13 | `ui/screens/s05`; id-assignment + AP18 |
| s02 lifecycle editing (supersede/deprecate Jobs 4/5) | P4 step 14 | `ui/screens/s02` lifecycle |
| s03 history + side-by-side compare | P4 step 15 | `ui/screens/s03` |
| Docker image + volume layout (D14/D18) | P5 step 16 | image packaging single-vs-compose resolved here |
| UX states (every screen) | distributed across the frontend steps | each screen's empty/loading/error/disabled/edge |
| The 9 interaction laws (`ui/design.md`) | binding on every frontend step (P3–P4) | shell + theme → P3 step 6 |
| step 3c (full-column) / TD-0004 (baseline) | OPEN FOLLOW-ONS | already filed; not re-planned here |
| Auth / login / hosting / the web portal (D17) | OUT-OF-SCOPE | the operator's |
| Job 3 — new-game-version campaign | OUT-OF-SCOPE (design §9) | a batch workflow over v1's primitives |
| driven evidence flows (R5) | OUT-OF-SCOPE (design §9) | values authorable; automation not built |
| multi-file rename journal (R11) | OUT-OF-SCOPE (design §9) | reserved |
