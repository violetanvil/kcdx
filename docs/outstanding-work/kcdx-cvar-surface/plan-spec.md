# Plan spec — `kcdx.cvar.*` CVar-read surface

## Goal

A mod author reads any game CVar's value by name: `kcdx.cvar.get_int(name)` /
`get_bool(name)` / `get_float(name)` (Lua) and the C++ mirror
`kcdxConsoleInterface::GetCVarInt/GetCVarBool/GetCVarFloat`. The engine resolves
the console + the ICVar accessors by name; the author supplies only the CVar
string they already hold (from a modding wiki, the in-game `~` console, or a
config). First live consumer: a hard `KCDX_DSCVAR` read for the asset-system
DirectStorage confirmation.

## Settled design decisions (this session, the user's calls)

- **Surface name** — `kcdx.cvar.*` domain (NOT the brief's flat `kcdx.get_cvar_*`,
  which violates `lua-api-surface.md` rules 2-3). Reads: `kcdx.cvar.get_int(name)`,
  `.get_bool(name)`, `.get_float(name)`. **Why "cvar":** the term every source an
  author finds these values in uses (CryEngine docs, the console, modding wikis);
  renaming to "setting" would force a translation on every lookup. Docs teach the
  term once.
- **`get_bool` = `get_int != 0`** — reuses `ICVar_GetIVal`, no separate entity.
- **C++ mirror** — `kcdxConsoleInterface` version 2→3, three accessors appended
  below the `--- APPEND-ONLY BELOW ---` marker (`skse-parity.md` — bump the
  version when the struct changes; never mid-struct insert, `plugin interface ABI
  is append-only`).
- **Two new Address Library entities, AP18-APPROVED 2026-06-03** —
  `ICVar_GetIVal` (vtable slot 2 / +0x10, `i32 (ptr self)`) and `ICVar_GetFVal`
  (vtable slot 4 / +0x20, `f32 (ptr self)`), both `kind=function` per the repo
  IConsole-method recording convention. Verified + gated: provenance
  `_research/icvar-getival-recon/FINDINGS.md` (commits `0971aaa` + `607a14f`).
- **Recording sequencing** — the maintainer-tool agent records the entities FIRST
  (DB-owner lane); the engine build steps come after, gated on the names
  resolving (`incremental-delivery.md`). The paste-ready spec is
  `_research/icvar-getival-recon/MAINTAINER-TOOL-HANDOFF.md`.
- **Ship all three** — int + bool + float (GetFVal verified + approved).

## Cross-step invariants

- **Full parity, same change** (`lua-api-surface.md`) — every Lua call has its C++
  mirror; the test plugin exercises BOTH surfaces.
- **No hardcoded addresses** (`no-hardcoded-addresses.md`) — the engine resolves
  `gEnv_pConsole` / `IConsole_GetCVar` / `ICVar_GetIVal` / `ICVar_GetFVal` by name
  via `refdb::ResolveAddrByName`; never a literal RVA in source.
- **Lua precision** (`lua-precision.md`) — an int CVar value pushes via an
  exact-int path; a float via `lua_pushnumber` (LUA_NUMBER=float, lossy above
  2^24 — CVar ints are typically small flags/modes, document the limit).
- **Lua bridge** (`lua-bridge.md`) — the binder uses raw Lua C API
  (`luaL_newmetatable` / `lua_pushcfunction` / `lua_setfield`); no new
  static-const sentinels.
- **Fail loud, never silent** (`logging.md`, `anti-patterns.md` AP14/silent-success)
  — a CVar that does not exist, or a console surface not ready, logs + returns a
  defined miss value (false / 0 / a documented sentinel), never a silent garbage
  read.
- **Docs + test move with the surface** (`docs-discipline.md`, `test-suite.md`) —
  same change.

## Coverage map

| Design element | Covered by | Notes |
|---|---|---|
| ICVar_GetIVal + ICVar_GetFVal recorded in DB | Step 0 (EXTERNAL, maintainer-tool lane) | Gated before step 1; spec = MAINTAINER-TOOL-HANDOFF.md |
| id-16 GetCVar stale-prose fix (`kcdx.get_cvar_*` → `kcdx.cvar.*`) | Step 0 (EXTERNAL) | Same maintainer-tool recording pass |
| engine resolve + GetCVar→ICVar→slot call (int + float) | Step 1 (`src/cvar.{h,cpp}`) | mirrors `console.cpp` resolve pattern |
| `kcdx.cvar.get_int / get_bool / get_float` Lua surface | Step 2 (`src/lua_bind_cvar.cpp`) | raw Lua C API; bool = int != 0 |
| C++ mirror `GetCVarInt/Bool/Float` | Step 3 (`Interfaces.h` v2→3 + impl) | append-only |
| regression — both surfaces exercised | Step 4 (`test-plugins/cap-71-cvar-read/`) | Lua + C++ read a known CVar; matrix row |
| docs — `kcdx.cvar.*` reference + glossary "CVar" | Step 5 (`docs/lua/cvar.md` + `docs/cpp/console.md` + index + glossary) | common-path-first |
| set_cvar / cvar exists / cvar list | OUT-OF-SCOPE (v1 is read-only) | the domain has room; not built now |
