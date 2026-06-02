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

The Address Library is the reference DB at `data/reference.sqlite`. It carries the curated entities across three logical tables (mirrored in the seed CSVs under `data/seeds/`): `module` (module registry), `address_names` (per-entity stable: id, name, supersession/deprecation), `address_versions` (per-version mutable: rva, signature, verification audit). The **DB is the authoring surface**; the three seed CSVs under `data/seeds/` are its **deterministic export** — the git-tracked diff/review layer, no longer hand-edited (the maintainer tool writes the DB and exports the CSVs, guaranteed by a byte-identity round-trip; design: [data/maintainer-tool/design.md](../../data/maintainer-tool/design.md)). There is NO in-source mirror — the compiled `kEntries[]` table was removed when refdb took ownership of the curated cache.

Status is not an authored column — it is derived per (kcdx_id, game_version) from `last_verified_at_version` plus entity-level supersession/deprecation flags. Authoring law: [data/seeds/policy.md](../../data/seeds/policy.md).

## Runtime resolution

`refdb` bulk-resolves the curated cache for the running game version once at `refdb::Open()`, then serves from in-memory maps (never SQL at runtime):

- **Engine-internal, by canonical name** → `kcdx::refdb::ResolveAddrByName("<name>")`.
- **Engine-internal, by stable id** → `kcdx::refdb::ResolveAddrById(id)`.
- **Plugin-facing** (the `kcdx.hook` / `kcdx.bytes` Lua binders, the C++ interface thunks) → `kcdx::address_library::ResolveByName`, which runs the self > engine > other precedence walk + alias resolution and delegates its engine-seed tier to refdb.

## Rules

- **New entity needs an RVA?** First check if an existing entity covers it (by name / rva / signature — query the DB, or read its seed-CSV export `data/seeds/address_names_seed.csv` + `address_versions_seed.csv`). If yes, reuse. If no, author a new entity in the DB via the maintainer tool (claim an unused id; ids are append-only, never renumber) per [data/seeds/policy.md](../../data/seeds/policy.md); the tool exports the seed CSVs. There is no in-source table to also edit. (The headless `import_to_sqlite.py --rebuild` from the exported seeds remains the baseline-build + round-trip oracle.)
- **A NEW entity/version requires explicit user approval before it lands (AP18).** Authoring a new entity/version in the DB (via the maintainer tool) grows the Address Library — a new curated game-binary target the project commits to maintaining across versions. STOP and get the user's explicit sign-off on the specific entity BEFORE authoring it; an addition that lands without it is unauthorized. This gates only the ADDITION, NOT the always-on resolve-by-name expectation (AP1) and NOT an UPDATE to an existing row (re-verify / deprecate / supersede). Warn-only `guard-seed-approval.py` flags the addition at author-time when the new entity lands as a net-new row in the exported `address_names_seed.csv` / `address_versions_seed.csv`; the review gates (`code-review` / `step-review`) carry the hard check. Full authoring law: [data/seeds/policy.md](../../data/seeds/policy.md) §"DB additions require explicit approval".
- **Plugin authors** call `kcdx::ResolveAddress(uint64_t id)` instead of hardcoding RVAs. Or use `address_id = N` in TOML.
- **Address Library IDs are stable across kcdx versions.** RVAs they map to may shift per KCD2 update — the DB carries `valid_from_version` per row.
- **Never paste an RVA literal** into a new plugin TOML when an ID exists or can be added. (When KCD2 patches break an RVA, only the Library updates — same model as SKSE's Address Library.)

## Vtable ID convention

IDs 3000+ in the seed are vtable INDEX constants (not RVAs). Status stays unverified until kcdx ships `[[vtable_hook]]` to consume them.
