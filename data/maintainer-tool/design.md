# Maintainer tool — design (DB-direct authoring with CSV auto-export)

> **Status:** v1 (settled). Date: 2026-06-02 (write mechanism revised 2026-06-03).
> **Write mechanism (revised 2026-06-03 — §10 D19/D20):** a maintainer edit is a
> **DIRECT INSERT/UPDATE on the DB** (the DB is the originator, D1), reusing the applier's
> existing `_apply_one_db` write helpers — NOT the seed-CSV-rebuild bridge the original D13
> recorded. The same single validator gate runs, re-targeted to the prospective DB state; the
> deferred-commit transaction's `ROLLBACK` undoes a PRE-commit failure, and a scoped
> restore-point (D21) undoes a POST-commit (export/git) failure — together a robust
> rollback on ANY failure (incl. PK
> auto-increment reset). The derived CSV export now writes to **`data/db-export/`** (D20),
> not back to the bootstrap `data/seeds/` (which stays the frozen one-time `run_rebuild`
> input). `create-version`-at-a-new-game-tag now works (a direct INSERT bypasses the
> seed-rebuild's baseline gate). The `run_rebuild` bootstrap is unchanged.
> **Scope:** the Address Library maintainer sub-system — `data/maintainer-tool/`,
> `data/seeds/`, and the `data/refdata-extractor/python/` toolchain that links them.
> This is the data sub-repo's own design doc; it is NOT `docs/design.md` (that doc
> is the v0.1 *engine* design, superseded and out of scope here).
> **UI design layer:** the tool's visual + interaction design (what it looks like and
> how it behaves) lives in [`ui/design.md`](ui/design.md) + [`ui/screens/`](ui/screens/)
> — the screen specs a builder conforms to. This doc fixes WHAT the tool does; the UI
> docs fix what it looks like. **Changelog:** [`changelog.md`](changelog.md).
> **Delivery (revised 2026-06-02 — web-app pivot, §10 D14–D18):** the tool is a
> **Dockerized web app** — a Python (FastAPI/Flask) backend wrapping the headless
> data-core + a React frontend — committing to the server-side git checkout on confirm,
> so maintainers manage the Address Library from any browser (including a phone). This
> SUPERSEDES the earlier PySide6 desktop-`.exe` plan (D6 local-commit + §8 `.exe`
> distribution + the server-side DLL resolver). The data-core (§5) + the round-trip /
> validator invariants (§4, §8 R3) carry over unchanged. The **UI design layer**
> (`ui/design.md` + the 7 screens) is re-expressed desktop→web in a separate `/ui-design`
> pass (see `changelog.md` — not edited from this doc). **Auth / login / hosting / the
> web portal are OUT of scope** — the app exposes auth-ready seams (an injected commit
> identity + an env push credential), wired by the operator (D17).
> **v1 scope (revised 2026-06-02):** v1 is the COMPLETE tool — the full six-job catalog
> (see §9), not the Job-2-only MVP the first draft scoped. The "build in steps" note
> below still holds (the data-core lands before the GUI); the *deferral* of Jobs 1/3/4/5/6
> is removed. See §10 D7.
> **Supersedes:** `requirements.md` R1/R6 (the "seed-editor-only / does-not-build-the-DB"
> framing) and `plan.md` §"two-phase" Phase-2 framing (the CSV-editor surface). Those are
> banner-noted at their sites; this doc is the live design they point to.
> **Builds on (unchanged):** `requirements.md` R2–R5, R7–R12; `plan.md` §1–§8 (the headless
> `apply` core, the shared `seeds_shared/` module, the `.rdata` version resolver);
> `data/seeds/policy.md` (the column-level authoring invariants); `fingerprint-per-kind.md`
> (the survival design).

## Contents

