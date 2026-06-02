# Maintainer tool — design (DB-direct authoring with CSV auto-export)

> **Status:** v1 (settled). Date: 2026-06-02.
> **Scope:** the Address Library maintainer sub-system — `data/maintainer-tool/`,
> `data/seeds/`, and the `data/refdata-extractor/python/` toolchain that links them.
> This is the data sub-repo's own design doc; it is NOT `docs/design.md` (that doc
> is the v0.1 *engine* design, superseded and out of scope here).
> **UI design layer:** the tool's visual + interaction design (what it looks like and
> how it behaves) lives in [`ui/design.md`](ui/design.md) + [`ui/screens/`](ui/screens/)
> — the screen specs a builder conforms to. This doc fixes WHAT the tool does; the UI
> docs fix what it looks like. **Changelog:** [`changelog.md`](changelog.md).
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

---

## 1. Vision <a name="1-vision"></a>

Move the Address Library off hand-edited seed CSVs: the maintainer edits the
reference DB **directly** through a GUI, which **auto-exports** the three seed CSVs
as a deterministic, git-tracked diff layer on every committed change, guaranteed
correct by a **bidirectional byte-identity round-trip**.

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
  audit-trio + full-row UPDATE for Job 2 / US-5, the INSERT shapes for Jobs 1/6, and
  the lifecycle UPDATE for Jobs 4/5 — all in v1 per §9/D7). It runs the shared
  validator (R3) BEFORE any write; a validation failure aborts with no write.
- **The GUI (`data/maintainer-tool/`, `ui/`)** — presentation only. It renders the
  navigator, the entity detail, the version history/compare, the field editor, the
  create flows, and the field-delta confirm (the seven screens in `ui/screens/`) and
  calls down into the data-core. It holds no validation, no SQL, no export logic.

Dependency direction: GUI → data-core → (schema, validators). The data-core depends
on nothing in the GUI. The round-trip oracle is a data-core test, no Qt.

## 6. User stories & acceptance — the full six-job tool <a name="6-user-stories"></a>

v1 is the complete tool. US-1…US-4 below are the load/browse/re-verify/save spine (the
original Job-2 path, unchanged). US-5…US-10 are the rest of the catalog, all in v1. Each
surface is specified visually in [`ui/screens/`](ui/screens/); the story names WHAT, the
screen spec names how it looks. The save spine (validate → write → export → round-trip →
field-delta confirm → atomic commit) is shared by every mutating story (US-3…US-10).

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

**US-10 — Verification context (the DLL link).** As a maintainer, I link a game DLL so the
tool can resolve the game version and mark/verify rows against it — but I am never blocked
when I haven't.
**Acceptance:** linking a DLL runs the `.rdata` resolver (US current-row resolution below);
the resolved version marks the matching row and is the prefill source for a new version;
**no action requires a linked DLL** — every flow proceeds unlinked with an advisory
"can't verify — no DLL linked" warning; any resolver failure (or the unlinked state) is
**overridable** by an explicit "I accept — save anyway" (the tool's verification is
advisory; the maintainer is final authority — §10 D9/D12).

**Current-version row resolution (R12).** When a DLL is linked, the "current"/"matches
linked DLL" row is the one whose `[valid_from, valid_through]` interval contains the linked
module's `.rdata`-resolved ordinal. **The resolver reads the real version string out of the
linked DLL's `.rdata` bytes** (the `release_M_N_BUILD_SUB` intern), requiring ≥2 agreeing
interns — a DLL with `<2` or disagreeing interns fails to resolve (an advisory warning +
override, never a block — D9). **When no DLL is linked, the tool default-selects the newest
authored row** (highest `valid_from_version`) — deterministic, always-works; there is no
blocking "degraded mode" (D10). The resolver is the existing `version_resolver.py`, bound
not rebuilt.

## 7. UX & states <a name="7-ux-states"></a>

The visual + interaction design — the window skeleton, the interaction laws, the token
system, and every screen's full state set — is specified in
[`ui/design.md`](ui/design.md) + [`ui/screens/`](ui/screens/). That layer is the authority
a builder conforms to (`spec-conformance.md`). This section fixes the cross-cutting state
+ flow requirements every screen there must satisfy (`ux-first-class.md`); the pixel/widget
conventions are the UI layer's.

**The save spine (every mutating story, US-3…US-10).** validate (shared validator, R3) →
atomic DB write → auto-export the 3 CSVs (diff-preserved) → round-trip oracle → a
**plain-language field-delta confirm** (`field: old → new`, only the changed fields) → on
Confirm, ONE atomic commit (DB + 3 CSVs, exact-path); on Cancel, nothing lands. **The field
delta is the acceptance signal** the maintainer reads (D8) — NOT the literal CSV diff
(which is oracle-verified and lands in the commit for a reviewer, invisible to the
maintainer). Git is invisible: the result reads "Saved `<entity> <version>`", never a hash.

**Required states (each screen specifies the ones that apply — `ui/screens/`):**

