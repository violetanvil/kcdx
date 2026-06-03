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
- **Deferred commit is THE write mechanism for the maintainer tool** (design §7 save spine;
  user-settled 2026-06-03): EVERY DB change the tool makes goes through a deferred-commit
  transaction -- open a transaction on both DBs, run validate -> write -> export -> round-trip
  INSIDE it, RETURN the open (uncommitted) connections, and COMMIT only when the user confirms
  (ROLLBACK on cancel or any failure). "On Cancel nothing lands" holds literally -- an
  uncommitted transaction is invisible and discardable; no file copy, no 1.3GB DEV-DB
  duplication, no live mutation before confirm. The LANDED data-core does NOT expose this: a
  GROUND-TRUTH probe of import_to_sqlite.apply_seeds confirmed it opens BOTH DBs (user + dev),
  commits each action INTERNALLY (per-action `BEGIN`/`COMMIT`, _apply_one_db), then CLOSES both
  before returning (import_to_sqlite.py:1784-1801) -- the transaction is fully internal and gone
  by return. So P2 step 4a adds a deferred-commit MODE to apply_seeds (additive + oracle-
  preserving, the 1b pattern: the existing internal-commit path stays for desktop/CLI/tests):
  run the writes under ONE outer transaction per DB (not per-action auto-commit), skip the
  COMMIT, and return the two open connections + the result; the data-core also exposes
  commit(conns) / rollback(conns). The backend (step 4b) holds the connections across the
  confirm; step 5 COMMITs the held transaction together with the git commit as ONE confirm
  transaction. The two-DB commit ORDERING sub-decision is SETTLED (user-confirmed 2026-06-03,
  landed in 63a2a92): commit() does USER-first then DEV, re-raising on a DEV-COMMIT failure
  (never swallowed). user + dev are two separate SQLite files with no cross-file atomic commit;
  user-first makes the only possible split "USER (the shipped curated DB) committed, DEV (the
  on-demand bulk DB) lagging" -- the more-recoverable split (a re-apply diffs only the DEV side,
  the applier's convergence making it safe), and the shipped DB the tool reads back is always
  correct. A truly-atomic two-file commit (ATTACH / single-file layout) was deferred as a
  potential follow-on, out of 4a's additive scope.
- **DB↔CSV information-equivalence + the round-trip** (design §4): every save re-asserts the
  byte-identity round-trip before commit; a divergence aborts with no write.
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
| Data-core deferred-commit seam — apply_seeds returns open uncommitted connections + commit/rollback (THE tool's write mechanism) | P2 step 4a | additive + oracle-preserving; the producer the save endpoints + step 5 consume |
| Save API — the six job shapes (data-core save spine) | P2 step 4b | each opens a deferred-commit txn via 4a, returns result+delta for the confirm gate; **consumes step 1b's tag seam + 4a's deferred-commit seam**; holds the txn for step 5 |
| Git commit + push on confirm (D16) + auth-ready seams (D17) | P2 step 5 | COMMIT the held 4a txn + the git commit as ONE confirm transaction; exact-path/live-lock/push; injected identity + env credential + dev default |
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
