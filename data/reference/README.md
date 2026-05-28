# reference.sqlite — the curated reference database (user)

`reference.sqlite` is the small static database the kcdx engine ships with every
release. It carries the **curated set of named targets** kcdx maintains across
game versions, plus the per-version verified facts (address, ABI, vtable slot,
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

## The model — a stable id, with per-version verified facts

The database is built on three ideas the rest of this document assumes:

1. **Every curated target has a stable `kcdx_id`.** A function, a curated
   callsite, a vtable slot — each is one row in `entities`, with a `kcdx_id` that
   never changes and is never recycled. A plugin references a target by `kcdx_id`
   (or by a curated name that resolves to one); that reference keeps resolving
   across game updates even though the underlying address moves.

2. **Per-version facts are stored as validity intervals.** A curated entity's
   address, bytes, and argument-shape can change when the game patches. Each
   distinct form is one row in `entity_versions` with a `valid_from` /
   `valid_through` version range. **`valid_through IS NULL` means "this is the
   current form."** The engine reads the open (NULL) row for the running version.

3. **Curated names and verified facts are separate.** A curated name
   (`kcdx_overlay`) is version-independent ("IsInCombat" is still IsInCombat).
   Its verified ABI / offset / vtable slot is per-version, in
   `kcdx_overlay_versions`, because those facts can move with the binary.

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

The user database has seven tables (plus `_dict_*` lookup tables — see Encoding).

### `entities` — the stable id authority

One row per curated entity, ever. Version-independent identity.

| Column | Meaning |
|---|---|
| `kcdx_id` | the stable id (primary key). The handle a plugin references; never recycled. |
| `entity_type` | `function` \| `vtable_slot` \| `data_slot` \| `callsite` \| `statement` (the last is reserved, not populated). Dictionary-encoded. |
| `module_id` | which module the entity lives in → `modules.id`. |

### `entity_versions` — per-version forms

One row per `(curated entity, version-interval)`. Carries everything about a
curated target that can change when the game patches.

| Column | Meaning |
|---|---|
| `id` | row id. |
| `kcdx_id` | the entity → `entities.kcdx_id`. |
| `content_hash` | BLAKE3 of the entity's on-disk bytes (32-byte blob). `NULL` for non-byte entities (a vtable slot has no bytes). |
| `rva` | the entity's address in this version. Resolve `kcdx_id` → address from the open row. |
| `length` | byte length — needed to reproduce the hashed span `[rva, rva+length)`. |
| `value` | the resolved integer for a non-byte entity (a vtable slot index, a data-slot offset). `NULL` for functions. |
| `signature` | the abi_walker width-floor for this entity (when computed). Kept for parity with the DEV DB; the *verified* signature lives in `kcdx_overlay_versions.signature`. |
| `observed_arg_slots` | the floor's argument-slot count. |
| `caller_reg_arg_count` | a caller-side register-argument estimate (≤4). |
| `caller_arg_agreement` | whether the callsites agreed on that estimate. Dictionary-encoded. |
| `valid_from` | first version this form held → `game_versions.id`. |
| `valid_through` | last version this form was valid → `game_versions.id`. **`NULL` = current.** |

There is at most one open (`valid_through IS NULL`) row per entity — that is the
current form.

### `kcdx_overlay` — the curated name layer (version-independent)

One row per curated **name**. A single `kcdx_id` may have more than one row (a
renamed entity keeps its old name row, deprecated, alongside the new one), so
`kcdx_id` here is **not** unique.

| Column | Meaning |
|---|---|
| `id` | the name-row id (primary key). |
| `kcdx_id` | the entity this name annotates → `entities.kcdx_id` (non-unique). |
| `name` | the gameplay name (e.g. `IsInCombat`). The resolution key a plugin can use instead of a raw id. May be `NULL` (a stable id can exist before a name is attached). |
| `kind` | the curated taxonomy: `function`, `function_no_sig`, `function_variadic`, `callsite`, `data_slot`, `string_anchor`, `instruction_anchor`, `vtable_base`, `vtable_index`. Dictionary-encoded. |
| `is_deprecated` | `1` if this name is superseded; it still resolves (to the same entity), with a use-the-new-name notice. |
| `superseded_by` | the name-row (`kcdx_overlay.id`) that replaces this one, if deprecated. |

### `kcdx_overlay_versions` — the curated verified facts (per-version)

One row per `(curated name, version-interval)`. Holds the **verified** facts that
move with the binary.

| Column | Meaning |
|---|---|
| `id` | row id. |
| `overlay_id` | the name-row → `kcdx_overlay.id`. (The entity is reached through it: `overlay_id` → `kcdx_overlay.kcdx_id`.) |
| `signature` | the verified argument signature for this version range — a real ABI, distinct from the floor in `entity_versions`. `NULL` for kinds that have no signature (a callsite, a vtable slot, a data slot). |
| `offset` | for a `callsite`, the offset a consumer applies relative to the recorded address. |
| `vtable_slot` | for a `vtable_index`, the slot integer. |
| `status` | `verified` \| `unverified` — **only `verified` rows resolve at runtime.** Dictionary-encoded. |
| `valid_from` | first version these verified facts hold → `game_versions.id`. |
| `valid_through` | last version valid → `game_versions.id`. **`NULL` = current.** |

The address of a curated entity is **not** stored here — it comes from the
entity's `entity_versions` open row.

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

1. `kcdx_overlay` where `name = ?` → the name-row (`id`, `kcdx_id`, `kind`).
2. `entity_versions` where `kcdx_id = ?` and `valid_through IS NULL` → the address
   (`rva`) for the running version.
3. `kcdx_overlay_versions` where `overlay_id = ?` and `valid_through IS NULL` →
   the **verified** `signature` (+ `vtable_slot` / `offset` by kind). Resolve only
   if `status = verified`.

**Resolve by `kcdx_id`:** `entity_versions` where `kcdx_id = ?` and
`valid_through IS NULL` → address.

**Resolve an author-declared target** (`kcdx.declare(module, name, versions)`):
not from this database. The engine reads the plugin's declare table for the
running game version and resolves the pattern against the live binary.

## Cross-version survival

When the game updates and a plugin uses a curated target, the engine compares
the plugin's authored-against `content_hash` against the open
`entity_versions.content_hash` for the same `kcdx_id`. Unchanged → the plugin
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
intervals; targets that didn't move silently extend, targets that changed get a
new interval, removed targets close. A curated target keeps its stable `kcdx_id`
across versions, so a plugin that references it by name or id continues to
resolve.

## The larger discovery dataset

The per-statement metadata, the call graph, the abi_walker floor for the binary's
full function table — none of these are in this user database. They live in a
separate, larger database (`reference-dev.sqlite`) that authors fetch on demand
when building plugins. See `data/reference-dev/`.
