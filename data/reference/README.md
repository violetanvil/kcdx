# reference.sqlite — the curated reference database (user)

`reference.sqlite` is the small static database the kcdx engine ships with every
release. It carries the **curated set of named targets** kcdx maintains across
game versions, plus the per-version resolve facts (address, ABI, vtable slot,
etc.) the engine needs to resolve a plugin's hook by name.

This is the **production** reference DB. It is intentionally narrow: it carries
only what kcdx tracks centrally — the curated targets — not the binary's full
function table. Plugins that target uncurated functions declare them themselves
(see "What this database does NOT carry" below).

## Not in this repo — it ships as a release asset

The database is a **generated binary artifact**, not a tracked file. It is built
per game version and **distributed with the kcdx release** (bundled in the release
archive). After a user unpacks a release, the engine finds `reference.sqlite` in
its install directory and opens it directly — nothing to download separately,
configure, or decompress.

- **Installed size:** ~0.1 MB on disk. Trivial — the database carries roughly the
  curated-target count's worth of rows (currently ~140), not the binary's whole
  function table.
- **Launch cost:** negligible. The engine memory-maps the file and reads only the
  rows for the entities a user's installed plugins actually reference by curated
  name — a handful of microseconds even for a large modlist.

## The three-track model — what's in here vs what's NOT

kcdx tracks targets across game versions on three tracks, scoped by who
maintains them:

1. **Curated set (Track 1).** A small, named, kcdx-maintained list of well-known
   targets (`IsInCombat`, `lua_pcall`, the `IConsole` vtable slots, `gEnv`, etc.).
   The maintainer keeps these current per game version, by hand. A plugin
   references them by name (`target = "IsInCombat"`) and the engine resolves to
   the per-version address and verified ABI. **This database is exactly that
   list.**
2. **Author-declared (Track 2).** A plugin that needs to hook a function NOT in
   the curated set declares it in the plugin itself, supplying its own
   per-version pattern/ABI via `kcdx.declare(module, name, versions)`. **Not in
   this database** — the author owns it.
3. **Bulk discovery (Track 3).** A separate, on-demand author download
   (`reference-dev.sqlite`) carrying the binary's full function table for
   discovery (`kcdx.find`). Authors use it to find uncurated targets that they
   then declare in their plugin (Track 2). **Not in this database, not shipped
   to users.**

## The model — a stable id, per-version resolve facts

The database rests on three ideas the rest of this document assumes:

1. **Every curated target has a stable `kcdx_id`.** The id is assigned once and
   never changes or recycles. A plugin references a target by `kcdx_id` (or by a
   curated name that resolves to one). That reference keeps resolving across
   game updates even when the target's address moves.
2. **The kcdx_id IS the `address_names.id`.** `address_names` has one row per
   curated entity; its primary key column IS the kcdx_id. (There is no separate
   `kcdx_id` column; the PK is the handle.)
3. **Per-version resolve facts are stored as validity intervals.** A curated
   entity's address, bytes, and argument-shape can change when the game patches.
   Each distinct form is one row in `address_versions` with a `valid_from` /
   `valid_through` version range. **`valid_through IS NULL` means "this is the
   current form."** The engine reads the open (NULL) row for the running version.
   A partial-unique index guarantees at most one open row per entity.

## What this database does NOT carry

This is the production curated DB; it intentionally omits everything that isn't
part of the curated cross-version tracking:

