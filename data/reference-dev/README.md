# reference-dev.sqlite — the discovery reference database (authors)

`reference-dev.sqlite` is the **full** reference database. It is a superset of the
shipped user database (`data/reference/`): every user table and column, **plus**
the per-statement metadata, the call graph, and the curated provenance prose that
back the authoring and discovery tools (`kcdx.find`, `kcdx_dev_inspect`).

## Not shipped to users — an on-demand author download

Mod **authors** fetch this database when building plugins; it is the maintainer's
source-of-truth. It is **not** bundled in the kcdx release (users never need the
discovery surface), and it is **not** a tracked file — it is a generated artifact,
built per game version.

- **Size:** ~1.1 GB on disk (it carries every statement of every function).
- The engine never opens this database at runtime for a normal user. The only
  in-game reader is the author discovery console (`kcdx.find`), in dev mode.

## Relationship to the user database

Everything in `data/reference/README.md` applies here unchanged — the same
`entities` id authority, the same `entity_versions` validity-interval model, the
same `kcdx_overlay` / `kcdx_overlay_versions` curated split, the same `modules` /
`game_versions` / `meta`, the same resolution paths, the same encoding. Read that
document first; this one only describes **what the dev database adds**.

## Columns the dev database adds to the shared tables

### `entity_versions` — two dev-only columns

| Column | Meaning |
|---|---|
| `auto_name` | the disassembler's auto-generated label for the entity (`FUN_<rva>` form). A display label for inspection only — it is derived from the address and is **never** a resolution key (it moves every version). Absent from the user database. |
| `decompile_quality` | `clean` / `partial` / `unanalyzable` — gates whether the per-statement tools apply to this entity. Dictionary-encoded. Absent from the user database. |

### `kcdx_overlay` — two dev-only columns

| Column | Meaning |
|---|---|
| `source` | the provenance tier of the curated name (how it was established). Dictionary-encoded. |
| `notes` | the maintainer's provenance prose for the curated entry. Absent from the user database. |

## Dev-only tables

These three tables exist only in the dev database. They key on the entity's
`kcdx_id` (→ `entities`), so they join to the shared model directly.

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

A statement's bytes are a sub-range of its function's bytes, so a function whose
`entity_versions.content_hash` is unchanged across a version has every statement
unchanged too — statement-level survival follows from the function's hash, which
is why statements are not themselves version-intervaled.

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

The binary-wide caller↔callee edge set. The discovery backbone: `kcdx.find` walks
it upward from an anchor (a string, a callee) to the gameplay function an author
wants, and it is the primary signal for re-identifying an entity across game
versions.

| Column | Meaning |
|---|---|
| `id` | row id. |
| `caller_kcdx_id` | the calling function → `entities.kcdx_id`. |
| `callee_kcdx_id` | the called function → `entities.kcdx_id`. |
| `callsite_rva` | the address of the call instruction. |

Indexed in both directions (by caller and by callee).

## Encoding

Identical to the user database — 32-byte blob hashes, dictionary-encoded
low-cardinality columns (`_dict_<table>_<column>`), integer address/count columns,
stock SQLite.
