---
paths:
  - "src/address_library.*"
  - "data/address-library/**"
  - "**/kcdx.toml"
  - "test-plugins/**/kcdx.toml"
  - "kcdx-engine/builtin/**/kcdx.toml"
---

# Address Library — use IDs, not literal RVAs

`src/address_library.{h,cpp}` compiles a seed CSV into `kEntries[]`. `kcdx::address_library::Resolve(uint64_t id)` does linear lookup gated on game-version match + `status == "verified"`. Plugin entry point: `api->ResolveAddress(id)`.

Seed source of truth: [data/address-library/seed.csv](../../data/address-library/seed.csv) (in-repo). In-source mirror: [src/address_library.cpp::kEntries[]](../../src/address_library.cpp). Adding new IDs requires editing both. Naming + status policy: [data/address-library/policy.md](../../data/address-library/policy.md).

## Rules

- **New plugin needs an RVA?** First check if a seed ID covers it. If yes, reuse. If no, add a new row (claim an unused id; ids are append-only, never renumber). Reflect in `kEntries[]`.
- **Plugin authors** call `kcdx::ResolveAddress(uint64_t id)` instead of hardcoding RVAs. Or use `address_id = N` in TOML.
- **Address Library IDs are stable across kcdx versions.** RVAs they map to may shift per KCD2 update — the database carries `game_version` per row.
- **Never paste an RVA literal** into a new plugin TOML when an ID exists or can be added. (When KCD2 patches break an RVA, only the Library updates — same model as SKSE's Address Library.)

## Vtable ID convention

IDs 3000+ in the seed are vtable INDEX constants (not RVAs). Status stays unverified until kcdx ships `[[vtable_hook]]` to consume them.
