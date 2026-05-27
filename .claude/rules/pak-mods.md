---
paths:
  - "test-fixtures/pak-mods/**"
---

# Pak mods (test fixtures)

`test-fixtures/pak-mods/` holds pak mods used as test fixtures for kcdx (`lua-sandbox-probe`, `lua-memory-verify`, `inventory-in-dialogue`, `inventory-in-dialogue-quicksave`). They are **read-only** — edit them only when a kcdx test needs a new fixture. Each fixture keeps its probe Lua source + built `.pak`; large splash-art source files are not vendored.

## Pak mod facts

- **A pak is `.zip` renamed `.pak`, zero compression.** 7zip won't work. Use Windows Explorer "compressed folder" or built-in zip with `compresslevel=0`.
- **Pak Lua has zero FFI.** `package.loadlib` returns `(nil, "dynamic libraries not enabled")`. `os.execute`/`os.remove`/`os.getenv` all `nil`. `io` table missing entirely. CryEngine compiled Lua without `LUA_USE_DLOPEN` / `LUA_DL_DLL`.
- **Pak mods cannot patch compiled code.** Surface = XML/Lua/Schematyc/tables/CVars only.
- **The only bridge between pak Lua and C++ is kcdx.** Exposing `kcdx.memory.*` via `RegisterKcdxTable` is the sole path for Lua mods to touch C++.
- **Nested-folder layout is intentional.** Outer dir = dev assets (README, splash, `.kra`); inner dir = what Steam Workshop Uploader points at. Inner dir's name = eventual `mods/<name>/` on player's machine.

## Distribution

- **Steam Workshop accepts only pak mods.** kcdx ships via Nexus / direct download.

## Live-confirmed (2026-05-18)

Probe at `test-fixtures/pak-mods/lua-sandbox-probe/` against KCD2 1.5.1164953. Probe source preserved for re-running on future KCD2 patches.

Related: `[[lua-bridge]]`.
