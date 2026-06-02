# Maintainer tool — requirements

The approved requirements for the maintainer tool. This file holds ONLY
requirements the user has explicitly approved.

> **⚠️ R1 and R6 are SUPERSEDED (2026-06-02) by [`design.md`](design.md).** The
> settled design inverts the source of truth: the tool edits the reference DB
> **directly** and **auto-exports** the seed CSVs as a derived, git-tracked diff
> layer (DB authoritative; CSVs no longer hand-edited). R1's "seed-editor-only /
> does-not-build-the-DB" framing and R6's "CSV-editor MVP" framing are replaced
> by that DB-direct architecture. R2–R5 and R7–R12 below remain in force and are
> built on top of the DB-direct design. See [`design.md`](design.md) §3
> (source-of-truth inversion) and §10 (decision record).

## R1 — Scope (SUPERSEDED — see design.md §3)

R1 originally scoped the tool to editing the three seed CSVs under `data/seeds/`
and excluded building the SQLite DBs. **That framing is superseded:** under
[`design.md`](design.md) the tool edits `reference.sqlite` directly and exports
the three CSVs (`module_seed.csv`, `address_names_seed.csv`,
`address_versions_seed.csv`) as a derived diff layer. The seed CSVs survive as
the git-tracked export — they are no longer the hand-authored surface.

**Why superseded:** the goal moved from a CSV editor to taking the Address
Library off hand-edited CSVs entirely — the DB becomes the authoring surface.

## R2 — Portability: runs on any Windows machine with the seed CSVs

The tool runs on any Windows machine that has the seed CSV files on disk. It
does NOT require:

- A local Ghidra install.
- A copy of `WHGame.dll`.
- An analyzed Ghidra project of `WHGame.dll`.
- Any artifact from the `refdata-extractor` dump pipeline as a prerequisite to
  launching the tool itself.

Higher-tier verification flows MAY introduce their own additional prerequisites
(e.g. read access to a dump directory). Those prerequisites attach to the
specific flow, not to the tool's baseline portability.

**Why:** the user stated this as the load-bearing portability invariant.

## R3 — Shared validators (extract, do not duplicate)

The tool's integrity validation reuses the rules currently implemented inside
`data/refdata-extractor/python/import_to_sqlite.py` (canonical-id checks,
audit-trio integrity, supersession/deprecation pair integrity, supersession
acyclicity, module FK resolution, `(kcdx_id, valid_from_version)` uniqueness,
`evidence_kind` enum, `verified_date` format, etc.).

Those rules are EXTRACTED from `import_to_sqlite.py` into a shared validator
module. Both the importer and the tool consume the shared module. The rules
are NOT reimplemented in the tool.

**Why:** the user chose extraction over duplication; a single source of truth
is the only way to keep the importer and the tool from drifting.

## R4 — Surface: GUI, not CLI

The tool's surface is a graphical user interface, not a command-line interface.

The choice of UI framework, the screen layout, and the command catalog are
deferred to future approved requirements.

**Why:** the user stated the tool needs a UI because the verification surface
involves richer interactions than a CLI can carry well.

## R5 — Verification: drives, not just records

The tool supports verification flows that go beyond record-only audit-trio
capture. At minimum one evidence tier (from the `evidence_kind` enum in
`data/seeds/policy.md`) is a DRIVEN flow — the tool surfaces evidence to the
maintainer on the verification screen, not only captures `verified_by` /
`verified_date` / `evidence_kind`.

Which specific tiers are driven, and what each driven tier shows, is deferred
to a future approved requirement.

**Why:** the user said the verification surface needs to do "some more complex
things" — i.e. record-only across all tiers is insufficient.

## R6 — MVP scope: Job 2 only (re-verify a single entity) — *MVP scope still Job 2; the WRITE TARGET is SUPERSEDED, see design.md*

The first shipping version of the tool covers exactly one workflow end-to-end:
**Job 2 — re-verify an existing curated entity at the current game version.**
Nothing else. **(The Job-2 MVP scope still holds; what changed is the surface —
see the banner below and [`design.md`](design.md) §6.)**

> **⚠️ SUPERSEDED write path (2026-06-02).** R6 originally wrote the edit to
> `address_versions_seed.csv` and exited "with seeds the importer accepts on the
> next `--rebuild`." Under [`design.md`](design.md) §6 the Job-2 MVP edits the
> reference DB **directly** (validated, atomic), then **auto-exports** the three
> CSVs (diff-preserved) and commits, with a bidirectional byte-identity
> round-trip as the oracle. The maintainer sees the exported CSV diff as the
> acceptance signal. The workflow (re-verify one entity, update the audit trio)
> is unchanged; the write target (DB-first, CSV-exported) is the supersession.

