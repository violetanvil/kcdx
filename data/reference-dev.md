# reference-dev.sqlite — the bulk discovery database (authors)

`reference-dev.sqlite` is the **on-demand author download** that backs Track 3 of
kcdx's three-track model — **discovery of uncurated functions in the game
binary**. It carries the binary's full function table + per-statement metadata +
call graph + the abi_walker argument-width floor, for authors building plugins
that need to hook a function NOT in the curated set.

It is **not shipped to users.** Users never need it; the production
`reference.sqlite` (curated targets only) is what the engine consumes at launch.

## What it's for

A mod author wants to hook a game function. Two paths:

- **The function is curated** — kcdx ships a name for it. The author writes
  `target = "IsInCombat"` and the production `reference.sqlite` resolves it.
  Nothing here is needed.
- **The function is NOT curated** — the author uses this database to find it
  (via `kcdx.find`, e.g. "what function references the string 'Crime
  reported'?"), then declares the target in their own plugin via
  `kcdx.declare(module, name, versions)`, supplying their own per-version
  pattern AND the ABI. **This database is the discovery surface; it does NOT
  ship anything to the user's runtime.** The author's `kcdx.declare` is what
  the user's engine consults at launch.

## Relationship to the production database

The same five shared tables are here (`modules`, `game_versions`,
`address_names`, `address_versions`, `meta`) — read [`data/reference.md`](reference.md)
first for the shared model (`address_names.id` IS the kcdx_id, `address_versions`
keys on `kcdx_id`, partial-unique-open-interval, derived verification state,
etc.).

**This database is a superset.** It contains:

1. **Everything the production DB has** — the ~140 curated `address_names` rows
   and their corresponding `address_versions` rows, so `kcdx.find` can show
   "this is already known as IsInCombat — don't redeclare it" alongside
   uncurated discoveries.
2. **PLUS the bulk** — `address_versions` rows for the binary's full ~321,000
   functions (each with auto-name, hash, abi_walker floor signature, decompile
   quality), plus `statements` + `referenced_vars` + `call_edges`. These bulk
   `address_versions` rows have NO `address_names` row (the function is
   uncurated; the row has `kcdx_id IS NULL` and represents only a discovery
   handle for `kcdx.find`).

The bulk is **discovery data**, not cross-version-tracked data. It is
regenerated per game version (one DEV DB per KCD2 version); it is not diffed
across versions automatically. An author updating their plugin for a new game
version fetches the DEV DB for that version, re-runs `kcdx.find` to re-locate
their targets, and updates their plugin's `kcdx.declare` rows.

## Columns the dev database adds to the shared tables

### `address_versions` — two dev-only columns

Production carries `address_versions` rows only for curated entities. The dev DB
carries them for **every** entity in the binary (curated + bulk). Two columns
exist only here:

| Column | Meaning |
|---|---|
| `auto_name` | the disassembler's auto-generated label as carried verbatim from the Ghidra dump (typically `FUN_<rva>` form, but the importer doesn't enforce the format). A display label for inspection only — it is derived from the address and is **never** a resolution key (it moves every version). Absent from the production database. |
| `decompile_quality` | `clean` / `partial` / `unanalyzable` — gates whether the per-statement tools apply to this entity. Dictionary-encoded. Absent from the production database. |

The verification audit trail columns
(`last_verified_at_version`, `verified_by`, `verified_date`, `evidence_kind`)
ship to BOTH databases. Bulk uncurated rows have all four NULL (nobody ever
signs off on the bulk).

**Row-level filtering, not table-level.** USER and DEV share the
`address_versions` table shape (modulo the two dev-only columns); USER
DROPS rows whose `kcdx_id IS NULL` (the bulk uncurated rows). The filter
is per-row, not per-table.

### `address_names` — no dev-only columns

`address_names` ships identically to USER and DEV. The entity-level prose
`notes` column ships to both (small enough to include, useful to authors
reading either DB).

## Dev-only tables (the bulk discovery surface)

These three tables exist only in the dev database. Each row carries **two** FK
columns to its owning function:

