# Maintainer tool — design (DB-direct authoring with CSV auto-export)

> **Status:** v1 (settled). Date: 2026-06-02.
> **Scope:** the Address Library maintainer sub-system — `data/maintainer-tool/`,
> `data/seeds/`, and the `data/refdata-extractor/python/` toolchain that links them.
> This is the data sub-repo's own design doc; it is NOT `docs/design.md` (that doc
> is the v0.1 *engine* design, superseded and out of scope here).
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
6. [User stories & acceptance — the Job-2 MVP](#6-user-stories)
7. [UX & states — the Job-2 screen](#7-ux-states)
8. [Constraints](#8-constraints)
9. [Scope — in / out / deferred](#9-scope)
10. [Decision record](#10-decision-record)

---

## 1. Vision <a name="1-vision"></a>

Move the Address Library off hand-edited seed CSVs: the maintainer edits the
reference DB **directly** through a GUI, which **auto-exports** the three seed CSVs
as a deterministic, git-tracked diff layer on every committed change, guaranteed
correct by a **bidirectional byte-identity round-trip**.

**v1 success criteria.** A maintainer re-verifies one curated entity at the current
game version end-to-end through the GUI — edits the DB directly, the validator gates
the write, the CSVs auto-export, the round-trip oracle confirms identity, the
maintainer sees the exact git diff a reviewer will see, and the change commits — with
no hand-edit of any CSV at any point.

**The end-state this moves toward.** Today the CSV is the authored source and the DB
is its generated cache (`policy.md`, `address-library.md`). This design inverts that:
the DB becomes the working/authoring surface, the CSVs become a derived export. v1
ships the inversion for ONE workflow (Job 2); later phases extend it to the rest of
the six-job catalog (`requirements.md` R7).

## 2. Glossary <a name="2-glossary"></a>

| Term | Meaning |
|---|---|
| **Reference DB** | `reference.sqlite` (USER, curated ~143 entities) + `reference-dev.sqlite` (DEV, bulk superset). Generated today; the **authoritative authoring surface** under this design. |
| **Seed CSVs** | the three files under `data/seeds/` (`module_seed.csv`, `address_names_seed.csv`, `address_versions_seed.csv`). Today hand-authored; under this design a **deterministic export** of the DB — the git-tracked diff/review layer, never hand-edited. |
| **The round-trip** | `import(export(DB)) == DB` AND `export(import(CSVs)) == CSVs`. The correctness oracle that makes the DB↔CSV pair safe. |
| **Data-core** | the headless, Qt-free authoring logic in `seeds_shared/` (validators, row-builder, the new exporter + DB-editor). The GUI is a thin shell over it. |
| **Job 2** | re-verify one curated entity at the current game version (update the audit trio). The v1 MVP workflow (`requirements.md` R6). |
| **Audit trio** | `last_verified_at_version` + `verified_by` + `verified_date` + `evidence_kind` — the per-row verification record (`policy.md` §"Verification audit trail"). |
| **The tool** | the PySide6/Qt6 GUI, PyInstaller-bundled single `.exe`, private, living in `data/maintainer-tool/` (`requirements.md` R9, R10). |

## 3. The source-of-truth inversion <a name="3-source-of-truth-inversion"></a>

**Decision: DB authoritative; CSVs auto-exported as the git-tracked diff layer**
(derived, never hand-edited).

```
AUTHORING:   tool -> reference.sqlite        (direct, validated edits)
                   |
                   v  (deterministic export on every committed change)
GIT-TRACKED: data/seeds/*.csv                (derived, diffable, reviewed)
                   |
                   v  (import, the existing baseline path)
GENERATED:   reference.sqlite + reference-dev.sqlite
```

What the inversion changes:

- **The CSVs survive** as a derived export. The git/review/publish-allowlist
  machinery is untouched: `data/seeds/` stays a private carve-out (the
  publish-public allowlist), `policy.md`'s file-format rules (UTF-8,
  `QUOTE_MINIMAL`, `#`-comments, header-literal column order) still describe the
  exported shape, and a reviewer still reads a human-readable CSV diff.
- **The CSV is no longer hand-edited.** The tool writes the DB; the export writes
  the CSVs. `policy.md` §"DB additions require explicit approval" (AP18), the audit
  trio, supersession/deprecation pair integrity, and the survival columns are now
  invariants on the **DB-write path** — the same rules, a different surface.
- **No new publish-boundary story is needed** — the boundary is unchanged because
  the CSVs still exist and still carry the published projection.

What the inversion does NOT change: the column-level authoring law (`policy.md`),
the survival design (`fingerprint-per-kind.md`), the `.rdata` version resolver
(`plan.md` §7 / `requirements.md` R12), the privacy carve-out (R10), or the single
`.exe` distribution (R9).

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
  in the CSV. The export cannot invent a column; the import cannot drop one. (A
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

**Decision: a headless data-core in `seeds_shared/`; the GUI is a thin shell.**
Satisfies `headless-testable.md` by construction (the whole authoring path is
exercisable with zero Qt). The importer reuses the exporter.

```
data/refdata-extractor/python/
  seeds_shared/                  (headless, Qt-free, fully unit-testable)
    schema.py        validators.py    row_builder.py    dict_codec.py
    version_resolver.py            (.rdata scan — exists)
    csv_exporter.py     <- NEW: DB -> the 3 CSVs, diff-preserved (R11)
    db_editor.py        <- NEW: validated, atomic DB-edit transactions
    (round-trip oracle test exercises csv_exporter + db_editor)
  import_to_sqlite.py            (reuses csv_exporter for any DB->CSV need)

data/maintainer-tool/
  <the PySide6 GUI — a thin presentation shell that CALLS the data-core>
  (Qt widgets/screens only; NO authoring logic lives here)
```

Each new unit's single responsibility (`structure-by-responsibility.md`):

- **`csv_exporter.py`** — given a DB, produce the three seed CSVs deterministically,
  preserving the diff (row order, comments, quoting, newline). Its sole job is
  DB→CSV; it owns the diff-preservation contract.
- **`db_editor.py`** — apply a validated, atomic edit transaction to the DB (the
  Job-2 audit-trio UPDATE for v1; later jobs' INSERT/PROMOTE shapes per `plan.md`
  §3 in later phases). It runs the shared validator (R3) BEFORE any write; a
  validation failure aborts with no write.
- **The GUI (`data/maintainer-tool/`)** — presentation only. It renders the entity
  list / row view / edit form / diff-confirm view and calls down into the data-core.
  It holds no validation, no SQL, no export logic.

Dependency direction: GUI → data-core → (schema, validators). The data-core depends
on nothing in the GUI. The round-trip oracle is a data-core test, no Qt.

## 6. User stories & acceptance — the Job-2 MVP <a name="6-user-stories"></a>

**US-1 — Load.** As a maintainer, I launch the tool and it loads the curated entity
set through the data-core.
**Acceptance:** the tool launches with no Ghidra / `WHGame.dll` / dump prerequisite
(R2); the curated entity list is shown; if no DB/seeds resolve at `<exe-dir>/../seeds/`
or the DB path, the empty state explains why (§7).

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
round-trip oracle asserts identity → the maintainer sees the git-style CSV diff (the
exact changed cells) → on Confirm, the tool commits (DB + 3 CSVs) by exact path; on
Revert, nothing lands. The exported diff IS the user-facing acceptance signal.

**Current-version row resolution (R12).** The "current row" shown is the one whose
`[valid_from, valid_through]` interval contains the linked module's `.rdata`-resolved
ordinal. Unlinked module → degraded mode: show ALL rows for the entity with a
"module not linked; showing all versions" notice (R12). The intern-agreement check
refuses a DLL with `<2` or disagreeing version interns.

## 7. UX & states — the Job-2 screen <a name="7-ux-states"></a>

The MVP is one screen with a clear flow; every state below is in scope
(`ux-first-class.md`).

**Visible states:**

- **Populated** — the curated entity list; on selection, the entity's current-version
  row with the audit-trio fields editable and `kcdx_id`/`name`/`valid_from_version`
  rendered read-only (visually distinct, not merely disabled-looking).
- **Empty** — no DB/seeds resolved (wrong working dir, missing files): a message
  naming where the tool looked (`<exe-dir>/../seeds/`, the DB path) and what to do,
  not a blank window.
- **Loading** — the data-core load in flight: a brief progress indication (the load
  is a one-shot, not a hot path).
- **Validation error** — inline, field-level, on the offending trio field; no write
  attempted; the Save action is the gate, the error names what's wrong (bad date
  shape, out-of-enum evidence kind, partial trio).
- **Write failure** — the atomic DB transaction failed and rolled back; the seeds and
  DB are in their pre-action state; the maintainer sees the failure + that nothing
  landed.
- **Diff-confirm** — after a successful write+export+round-trip: the git-style CSV
  diff (only the changed cells) with Confirm / Revert. This is the acceptance moment.
- **Commit result** — Confirm → committed (short-hash shown) or commit-blocked (a
  live shared-index lock → retry guidance, never a forced reap; §8).
- **Edge content** — an entity with multiple version-history rows (post-Job-6
  future): the all-versions view handles zero/one/many rows; long names/signatures
  don't break the layout.

**Flow & feedback:** Launch → (load) → pick entity → see current row → edit trio →
Save → (validate → write → export → round-trip) → see diff → Confirm/Revert → commit
result. Every step gives feedback; no silent success, no dead-end error. The maintainer
never reads a raw log — the diff and the result line are the signals.

**Accessibility & consistency:** standard Qt6 widgets, keyboard-reachable form
fields and the Confirm/Revert actions, labels on every field, read-only state
conveyed by more than color. The screen uses one consistent Qt layout idiom, not a
one-off per control. (The concrete Qt component/style conventions are the tool's to
settle as it is built; this doc fixes the states + flow, not the pixel styling.)

## 8. Constraints <a name="8-constraints"></a>

- **Distribution (R9):** single self-contained Windows `.exe` (PyInstaller bundles
  Python + PySide6 + the data-core). Lives in `data/maintainer-tool/`; resolves seeds
  via `<exe-dir>/../seeds/`. Release artifact, gitignored, published on the private
  GitHub Releases page.
- **Privacy (R10):** all of `data/maintainer-tool/` is private (the publish-public
  `$PrivateSubpaths` carve-out). The tool freely imports the private data-core.
- **Version resolution (R12):** per-module linked DLL, `.rdata` version scan, hard
  intern-agreement, sidecar cache `data/maintainer-tool/.maintainer-tool-cache.json`
  (gitignored, next to the `.exe`), interval-contains-ordinal current-row filter,
  degraded mode when unlinked.
- **The tool commits on Confirm — under the repo's git-concurrency discipline**
  (`concurrency-git.md`). The tool is another writer of the shared `.git`/index, so
  its commit MUST: stage by **exact path** (only the DB + the three CSVs — never
  `-A`/`.`/`-u`); **respect a live `index.lock`** (block-and-retry, never reap a live
  lock); author its own commit message. This is the existing committer discipline
  applied to the tool, not a new policy. (A maintainer machine not running parallel
  kcdx chats faces little of this race; the discipline holds regardless.)
- **Validation is the data-core's, single-source (R3):** every invariant runs through
  the shared validator module; the GUI and the importer both consume it; no rule is
  reimplemented in the tool.

## 9. Scope — in / out / deferred <a name="9-scope"></a>

**In (v1 — the Job-2 MVP):** the DB-direct re-verify workflow end-to-end — load,
browse, pick, current-row + full-history view, edit the audit trio, validate, atomic
DB write, auto-export the three CSVs, round-trip oracle, diff-confirm, commit. The new
data-core units (`csv_exporter.py`, `db_editor.py`) + the round-trip oracle test. The
PySide6 GUI shell for this one screen.

**Out / deferred (later phases, `requirements.md` R7 order — Job 1 → Job 3 →
Jobs 4/5/6 → driven evidence flows):**

- Jobs 1 (add entity), 3 (new-game-version campaign), 4 (supersede), 5 (deprecate),
  6 (add versions row) — each is a later DB-direct workflow built on the same
  data-core + round-trip, scoped when its phase starts.
- Driven evidence flows (R5) — `pattern_scan` AOB-uniqueness, `live_test_plugin`
  coverage convention.
- The new-game-version campaign delta report (unchanged/moved/gone) against a new
  dump dir.
- The multi-file rename-sequence journal (R11) — reserved, not built until the
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
| D3 | Sequencing | MVP is DB-direct from day one (Job 2 on the DB). No CSV-editor surface ever built. | Ship CSV-editor MVP first, flip later (throwaway R11 CSV-write path + a migration phase). |
| D4 | Data-layer seam | Headless data-core in `seeds_shared/` (`csv_exporter` + `db_editor`); GUI is a thin shell. | Logic inside the maintainer-tool package (backwards importer dependency; GUI-entangled core). |
| D5 | Save/commit UX | validate → write DB → auto-export → round-trip → show the CSV diff for confirm/revert. | Silent export + success toast (no edit-time view of the actual change artifact). |
| D6 | Commit boundary | The tool commits on Confirm (exact-path staging + live-lock respect + self-authored message). | Tool writes files only, committing left to the separate `/commit` flow. *(Agent flagged the parallel-chat index-race concern per `concurrency-git.md`; user chose tool-commits with the guards baked in.)* |

These supersede the earlier repo-owns-the-format / CSV-editor decisions recorded in
`requirements.md` R1/R6 and `plan.md` §"two-phase".