"End-to-end" (under design.md §6) means: load the curated entity set through the
shared validator module (R3); browse the curated entity list; pick an entity;
see its current-version row (+ full version history per R8); enter the audit trio
(`last_verified_at_version`, `verified_by`, `verified_date`, `evidence_kind`);
validate the prospective edit through the shared validator; write atomically to
the **DB**; auto-export the three CSVs diff-preserved; round-trip oracle; show the
CSV diff; commit on confirm.

The MVP does NOT include: a worklist of entities-needing-re-verification, a
dump-read for hash checks, AOB uniqueness scans against `WHGame.dll`, test-
plugin coverage validation, a new `game_versions` row registration flow, or
any of Jobs 1, 3, 4, 5, 6.

**Why:** Job 2 is the highest-frequency workflow and a self-contained primitive
that later jobs reuse. Shipping it alone takes the most common operation off
CSV hand-edits immediately, without staging a larger UI surface that has not
been exercised yet.

## R7 — The six jobs (full scope; MVP is Job 2 only)

The tool's full scope covers six maintainer workflows. R6 commits ONLY Job 2
for the MVP; the other five are enumerated here as the planned scope of later
phases. The scope of each later phase is approved at the time that phase is
started; this requirement only commits the workflow list and the phase order.

1. **Job 1 — Add a new curated entity.** A new function joins the curated set.
   Atomic two-file write (`address_names_seed.csv` + `address_versions_seed.csv`);
   pick next free `kcdx_id`; pick a `kind` from the 9-kind taxonomy; record the
   audit trio.
2. **Job 2 — Re-verify an existing entity at the current game version.** Find
   the row, update the audit trio (`last_verified_at_version` + `verified_by` +
   `verified_date` + `evidence_kind`). The RVA and signature do NOT change.
   **MVP.**
3. **Job 3 — New-game-version re-verification campaign.** When KCD2 ships a
   new build: register the new `game_versions` row; produce a per-entity delta
   report (unchanged / hash-changed / gone / moved) against a new dump dir;
   drive re-verification or Job-6-style new-versions-row creation per entity.
4. **Job 4 — Supersede entity X with entity Y (rename).** Three atomic edits:
   add Y to names, add Y's versions row, set X's supersession pair. Includes
   acyclicity validation up front.
5. **Job 5 — Deprecate entity X.** Set `is_deprecated = 1` and
   `deprecated_at_version`; optionally set `deprecation_replacement`.
6. **Job 6 — Add a new versions row for an existing entity** (RVA moved between
   game versions). New `valid_from_version`, new RVA / signature / audit trio;
   close the previous open interval; the old row stays authoritative for its
   prior version window.

**Phase order after MVP:** Job 1 → Job 3 → Jobs 4 / 5 / 6 → evidence-tier
driven flows (AOB uniqueness via optional `WHGame.dll` access, test-plugin
coverage via a separate manifest convention). Each phase's detailed scope and
the cross-cutting concerns it pulls in (dump-read posture, `WHGame.dll`
posture, test-plugin-coverage convention) are decided at the time that phase
is started.

**Why:** the user approved listing all six jobs to make the tool's full scope
visible in the requirements doc, while keeping MVP scope (R6) at Job 2 only.

## R8 — Job 2 details: all-versions view + editability cut

**Entity selection.** When the maintainer picks a curated entity, the screen
first shows the row for the **current game version** (the row whose
`valid_from_version` is the open interval at `V_current`). A separate action
on the entity reveals the **full version history** — every row in
`address_versions_seed.csv` whose `kcdx_id` matches the selected entity —
allowing the maintainer to edit historical rows as well as the current one.

Today most entities have one row (the 1.5.1164953 baseline); the all-versions
view becomes meaningful once Job 6 lands and entities accumulate multiple
`valid_from_version` rows.

**Editability cut (initial, deliberately permissive).** Three fields are
read-only in Job 2:

| Field | Why read-only |
|---|---|
| `kcdx_id` | Canonical, append-only handle per `policy.md` §"ID assignment". Changing it = changing entity identity. |
| `name` | Stable resolution key plugins type as `target = "..."`. Renaming is the supersession workflow (Job 4), not an in-place edit. |
| `valid_from_version` | `policy.md` §"valid_from_version vs. last_verified_at_version": "NEVER changes once authored." A row stays anchored to the version where its `(module, rva, signature)` first became authoritative. |

Every other field is editable in Job 2.

This is the **initial** cut; tightening (e.g. making `module`, `rva`,
`signature` read-only in Job 2 so RVA/signature changes flow through Job 6
instead) is reserved as a follow-up adjustment as the workflow is exercised.

**Why:** the user chose to start with a minimal three-field read-only set and
restrict further as the tool is used, rather than locking down up front.

