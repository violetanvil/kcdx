---
paths:
  - "src/address_library.*"
  - "data/seeds/**"
  - "**/kcdx.toml"
  - "test-plugins/**/kcdx.toml"
  - "kcdx-engine/builtin/**/kcdx.toml"
---

# Address Library — use IDs, not literal RVAs

`src/address_library.{h,cpp}` compiles a seed table into `kEntries[]`. `kcdx::address_library::Resolve(uint64_t id)` does linear lookup gated on game-version match. Plugin entry point: `api->ResolveAddress(id)`.

**Note (2026-05-28):** The seed shape was redesigned — the single legacy `data/address-library/seed.csv` was split into three files under `data/seeds/`: `module_seed.csv` (module registry), `address_names_seed.csv` (per-entity stable), `address_versions_seed.csv` (per-version mutable). Status is no longer authored — it is derived per (kcdx_id, game_version) from `last_verified_at_version` plus entity-level supersession/deprecation flags. Authoring law: [data/seeds/policy.md](../../data/seeds/policy.md). In-source mirror: [src/address_library.cpp::kEntries[]](../../src/address_library.cpp). The engine consumer (refdb) is being rewritten to read the runtime DB at `data/reference.sqlite` directly; the in-source `kEntries[]` mirror is a transitional fallback.

## Rules

- **New plugin needs an RVA?** First check if a seed ID covers it. If yes, reuse. If no, add a new row (claim an unused id; ids are append-only, never renumber). Reflect in `kEntries[]`.
- **Plugin authors** call `kcdx::ResolveAddress(uint64_t id)` instead of hardcoding RVAs. Or use `address_id = N` in TOML.
- **Address Library IDs are stable across kcdx versions.** RVAs they map to may shift per KCD2 update — the database carries `game_version` per row.
- **Never paste an RVA literal** into a new plugin TOML when an ID exists or can be added. (When KCD2 patches break an RVA, only the Library updates — same model as SKSE's Address Library.)

## Vtable ID convention

IDs 3000+ in the seed are vtable INDEX constants (not RVAs). Status stays unverified until kcdx ships `[[vtable_hook]]` to consume them.