1. [Vision](#1-vision)
2. [Glossary](#2-glossary)
3. [The source-of-truth inversion](#3-source-of-truth-inversion)
4. [The round-trip contract](#4-round-trip-contract)
5. [Structure — the responsibility units](#5-structure)
6. [User stories & acceptance — the full six-job tool](#6-user-stories)
7. [UX & states — the Job-2 screen](#7-ux-states)
8. [Constraints](#8-constraints)
9. [Scope — in / out / deferred](#9-scope)
10. [Decision record](#10-decision-record)
11. [The reference-DB schema — flat + final](#11-schema-flat-final)

---

## 1. Vision <a name="1-vision"></a>

Move the Address Library off hand-edited seed CSVs: the maintainer edits the
reference DB **directly** (a real INSERT/UPDATE on the DB rows — the DB is the
originator, D19) through a **web app** (any browser, including a phone), which
**auto-exports** the three CSVs as a deterministic, git-tracked diff record on every
committed change, written to the **derived-export location `data/db-export/`** (NOT back
to the bootstrap `data/seeds/`, D20), guaranteed correct by an integrity check. (The
surface is a Dockerized web app — a Python backend over the headless data-core + a React
frontend, committing server-side on confirm; see the delivery note + §8 + §10 D14–D20.
The earlier PySide6 desktop plan is superseded.)

**v1 success criteria.** A maintainer manages the entire reference DB end-to-end through
the GUI — browses/searches/filters the curated set, views any entity's full record and
all its game-version rows, compares versions side-by-side, and authors any of the six
jobs (create entity, re-verify, supersede, deprecate, create version, plus correcting any
existing version's full columns). Every mutation: the validator gates the write, the CSVs
auto-export, the round-trip oracle confirms identity, the maintainer sees a plain-language
field delta (`field: old → new`) of exactly what changes, and the change commits as one
atomic transaction — with no hand-edit of any CSV at any point, and git invisible to the
maintainer.

**The end-state this moves toward.** Today the CSV is the authored source and the DB
is its generated cache (`policy.md`, `address-library.md`). This design inverts that:
the DB becomes the working/authoring surface, the CSVs become a derived export. **v1
ships the inversion for the WHOLE six-job catalog** — the complete tool. (The first draft
scoped v1 to Job 2 only and deferred the rest; that deferral is removed — see §9, §10 D7.)
The work may still be BUILT in steps (the data-core before the GUI; jobs in a sensible
order), but nothing in the catalog is out of v1's scope.

## 2. Glossary <a name="2-glossary"></a>

| Term | Meaning |
|---|---|
| **Reference DB** | `reference.sqlite` (USER, curated ~143 entities) + `reference-dev.sqlite` (DEV, bulk superset). Generated today; the **authoritative authoring surface** under this design. |
| **Bootstrap seeds** | the three files under `data/seeds/` (`module_seed.csv`, `address_names_seed.csv`, `address_versions_seed.csv`). The genesis input: `run_rebuild` reads them for ONE FINAL from-dump rebuild to construct the DB, after which they are frozen — never written by the maintainer tool (D20). |
| **The derived export (CSV record)** | the three CSVs the maintainer tool exports from the DB on every committed save, written to **`data/db-export/`** — the git-tracked diff/review record of the DB's history (D1/D19/D20). The DB is the originator; this is its deterministic export, distinct from the bootstrap seeds the DB was born from. |
| **The round-trip / integrity check** | `import(export(DB)) == DB` AND `export(import(CSVs)) == CSVs` — the full bidirectional oracle (a BUILD-time gate). The maintainer tool's per-save integrity check is the cheap `export(DB)`-is-deterministic direction (re-export the committed DB, assert the CSV record matches); the full bidirectional round-trip rebuilds the bulk DB + needs the dump, so it stays the build-time oracle, not run per save. |
| **Data-core** | the headless, Qt-free authoring logic in `seeds_shared/` (validators, row-builder, the new exporter + DB-editor). The GUI is a thin shell over it. |
| **Job 2** | re-verify one curated entity at the current game version (update the audit trio). The v1 MVP workflow (`requirements.md` R6). |
| **Audit trio** | `last_verified_at_version` + `verified_by` + `verified_date` + `evidence_kind` — the per-row verification record (`policy.md` §"Verification audit trail"). |
| **The tool** | the maintainer web app — a Python backend over the data-core + a React frontend, Docker-delivered, private, living in `data/maintainer-tool/` (D14, R9, R10). |

## 3. The source-of-truth inversion <a name="3-source-of-truth-inversion"></a>

**Decision: DB authoritative; CSVs auto-exported as the git-tracked diff layer**
(derived, never hand-edited).

```
BOOTSTRAP (one-time genesis, unchanged):
  Ghidra dump + data/seeds/*.csv  --run_rebuild-->  reference.sqlite + reference-dev.sqlite

AUTHORING (every maintainer edit, the inverted path — D19):
  tool --direct INSERT/UPDATE-->  reference.sqlite + reference-dev.sqlite   (validated, deferred-commit txn)
                                          |
                                          v  (deterministic export on every committed change)
  GIT-TRACKED RECORD:  data/db-export/*.csv          (derived, diffable, reviewed — D20)
```
The DB is the originator. The bootstrap seeds (`data/seeds/`) build the DB ONCE; thereafter
every edit writes the DB directly and exports the derived record to `data/db-export/` — the
seeds are never re-read on the authoring path, and the export never writes back to `data/seeds/`.

What the inversion changes:

- **The CSVs survive** as a derived export — now at **`data/db-export/`** (D20), a
  new private path (private by default — the publish-public allowlist is opt-in, so a
  new `data/` subdir is not published unless added). `policy.md`'s file-format rules
  (UTF-8, `QUOTE_MINIMAL`, `#`-comments, header-literal column order) still describe the
  exported shape, and a reviewer still reads a human-readable CSV diff. `data/seeds/`
  stays a private carve-out too, but as the **frozen bootstrap input** (read once by
  `run_rebuild`), not the maintainer-tool's write target.
- **The CSV is no longer hand-edited, and the DB is no longer rebuilt from it.** The
  tool writes the DB **directly** (D19); the export writes the `data/db-export/` record.
  `policy.md` §"DB additions require explicit approval" (AP18), the audit trio,
  supersession/deprecation pair integrity, and the survival columns are now invariants on
  the **DB-write path** (validated against the prospective DB state) — the same rules, a
  different surface.
- **No new publish-boundary story is needed** — the boundary is unchanged because the
  CSVs still exist (now under `data/db-export/`, also private) and still carry the
  published projection.

What the inversion does NOT change: the column-level authoring law (`policy.md`),
the survival design (`fingerprint-per-kind.md`), the `.rdata` version resolver
(`plan.md` §7 / `requirements.md` R12), the privacy carve-out (R10), the `run_rebuild`
one-time bootstrap, or the web-app delivery (D14–D18).

## 4. The round-trip contract <a name="4-round-trip-contract"></a>

**Decision: bidirectional byte-identity round-trip** — the strongest oracle.

```
INVARIANT (both directions):
  import(export(DB))   == DB     (byte-identical DB rows)
  export(import(CSVs)) == CSVs   (diff-preserved: row order, #-comments,
                                  QUOTE_MINIMAL per cell, trailing newline)
```

Consequences this design carries forward:

- **DB and CSV are information-equivalent.** No field lives only in the DB or only
  in the CSV. The export cannot invent a column; the import cannot drop one. **The
  importer must therefore persist NULL for any authored field the seed left blank —
  it must not promote a bulk-dump value onto a curated row's blank cell.** (Settled
  2026-06-02, surfaced by the exporter: the importer was writing the bulk `abi_walker`
  floor signature onto curated `function_no_sig` / `function_variadic` rows whose seed
  `signature` cell was blank, so the DB carried a value the seed left empty and the
  round-trip could not reconstruct the blank. The fix: a curated function-kind row with
  a blank seed `signature` keeps the DB `signature` NULL. Safe — the survival/fingerprint
  path keys these kinds on the body-hash, not the signature.) (A
  derived/cache column the CSV does not mirror would break this — such a column is
  forbidden on the authored surface; it belongs to the bulk-dump dev-only tables,
  which the export does not touch.)
- **A divergence is a tool bug**, caught by the round-trip oracle test — the same
  discipline as the existing apply==rebuild oracle (`plan.md` §8). The round-trip
  test lives in the data-core (§5), runs headless, and is part of every change that
  touches the exporter or the DB-editor.
- **Diff-preservation per R11** is the export's binding contract: a tool-authored
  edit's `git diff` shows ONLY the cells the action changed — never whole-file
  reformatting churn.

## 5. Structure — the responsibility units <a name="5-structure"></a>

**Decision: a headless data-core in `seeds_shared/`; the web app is a thin shell over it.**
Satisfies `headless-testable.md` by construction (the whole authoring path is exercisable
with zero UI). The importer reuses the exporter. (The data-core is delivery-agnostic — the
desktop shell and the web backend both call the same units; the web pivot, D14, changes only
the shell.)

```
data/refdata-extractor/python/
  seeds_shared/                  (headless, UI-free, fully unit-testable — LANDED, Phase 1)
    schema.py        validators.py    row_builder.py    dict_codec.py
    version_resolver.py            (.rdata scan — the test-of-record for the JS port, D15)
    csv_exporter.py                DB -> the 3 CSVs, diff-preserved (R11)
    db_editor.py                   validated, atomic DIRECT-DB-edit transactions (reuses _apply_one_db's write helpers, D19)
    field_delta.py                 saved-vs-prospective field delta (D8) + nothing-changed (D12)
    round_trip.py                  bidirectional byte-identity oracle (D2)
  import_to_sqlite.py            (apply_seeds — the single validated applier db_editor drives)

data/maintainer-tool/            (the web app — D14; a thin shell over the data-core)
  backend/   <- the Python (FastAPI/Flask) API over the data-core + git commit/push (D16)
             + the auth-ready seams (injected identity + env push credential, D17);
             a thin adapter maps a chosen version tag -> the data-core's params (no DLL server-side).
  frontend/  <- the React app (the 7 screens re-expressed for web) + the client-side
             JS .rdata resolver (D15). NO authoring logic — it calls the API.
  Dockerfile / compose  <- the image + the volume-mounted-checkout layout (D18).
  (NO validation, SQL, or export logic in the backend/frontend — they call the data-core.)
```

Each new unit's single responsibility (`structure-by-responsibility.md`):

- **`csv_exporter.py`** — given a DB, produce the three seed CSVs deterministically,
  preserving the diff (row order, comments, quoting, newline). Its sole job is
  DB→CSV; it owns the diff-preservation contract.
- **`db_editor.py`** — apply a validated, atomic edit transaction to the DB (the
  audit-trio + full-row UPDATE for Job 2 / US-5, the INSERT shapes for Jobs 1/6, and
  the lifecycle UPDATE for Jobs 4/5 — all in v1 per §9/D7). It runs the shared
  validator (R3) BEFORE any write; a validation failure aborts with no write.
  **Mechanism (D19, settled 2026-06-03 — supersedes the D13 seed-rebuild bridge):**
  `db_editor` does NOT author a parallel write/validate path, AND it does NOT rebuild
  the DB from seed CSVs. It performs the six jobs as **DIRECT DB INSERT/UPDATE** through
  the existing applier's `_apply_one_db` **write helpers** (the real `INSERT`/`UPDATE`
  statements that already run inside `import_to_sqlite`), fed **edit parameters** rather
  than CSV-diff-derived actions. Those helpers carry the 8 load-bearing behaviors a
  naive direct write would lose — the 1:1 `survival` sibling INSERT, the interval-close
  before an add, the function-kind promote-vs-mint + fingerprint-carry + `BaselineRefusal`
  gate, the per-DB column projection, and FK-id resolution (never minting) — so reusing
  them preserves them. The **same single whole-state validator gate** runs (row-level AND
  cross-row — supersession acyclicity / tuple-uniqueness / pair-integrity / FK closure),
  re-targeted to the **prospective DB state** instead of a prospective seed CSV. The write
  runs inside the **deferred-commit transaction** (the connection-level seam): the DB ops
  land uncommitted; a PRE-commit failure (validation) `ROLLBACK`s the held txn (discarding
  the change incl. `sqlite_sequence`/PK bumps — nothing committed). On Confirm the txn
  commits (USER-first then DEV); a POST-commit failure (export/CSV/git, which run after the
  irreversible commit) is undone by a **scoped restore-point** captured before the commit
  (D21) — the deferred rollback is gone once the txn commits. Together they give the robust
  rollback on ANY failure (D21). After a successful write, `csv_exporter.export_seeds`
  exports the committed DB → the **derived CSV record at `data/db-export/`** (diff-preserved, D20). The original `run_rebuild` bootstrap (Ghidra
  dump + `data/seeds/*.csv` → DB) is the ONE-TIME genesis build, unchanged. This is the
  single gate the design demands; the validator has no row-level entry point, so the
  prospective-DB-state validation reuses the whole-state gate (see §10 D19 + D13).
- **The backend (`data/maintainer-tool/backend/`)** — the Python API over the data-core
  (D14). It exposes the six jobs + browse/view/compare as endpoints, runs the data-core's
  save spine on a confirmed edit, and performs the git commit + push (D16) using the
  injected identity + env credential (D17). It adapts a chosen version tag → the data-core's
  parameters (no DLL server-side). It holds NO validation/SQL/export rule logic — it calls
  the data-core (R3).
- **The frontend (`data/maintainer-tool/frontend/`, `ui/`)** — presentation only (D14). The
  React app renders the navigator, entity detail, version history/compare, field editor,
  create flows, and the field-delta confirm (the seven screens, re-expressed for web in the
  `/ui-design` pass) + the client-side JS `.rdata` resolver (D15); it calls the API. No
  authoring logic.

Dependency direction: frontend → backend (API) → data-core → (schema, validators). The
data-core depends on nothing in the shell (backend or frontend); it is delivery-agnostic.
The round-trip oracle is a data-core test, no UI.

## 6. User stories & acceptance — the full six-job tool <a name="6-user-stories"></a>

v1 is the complete tool. US-1…US-4 below are the load/browse/re-verify/save spine (the
original Job-2 path, unchanged). US-5…US-10 are the rest of the catalog, all in v1. Each
surface is specified visually in [`ui/screens/`](ui/screens/); the story names WHAT, the
screen spec names how it looks. The save spine (validate → write → export → round-trip →
field-delta confirm → atomic commit) is shared by every mutating story (US-3…US-10).

**US-1 — Load.** As a maintainer, I open the web app and it loads the curated entity
set through the data-core (the backend reads the DB/seeds from the mounted checkout).
**Acceptance:** the app loads with no Ghidra / `WHGame.dll` / dump prerequisite
(R2); the curated entity list is shown; if no DB/seeds resolve at the configured
checkout path (D18), the empty state explains why (§7).

**US-2 — Browse & pick.** As a maintainer, I browse the curated entities and select
one to re-verify.
**Acceptance:** the list is searchable/scannable; selecting an entity shows its
current-version row first, with a separate action revealing the full version history
(R8); the three read-only fields (`kcdx_id`, `name`, `valid_from_version`) are
visibly non-editable (R8).

**US-3 — Edit the audit trio.** As a maintainer, I update
`last_verified_at_version`, `verified_by`, `verified_date`, `evidence_kind` for the
selected row.
**Acceptance:** `evidence_kind` is picked from the `policy.md` enum; all four trio
fields move together (the trio is all-set-or-all-null per `policy.md`); inline
validation rejects a malformed `verified_date` / out-of-enum `evidence_kind` /
partial trio BEFORE any write.

**US-4 — Validate, write, export, confirm, commit.** As a maintainer, I save and the
tool lands the change end-to-end.
**Acceptance (the full chain):** shared-validator gate (R3) → atomic DB write
(`db_editor`) → auto-export the three CSVs (`csv_exporter`, diff-preserved) →
round-trip oracle asserts identity → the maintainer sees a **plain-language field delta**
(`field: old → new`, only the changed fields) → on Confirm, the tool commits (DB + 3 CSVs)
by exact path as ONE atomic transaction; on Cancel, nothing lands. **The field delta is
the user-facing acceptance signal** (the literal CSV diff is verified by the round-trip
oracle and lands in the git commit for a reviewer, but is NOT the human's surface — see
§7 + §10 D8). Git is invisible to the maintainer (the result reads "Saved", not a commit
hash).

**US-5 — Edit any existing version's full columns.** As a maintainer, I correct any
column on any existing version row (not just the audit trio) — `module`, `kind`, `rva`,
`signature`, the trio, the six survival columns.
**Acceptance:** all version-row columns editable EXCEPT the identity key
`valid_from_version` (and the entity identity `kcdx_id`/`name`), which stay read-only (R8,
`policy.md`); editing an already-decided version raises a confirmation that it is an EDIT,
not a new version (so a correction is never mistaken for authoring a new game version);
the shared validator gates the write; the save spine commits it.

**US-6 — Create a new version (Job 6).** As a maintainer, I author a new game-version row
for an existing entity, prefilled from a source version.
**Acceptance:** the form prefills ALL columns from a chosen source row (including the audit
trio); `valid_from_version` is the field I set (prefilled from the linked DLL's resolved
version when linked, editable regardless); saving a row identical to its source is BLOCKED
with steering copy that routes me to re-verify the existing row instead of creating a
duplicate (§7); the new row is approval-gated (AP18, §10 D11); the save spine commits it.

**US-7 — Create a new entity (Job 1).** As a maintainer, I author a brand-new entity from
scratch.
**Acceptance:** the tool assigns the next free `kcdx_id` (append-only, no hand-typing —
`policy.md` §"ID assignment"); I author `name` + the first `address_versions` row
(required: `valid_from_version`, `module`, `kind`); the new entity is approval-gated (AP18,
§10 D11); the save spine commits it.

**US-8 — Supersede / deprecate an entity (Jobs 4/5).** As a maintainer, I supersede or
deprecate an entity via its lifecycle flags.
**Acceptance:** supersede sets `superseded_by` + `superseded_at_version` together
(both-or-neither, no self-supersede, no cycle); deprecate sets `is_deprecated` +
`deprecated_at_version` together, with `deprecation_replacement` allowed only when
deprecated; the shared validator enforces pair-integrity + acyclicity (`policy.md`); the
save spine commits it. (This is an UPDATE to an existing entity — NOT approval-gated.)

**US-9 — Compare versions side-by-side.** As a maintainer, I select two or more of an
entity's version rows and compare their full records side-by-side, with the differing
fields clearly marked, and I can edit any version directly from the comparison.
**Acceptance:** the compare reads `address_versions` rows (game-version data rows, NOT git
history) and diffs their columns dynamically; differing fields are marked (a glyph + a row
band, not color-alone — R8/UX); identical fields render plain; the column count drives
dynamic horizontal scroll; editing a column enters the edit-existing flow (US-5).

**US-10 — Verification context (the version pick + the client-side DLL check).** As a
maintainer, I tell the app which game version an edit targets — by picking from a version
dropdown (the default, works from a phone), OR by linking a game DLL on MY machine so the
app reads the version from it — and I am never blocked.
**Acceptance:** the version dropdown is populated from the known game versions (the
`game_versions` tags the server holds); selecting one is the verification context, and
default-selects/marks the matching row. **Linking a DLL is client-side** (§10 D15): the
browser reads a locally-picked DLL via the File API and runs the `.rdata` scan IN THE
BROWSER (a JS port of the resolver), sending the server ONLY the resolved version string —
never the DLL. The resolved version then marks the matching row + is the new-version prefill
source. **No action requires either** — every flow proceeds with just a picked version (or
none → newest-row default, D10), with an advisory "not verified against a DLL" notice; any
resolver failure or unverified state is **overridable** by an explicit "I accept — save
anyway" (verification is advisory; the maintainer is final authority — §10 D9/D12).

**Current-version row resolution (R12).** The "current"/"matches" row is the one whose
`[valid_from, valid_through]` interval contains the resolved ordinal. **The resolver reads
the real version string from a DLL's `.rdata` bytes** (the `release_M_N_BUILD_SUB` intern),
requiring ≥2 agreeing interns — `<2` or disagreeing interns fails (advisory + override,
never a block — D9). **On the web app this scan runs CLIENT-SIDE** — a small JS port of the
resolver in the browser, reading a locally-picked DLL (no upload; only the version tag
crosses the wire). The Python `version_resolver.py` stays the **test-of-record**: a
cross-implementation test asserts the JS port and the Python resolver agree on a known DLL
(D15). **When no version is resolved/picked, the app default-selects the newest authored
row** (highest `valid_from_version`) — deterministic, always-works; no blocking "degraded
mode" (D10).

## 7. UX & states <a name="7-ux-states"></a>

The visual + interaction design — the window skeleton, the interaction laws, the token
system, and every screen's full state set — is specified in
[`ui/design.md`](ui/design.md) + [`ui/screens/`](ui/screens/). That layer is the authority
a builder conforms to (`spec-conformance.md`). This section fixes the cross-cutting state
+ flow requirements every screen there must satisfy (`ux-first-class.md`); the pixel/widget
conventions are the UI layer's.

**The save spine (every mutating story, US-3…US-10).** **Save** = validate the prospective
edit (shared validator R3, against the prospective DB state — D19) + show the
**plain-language field-delta** (`field: old → new`, only the changed fields), NO write.
**Confirm** = ONE synchronous atomic transaction: direct-DB write (D19) → commit the DB →
auto-export the 3 CSVs to `data/db-export/` (diff-preserved — D20) → cheap integrity check →
git commit + push (exact-path). On ANY failure (validation pre-commit, or export/integrity/git
post-commit) **nothing lands** — the deferred-commit `ROLLBACK` undoes a pre-commit failure, a
scoped restore-point undoes a post-commit one, including PK auto-increment reset (D21); the page
waits for the success/failure status. **The field delta is the acceptance signal** the maintainer
reads (D8) — NOT the literal CSV diff (which is integrity-verified and lands in the commit for a
reviewer, invisible to the maintainer). Git is invisible: the result reads "Saved `<entity>
<version>`", never a hash.

**Required states (each screen specifies the ones that apply — `ui/screens/`):**

- **Populated** — the resting view (list, detail, editor, compare).
- **Empty** — no DB/seeds resolved (names where the backend looked: the configured checkout
  path, D18); no entity selected; no search match (each with distinct, cause-appropriate
  copy) — never a blank surface.
- **Loading** — the data-core load / entity load in flight (a one-shot, not a hot path).
- **Validation error** — inline, field-level, the shared validator's verdict on the
  offending field; no write attempted; the error names the cause.
- **Write failure** — the atomic transaction failed and rolled back; the DB + CSVs are in
  their pre-action state; the maintainer sees the failure + that nothing landed.
- **Field-delta confirm** — the acceptance moment: the changed fields as `old → new`, plus
  the AP18 approval acknowledgment for a new entity/version (D11) and the "I accept — save
  anyway" override for an unresolved verify state (D9/D12).
- **Save result** — "Saved" (success) or "Save blocked — files locked, Retry" (a live
  shared-index lock → retry, never a forced reap; §8). In the persistent status bar.
- **Verification-context states** — a picked version (the dropdown default), or "checked
  against your DLL: version `<v>`" (client-side resolve), or "not verified against a DLL"
  (advisory, normal — never a block), or "couldn't resolve version from that DLL (interns
  disagree)" (advisory + override). NOT a "degraded mode" — the app always works without a
  DLL (D9/D10/D15).
- **Edge content** — many version rows (history/compare scroll; horizontal scroll in
  compare when columns exceed width); long names/signatures wrap or truncate without
  reflowing siblings (the layout-stable law — `ui/design.md` law 1).

**Flow & feedback:** every action gives feedback; no silent success, no dead-end error.
The maintainer never reads a raw log — the field delta and the status-bar result are the
signals. A state change updates content in place; nothing jumps (layout stability is law 1
of the UI layer).

**Accessibility & consistency:** the React component library's accessible primitives (the
web equivalent of the desktop widget set), every field/control/list-row keyboard-reachable
and labelled, read-only identity state conveyed by more than color, one consistent layout
idiom, responsive to a phone viewport. Full accessibility + token discipline + the
desktop→web re-expression is `ui/design.md` (the `/ui-design` pass).

## 8. Constraints <a name="8-constraints"></a>

- **Distribution (R9 — web app, D14/D18):** a **Docker image** — a Python (FastAPI/Flask)
  backend serving the API + the built React frontend, over the headless data-core. The git
  checkout (the repo with `data/seeds/` + the reference DB) lives on a **mounted volume at a
  configured path** the container reads/writes/commits; the image carries only the app code.
  The operator provides the volume + the push credential (below). The exact image packaging
  (single image vs a `docker-compose` backend+frontend split) is an explicit-but-open
  sub-decision (D14), not blocking. (Supersedes the PyInstaller `.exe`.)
- **Privacy (R10):** all of `data/maintainer-tool/` is private (the publish-public
  `$PrivateSubpaths` carve-out). The backend freely imports the private data-core.
- **Version resolution (R12, D15):** the `.rdata` version scan (hard intern-agreement,
  interval-contains-ordinal current-row filter) runs **CLIENT-SIDE** — a small JS port in
  the browser, reading a locally-picked DLL via the File API; only the resolved version tag
  reaches the server (no DLL upload). The Python `version_resolver.py` is the
  **test-of-record** (a cross-implementation test asserts the two agree on a known DLL).
  **Verification is advisory, never required** (D9): a picked-from-dropdown version (or the
  newest-row default — D10) works without any DLL; a resolver failure or unverified state is
  overridable by an explicit "I accept — save anyway".
- **The backend commits + pushes on Confirm — under the repo's git-concurrency discipline**
  (`concurrency-git.md`, D16). On a confirmed edit the backend stages by **exact path** (only
  the DB + the three CSVs — never `-A`/`.`/`-u`), **respects a live `index.lock`**
  (block-and-retry, never reap), authors the commit message, AND **pushes to the GitHub
  remote** so the edit is durable beyond the container. The commit **author identity** comes
  from the request context the operator's login layer supplies; the **push credential** is
  **env-injected** — the app is auth-ready, the operator wires the login/credential (D17;
  auth/hosting out of scope). A documented dev default lets the app boot + run locally
  without the operator's auth layer (a fallback identity, push skippable) for testing.
- **Validation is the data-core's, single-source (R3):** every invariant runs through the
  shared validator module; the web backend and the importer both consume it (the data-core
  unchanged from desktop); no rule is reimplemented in the frontend or the backend.

## 9. Scope — in / out <a name="9-scope"></a>

**In (v1 — the complete six-job tool):** the DB-direct management of the whole reference
DB end-to-end — load, browse/search/filter, view any entity's full record + all version
rows, side-by-side version compare, and the full job catalog: **Job 1** (create entity),
**Job 2** (re-verify the audit trio), **Job 4** (supersede), **Job 5** (deprecate),
**Job 6** (create version), plus **editing any existing version's full columns** (general
correction). The advisory verification context (D9) — a version dropdown + the client-side
DLL check (D15). The shared save spine (validate → write → auto-export → round-trip →
field-delta confirm → atomic commit + push). The data-core units (`csv_exporter.py`,
`db_editor.py`, plus the editor shapes each job needs — INSERT for Jobs 1/6, the lifecycle
UPDATE for Jobs 4/5, the audit-trio/full-row UPDATE for Job 2 / US-5) + the round-trip
oracle test. **The web app (D14): the Python backend** (the API over the data-core + the
git commit/push + the auth-ready seams), **the React frontend** (the 7 screens re-expressed
for web + the client-side JS resolver), **and the Docker image + volume layout** (D18). The
UI design layer is re-expressed desktop→web in the `/ui-design` pass (`ui/`).

This v1 spans the catalog; it may be BUILT in steps (the data-core before the backend +
frontend; the jobs in a dependency-sensible order, `incremental-delivery.md`), but no
catalog job is out of scope. (Supersedes the first draft's Job-2-only MVP scope — §10 D7.)

**Out of v1 (genuinely not built — the operator's, per D17):**

- **Auth / login / hosting / the web portal / the reverse proxy / TLS / the push
  credential itself.** The app exposes auth-ready seams (an env-injected push credential +
  a request-context commit identity, D17); the operator wires the login + provides the
  credential + hosts the container. The build delivers the Docker image + the seams, not
  the surrounding deployment.

**Out of v1 (deferred, as before):**

- **Job 3 — the new-game-version campaign** (the bulk delta report unchanged/moved/gone
  against a fresh dump dir). Job 3 is a batch *workflow* over the same primitives v1
  builds (re-verify / deprecate / supersede across many entities at once); the per-entity
  primitives are in v1, the campaign orchestration UI is not.
- **Driven evidence flows (R5)** — `pattern_scan` AOB-uniqueness automation, the
  `live_test_plugin` coverage convention. (The `evidence_kind` values are authorable in
  v1; the automated evidence-gathering flows are not.)
- **The multi-file rename-sequence journal (R11)** — reserved, not built until the
  atomic-rename window bites in practice.

**Explicitly NOT built:** any CSV-editor surface (the inversion makes the DB the
authoring surface from day one — the R1/R6 CSV-editor framing is superseded, not
deferred). No throwaway CSV-write path, no later source-of-truth migration.

## 10. Decision record <a name="10-decision-record"></a>

Settled in the design dialogue 2026-06-02 (each the user's call, per
`design-authority.md`):

| # | Decision | Settled value | Rejected alternative |
|---|---|---|---|
| D1 | Source of truth | DB authoritative; CSVs auto-exported as the git-tracked diff layer (derived, never hand-edited). | DB tracked-as-binary + CSVs deleted (gives up readable git diffs + forces a new review/publish story). |
| D2 | Round-trip contract | Bidirectional byte-identity (`import(export(DB))==DB` AND `export(import(CSVs))==CSVs`). | DB→CSV export only (weaker oracle; allows DB-only fields → drift). |
| D3 | Sequencing | DB-direct from day one. No CSV-editor surface ever built. ~~MVP is Job 2 only.~~ **(Job-2-only superseded by D7 — v1 is the full catalog.)** | Ship CSV-editor MVP first, flip later (throwaway R11 CSV-write path + a migration phase). |
| D4 | Data-layer seam | Headless data-core in `seeds_shared/` (`csv_exporter` + `db_editor`); GUI is a thin shell. | Logic inside the maintainer-tool package (backwards importer dependency; GUI-entangled core). |
| D5 | Save/commit UX | validate → write DB → auto-export → round-trip → confirm → commit. ~~show the CSV diff for confirm/revert.~~ **(The confirm surface is a plain-language field delta, not the CSV diff — superseded by D8.)** | Silent export + success toast (no edit-time view of the actual change). |
| D6 | Commit boundary | The tool commits on Confirm (exact-path staging + live-lock respect + self-authored message). **(Web pivot D16: the SERVER backend commits to its volume-mounted checkout AND pushes to GitHub on confirm; the local-desktop framing is superseded — the discipline is unchanged, the writer moved to the server.)** | Tool writes files only, committing left to the separate `/commit` flow. *(Agent flagged the parallel-chat index-race concern per `concurrency-git.md`; user chose tool-commits with the guards baked in.)* |

Settled in the UI design dialogue 2026-06-02 (the second pass, building the UI layer):

| # | Decision | Settled value | Rejected alternative |
|---|---|---|---|
| D7 | v1 scope | **v1 is the complete six-job tool** (Jobs 1/2/4/5/6 + edit-any-version + compare), built in steps but nothing deferred. | Job-2-only MVP with Jobs 1/3/4/5/6 deferred to later phases (the first draft). User: "the entire tool will be v1 when complete… all of this is required." |
| D8 | The confirm surface | A **plain-language field delta** (`field: old → new`, only changed fields) is the human's acceptance signal; the literal CSV diff is oracle-verified + lands in the commit, but is not shown. | Show the git-style CSV diff as the acceptance surface (the maintainer reads CSV cells — less clarity); or field-delta + collapsible CSV diff. |
| D9 | Verification | **Advisory, never required.** Any action proceeds with just a picked version (or none) and a "not verified against a DLL" notice; a resolver failure or unverified state is overridable by an explicit "I accept — save anyway" (the maintainer is final authority over a tool error). *(Web pivot D15: the DLL check is client-side; verification is otherwise unchanged.)* | DLL required for version-stamping actions (blocks work); or a blocking "degraded mode". |
| D10 | Default row (unresolved) | **Newest authored row** (highest `valid_from_version`) is default-selected when no version is resolved/picked. | Nothing pre-selected until the maintainer picks. |
| D11 | New-row approval (AP18) | Creating a new entity (Job 1) or new version (Job 6) is **approval-gated in the confirm step** (an explicit acknowledgment before it lands); an UPDATE is not gated. | Treat a new row like any UPDATE (no approval gate) — violates `policy.md` AP18. |
| D12 | New-version "nothing changed" | Saving a new version identical to its source is **blocked with steering copy** routing the maintainer to re-verify the existing row instead of creating a duplicate. | Silently allow a duplicate version row; or clear the audit trio on a new version (the user chose prefill-all + the nothing-changed guard). |
| D13 | How `db_editor` reuses the single validator gate | `db_editor` reuses the existing validated applier's **write helpers + the single whole-state validator gate** — zero rule logic in `db_editor`, the gate covers row-level AND cross-row invariants, one path generalises to all six jobs. *(Settled mid-build 2026-06-02; the validator has no row-level entry point and a per-row check could not see the cross-row invariants Jobs 4/5/6 need.)* **⚠ Mechanism superseded by D19 (2026-06-03):** the original D13 mechanism wrapped `import_to_sqlite.run_apply` — the seed-CSV-REBUILD bridge (export the DB → edit a temp seed CSV → re-apply by diffing the prospective seed against the DB). That contradicts D1 (the DB is the originator, not rebuilt from seeds). D19 corrects it: `db_editor` performs **direct-DB INSERT/UPDATE** through the applier's existing `_apply_one_db` write helpers (fed edit parameters, not CSV-diff-derived actions), inside the deferred-commit transaction. The gate-reuse insight (one whole-state validator, all six jobs) is UNCHANGED and preserved; only the rebuild-bridge mechanism is replaced by direct writes. See D19. | A — export→validate→commit per edit. B — validate-before-open. C — row-level validator entry (can't see cross-row invariants). |

Settled in the web-app pivot dialogue 2026-06-02 (the third pass — the tool becomes a
hostable web app instead of a PySide6 desktop `.exe`):

| # | Decision | Settled value | Rejected alternative |
|---|---|---|---|
| D14 | Delivery surface | A **Dockerized web app** — a Python (FastAPI/Flask) backend wrapping the headless data-core + a **React frontend** (a component library strong on forms/lists/dropdowns/modals; the 7 screens map to its table/form/modal primitives). Reachable from any browser incl. a phone. *(Sub-decisions explicit-but-open: the component lib (Mantine vs MUI); the image packaging (single image vs `docker-compose` split).)* | PySide6 desktop `.exe` (one machine, no phone/web — the superseded plan). A non-Python backend shelling to the data-core (a process boundary + two languages for no gain). |
| D15 | DLL version resolver | **Client-side** — a small JS port of the `.rdata` scan runs in the browser on a locally-picked DLL (File API, no upload); only the resolved version tag reaches the server. Python `version_resolver.py` stays the test-of-record (a cross-impl agreement test). A version **dropdown** is the default (phone-friendly); link-a-DLL-to-verify is the at-a-machine path. | Server holds the game DLL(s) + resolves server-side (ships multi-GB binaries into the image; licensing; only versions the server has). Pyodide (CPython-in-WASM) to run the Python resolver unchanged (~6–10 MB runtime for a ~30-line scan). Dropdown-only, no client resolver in v1. |
| D16 | Server commit + push | The backend **commits to its volume-mounted checkout AND pushes to GitHub on confirm** (D6's exact-path / live-lock / self-authored-message discipline, unchanged — the writer moved to the server). Push so edits are durable beyond the container. | Commit locally, push as a separate/manual step (a container's un-pushed commits are fragile if it is recreated). |
| D17 | Auth seam (auth out of scope) | The app is **auth-agnostic but auth-ready**: the commit **author identity** comes from the request context the operator's login supplies; the **push credential** is **env-injected**. No login/auth/hosting/portal code from the build — only the documented seams (env var names, the identity field the API expects). A dev default lets the app boot + run locally without the operator's auth, for testing. | Build the auth/login/hosting (out of the user's stated scope — the user wires it). No dev default (the app can't be tested standalone before auth is wired). |
| D18 | Container data layout | The git checkout (`data/seeds/` + the reference DB) lives on a **mounted volume at a configured path** the container reads/writes/commits; the image carries only the app code; the operator provides the volume + the push credential. | Bake the repo into the image (heavy, stale). Bind-mount the host's existing clone (tighter host coupling) — recorded as an operator option, not the app's assumption. |
| D19 | The maintainer write mechanism (DB-direct, not seed-rebuild) | **A maintainer edit is a DIRECT INSERT/UPDATE on the DB rows** — the DB is the originator (D1). `db_editor` performs the six jobs as direct DB ops through the applier's EXISTING `_apply_one_db` write helpers (the av/names/survival INSERTs, the interval-close, the function-kind promote-vs-mint + fingerprint-carry + `BaselineRefusal` gate, the per-DB column projection, the FK-id resolution-never-minting), fed **edit parameters** instead of CSV-diff-derived actions — NOT the export-seed→edit→`apply_seeds`-rebuild bridge (the original D13 mechanism). The **same single validator gate** runs, re-targeted to the **prospective DB state** (the DB as it would be after the write) instead of seed CSVs — every invariant (tuple-uniqueness, audit-trio integrity, supersession pair-integrity + acyclicity, FK closure, enum/required) still holds. The write runs inside the **deferred-commit transaction (4a, reused verbatim — connection-level)**; a PRE-commit failure (validation) `ROLLBACK`s the held txn, discarding the change including `sqlite_sequence`/PK-autoincrement bumps (nothing was committed). A POST-commit failure (export/CSV/git — which run AFTER the irreversible DB commit, because the export reads the committed DB) is undone by a **scoped restore-point** captured before the commit (D21) — NOT by the deferred-commit rollback (which is gone once the txn commits). Together they give the robust **rollback on ANY failure** (reset auto-increments etc. — D21). After a successful write, **`csv_exporter.export_seeds` exports the DB → the derived CSV record at `data/db-export/`** (diff-preserved). **`create-version`-at-a-new-game-tag now WORKS** — a direct INSERT (the new `game_versions` row + the interval-close + the new `address_versions` row) bypasses the seed-rebuild's `GAME_VERSION_TAG`/baseline-matcher gate that materialised zero rows; the UX is prefill-from-the-last-working-version + a version-bump diff. The original **`run_rebuild` bootstrap** (Ghidra dump + `data/seeds/*.csv` → DB, a one-time build) is UNCHANGED. *(Settled 2026-06-03 — corrects D13's mechanism to match D1's vision; the gate-reuse insight is preserved, only the rebuild bridge is replaced by direct writes. The 8 load-bearing behaviors above must all be preserved by the direct path — they ARE `_apply_one_db`'s helpers, so reusing them preserves them.)* | The seed-rebuild bridge (the original D13 — contradicts D1, can't create a new-tag version, an indirection per edit). A parallel hand-written direct-SQL path (re-implements `_apply_one_db`'s 8 behaviors + risks drift from the bootstrap's write logic). |
| D20 | Seeds vs the derived export (the two CSV roles, separated) | The three CSVs serve TWO roles, now **physically separated**. **`data/seeds/*.csv` = the bootstrap seeds** (genesis): `run_rebuild` reads them for ONE FINAL from-dump rebuild to construct the DB, after which they are frozen genesis input — never written by the maintainer tool. **`data/db-export/*.csv` = the derived export record** (the living history): the maintainer tool exports the DB here on every confirmed save (diff-preserved) and git-commits it — the git-trackable diff/review layer D1 names. The DB is the originator; `data/db-export/` is its exported record, distinct from the seeds the DB was born from. `data/db-export/` is a new private path (defaults to private — the publish allowlist is opt-in). | Keep writing the export back to `data/seeds/` (conflates the genesis seeds with the living export; "seeds" misleads — it implies the DB is rebuilt FROM them, the exact wrong model). Rename/move `data/seeds/` itself (a large survivor sweep across the bootstrap pipeline + allowlist + every reference, for the incremental path's benefit only — deferred; the seeds keep their path + name for the final rebuild). |
| D21 | The robust rollback — pre-commit vs post-commit (two mechanisms) | "On Confirm one atomic transaction; on ANY failure nothing lands, including PK auto-increment reset" (the user's explicit, non-negotiable requirement) is delivered by **TWO mechanisms split at the irreversible DB commit**, because the data-core's `commit(handle)` COMMITs+closes both connections one-way (after it, the deferred rollback is gone) AND the export must run POST-commit (`export_seeds` opens its own fresh connection and cannot read the uncommitted held txn). **(a) PRE-commit failure** (validation) → the **deferred-commit `ROLLBACK`** (4a) discards the held txn incl. `sqlite_sequence`/PK bumps — nothing committed. **(b) POST-commit failure** (export / integrity / git) → a **SCOPED restore-point**, a DATA-CORE capability (D13/law 6 — it owns the write semantics + the open connections + knows exactly which rows it touched): before the irreversible commit it captures ONLY the touched rows (across `address_versions` / `survival` / `game_versions` / `address_names` in both DBs) + each DB's `sqlite_sequence` values + the `data/db-export/` CSVs; on a post-commit failure it restores those rows, resets the sequence, and reverts the CSVs. A few-KB capture regardless of DB size — the ~1.3GB DEV DB is never copied. | A full-file snapshot (`shutil.copy2` both DB files before commit) — same guarantee but copies the ~1.3GB DEV DB per committing confirm, the worse mechanism the cornerstone order (UX > Capability > Performance; the cheaper mechanism for the same guarantee wins) rejects. Accept "git failure leaves the DB ahead, retryable" — contradicts the user's explicit "nothing lands" (a deferred-correctness-gap, AP). Put the restore-point in the BACKEND — it would hold write-semantics rule logic (which rows each job touches), a D13/law-6 violation. *(Settled 2026-06-03 — corrects the earlier D19/§7 text that wrongly said the deferred-commit ROLLBACK alone covers a post-commit failure; surfaced via architect-review, the user chose the scoped data-core restore-point.)* |

Settled in the schema-finality dialogue 2026-06-03 (the fourth pass — making the
reference-DB schema flat + final so it never migrates again except on a new kind):

| # | Decision | Settled value | Rejected alternative |
|---|---|---|---|
| D22 | The reference-DB schema is FLAT + FINAL | The reference DB is **one flat table per concern with one typed column per fact** — `address_names` (entity-stable) + `address_versions` (per-version-interval resolve facts), no polymorphic / discriminated-union / EAV structure anywhere. **The `survival` sibling table is folded into `address_versions` and deleted** (it was the design's only polymorphic structure — a `kind_form` discriminator + mutually-exclusive payload — and the recurring schema-churn source): its genuinely-survival-only columns (`aob`, `anchor_string`, `rule`, `slot_count`, `expect_unique`, `derives_from`) move up as nullable typed columns; `survival.content_hash`/`length` are DROPPED as redundant (they were a verified row-for-row copy of the av row's body fingerprint — 157/157 rows identical, zero independent values — so the existing `address_versions.content_hash`/`length` serve both the resolve path and the function-hash survival check); `kind_form` is deleted (the `kind` column already determines which cells a row populates); `derives_from` folds in as a nullable self-FK (the same shape as `valid_through` / `superseded_by` — not polymorphism). **Comprehensiveness is a checkable invariant, not a hope:** the schema is the back-projection of the engine's `ResolveResult` struct (`src/refdb.h`) — every column maps to a field a resolve site consumes; a column with no consuming field is dead weight, a `ResolveResult` field with no backing column is a hole (caught at decode/compile, never silently). The **`kind` enum (`data/seeds/policy.md`) is the CLOSED universe of resolvable things**; **a genuinely new kind is the ONLY event that grows the schema** — and it is a deliberate, surfaced, AP18-class migration with a fixed checklist (§11), never a silent or frequent churn. "Never migrate again" = "migrate only on a new kind, which is rare and deliberate" (stated plainly — not literal immutability). Full schema inventory, the fold mapping, the contract, and the new-kind migration checklist: §11. | A flat schema + a nullable `extra` JSON/TEXT escape column (absorbs any future fact with zero migration — but re-introduces an opt-in polymorphism bag against the "every item its own column / no polymorphic relationships" goal; the user chose the fully-legible no-escape schema, accepting a rare per-new-kind migration over an opaque bag). A generic `(kcdx_id, version, attr_name, attr_value)` EAV attribute table (truly never-migrate, but the OPPOSITE of "every item its own column" — no typed columns, stringly-typed rows, the maintainer tool can't show typed fields, the engine loses its typed contract). Keeping the `survival` sibling (preserves a hot/cold resolve-path separation, but it is the polymorphic churn source the goal exists to kill, and the separation buys ~nothing for a once-per-`refdb::Open()` cold read — UX > Performance, `cornerstones.md`). |

These supersede the earlier repo-owns-the-format / CSV-editor decisions recorded in
`requirements.md` R1/R6 and `plan.md` §"two-phase", the Job-2-only MVP framing, **the
PySide6 desktop-`.exe` delivery (D14–D18 — the tool is now a hostable web app), and the
`survival` sibling-table shape (D22 folds it into `address_versions`).**

## 11. The reference-DB schema — flat + final <a name="11-schema-flat-final"></a>

The schema the maintainer tool authors into. Settled D22. The goal: **never migrate
the schema again — except when a genuinely new `kind` of resolvable thing is added,
which is rare and deliberate.** This section is the durable guarantee that makes that
checkable rather than hopeful.

### 11.1 The shape — two flat tables, one typed column per fact

The reference DB is **two flat tables, no polymorphic structure anywhere**:

- **`address_names`** — one row per curated entity (entity-stable facts): the stable
  `id` (== the `kcdx_id` plugins reference), `name`, the supersession pair
  (`superseded_by` + `superseded_at_version`), the deprecation pair (`is_deprecated` +
  `deprecated_at_version` + `deprecation_replacement`), `notes`.
- **`address_versions`** — one row per (entity, version-interval) with every per-version
  RESOLVE FACT as its own typed column: identity (`kcdx_id`, `kind`, `module_id`,
  `valid_from`, `valid_through`), location (`rva`, `offset`, `vtable_slot`,
  `struct_offset`, `value`), ABI (`signature`, `observed_arg_slots`,
  `caller_reg_arg_count`, `caller_arg_agreement`, `length`), trust (`last_verified_at_version`,
  `verified_by`, `verified_date`, `evidence_kind`), survival/re-find (`content_hash` +
  `length`, OR `aob` / `anchor_string` / `rule` / `slot_count` / `expect_unique` /
  `derives_from`).

`kind` (the address-kind enum) gates which cells a given row populates — a `function`
row carries `rva` + `content_hash`/`length`; a `callsite` carries `offset` + `aob`; a
`vtable_index` carries `vtable_slot` + `value`; etc. The cells a kind does not use are
NULL. **This per-kind NULL-ing is NOT polymorphism** — every column is a plain typed
column present on every row; `kind` is just authored metadata, not a discriminator that
changes the row's structure. There is no discriminated-union table, no `kind_form`
sub-type table, no EAV attribute bag, no JSON blob. One row = every fact that row can
carry, each its own cell. This is what makes the maintainer tool a flat row editor with
no joins and no per-kind form-switching beyond which cells are relevant.

### 11.2 The `survival` fold (D22) — the one structure removed

The former `survival` sibling table (a `kind_form` discriminator + mutually-exclusive
payload columns, the design's only polymorphic structure and its recurring
schema-churn source) is folded into `address_versions` and deleted:

| Former `survival` column | Folds to | Note |
|---|---|---|
| `aob`, `anchor_string`, `rule`, `slot_count`, `expect_unique` | new nullable typed columns on `address_versions` | the genuinely-survival-only facts; gated by `kind` like every other location/ABI cell |
| `derives_from` | a nullable self-FK column on `address_versions` (→ `address_versions.id`) | the survival-DAG edge; same shape as the existing `valid_through` / `superseded_by` self-FKs — not polymorphism |
| `content_hash`, `length` | **dropped** (redundant) | were a verified row-for-row copy of the av row's body fingerprint (157/157 rows identical, never independently set); the existing `address_versions.content_hash`/`length` serve both the resolve path and the function-hash survival check |
| `kind_form` | **deleted** (the discriminator) | `kind` already determines the survival form (`function` → reuse the body hash; `callsite`/`instruction_anchor` → `aob`; `string_anchor` → `anchor_string`; `data_slot` → `rule`; `vtable_base` → `slot_count`; `vtable_index` → deferred) |
| `id`, `address_version_id` | gone | the 1:1 sibling row is gone; the facts live on the av row itself |

The fold loses nothing: every survival fact either moves to its own av column or was a
proven duplicate of an existing av column. (The hot/cold resolve-path separation the
sibling provided is given up deliberately — survival is a once-per-`refdb::Open()` cold
read, and `cornerstones.md` ranks authoring-UX + schema-stability above that
performance nicety.)

### 11.3 The comprehensiveness contract — `ResolveResult` is the authority

How you KNOW the schema is comprehensive (and that a hole cannot open silently):

**The schema is the back-projection of the engine's `ResolveResult` struct
(`src/refdb.h`).** Every resolve site in the engine consumes a `ResolveResult` (the
engine serves from in-memory maps built from the DB at `refdb::Open()`, never SQL at
runtime — `address-library.md`). So:

- **Every `address_versions` column maps to a `ResolveResult` field** a resolve site
  reads. A column with no consuming field is dead weight (remove it).
- **Every `ResolveResult` field maps back to a column** (or is derived at decode from
  columns that exist — e.g. the verification state). A field with no backing column is
  a HOLE — and it surfaces at `DecodeVersionRow` / compile time, never silently in
  production.

This turns "is the schema comprehensive?" into a mechanical check anyone can run: diff
the `ResolveResult` struct against the `address_versions` column set; a field on one
side with no counterpart on the other is the defect. Comprehensiveness is an invariant
the build can hold, not a judgment call.

### 11.4 The closed universe — `kind` is the only growth axis

**The `kind` enum (`data/seeds/policy.md` §"kind") is the closed set of things the
engine resolves by address.** Everything the kcdx engine references by address — a
function, a callsite, a vtable slot, a vtable base, a data slot, a string anchor, an
instruction anchor — is one of those kinds. A new entity at an existing kind is pure
data (an AP18-gated row, zero schema change). A new *location form* for an existing
kind that an existing column already expresses is also zero-schema. **The ONLY event
that grows the schema is a genuinely new `kind`** — a new class of resolvable thing not
expressible by any current kind's columns. That is rare, deliberate, and surfaced (a
design decision the user owns) — never a silent or incremental churn.

### 11.5 The new-kind migration checklist (the only sanctioned schema change)

When a genuinely new `kind` is added (the rare, deliberate event), the migration is
mechanical, not archaeology — do all of, in one coordinated change:

1. **`ResolveResult`** (`src/refdb.h`) — add the field(s) the new kind's resolve sites
   consume (append-only, per the struct's append convention).
2. **Schema** (`seeds_shared/schema.py`) — add the new typed column(s) to
   `address_versions` (+ the USER projection allowlist if the column ships to USER) and
   the new value to the `kind` enum.
3. **Importer** (`import_to_sqlite` / the row + survival builders) — populate the new
   column(s) for the new kind; gate the fingerprint-vs-NULL / promote-vs-mint branch on
   the new kind if it carries a body hash.
4. **Exporter** (`csv_exporter.py`) — round-trip the new column(s) to/from the seed CSV
   (byte-identity preserved).
5. **Engine SELECT / `DecodeVersionRow`** (`src/refdb.cpp`) — decode the new column(s)
   into the new `ResolveResult` field(s).
6. **Baseline re-capture** (`test_rebuild_oracle.py --capture`) — deliberate + inspected,
   documented in the oracle's BASELINE PROVENANCE log (the new column drifts the
   `address_versions` hash; confirm nothing else moved before recording).

A change that is NOT a new kind (a new entity, a re-verify, a value correction) touches
NONE of the above — only data. That asymmetry is the point: the schema is stable; the
data grows freely.
