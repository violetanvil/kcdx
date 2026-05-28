# reference.sqlite — the function reference database (user)

`reference.sqlite` is the static reference database the kcdx engine consults at
launch. Per game-binary entity (a function, a curated callsite, a vtable slot, a
data slot) it carries the data the engine needs to (1) keep installed plugins
working across game updates, and (2) resolve a hook target by name or id to its
address and argument shape.

## Not in this repo — it ships as a release asset

The database is a **generated binary artifact**, not a tracked file. It is built
per game version and **distributed with the kcdx release** (bundled in the release
archive). After a user unpacks a release, the engine finds `reference.sqlite` in
its install directory and opens it directly — nothing to download separately,
configure, or decompress.

- **Installed size:** ~35 MB on disk (the release archive compresses it for
  download).
- **Launch cost:** negligible. The engine memory-maps the file and reads only the
  rows for the entities a user's installed plugins actually touch — a handful of
  milliseconds even for a large modlist. The file size does not affect launch
  speed.

## The model — a stable id, with per-version validity intervals

The database is built on three ideas that the rest of this document assumes:

1. **Every targetable entity has a stable `kcdx_id`.** A function, a curated
   callsite, a vtable slot — each is one row in `entities`, with a `kcdx_id` that
   never changes and is never recycled. A plugin references a target by `kcdx_id`
   (or by a curated name that resolves to one); that reference keeps resolving
   across game updates even though the underlying address moves.

2. **Facts that change between game versions are stored as validity intervals.**
   An entity's bytes, address, and argument-shape can change when the game
   patches. Each distinct form is one row in `entity_versions` with a
   `valid_from` / `valid_through` version range. **`valid_through IS NULL` means
   "this is the current form."** The engine reads the open (NULL) row for the
   running version.

3. **The curated layer is separate from the bulk.** Most entities are auto-derived
   and carry only a coarse argument-width floor. A small curated set (`kcdx_overlay`)
   carries a verified gameplay name and a verified argument signature. Curated
   facts that move with the binary live in their own interval table
   (`kcdx_overlay_versions`), so a verified signature is valid for a specific
   version range just like a hash is.

## The tables (user database)

The user database has seven tables (plus `_dict_*` lookup tables — see Encoding).

### `entities` — the stable id authority

One row per entity, ever. Version-independent identity.

| Column | Meaning |
|---|---|
| `kcdx_id` | the stable id (primary key). The handle a plugin references; never recycled. |
| `entity_type` | `function` \| `vtable_slot` \| `data_slot` \| `callsite` \| `statement` (the last is reserved, not populated). Dictionary-encoded. |
| `module_id` | which module the entity lives in → `modules.id`. |

### `entity_versions` — per-version forms (the spine)

One row per `(entity, version-interval)`. Carries everything that can change when
the game patches, including the auto-derived argument-width floor.

| Column | Meaning |
|---|---|
| `id` | row id. |
| `kcdx_id` | the entity → `entities.kcdx_id`. |
| `content_hash` | BLAKE3 of the entity's on-disk bytes (32-byte blob). The cross-version survival check. `NULL` for non-byte entities (a vtable slot has no bytes). |
| `rva` | the entity's address in this version. Resolve `kcdx_id` → address from the open row. |
| `length` | byte length — needed to reproduce the hashed span `[rva, rva+length)`. |
| `value` | the resolved integer for a non-byte entity (a vtable slot index, a data-slot offset). `NULL` for functions. |
| `signature` | the auto-derived argument-width floor (e.g. `? (i64, i32)`): one width-typed slot per detected argument, return unknown (`?`). An honest lower bound, never a verified type — use it only when no curated signature exists. |
| `observed_arg_slots` | the floor's argument-slot count (a lower bound, not exact arity). |
| `caller_reg_arg_count` | a caller-side register-argument estimate (≤4); a tighter lower bound on argument count. |
| `caller_arg_agreement` | whether the callsites agreed on that estimate. Dictionary-encoded. |
| `valid_from` | first version this form held → `game_versions.id`. |
| `valid_through` | last version this form was valid → `game_versions.id`. **`NULL` = current.** |

There is at most one open (`valid_through IS NULL`) row per entity — that is the
current form.

