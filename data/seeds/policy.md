# Address Library — seed authoring policy

**Authoritative law for the three seed files under `data/seeds/`.**

This document governs the SEEDS ONLY — what the maintainer writes, in what
shape, with what required fields. The shape of the generated SQLite DBs and
the engine's runtime semantics are documented elsewhere
([`data/reference.md`](../reference.md) and
[`data/reference-dev.md`](../reference-dev.md)). A rule in this file is
binding on every seed-edit commit.

Most rules below are enforced as fail-loud checks in the importer
(`data/refdata-extractor/python/import_to_sqlite.py`); the harness
(`validate_db_shape.py`) re-asserts them against the generated DBs.
Rules explicitly marked **policy-only** are NOT auto-enforced — the
maintainer is responsible for compliance (review, the test-suite matrix,
the verification checkpoint).

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

## DB additions require explicit approval

**Adding a NEW entity or version row to `address_names_seed.csv` or
`address_versions_seed.csv` requires explicit maintainer approval before it
lands.** This is the one authoring action that grows the Address Library DB —
a new curated game-binary target (RVA, byte/AOB pattern, vtable slot, or
game-struct offset) committing the project to maintain that address across
game versions. The maintainer approves the specific entity BEFORE the seed
row is written; an addition that lands without that sign-off is unauthorized.

Scope: this gates the **seed-row ADDITION** (a new data row). It does NOT
gate the in-code side — resolving a game address by name/id instead of a raw
literal is the always-on expectation (plugins and engine code resolve through
the Address Library, never hardcode an RVA), not a per-use approval. Nor does
it gate an UPDATE to an existing row (re-verifying
a version, bumping `last_verified_at_version`, deprecating/superseding) — those
mutate an already-approved entity. Only the appearance of a NEW entity/version
row is the approval point.