- **Populated** — the resting view (list, detail, editor, compare).
- **Empty** — no DB/seeds resolved (names where the tool looked: `<exe-dir>/../seeds/`, the
  DB path); no entity selected; no search match (each with distinct, cause-appropriate
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
- **Verification-context states** — "No DLL linked" (advisory, normal — never a block),
  "Linked: version `<v>`", "couldn't resolve version (interns disagree)" (advisory +
  override). NOT a "degraded mode" — the tool always works unlinked (D9/D10).
- **Edge content** — many version rows (history/compare scroll; horizontal scroll in
  compare when columns exceed width); long names/signatures wrap or truncate without
  reflowing siblings (the layout-stable law — `ui/design.md` law 1).

**Flow & feedback:** every action gives feedback; no silent success, no dead-end error.
The maintainer never reads a raw log — the field delta and the status-bar result are the
signals. A state change updates content in place; nothing jumps (layout stability is law 1
of the UI layer).

**Accessibility & consistency:** standard Qt6 widgets, every field/control/list-row
keyboard-reachable and labelled, read-only identity state conveyed by more than color, one
consistent layout idiom (the UI layer's token system + component silhouettes). Full
accessibility + token discipline is `ui/design.md`.

## 8. Constraints <a name="8-constraints"></a>

- **Distribution (R9):** single self-contained Windows `.exe` (PyInstaller bundles
  Python + PySide6 + the data-core). Lives in `data/maintainer-tool/`; resolves seeds
  via `<exe-dir>/../seeds/`. Release artifact, gitignored, published on the private
  GitHub Releases page.
- **Privacy (R10):** all of `data/maintainer-tool/` is private (the publish-public
  `$PrivateSubpaths` carve-out). The tool freely imports the private data-core.
- **Version resolution (R12):** per-module linked DLL, `.rdata` version scan, hard
  intern-agreement, sidecar cache `data/maintainer-tool/.maintainer-tool-cache.json`
  (gitignored, next to the `.exe`), interval-contains-ordinal current-row filter.
  **The link is advisory, never required** (D9): unlinked is a normal working state
  (default-select the newest authored row — D10), every action proceeds with a
  "can't verify — no DLL linked" warning, and a resolver failure or the unlinked state
  is overridable by an explicit "I accept — save anyway". (Replaces the earlier
  blocking "degraded mode" framing.)
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

## 9. Scope — in / out <a name="9-scope"></a>

**In (v1 — the complete six-job tool):** the DB-direct management of the whole reference
DB end-to-end — load, browse/search/filter, view any entity's full record + all version
rows, side-by-side version compare, and the full job catalog: **Job 1** (create entity),
**Job 2** (re-verify the audit trio), **Job 4** (supersede), **Job 5** (deprecate),
**Job 6** (create version), plus **editing any existing version's full columns** (general
correction). The advisory DLL-link verification context (D9). The shared save spine
(validate → write → auto-export → round-trip → field-delta confirm → atomic commit). The
data-core units (`csv_exporter.py`, `db_editor.py`, plus the editor shapes each job needs
— INSERT for Jobs 1/6, the lifecycle UPDATE for Jobs 4/5, the audit-trio/full-row UPDATE
for Job 2 / US-5) + the round-trip oracle test. The full PySide6 GUI (`ui/`).

This v1 spans the catalog; it may be BUILT in steps (the data-core before the GUI; the
jobs in a dependency-sensible order, `incremental-delivery.md`), but no catalog job is out
of scope. (Supersedes the first draft's Job-2-only MVP scope — §10 D7.)

**Out of v1 (genuinely not built):**

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
| D6 | Commit boundary | The tool commits on Confirm (exact-path staging + live-lock respect + self-authored message). | Tool writes files only, committing left to the separate `/commit` flow. *(Agent flagged the parallel-chat index-race concern per `concurrency-git.md`; user chose tool-commits with the guards baked in.)* |

Settled in the UI design dialogue 2026-06-02 (the second pass, building the UI layer):

| # | Decision | Settled value | Rejected alternative |
|---|---|---|---|
| D7 | v1 scope | **v1 is the complete six-job tool** (Jobs 1/2/4/5/6 + edit-any-version + compare), built in steps but nothing deferred. | Job-2-only MVP with Jobs 1/3/4/5/6 deferred to later phases (the first draft). User: "the entire tool will be v1 when complete… all of this is required." |
| D8 | The confirm surface | A **plain-language field delta** (`field: old → new`, only changed fields) is the human's acceptance signal; the literal CSV diff is oracle-verified + lands in the commit, but is not shown. | Show the git-style CSV diff as the acceptance surface (the maintainer reads CSV cells — less clarity); or field-delta + collapsible CSV diff. |
| D9 | DLL link / verification | **Advisory, never required.** Any action proceeds unlinked with a "can't verify" warning; a resolver failure or the unlinked state is overridable by an explicit "I accept — save anyway" (the maintainer is final authority over a tool error). | DLL-link required for version-stamping actions (blocks work); or a blocking "degraded mode" when unlinked. |
| D10 | Default row (unlinked) | **Newest authored row** (highest `valid_from_version`) is default-selected when no DLL is linked. | Nothing pre-selected until the maintainer picks. |
| D11 | New-row approval (AP18) | Creating a new entity (Job 1) or new version (Job 6) is **approval-gated in the confirm step** (an explicit acknowledgment before it lands); an UPDATE is not gated. | Treat a new row like any UPDATE (no approval gate) — violates `policy.md` AP18. |
| D12 | New-version "nothing changed" | Saving a new version identical to its source is **blocked with steering copy** routing the maintainer to re-verify the existing row instead of creating a duplicate. | Silently allow a duplicate version row; or clear the audit trio on a new version (the user chose prefill-all + the nothing-changed guard). |

These supersede the earlier repo-owns-the-format / CSV-editor decisions recorded in
`requirements.md` R1/R6 and `plan.md` §"two-phase", and the Job-2-only MVP framing.
