# Maintainer tool — design changelog

Newest-first. Tracks revisions to [`design.md`](design.md) (the TRD) and the UI design
layer [`ui/design.md`](ui/design.md) + [`ui/screens/`](ui/screens/).

## 2026-06-02 — web-app pivot: Dockerized web app supersedes the PySide6 desktop tool

The tool becomes a **hostable web app** instead of a PySide6 desktop `.exe`, so maintainers
manage the Address Library from any browser (including a phone) "on the go." The entire
headless data-core (Phase 1: csv_exporter, db_editor, field_delta, the validator, the
round-trip oracle) carries over UNCHANGED — only the shell changes.

- **D14 — Delivery:** a Dockerized web app — a Python (FastAPI/Flask) backend wrapping the
  data-core + a React frontend (a component lib strong on forms/lists/dropdowns/modals).
  Sub-decisions explicit-but-open: the component lib (Mantine vs MUI); the image packaging
  (single image vs compose).
- **D15 — DLL resolver client-side:** the `.rdata` version scan is ported to a small JS
  function that runs in the browser on a locally-picked DLL (File API, no upload — only the
  version tag reaches the server). `version_resolver.py` stays the test-of-record (a
  cross-impl agreement test). A version dropdown is the phone-friendly default.
- **D16 — Server commit + push:** the backend commits to its volume-mounted checkout AND
  pushes to GitHub on confirm (D6's exact-path / live-lock / self-authored-message discipline
  unchanged; the writer moved server-side).
- **D17 — Auth out of scope, auth-ready seams:** the commit identity comes from the request
  context the operator's login supplies; the push credential is env-injected. No
  login/auth/hosting/portal code from the build — only the seams + a dev default for local
  testing.
- **D18 — Container data layout:** the git checkout (`data/seeds/` + the reference DB) lives
  on a mounted volume at a configured path; the image carries only app code; the operator
  provides the volume + credential.
- **Superseded:** D6 local-commit → server commit+push (D16); §8 PySide6 `.exe` distribution
  → Docker (D14/D18); US-10/R12 server-side DLL resolver → client-side (D15); D9/D10 "DLL
  link" framing → version-pick + client check.

**Integrated in:** the delivery note (frontmatter), §1, §5 (the structure — backend +
frontend units + the data-core tree), §6 US-10, §7 (the verification-context + accessibility
states), §8 (R9 distribution, R12 resolver, the commit constraint, R3), §9 (in: the web
stack; out: auth/hosting), §10 (D6/D9/D10 amended; D14–D18 added).

### UI-layer hand-off (action for `/ui-design` — NOT done here)

The UI design layer (`ui/design.md` + the 7 screens `ui/screens/s01–s07`) was authored for
**PySide6 desktop** — a desktop window skeleton, Qt component silhouettes, a desktop two-pane
layout. It needs a **desktop→web re-expression**, which is `/ui-design`'s job, not `/design`'s
(`/design` edits the TRD + this changelog only):

- The window skeleton → a **responsive web layout** (the two-pane navigator+detail must
  reflow for a phone viewport — likely a list→detail drill-down on narrow screens).
- The component silhouettes (PySide6 widgets) → the **React component library's** primitives
  (table, form inputs, dropdowns, modals).
- The DLL-link surface (s07) → the **version dropdown + the client-side "check against a local
  DLL" control** (D15), not a desktop file-link.
- **Carries over unchanged:** the information architecture (navigator / entity detail /
  version compare / field-delta confirm / the states), the interaction laws (layout
  stability, advisory verification, atomic confirmed transaction, the single-gate, read-only
  identity), and the field-delta confirm as the human surface (D8). ~80% of the UI design is
  toolkit-agnostic and survives.

Run `/ui-design` to re-express the UI layer for web, then `/plan` to re-decompose Phase 2+
for the web stack (Phase 1 data-core carries over).

## 2026-06-02 — db_editor reuses the existing run_apply path (D13)

Surfaced mid-build (Phase 1 step 3): `db_editor` must run every write through the single
validator gate (design §5/§8, R3 — no rule reimplemented), but the validator has no
row-level entry point (its per-row rules are inline in the CSV-file reader), and
`db_editor`'s writes reach cross-row invariants (supersession acyclicity for Jobs 4/5,
tuple-uniqueness for Jobs 1/6) a row-level check cannot see.

- **Decision (D13):** `db_editor` is the headless in-process entry point to the EXISTING
  `import_to_sqlite.run_apply` validated atomic applier — refactor `run_apply`'s CLI shape
  (`sys.exit`/prints/`--dll`) into a library-callable form `db_editor` invokes. Zero rule
  logic in `db_editor`; the whole-state gate covers row-level AND cross-row invariants;
  one path generalises to all six jobs.
- **Constraint:** the refactor preserves the existing apply==rebuild oracle byte-identically.
- **Rejected:** A (export→validate→commit per edit — the verbatim-gate-reuse fallback if
  the refactor proves too heavy), B (validate-before-open), C (add a row-level validator
  entry — modifies the shared gate + cannot see cross-row invariants, so the fork would
  reappear worse at steps 4/5).
- **Surfaced via architect-review** (it found `run_apply` already covers the whole v1 job
  catalog, which the first framing missed); the user chose D.

**Integrated in:** §5 (db_editor responsibility — the mechanism) + §10 D13. Reshapes plan
steps 3/4/5 — each becomes a thin caller of the refactored run_apply library entry for its
job, not a separate write shape.

## 2026-06-02 — importer persists NULL for a blank authored field (round-trip fix)

