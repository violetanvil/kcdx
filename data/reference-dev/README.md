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

Everything in `data/reference/README.md` applies here unchanged for the curated
portion — the same `entities` id authority, the same `entity_versions` model,
the same `kcdx_overlay` / `kcdx_overlay_versions` split, the same `modules` /
`game_versions` / `meta`. Read that document first.

**This database is a superset.** It contains:

1. **Everything the production DB has** — the ~140 curated entities (so
   `kcdx.find` can show "this is already known as IsInCombat — don't redeclare
   it" alongside uncurated discoveries).
2. **PLUS the bulk** — the binary's full function table (~321,000 functions)
   with their auto-names, hashes, the abi_walker argument-width floor, decompile
   quality, plus per-statement metadata, variable storage, and the call graph.

The bulk is **discovery data**, not cross-version-tracked data. It is regenerated
per game version (one DEV DB per KCD2 version); it is not diffed across versions
automatically. An author updating their plugin for a new game version fetches the
DEV DB for that version, re-runs `kcdx.find` to re-locate their targets, and
updates their plugin's `kcdx.declare` rows.

## Columns the dev database adds to the shared tables

### `entity_versions` — populated for bulk functions + two dev-only columns

Production carries `entity_versions` rows only for curated entities. The dev DB
carries them for **all** entities, including the bulk function table — so
`kcdx.find` can show the abi_walker floor signature, the function length, the
auto-name, etc. for an uncurated function being investigated.

Two columns exist only in the dev DB:

| Column | Meaning |
|---|---|
| `auto_name` | the disassembler's auto-generated label for the entity (`FUN_<rva>` form). A display label for inspection only — it is derived from the address and is **never** a resolution key (it moves every version). Absent from the production database. |
| `decompile_quality` | `clean` / `partial` / `unanalyzable` — gates whether the per-statement tools apply to this entity. Dictionary-encoded. Absent from the production database. |

### `kcdx_overlay` — two dev-only columns

| Column | Meaning |
|---|---|
| `source` | the provenance tier of the curated name (how it was established). Dictionary-encoded. |
| `notes` | the maintainer's provenance prose for the curated entry. Absent from the production database. |

## Dev-only tables (the bulk discovery surface)

These three tables exist only in the dev database. They key on the entity's
`kcdx_id` and contain entries for the bulk function table — they are the
substance of what `kcdx.find` walks.

### `statements` — per-statement metadata

One row per decompiled statement of each analyzable function. Backs
`kcdx_dev_inspect` and locator matching.

| Column | Meaning |
|---|---|
| `id` | row id. |
| `kcdx_id` | the owning function → `entities.kcdx_id`. |
| `idx` | the statement's ordinal within the function. |
| `kind` | the statement kind (call / assign / branch / …). Dictionary-encoded. |
| `pseudo_text` | the decompiled pseudo-code line. |
| `byte_range_start` | the statement's machine-code start address. |
| `byte_range_len` | the statement's machine-code byte length. |
| `content_hash` | BLAKE3 of the statement's byte sub-range (32-byte blob). |
| `callee` | the call target's name when it is a named function; `NULL` when the target is just an auto-named function (use the call graph for that case). |
| `string_ref` | a string literal the statement references, if any — a discovery anchor. |

### `referenced_vars` — per-statement variable storage

| Column | Meaning |
|---|---|
| `id` | row id. |
| `kcdx_id` | the owning function → `entities.kcdx_id`. |
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
| `caller_kcdx_id` | the calling function → `entities.kcdx_id`. |
| `callee_kcdx_id` | the called function → `entities.kcdx_id`. |
| `callsite_rva` | the address of the call instruction. |

Indexed in both directions (by caller and by callee).

## Encoding

Identical to the production database — 32-byte blob hashes, dictionary-encoded
low-cardinality columns (`_dict_<table>_<column>`), integer address/count columns,
stock SQLite.

## Why this is a separate artifact from the production DB

The production database stays small (~0.1 MB, ships with every release) by
holding ONLY the curated cross-version-tracked set. This database is ~1 GB and
exists only to serve authors during plugin development. Splitting them means:

- Every user's install is tiny + fast to launch (no bulk data to load).
- Authors who do need the bulk fetch it once per game version, on demand, only
  when actively building or updating a plugin.
- The two artifacts have disjoint purposes: the production DB serves curated
  cross-version resolution; this DB serves within-version discovery of
  uncurated targets.