- **The bulk function table** (the binary's other ~321,000 auto-named functions)
  — lives only in the DEV discovery DB (`reference-dev.sqlite`).
- **Per-statement metadata, call graph, variable storage** — DEV-only.
- **The abi_walker argument-width floor for uncurated functions** — also
  DEV-only. A Track-2 plugin hooking an uncurated function declares its own ABI
  via `kcdx.declare`.

A plugin that wants to hook an uncurated function does NOT look here. It uses
`kcdx.find` against the DEV DB to discover the target, then declares it in its
own plugin file with `kcdx.declare(module, name, versions)`, including the
pattern AND the ABI.

## The tables

The user database has **five** tables (plus `_dict_*` lookup tables — see
Encoding).

### `address_names` — the curated entity registry

One row per curated entity, ever. `id` IS the `kcdx_id` (the stable handle
plugins reference); not autoincrement — the importer assigns it.

| Column | Meaning |
|---|---|
| `id` | the **kcdx_id**. Primary key. The stable handle a plugin references. Never changes, never recycled. |
| `name` | the curated name (e.g. `IsInCombat`). Unique per row. |
| `is_deprecated` | `1` if this entity is superseded by another entity; the row still resolves through the chain. |
| `superseded_by` | another `address_names.id` (== another kcdx_id) — entity-to-entity supersession. |

### `address_versions` — per-version resolve facts (the spine)

One row per `(entity, version-interval)`. Carries everything about an entity
that can change when the game patches. `kcdx_id` references `address_names.id`.

| Column | Meaning |
|---|---|
| `id` | row id. |
| `kcdx_id` | the entity → `address_names.id`. Non-unique (one row per version for the same entity). |
| `kind` | the curated taxonomy: `function`, `function_no_sig`, `function_variadic`, `callsite`, `data_slot`, `string_anchor`, `instruction_anchor`, `vtable_base`, `vtable_index`. Dictionary-encoded. |
| `module_id` | which module the entity lives in → `modules.id`. |
| `rva` | the entity's address in this version. Resolve `kcdx_id` → address from the open row. `NULL` for non-byte entities (e.g. `vtable_index`). |
| `length` | byte length — needed to reproduce the hashed span `[rva, rva+length)`. |
| `content_hash` | BLAKE3 of the entity's on-disk bytes (32-byte blob). `NULL` for non-byte entities. |
| `value` | the resolved integer for a non-byte entity (a vtable slot index, a data-slot offset). |
| `signature` | the verified ABI for this version (or the abi_walker width-floor if no verified value was set). For kinds that have no signature (callsite, vtable slot, data slot), `NULL`. |
| `observed_arg_slots` | abi_walker floor: arg-slot count. |
| `caller_reg_arg_count` | caller-side register-argument estimate (≤4). |
| `caller_arg_agreement` | whether the callsites agreed on that estimate. Dictionary-encoded. |
| `offset` | for a `callsite`, the offset a consumer applies relative to the recorded address. |
| `vtable_slot` | for a `vtable_index`, the slot integer (mirrors `value` for that kind). |
| `status` | `verified` \| `unverified` — only `verified` rows resolve at runtime. Dictionary-encoded. |
| `valid_from` | first version this form held → `game_versions.id`. |
| `valid_through` | last version this form was valid → `game_versions.id`. **`NULL` = current.** A partial-unique index enforces at most one open row per `kcdx_id`. |

### `modules` — the module registry

| Column | Meaning |
|---|---|
| `id` | module id. |
| `name` | the module filename (e.g. `WHGame.dll`). |

### `game_versions` — the version registry

Backs every `valid_from` / `valid_through`.

| Column | Meaning |
|---|---|
| `id` | version id. |
| `tag` | the human version string (e.g. `1.5.1164953`). |
| `ordinal` | the monotonic sort key (the game build number); orders versions correctly. |
| `released` | release date, if known. |

### `meta` — database header (one row)

| Column | Meaning |
|---|---|
| `id` | always 1. |
| `schema_version` | the schema shape version — a consumer checks this before trusting the database. |
| `abi_confidence` | the argument-floor policy string. |

## How the engine resolves a curated target

**Resolve by curated name** (`target = "IsInCombat"`):

1. `address_names` where `name = ?` → the kcdx_id (= `address_names.id`).
2. `address_versions` where `kcdx_id = ?` and `valid_through IS NULL` → the
   open row carrying `rva`, `signature`, `kind`, etc. for the running version.
   Resolve only if `status = verified`.

```sql
SELECT n.id AS kcdx_id, v.kind, v.rva, v.signature, v.status
  FROM address_names n
  JOIN address_versions v
    ON v.kcdx_id = n.id AND v.valid_through IS NULL
 WHERE n.name = ?
```

**Resolve by `kcdx_id`:** skip `address_names`; just query
`address_versions` where `kcdx_id = ?` and `valid_through IS NULL`.

**Resolve an author-declared target** (`kcdx.declare(module, name, versions)`):
not from this database. The engine reads the plugin's declare table for the
running game version and resolves the pattern against the live binary.

## Cross-version survival

When the game updates and a plugin uses a curated target, the engine compares
the plugin's authored-against `content_hash` against the open
`address_versions.content_hash` for the same `kcdx_id`. Unchanged → the plugin
keeps working silently. Changed → a notice naming the target. **This only applies
to curated targets** — Track-2 (author-declared) targets carry their own
per-version patterns, and the survival check for them is "does the pattern
resolve on the running version" plus runtime rollback if the hook misbehaves
(see the engine's recovery model).

## Encoding

A stock SQLite database, losslessly encoded for size: byte hashes are 32-byte
blobs (not hex text); repetitive low-cardinality columns are dictionary-encoded
into integer keys with `_dict_<table>_<column>` lookup tables (`id INTEGER, val
TEXT`); address and count columns are integers. No special SQLite build or
extension is required to read it.

## Per-version refresh

The database is updated per game version. The maintainer re-verifies the curated
set when KCD2 patches and adds a new `game_versions` row + per-version
intervals: targets that didn't move silently extend (the existing open row stays
open), targets that changed get a new open interval (the old one's
`valid_through` is set), removed targets close. A curated target keeps its
stable `kcdx_id` across versions, so a plugin that references it by name or id
continues to resolve.

## The larger discovery dataset

The per-statement metadata, the call graph, the abi_walker floor for the binary's
full function table — none of these are in this user database. They live in a
separate, larger database (`reference-dev.sqlite`) that authors fetch on demand
when building plugins. See `data/reference-dev/`.