Surfaced while building the CSV exporter (Phase-1 step 1 of the maintainer-tool plan): the
importer (`row_builder.build_curated_row`) promoted the bulk-dump `abi_walker` floor
signature (`? (...)`) onto curated `function_no_sig` / `function_variadic` rows whose seed
`signature` cell was blank. The DB then carried a signature the seed left empty, so the
exporter could not reconstruct the blank cell and the byte-identity round-trip
(`export(import(CSVs)) == CSVs`, D2) failed on 12 rows.

- **Decision:** the importer persists NULL for any authored field the seed left blank — it
  must not promote a bulk-dump value onto a curated row's blank cell. A curated
  function-kind row with a blank seed `signature` keeps the DB `signature` NULL.
- **Why safe:** the survival/fingerprint path keys these kinds on the body-hash
  (`function_hash`), not the signature — NULLing the floor signature on a curated row
  changes no survival behavior.
- **Why it matters:** restores DB↔CSV information-equivalence (design §4) at the source —
  the invariant the whole round-trip contract rests on.

**Integrated in:** §4 (the information-equivalence consequence). Lands as plan step 1b
(`row_builder` fix + a re-import round-trip oracle on the affected rows), ordered before
the exporter (step 1) can commit byte-identical.
**Why:** the exporter (step 1) demands the exact round-trip the §4 invariant promises; the
pre-existing import behavior violated it. Settled by the user 2026-06-02 during the build.

## 2026-06-02 — UI design layer authored; v1 expanded to the full six-job tool

Authored the UI design layer ([`ui/design.md`](ui/design.md) + seven screen specs under
[`ui/screens/`](ui/screens/)) through the UI design dialogue, and revised the TRD to match
the decisions that emerged. A maintainer-tool, function-first design: stable layout (no
elements jump on state change), searchable/filterable navigation, editable forms with
gated dropdowns, side-by-side version compare, a plain-language field-delta confirm.

- **v1 is now the complete six-job tool** (D7) — create entity (Job 1), re-verify (Job 2),
  supersede (Job 4), deprecate (Job 5), create version (Job 6), plus edit-any-version and
  side-by-side compare. The first draft's Job-2-only MVP scope (and the deferral of the
  rest) is removed. v1 may be built in steps, but no catalog job is out of scope.
- **The confirm surface is a plain-language field delta** (`field: old → new`), not the
  literal CSV diff (D8) — the human reasons about record fields; the CSV diff is
  oracle-verified and lands in the commit for a reviewer, invisible to the maintainer.
- **The DLL link is advisory, never required** (D9) — every action proceeds unlinked with
  a "can't verify" warning; a resolver failure or unlinked state is overridable ("I accept
  — save anyway"). Replaces the blocking "degraded mode" framing. Unlinked default-selects
  the newest authored row (D10).
- **New entity / new version are approval-gated in the confirm step** (D11, AP18); a new
  version identical to its source is blocked with steering copy routing to re-verify (D12).
- **The UI design layer** (window skeleton, nine interaction laws, token system, seven
  screen specs) is the artifact a builder conforms to (`spec-conformance.md`).

**Integrated in:** TRD frontmatter, §1, §6 (US-1…US-10), §7, §8, §9, §10 (D3/D5 amended;
D7–D12 added); new `ui/design.md` + `ui/screens/*.md`.
**Why:** the work-plan tree was authored before the UI was designed; the design dialogue
both specified the UI and surfaced that the maintainer's real needs (manage all items, see
+ compare past versions, edit and create versions, clarity about exactly what changes) span
the whole catalog, not Job 2 alone — the user settled v1 = the complete tool.

### Plan hand-off (action for `/plan` — NOT done here)

The work-plan tree `docs/outstanding-work/maintainer-tool-db-direct/` was authored against
the **Job-2-only** scope and is now under-scoped:

- **Phase 2 (GUI shell)** assumes one Job-2 screen (steps 4–8: load → entity-list-pick →
  audit-trio-edit → save-chain-diff → commit). The settled UI is seven screens covering
  six jobs + compare. Phase 2 needs re-decomposition against `ui/screens/` (one or more
  steps per screen/job, dependency-ordered per `incremental-delivery.md`).
- **Phase 1 (data-core)** needs the editor shapes the new jobs require: INSERT (Jobs 1/6),
  the lifecycle-UPDATE (Jobs 4/5) — beyond step-3's audit-trio UPDATE. The exporter +
  round-trip oracle (steps 1–2) are unaffected.
- **`plan-spec.md`'s coverage map** maps design elements to steps against the old §-numbers
  and the Job-2 scope; it needs re-mapping to the revised §6 (US-1…US-10) + the UI screens.

**Stale prescriptive references outside the design-doc edit boundary** (NOT edited here —
`/design`/`ui-design` edit the design doc + changelog only; these are surfaced for the
reconciliation, per `deletion-hygiene.md`):

- `requirements.md` R7 ("The six jobs (full scope; **MVP is Job 2 only**)") and R6's
  "Job-2 MVP" framing now contradict D7 (v1 = the full catalog). R7's workflow list +
  phase-order is still useful; its "MVP is Job 2 only / the other five are later phases"
  assertion is superseded. Reconcile when re-planning (a banner note at R6/R7 pointing to
  design.md §9/§10 D7, matching how R1/R6 already banner-note the source-of-truth inversion).
- `plan.md` §-references to "the Job-2 MVP" (e.g. §27) inherit the same staleness; the plan
  tree re-decomposition covers them.

Run `/plan` to re-decompose the tree against this revised design + the `ui/` screen specs.
(`/design` does not decompose — `spec-conformance.md`: a plan is a pointer to the design,
re-pointed here.)
