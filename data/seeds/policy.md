# Address Library — seed authoring policy

**Authoritative law for the three seed files under `data/seeds/`.**

This document governs the SEEDS ONLY — what the maintainer writes, in what
shape, with what required fields. The shape of the generated SQLite DBs and
the engine's runtime semantics are documented elsewhere
([`data/reference.md`](../reference.md) and
[`data/reference-dev.md`](../reference-dev.md)). A rule in this file is
binding on every seed-edit commit.

The importer enforces every rule below as a fail-loud check
(`data/refdata-extractor/python/import_to_sqlite.py`). The harness
(`validate_db_shape.py`) re-asserts the same rules against the generated DBs.

## The three seed files

| File | One row = | Key |
|---|---|---|
| `module_seed.csv` | A module (DLL) the address library can reference. | `id` |
| `address_names_seed.csv` | A curated entity — the stable cross-version handle plugins reference. | `id` (== kcdx_id) |
| `address_versions_seed.csv` | A per-version resolve fact for one entity. | `(kcdx_id, valid_from_version)` |

A row's identity NEVER changes once authored. Adding new evidence,
re-verifying for a new game version, deprecating an entity — every change is
an UPDATE to existing column values, or an APPEND of a new row. Never a
renumber, never a delete-and-rewrite.

## ID assignment

- `module_seed.csv.id`, `address_names_seed.csv.id` — canonical, maintainer-
  supplied integers. NO autoincrement. NULL = HARD ERROR. Duplicate = HARD
  ERROR.
- APPEND-ONLY. An id, once assigned, is permanent. Never renumber. Never
  recycle.
- No bands. The next free integer is the next id.
- `address_versions_seed.csv` has NO `id` column. Row identity is the
  `(kcdx_id, valid_from_version)` tuple. Duplicate tuple = HARD ERROR.

## Required columns

A row is rejected with a hard error if any REQUIRED column is empty.

**`module_seed.csv`** — every column required: `id`, `name`, `path`.

**`address_names_seed.csv`** — required: `id`, `name`. Optional (NULL-valid):
`superseded_by`, `superseded_at_version`, `is_deprecated`,
`deprecated_at_version`, `deprecation_replacement`, `notes`.

**`address_versions_seed.csv`** — required: `kcdx_id`, `valid_from_version`,
`module`. Optional (NULL-valid): `rva`, `signature`,
`last_verified_at_version`, `verified_by`, `verified_date`, `evidence_kind`.

## Module column resolution (`address_versions_seed.csv.module`)

The `module` column references a `module_seed.csv` row. Resolution heuristic:

1. If the value parses as an integer, look up `module_seed.csv.id` by that
   integer.
2. Else look up `module_seed.csv.name` by string match.
3. No match = HARD ERROR.

## valid_from_version vs. last_verified_at_version

These are different fields with different jobs. Both are game-version
strings (e.g. `1.5.1164953`), but their semantics diverge.

| Column | Meaning |
|---|---|
| `valid_from_version` | The EARLIEST game version this row's `(module, rva, signature)` is correct for. The row is authoritative from this version forward, until a newer same-entity row supersedes it OR the entity itself is deprecated/superseded. NEVER changes once authored. |
| `last_verified_at_version` | The LATEST game version the maintainer has actually signed off on this row for. NULL when never verified. |

The pair `(valid_from_version, last_verified_at_version)` defines the
inclusive window the row is currently trusted for: the row is VERIFIED at
any game version V where `valid_from_version <= V <= last_verified_at_version`.

`last_verified_at_version` MUST be `>= valid_from_version` when set. Violation
is a HARD ERROR (you can't verify a row at a version older than where the row
claims to start).

## Status is NOT an authored column

There is no `status` field on any seed row. A row's verification state is
DERIVED at query time from:

```
current_version V
plus row.valid_from_version, row.last_verified_at_version
plus entity.is_deprecated, entity.deprecated_at_version
plus entity.superseded_by, entity.superseded_at_version
```

Derivation (the engine and the maintainer both reason about this — but the
engine is documented in the reference READMEs; this section is here because
the maintainer must understand what their authored columns produce):

