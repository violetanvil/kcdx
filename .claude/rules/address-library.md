---
paths:
  - "src/address_library.*"
  - "data/seeds/**"
  - "**/kcdx.toml"
  - "test-plugins/**/kcdx.toml"
  - "kcdx-engine/builtin/**/kcdx.toml"
---

# Address Library — use IDs, not literal RVAs

## Source of truth — the reference DB

The Address Library is the reference DB at `data/reference.sqlite`, authored from the three seed CSVs under `data/seeds/`: `module_seed.csv` (module registry), `address_names_seed.csv` (per-entity stable: id, name, supersession/deprecation), `address_versions_seed.csv` (per-version mutable: rva, signature, verification audit). The seeds are the maintainer-authored source; the maintainer toolchain (`data/refdata-extractor/python/import_to_sqlite.py`) regenerates `reference.sqlite` from them. There is NO in-source mirror — the compiled `kEntries[]` table was removed when refdb took ownership of the curated cache.

Status is not an authored column — it is derived per (kcdx_id, game_version) from `last_verified_at_version` plus entity-level supersession/deprecation flags. Authoring law: [data/seeds/policy.md](../../data/seeds/policy.md).

## Runtime resolution

`refdb` bulk-resolves the curated cache for the running game version once at `refdb::Open()`, then serves from in-memory maps (never SQL at runtime):

- **Engine-internal, by canonical name** → `kcdx::refdb::ResolveAddrByName("<name>")`.
- **Engine-internal, by stable id** → `kcdx::refdb::ResolveAddrById(id)`.
- **Plugin-facing** (the `kcdx.hook` / `kcdx.bytes` Lua binders, the C++ interface thunks) → `kcdx::address_library::ResolveByName`, which runs the self > engine > other precedence walk + alias resolution and delegates its engine-seed tier to refdb.

## Rules

- **New entity needs an RVA?** First check if a seed ID covers it (`data/seeds/address_names_seed.csv` by name, `address_versions_seed.csv` for the rva/signature). If yes, reuse. If no, add a row to the seed CSVs (claim an unused id; ids are append-only, never renumber) per [data/seeds/policy.md](../../data/seeds/policy.md). The DB regenerates from the seeds — there is no in-source table to also edit.
- **Plugin authors** call `kcdx::ResolveAddress(uint64_t id)` instead of hardcoding RVAs. Or use `address_id = N` in TOML.
- **Address Library IDs are stable across kcdx versions.** RVAs they map to may shift per KCD2 update — the DB carries `valid_from_version` per row.
- **Never paste an RVA literal** into a new plugin TOML when an ID exists or can be added. (When KCD2 patches break an RVA, only the Library updates — same model as SKSE's Address Library.)

## Vtable ID convention

IDs 3000+ in the seed are vtable INDEX constants (not RVAs). Status stays unverified until kcdx ships `[[vtable_hook]]` to consume them.