### `kcdx_overlay` — the curated name layer (version-independent)

One row per curated **name**. Sparse — only entities a maintainer has named. A
single `kcdx_id` may have more than one row (a renamed entity keeps its old name
row, deprecated, alongside the new one), so `kcdx_id` here is **not** unique.

| Column | Meaning |
|---|---|
| `id` | the name-row id (primary key). |
| `kcdx_id` | the entity this name annotates → `entities.kcdx_id` (non-unique). |
| `name` | the gameplay name (e.g. `IsInCombat`). The resolution key a plugin can use instead of a raw id. May be `NULL` (a stable id can exist before a name is attached). |
| `kind` | the curated taxonomy: `function`, `function_no_sig`, `function_variadic`, `callsite`, `data_slot`, `string_anchor`, `instruction_anchor`, `vtable_base`, `vtable_index`. Dictionary-encoded. |
| `is_deprecated` | `1` if this name is superseded; it still resolves (to the same entity), with a use-the-new-name notice. |
| `superseded_by` | the name-row (`kcdx_overlay.id`) that replaces this one, if deprecated. |

### `kcdx_overlay_versions` — the curated verified facts (per-version)

One row per `(curated name, version-interval)`. Holds the verified facts that move
with the binary. Reached from a name via `overlay_id`.

| Column | Meaning |
|---|---|
| `id` | row id. |
| `overlay_id` | the name-row → `kcdx_overlay.id`. (The entity is reached through it: `overlay_id` → `kcdx_overlay.kcdx_id`.) |
| `signature` | the **verified** argument signature for this version range — a real ABI, not the floor. `NULL` for kinds that have no signature (a callsite, a vtable slot, a data slot). |
| `offset` | for a `callsite`, the offset a consumer applies relative to the recorded address. |
| `vtable_slot` | for a `vtable_index`, the slot integer. |
| `status` | `verified` \| `unverified` — **only `verified` rows resolve at runtime.** Dictionary-encoded. |
| `valid_from` | first version these verified facts hold → `game_versions.id`. |
| `valid_through` | last version valid → `game_versions.id`. **`NULL` = current.** |

The address of a curated entity is **not** stored here — it comes from the
entity's `entity_versions` open row (one source of truth for location).

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

## How the engine resolves a target

**Resolve a hook target by curated name** (`target = "IsInCombat"`):

1. `kcdx_overlay` where `name = ?` → the name-row (`id`, `kcdx_id`, `kind`).
2. `entity_versions` where `kcdx_id = ?` and `valid_through IS NULL` → the address
   (`rva`) for the running version.
3. `kcdx_overlay_versions` where `overlay_id = ?` and `valid_through IS NULL` →
   the **verified** `signature` (+ `vtable_slot` / `offset` by kind). Resolve only
   if `status = verified`.

**Resolve by id** (`target = <kcdx_id>`): `entity_versions` where `kcdx_id = ?`
and `valid_through IS NULL` → address. If the target is hooked as a callback and
no curated signature exists, the engine falls back to the `entity_versions`
argument-width floor.

**Cross-version survival check.** When the game updates, the engine compares the
current on-disk hash of each entity a plugin touched against the open
`entity_versions.content_hash`. Unchanged → the plugin keeps working silently.
Changed → a clear notice naming the entity, and the entry proceeds (or skips, if
the author marked it safety-critical). Plugins do not break wholesale on a game
update — only when an entity they specifically target actually changed.

## Encoding

A stock SQLite database, losslessly encoded for size: byte hashes are 32-byte
blobs (not hex text); repetitive low-cardinality columns are dictionary-encoded
into integer keys with `_dict_<table>_<column>` lookup tables (`id INTEGER, val
TEXT`); address and count columns are integers. No special SQLite build or
extension is required to read it.

## Per-version refresh

The database is updated per game version. A target keeps its stable `kcdx_id`
across versions, so a plugin that references an entity by name or id continues to
resolve it even when the game updates and the entity moves — the engine reads the
open (`valid_through IS NULL`) row for the running version.

## The larger discovery dataset

The per-statement metadata and the call graph that back the authoring/discovery
tools are **not** in this user database — a separate, larger database serves
those, fetched by mod authors when building plugins. See `data/reference-dev/`.