1. If `entity.is_deprecated` AND `V >= entity.deprecated_at_version`: DEPRECATED.
2. Else if `entity.superseded_by` AND `V >= entity.superseded_at_version`: SUPERSEDED (engine auto-walks to the successor).
3. Else if `row.last_verified_at_version >= V` AND `row.valid_from_version <= V`: VERIFIED.
4. Else: UNVERIFIED.

A new game version shipping FLIPS rows from VERIFIED to UNVERIFIED
automatically — no row mutation required. The maintainer re-verifies and
bumps `last_verified_at_version`. Rows that prove broken in the new version
get the entity deprecated or superseded (NOT mutated in place on the
versions seed; the per-version row at the older `valid_from_version` stays
correct for that older version).

## Verification audit trail (the trio)

When `last_verified_at_version` is set, ALL THREE of `verified_by`,
`verified_date`, `evidence_kind` MUST be set. When `last_verified_at_version`
is NULL, all three MUST be NULL. Partial sets are a HARD ERROR.

| Column | Format | Job |
|---|---|---|
| `verified_by` | TEXT — person identifier (e.g. `VioletAnvil`) | Who signed off. |
| `verified_date` | `YYYY-MM-DD` (ISO). Other formats = HARD ERROR. | When. |
| `evidence_kind` | One of the enum below. | How (the evidence tier). |

### evidence_kind enum (quality ranking, strongest first)

1. `live_production` — A kcdx engine production hook uses this row, or a
   shipping kcdx feature consumes it in the live game. The row is exercised
   on every kcdx-enabled launch.
2. `live_test_plugin` — A `test-plugins/` plugin (a `cap-NN` / `comp-NN` row
   in the test matrix) exercises this row. The row is exercised by `/verification-checkpoint` in the verification cycle.
3. `maintainer_ghidra` — The maintainer has done a body-shape walk in Ghidra
   (or capstone disassembly) against the binary for the named version and
   signed off. No automated test exercises it.
4. `predecessor_sig` — The RVA/signature matches a verified entry in another
   KCD reverse-engineering project. Useful when no test plugin exists yet
   and no direct binary walk has been done.
5. `pattern_scan` — AOB pattern is `.text`-unique against the binary. No
   body-shape verification, no test plugin. Thinnest evidence tier; flag
   for upgrade.

A value not in the enum = HARD ERROR. No `inferred` tier — if no real
evidence exists, the row's `last_verified_at_version` stays NULL.

## Supersession (entity rename; engine auto-follows)

A cosmetic rename: the new entity occupies the same address with the same
ABI, just under a new canonical name. Resolution at a version `V >= superseded_at_version` returns the SUCCESSOR's resolve facts; the original
name still resolves (via the chain walk).

To rename an entity X to Y at game_version `V`:

1. Add a new `address_names_seed.csv` row for Y (next free id, all entity
   fields populated).
2. Add an `address_versions_seed.csv` row for Y (its own resolve facts for V).
3. On the existing X row, set `superseded_by` to Y's name + `superseded_at_version` to V.

Pair integrity: `superseded_by` and `superseded_at_version` are both-or-
neither. Setting one without the other = HARD ERROR. A row pointing
`superseded_by` at itself = HARD ERROR. A cycle in the supersession graph
= HARD ERROR.

## Deprecation (entity behavior change; engine warns)

The entity's behavior changed in a way that affects callers. The engine
emits a warning at resolve time but DOES resolve the address. The maintainer
optionally points authors at a replacement entity via `deprecation_replacement`
— this is advisory, the engine does NOT auto-follow it.

To deprecate an entity at game_version `V`:

1. Set `is_deprecated = 1` + `deprecated_at_version = V`.
2. Optionally set `deprecation_replacement` to a different entity's name.
3. The row stays in the seeds; the entity is still resolvable.

Pair integrity: `is_deprecated = 1` and `deprecated_at_version` are
both-or-neither. `deprecation_replacement` is allowed ONLY when
`is_deprecated = 1`. Violation = HARD ERROR.

## Test plugin requirement