Enforcement (mirrors the other seed-authoring rules — author-time reminder +
review gate, NOT an importer hard error): a warn-only author-time check flags
any edit that adds a row to either curated seed CSV; the change-review pass
treats a seed addition with no recorded approval as a finding. **Policy-only**
at the importer level — the importer does not verify approval (it cannot see
the maintainer's sign-off); compliance is the maintainer's, enforced by
review.

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
`module`, `kind`. Optional (NULL-valid): `rva`, `signature`,
`last_verified_at_version`, `verified_by`, `verified_date`, `evidence_kind`,
and the six survival columns `survival_aob`, `survival_anchor_string`,
`survival_derives_from`, `survival_rule`, `survival_slot_count`,
`survival_expect_unique` (see §"Survival columns").

`kind` sits right after `rva` in the column order (it is core row-identity,
authored alongside the rva/signature). It is one of the nine address kinds (see
§"Address kinds"). An empty or out-of-enum `kind` is a HARD ERROR.

## Module column resolution (`address_versions_seed.csv.module`)

The `module` column references a `module_seed.csv` row. Resolution heuristic:

1. **Int-first:** if the value parses as an integer, look up `module_seed.csv.id`
   by that integer. If no module with that id exists = HARD ERROR (the
   resolver does NOT fall through to step 2 once the value parsed as int).
2. **By-name (only when not int):** if the value does not parse as an
   integer, look up `module_seed.csv.name` by exact string match.
3. No match = HARD ERROR.

## Address kinds (the `kind` column)

`kind` classifies what an address row IS. It is **AUTHORED** — the maintainer
writes the kind into the seed cell. It was previously GUESSED by the importer
from the entity's `notes` prose, which silently mis-classified rows whose prose
lacked a magic cue (e.g. `gEnv` read as a function instead of a data slot;
`CScriptSystem_vtable` read as a function instead of a vtable base). The kind
gates the survival `kind_form`, the fingerprint-vs-NULL branch, and the bulk-row
promote gate, so a wrong kind silently dropped authored data. It is now an
explicit required column.

The nine legal kinds:

| Kind | What it is |
|---|---|
| `function` | A regular function entry (has a verified signature). |
| `function_variadic` | A variadic function entry (`...` in the arg list). |
| `function_no_sig` | A real function entry whose signature is not yet known. |
| `callsite` | A mid-function call instruction (NOT a function entry). |
| `vtable_index` | A vtable SLOT INDEX constant (no RVA; slot-only). |
| `vtable_base` | A concrete-class vtable base ADDRESS (the pointer table). |
| `data_slot` | A static `.data` pointer slot (resolved by a derivation). |
| `string_anchor` | An `.rdata` string literal used as an anchor. |
| `instruction_anchor` | A specific instruction used as an anchor (e.g. a `mov`). |

The function kinds (`function` / `function_variadic` / `function_no_sig`) carry a
body-hash fingerprint and PROMOTE a matching bulk-dump row; every other kind
MINTS with the fingerprint columns NULL (a body hash is the wrong survival datum
for a callsite / data_slot / vtable / string). When authoring a new row's kind,
the importer's `infer_kind` heuristic can produce a first guess, but the authored
value is the source of truth — verify it against this list and the actual RE
finding before writing.

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

**Version comparison caveat (string vs ordinal):** the importer compares
version-tag strings with Python's `<` operator. This works for the current
`<major>.<minor>.<build>` format as long as the minor stays a single digit
(`"1.5.x" < "1.6.y"` lex-sorts correctly). The moment a two-digit minor
ships (`"1.10.x"`), string compare breaks (`"1.10.x"` lex-sorts BELOW
`"1.5.x"`). When that happens, the comparison must move to ordinal-based
lookup via `game_versions.ordinal`. Until then, the string compare is
correct AND the importer's "last_verified >= valid_from" check is the only
gate that uses it on the seed side; the engine's runtime derivation
already uses ordinal compare.

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
| `verified_date` | `YYYY-MM-DD` (ISO). Other shapes = HARD ERROR. The check is shape-only (regex `^\d{4}-\d{2}-\d{2}$`), NOT semantic — `9999-99-99` passes the check. The maintainer is responsible for entering a real date. | When. |
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

## Survival columns (the per-kind survival datum)

`address_versions_seed.csv` carries six OPTIONAL per-version columns that hold
the **survival datum** — the load-bearing fact a future engine survival check
re-verifies to answer "at the current game version, is the thing this row names
still the thing it was verified to be?" The check's *form* mirrors how the kind
resolves: an AOB-matched kind is re-checked by re-matching the AOB, a derived
slot by re-running its derivation, and so on. Each curated row maps to exactly
one row in the generated `survival` table; the importer reads these columns and
populates that table.

All six are NULL-valid. A kind uses only the column(s) its form needs; the rest
stay empty. An unfilled column = an empty survival payload (the importer emits
the survival row with the right `kind_form` and an empty payload — never a
guessed value, never a value parsed from `notes`). A **malformed PRESENT** value
is a HARD ERROR; an EMPTY value is always allowed.

| Column | Format | Used by kind(s) | Job |
|---|---|---|---|
| `survival_aob` | Whitespace-separated AOB tokens; each token is a 2-hex byte (`48`, `8B`, `ff`) or a wildcard (`?` / `??`). The wildcard mask is FOLDED INTO this column — there is NO separate mask column. | `callsite`, `instruction_anchor` | The AOB pattern (bytes + mask) the survival check scans `.text` for. |
| `survival_anchor_string` | Free text — the literal string bytes. No importer format check (any non-empty value accepted). | `string_anchor` | The literal the survival check searches `.rdata` for. |
| `survival_derives_from` | An INTEGER kcdx_id. Must reference an existing `address_names_seed.csv` entity (cross-row FK check, like `kcdx_id`). | `data_slot` (→ its instruction/anchor); `instruction_anchor` (→ its string_anchor); `vtable_index` (→ its vtable_base) | The cross-row survival-DAG edge: the entity this row's survival derivation depends on. The importer resolves the kcdx_id to the dependency's `address_versions.id` (the `survival.derives_from` FK). |
| `survival_rule` | A structured derivation-rule string (grammar below). The importer does NOT parse the grammar — any non-empty string is accepted at author time; the future engine consumer parses it. | `data_slot` | The derivation rule the survival check re-runs to reach the slot. |
| `survival_slot_count` | A non-negative INTEGER. | `vtable_base` | The expected vtable slot count (the survival check reads N qwords + asserts each is a `.text` pointer). |
| `survival_expect_unique` | A boolean — exactly `1` or `0` (empty = NULL). Any other value = HARD ERROR. | `callsite`, `instruction_anchor`, `string_anchor` | Whether the locator is expected to resolve to exactly ONE site at the current version: the AOB-unique assertion for `callsite`/`instruction_anchor` (the `survival_aob` pattern matches exactly one `.text` span), and the unique-xref assertion for `string_anchor` (the literal has exactly one `.text` xref). A `1` lets the survival check fail loud on an ambiguous re-match (the locator went stale into non-uniqueness). |

**`survival_rule` grammar (the minimal form step 5.2 authors).** A data_slot is
reached by following a RIP-relative displacement from an anchor, or by a fixed
offset from another slot. The rule string encodes which:

- `disp32@<kid>` — follow the 32-bit RIP-relative displacement at the
  instruction the entity `<kid>` names (the anchor row). Example: `disp32@9`
  (follow the disp32 at instruction_anchor id 9). Pair this with
  `survival_derives_from = <kid>`.
- `<kid>-0x<hex>` / `<kid>+0x<hex>` — a fixed signed offset from the `.data`
  slot the entity `<kid>` resolves to. Example: `10-0xA8` (`gEnv = pConsole −
  0xA8`, where id 10 is `pConsole`). Pair this with `survival_derives_from =
  <kid>`.

Keep the rule and `survival_derives_from` consistent: the `<kid>` named in the
rule is the entity the dependency edge points at. (The grammar is intentionally
small — extend it here, in this section, when a new derivation shape is needed.)

**`vtable_index` is DEFERRED.** Its survival datum (resolve the base, take the
slot, hash the target function's body) needs the runtime-vtable verification
path. Leave its survival columns empty — the importer emits its survival row with
`kind_form = slot_target` and an empty payload; population lands with that future
path. The base reference is carried via `survival_derives_from` (the vtable_base
entity) when authored; the slot INDEX is the row's existing `value` / vtable-slot
datum, not a new column.

**`function` kinds need no survival authoring.** A function's survival datum is
its body fingerprint (`content_hash` + `length`), already on the
`address_versions` row from the bulk promote. The importer reuses it; the
maintainer never hand-authors a function's survival columns.

The six columns are read + format-validated by the importer (`seeds_shared/
validators.py`); the `survival_derives_from` FK closure is a cross-seed check
(below). `survival_expect_unique` is used by the search-locating kinds
(`callsite` + `instruction_anchor` via the AOB-unique assertion, `string_anchor`
via the unique-xref assertion); the other kinds leave it empty. The generated `survival` table shape lives in `seeds_shared/schema.py`;
the per-kind survival design rationale is in
[`data/maintainer-tool/fingerprint-per-kind.md`](../maintainer-tool/fingerprint-per-kind.md).

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

## Cross-seed FK + coverage checks (importer-enforced)

Beyond the per-file rules above, the importer enforces a handful of
cross-file invariants that catch typical authoring mistakes:

- **`address_versions_seed.kcdx_id` MUST resolve to an existing
  `address_names_seed.id`** (every per-version fact must reference an
  entity). Violation = HARD ERROR.
- **`last_verified_at_version` MUST resolve to a known `game_versions.tag`**
  (the baseline importer today only knows `GAME_VERSION_TAG`; a tag string
  that doesn't match an existing game_versions row fails loud).
- **Every entity must have at least one baseline-version
  `address_versions_seed` row** for the import's current `GAME_VERSION_TAG`.
  A named entity with no resolve facts for the baseline version =
  HARD ERROR.
- **A non-empty `survival_derives_from` MUST resolve to an existing
  `address_names_seed.id`** (the survival-DAG dependency edge must reference a
  real entity). Violation = HARD ERROR. EMPTY = allowed (no dependency).

**Surprise the maintainer should know about:** the importer SILENTLY SKIPS
`address_versions_seed` rows whose `valid_from_version != GAME_VERSION_TAG`
of the current import run. Rows for future game versions can sit in the
seed unmaterialized until an import run targets that version. If you add
a `1.6.xxxxx` row today and the importer is still building against `1.5.x`,
that 1.6 row is not in the output DB — it's not lost, just not yet
materialized.

## Test plugin requirement (policy-only)

Every non-deprecated, non-superseded entity in `address_names_seed.csv` MUST
be exercised by at least one `test-plugins/` plugin. **This is a policy
rule, not an importer check** — the importer does NOT cross-reference
`address_names_seed.csv` against the `test-plugins/` tree. Compliance is
enforced by review (`/code-review`, `/verification-checkpoint`) and by the
test-suite matrix at `test-plugins/README.md`.

The test plugin IS the re-verification mechanism when a new game version
ships:

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
- `rva` SHOULD be hex with `0x` prefix (e.g. `0x0071A5A4`) for consistency
  with existing rows. The importer's `parse_int` also accepts decimal, so
  a decimal value would technically be parsed — but mixing formats in the
  seed makes review harder and is a policy violation.
- Empty cells are empty strings in the CSV (`,,`), interpreted as NULL by
  the importer.

## What changed (this file's history)

Policy doc rewritten 2026-05-28. Previous version (the v0.1 single-CSV +
banded-IDs + engine-blocks-on-unverified model) is fully superseded.