## R9 — Distribution: self-contained Windows `.exe`, no install steps

The tool ships as a single self-contained Windows `.exe` that bundles the
Python interpreter, PySide6 (Qt6), and the shared validator module (R3). The
maintainer downloads the `.exe` and runs it. No Python install, no `pip`, no
venv, no setup script, no command-line invocation on first use.

**Build tooling.** PyInstaller produces the bundled `.exe`. Packaging runs on
the lead maintainer's machine as part of the release process; the build step
is not something contributors run.

**Location convention.** The `.exe` lives in `data/maintainer-tool/` (the same
directory as this requirements file). The seed CSVs the tool edits live in
the sibling directory `data/seeds/`. The tool resolves its seed CSV paths
relative to its own executable location at launch — `<exe-dir>/../seeds/` —
not from a hard-coded absolute path, not from `%APPDATA%`, not from a
pick-on-first-launch prompt. The maintainer downloads the `.exe`, drops it
into `data/maintainer-tool/`, and runs it; the tool finds the seeds.

**Why:** the user required "download and run, no extra steps." A PyInstaller
bundle satisfies that while preserving R3 (shared Python validators imported
directly, no IPC, no language split). The location convention reflects the
user's decision that the `.exe` lives alongside the CSVs in the repo, so
resolving seeds by relative path is unambiguous and removes the need for a
configuration UI.

**Distribution channel.** The `.exe` is a release artifact, NOT a tracked file
in git. `.gitignore` already excludes `*.exe`. The lead maintainer publishes
the `.exe` on the private GitHub Releases page of `kcdx-private`; another
maintainer downloads it from there and drops it into `data/maintainer-tool/`
in their own clone of the private repo. The repo never holds the binary.

## R10 — Private tool: `data/maintainer-tool/` is a private subpath

The entire `data/maintainer-tool/` directory is private. It does NOT project
to the public repo through `publish-public.ps1`. This applies to the
requirements doc, the tool's source, the cache file (R12), the release-build
configuration, and anything else under the directory.

**Implementation.** `publish-public.ps1`'s `$PrivateSubpaths` array gains the
entry `'data/maintainer-tool/'` in the same commit that lands this requirement.
The carve-out behavior matches the existing `data/refdata-extractor/` and
`data/seeds/` carve-outs: `data/` projects public, the named subpaths under it
do not.

**Why:** the user stated this is a private tool. The tool consumes private
artifacts (the seed CSVs under `data/seeds/`, the shared validator module
extracted from `data/refdata-extractor/python/import_to_sqlite.py`) and exposes
maintainer-only workflows; nothing about it is for mod authors. Keeping it
private avoids a `public-private-boundary.md` violation by construction (the
tool can freely import from the private validator module) and matches the
disposition of its peer dirs.

## R11 — Atomic transaction across the three CSVs

A maintainer action in the tool that edits one or more seed CSVs is a single
all-or-nothing transaction. The seeds on disk are either in the pre-action
state OR in the fully-applied post-action state, never in a partial state.
This holds across both single-file and multi-file edits.

**Within-file writes.** Every CSV write is "write to a sibling temp file,
fsync, atomic rename over the original." A crash during the write leaves the
original file untouched on disk. Edits never mutate the original file in place.

**Multi-file edits.** Workflows that span two or three CSVs (Job 1: names row
+ versions row; Job 4: names rows + versions rows + supersession edit; Job 6:
close-old-row + open-new-row) write to ALL their temp files first, then rename
them in sequence. If any temp write fails, none of the renames happen and all
temp files are removed; the seeds remain in their pre-action state. The
window where a crash could leave the rename sequence half-applied is
acknowledged as small but non-zero; if it ever bites in practice, the tool
gains a journal file (a sibling under `data/maintainer-tool/` tracking
in-flight rename sequences) that the tool's next launch detects and either
completes or rolls back. Journal mechanism is reserved, not built in MVP.

**Diff preservation.** CSV writes preserve everything the importer's policy
treats as semantic: row order is not changed unless the action explicitly
adds, removes, or reorders rows; `#`-prefixed comment lines are preserved
verbatim including position; the file's quoting style (`QUOTE_MINIMAL` per
`policy.md` §"File-format details") is preserved per cell; trailing newline
convention is preserved. A `git diff` of a tool-authored edit MUST show only
the cells the action changed, not whole-file reformatting churn.

**Validation gate.** No write begins until the shared validator module (R3)
accepts the prospective post-action state. If validation fails, the action
aborts; no temp files are created; the seeds are untouched. The shared
validator is the single source of truth — the tool does not pre-filter or
re-implement any rule.