Every non-deprecated, non-superseded entity in `address_names_seed.csv` MUST
be exercised by at least one `test-plugins/` plugin. The test plugin IS the
re-verification mechanism when a new game version ships:

- New entity → MUST land with a `test-plugins/cap-NN` (or `comp-NN`) row in
  the same unit of work. A new entity without a test plugin is a policy
  violation.
- Deprecated entity (`is_deprecated = 1`) → test plugin obligation lifts.
- Superseded entity (`superseded_by` set) → test plugin obligation transfers
  to the successor; the chain head's plugin covers both via the engine's
  auto-follow.

This requirement is intentionally tight: re-verification for a new game
version is just running the test plugin against the new binary, then
updating `last_verified_at_version` + `verified_by`/`verified_date` +
`evidence_kind = live_test_plugin`. An entity without a test plugin can
only be re-verified by `maintainer_ghidra` — slower, lower tier, and
doesn't scale across versions.

Pre-existing entities lacking a test plugin (the inherited backlog from
before this policy landed) are a documented debt — backfill as each
entity is touched.

## Naming

Format: snake_case for new submissions. Preserve CamelCase when it matches
a canonical engine-source identifier. The name is what the plugin author
types into `target = "..."` — choose what reads idiomatically in plugin
code.

Examples:

- `lua_pcall` — Lua C API name, literal.
- `CGame_Update` — CryEngine method `CGame::Update`, `::` → `_`.
- `IConsole_AddCommand` — vtable method, CamelCase preserved.
- `IConsole_AddCommand_script_overload` — disambiguating suffix only when
  no canonical engine name exists for the variant.
- `outfit_swap_callsite_aob` — domain-specific name for a mid-function
  patch site with no engine-source identifier.
- `IGame_CompleteInit_vtable_idx` — vtable INDEX constant (not an RVA).
- `gEnv_pConsole_mov_instruction` — the MOV instruction loading
  `gEnv->pConsole`, not the pointer slot itself.

Subsystem vocabulary: `lua`, `CGame`, `gEnv`, `IConsole`, `IScriptSystem`,
`IGame`, `IGameFramework`, `physics`, `audio`, `input`, `entity_system`,
`inventory`, `dialog`, `quest`, `save`, `serialization`. A new top-level
subsystem prefix is a maintainer decision, not a contributor one.

## New game version workflow

When KCD2 ships a new build:

1. Most rows need NO authoring action. Their existing
   `(valid_from_version, last_verified_at_version)` pair still describes
   the version range they were last signed off for. The derived status
   automatically flips to UNVERIFIED for rows whose
   `last_verified_at_version < new_current_version`.
2. Run the test-plugin matrix against the new binary. For each row whose
   test plugin passes: bump `last_verified_at_version` to the new game
   version on its `address_versions_seed.csv` row, refresh `verified_by`
   + `verified_date` + `evidence_kind`. The RVA + signature columns do
   NOT change (the row is the same row, verified for a longer window).
3. For each row whose test plugin fails:
   - If the RVA moved but the entity still exists: add a NEW
     `address_versions_seed.csv` row with the new `valid_from_version`,
     new RVA/signature, and the new audit columns. The original row
     stays — it's still authoritative for the older version range.
   - If the entity is gone: deprecate it (set `is_deprecated = 1`,
     `deprecated_at_version = <new version>`).
   - If the entity was renamed: superseded path — add the new entity,
     set the old one's `superseded_by` + `superseded_at_version`.

## File-format details (boring but binding)

- All three seed files are UTF-8 CSV with `QUOTE_MINIMAL` quoting.
- Lines starting with `#` (after any leading whitespace) are treated as
  comments by the importer and skipped.
- Column order in the file MUST match the header literally — the importer
  reads by column name (DictReader) so order is human-readable cosmetics.
  Don't reorder existing rows' columns; do match the header on new rows.
- `rva` is hex with `0x` prefix (e.g. `0x0071A5A4`).
- Empty cells are empty strings in the CSV (`,,`), interpreted as NULL by
  the importer.

## What changed (this file's history)

Policy doc rewritten 2026-05-28. Previous version (the v0.1 single-CSV +
banded-IDs + engine-blocks-on-unverified model) is fully superseded.