- `address_version_id` — **always set**; FK to `address_versions.id`. The
  universal "which function row" pointer. This is what `kcdx.find` walks (works
  for both curated and uncurated bulk functions).
- `kcdx_id` — **nullable**, non-unique; FK to `address_names.id` when the
  function is curated, `NULL` otherwise. An ergonomic shortcut for curated-subset
  joins; redundant with `address_version_id` (mostly NULL in practice, since
  ~99.9% of the binary is uncurated).

### `statements` — per-statement metadata

One row per decompiled statement of each analyzable function. Backs
`kcdx_dev_inspect` and locator matching.

| Column | Meaning |
|---|---|
| `id` | row id. |
| `address_version_id` | the owning function row → `address_versions.id`. Always set. |
| `kcdx_id` | the owning function's curated id → `address_names.id`. `NULL` for uncurated bulk functions (the vast majority). |
| `idx` | the statement's ordinal within the function. |
| `kind` | the statement kind (call / assign / branch / …). Dictionary-encoded. |
| `pseudo_text` | the decompiled pseudo-code line. |
| `byte_range_start` | the statement's machine-code start address. |
| `byte_range_len` | the statement's machine-code byte length. |
| `content_hash` | BLAKE3 of the statement's byte sub-range (32-byte blob). |
| `callee` | the call target's name when it is meaningful; the importer nulls the column when the value is the redundant auto-name of `callee_rva` (exact `FUN_<rva>` / `FUN_<padded-rva>` shapes). Other auto-name shapes survive verbatim. Use the call graph (`call_edges`) for the structural answer to "what does this statement call." |
| `string_ref` | a string literal the statement references, if any — a discovery anchor. |

### `referenced_vars` — per-statement variable storage

| Column | Meaning |
|---|---|
| `id` | row id. |
| `address_version_id` | the owning function row → `address_versions.id`. Always set. |
| `kcdx_id` | the owning function's curated id → `address_names.id`. `NULL` for uncurated bulk. |
| `statement_idx` | the owning statement's ordinal. |
| `var_name` | the variable name, if recovered. |
| `storage_kind` | register / stack / global / … Dictionary-encoded. |
| `storage_detail` | the register name or stack offset. |
| `size_bytes` | the access width in bytes. |
| `data_type` | the approximate type. Dictionary-encoded. |

### `call_edges` — the call graph

The binary-wide caller↔callee edge set. `kcdx.find` walks it upward from an
anchor (a string, a callee) to the gameplay function an author wants.

| Column | Meaning |
|---|---|
| `id` | row id. |
| `caller_address_version_id` | the calling function row → `address_versions.id`. Always set. |
| `callee_address_version_id` | the called function row → `address_versions.id`. Always set (a call has to land here). |
| `caller_kcdx_id` | the calling function's curated id, if curated; `NULL` otherwise. |
| `callee_kcdx_id` | the called function's curated id, if curated; `NULL` otherwise. |
| `callsite_rva` | the address of the call instruction. |

Indexed in both directions (by caller's address_version_id, by callee's, and by
both kcdx_ids).

## Encoding

Identical to the production database — 32-byte blob hashes, dictionary-encoded
low-cardinality columns (`_dict_<table>_<column>`), integer address/count columns,
stock SQLite. The `_dict_*` lookup tables ship to BOTH databases (the importer
materializes every dict the row data references; tables that exist only in DEV
contribute their own `_dict_statements_kind`, `_dict_referenced_vars_storage_kind`,
`_dict_referenced_vars_data_type` lookups that USER doesn't carry).

## Why this is a separate artifact from the production DB

The production database stays small (~0.1 MB, ships with every release) by
holding ONLY the curated cross-version-tracked set. This database is ~1.3 GB and
exists only to serve authors during plugin development. Splitting them means:

- Every user's install is tiny + fast to launch (no bulk data to load).
- Authors who do need the bulk fetch it once per game version, on demand, only
  when actively building or updating a plugin.
- The two artifacts have disjoint purposes: the production DB serves curated
  cross-version resolution; this DB serves within-version discovery of
  uncurated targets.