**Why:** the user stated the commit must land as if it were a single
transaction. `policy.md`'s seed-edit commit discipline + the importer's
fail-loud disposition require that a tool-authored change either lands
cleanly or doesn't land at all. Mid-write crashes silently corrupting a CSV
is the failure mode this guards against; whole-file reformatting churn is the
review-time failure mode the diff-preservation clauses guard against.

## R12 — Game version resolution via maintainer-linked DLLs

The tool's "current game version" for a row is the version of the row's
module's DLL, scanned at tool-load time from a maintainer-linked DLL file. The
mechanism is per-module, not global, because the seeds will eventually
reference more than one module (today: `WHGame.dll` only; tomorrow: N
modules), and each module ships at its own version.

**Per-module DLL linking.** The tool maintains a per-module link table. For
each row in `module_seed.csv`, the maintainer links one DLL file on their
local disk as the "current" DLL for that module. The maintainer can have
multiple DLLs on disk for the same module (e.g. the live game install plus
archived copies of older builds) and switch between them; the link table
remembers a list of recent paths per module so switching does not require
re-browsing.

**Version resolution from a linked DLL.** Given a linked DLL file, the tool
resolves its version by scanning the `.rdata` section for the regex
`release_(\d{1,3})_(\d{1,3})_(\d{4,8})_(\d{1,4})` (matching the
`release_M_N_BUILD_SUB` form the KCD2 build interns at two `.rdata` addresses
— verified at va=0x183c3edef and va=0x183dba258 in 1.5.1164953;
[_research/init-cycle-recon/_version_strings.txt](../../_research/init-cycle-recon/_version_strings.txt)).
The resolved tag is `M.N.BUILD`; the resolved ordinal is the integer BUILD.

**Hard intern-agreement check.** The scan finds ALL matches in `.rdata`. At
least two matches are expected for a normal DLL (the linker does not dedupe
these interns). If two or more matches are found AND they all agree on the
string, the DLL is accepted at the resolved version. If matches disagree
(different `(M, N, BUILD, SUB)` tuples in different interns), the DLL is
REFUSED — the tool surfaces the discrepancy with each match's VA and string,
and the maintainer must investigate before linking it. A DLL with disagreeing
interns is one the tool will not trust silently, because it is a strong signal
the binary was hand-patched, partially modified, or corrupted. A DLL with
fewer than two matches is also refused (the same failure modes apply).

**Current-row filter (refining R8).** When the screen shows "the current
row for this entity," the row is the one whose interval
`[valid_from, valid_through]` contains the linked module's resolved
ordinal. The interval per row is computed at tool-load time: for a row R
whose entity has rows R1 < R2 < ... ordered by `valid_from`, R's
`valid_through` is the `valid_from` of the next row in the chain minus one
ordinal step; the last row in the chain has an open interval
(`valid_through = +inf`). The resolution rule is
`row.valid_from.ordinal <= V_module.ordinal AND (row interval is open OR
row.valid_through.ordinal >= V_module.ordinal)`. Today every entity has one
row with an open interval; the interval rule degrades cleanly to "the only
row's `valid_from` is the version that matters." When Job 6 lands and
entities accumulate multiple rows, the interval rule handles it without
schema or UI change.

**Degraded mode (preserving R2).** If a module is not linked, the
"current row" filter has no answer for that module's rows. R8's "show the
current-version row first" is replaced by "show ALL rows for that entity";
the UI surfaces "module `X` not linked; showing all versions" so the
maintainer knows why the filter is degraded. The tool launches and operates
on the seeds without any DLL linked.

**Shared resolver lives in R3's module.** The `.rdata` version-scan logic
is placed in the shared validator module so the importer can adopt it later
if it chooses. The importer's existing `whdlversions.json` path remains
valid; the resolver is an alternative, not a replacement.

**Persistence: sidecar file next to the `.exe`.** The per-module link table
persists in `data/maintainer-tool/.maintainer-tool-cache.json` (literal
filename — leading dot signals "tool state, not authored content"). The
file lives next to the `.exe`, NOT in `%APPDATA%`. It is added to
`.gitignore` so per-machine absolute DLL paths never enter the repo. The
tool reads it at launch and writes it on every link-table change. If the
file is missing or unreadable, the tool starts with an empty link table
(all modules unlinked, degraded mode for every row).

**Why:** the user chose per-module DLL picking with a recent-paths history
(Q1c), hard intern-agreement on the scan (Q2), per-module independent
version tracking (Q3b), and an exe-sidecar cache file (no `%APPDATA%`, no env
var). The `.rdata` scan is the version mechanism the user verified against
the binary; the disagreement-refuses-the-DLL rule preserves the fail-loud
posture `policy.md` establishes; the per-module link table is the shape that
scales to N modules without re-doing the requirement. The interval-contains-V
filter rule is the user's correction to my earlier valid_from-equals-V
framing — the interval semantics match `address_versions`' actual schema.
