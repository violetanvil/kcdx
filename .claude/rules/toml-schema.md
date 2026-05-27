---
paths:
  - "src/config.cpp"
  - "src/config.h"
  - "**/kcdx.toml"
  - "test-plugins/**/kcdx.toml"
  - "kcdx-engine/builtin/**/kcdx.toml"
---

# TOML schema — conventions

## `kcdx.toml` is MANIFEST-ONLY (Phase 5+)

A plugin's `kcdx.toml` declares **identity + metadata only** — it does NOT
declare behavior. The legacy behavior tables (`[[patch]]`, `[[hook]]`,
`[[mid_hook]]`, `[[trampoline]]`, `[[scan]]`) and their parsers
(`ParseOnePatch`/`Hook`/`MidHook`/`Trampoline`/`Scan`) were **deleted in
Phase 5** (`95854fe`). Behavior ships in code:

- **Lua** — `kcdx.bytes` / `kcdx.hook` / `kcdx.code` / `kcdx.command` /
  `kcdx.on` / `kcdx.publish` / `kcdx.scan` in `plugin.lua`.
- **C++** — `kcdxBytesInterface` / `kcdxHookInterface` /
  `kcdxTrampolineInterface` from `kcdxPlugin_Load`.

`LoadOneFile` (`src/config.cpp`) parses exactly three tables: `[kcdx]`,
`[plugin]`, `[entrypoints]`. A stray legacy behavior table in a `kcdx.toml`
is silently unparsed (no behavior, boots fine) — kcdx is prerelease, so there
is no migration-WARN path.

## Rules

- **The three manifest tables**: `[kcdx]` (engine/suite flags),
  `[plugin]` (identity — see below), `[entrypoints]` (`lua = "..."` /
  `dll = "..."` / `lua_after = "..."`). No behavior tables.
- **snake_case keys** throughout, not PascalCase. (The C++ DLL surface mirrors
  SKSE PascalCase for parity; the TOML manifest stays snake_case.)
- **Adding a new MANIFEST key** → extend `ParsePluginManifest` /
  `LoadEngineConfig` in `src/config.cpp` using the `OptString`/`OptInt`/
  `OptBool` helpers. (Do NOT add a new behavior table — behavior is a
  `kcdx.*` Lua surface or a `kcdx*Interface` C++ method, never TOML.)

## `[plugin].author` / `[plugin].name` / `[plugin].display_name` — different fields, different jobs

The shared-name model is `<author>.<plugin>.<bare>` (2-dot, 3-component) — see
`naming-namespaces.md`. The manifest carries the first two components:

- **`author`** — short, stable namespace ID for the author/publisher. Charset
  `[a-z0-9_]`, 2–128 chars. The LEADING namespace component the engine stamps
  on every shared name the plugin exports. Prefer short: `redmoon`. The
  reserved `kcdx` root is rejected for author plugins. An invalid `author` is a
  **hard manifest rejection** — see `naming-namespaces.md`.
- **`name`** — short, stable namespace ID for the plugin within its author
  namespace. Charset `[a-z0-9_]`, 2–128 chars (raised from 32 to accommodate
  engine-prefix author families like `kcdx_builtin_<short>`; aliases / target
  names stay 2-32). It is the SECOND namespace component the engine stamps on
  every shared name the plugin exports AND the dependency / messaging / cosave
  key. Prefer short: `outfit`, not `red-moons-immersive-inventory-overhaul`. An
  invalid `name` is a **hard manifest rejection** (a bad prefix corrupts every
  exported name) — see `naming-namespaces.md`.
- **`display_name`** — the human-facing title (launcher UI, `kcdx_list_plugins`).
  Free-form. Never used as a key or prefix.

## Locators live on the code surface, not in TOML

Locator selection (`target` name / `pattern` AOB / `address_id` / `targetSymbol`)
is a property of the `kcdx.*` Lua call or the `kcdx*Interface` C++ method
(`kcdxHookOptions` / `kcdxBytesOptions`), NOT a TOML table. The common path is
a `target` NAME (the engine carries address + ABI — the disassembler test,
`cornerstones.md`); `pattern` is the labeled expert hatch. See `docs/lua/` /
`docs/cpp/` for the per-call locator contract.

## Every plugin declares behavior in code

A `kcdx.toml` is metadata; behavior is in `plugin.lua` and/or a DLL
(`[entrypoints]`). This applies to `kcdx-engine/builtin/` first-party engine
fixes too — a builtin is just a first-party plugin (its behavior is a `kcdx.*`
Lua call or a `kcdx*Interface` method, same as any author plugin). There is no
"pure-TOML behavior" plugin anymore.

## Load-order hints — the per-plugin `[load_order]` table (`zone` / `priority`)

A plugin's optional `[load_order]` table tells kcdx where this plugin prefers to
sit in the global load order. Both keys are optional; an absent table defaults
both. See `docs/load-order.md` for the full model.

```toml
[load_order]
zone     = "before_game"   # or "after_game"; omit to derive from capabilities
priority = 30              # 0..100; 0 = earliest, 100 = latest, 50 = default
```

- `zone = "before_game"` or `"after_game"` — which side of the `game.exe` sentinel to load on. Omit to let kcdx derive from capabilities (engine builtins → before_game; user plugins → after_game).
- `priority = 0..100` — where in the chosen zone. `0` = earliest, `100` = latest, `50` = middle (default).

Both are author hints — `kcdx-engine/load_order.toml` overrides them when present.

This per-plugin `[load_order]` table (in the plugin's own `kcdx.toml`, parsed by
`config.cpp` `ParsePluginManifest`) is DISTINCT from the engine-wide override
file `kcdx-engine/load_order.toml`, whose top-level `[[plugin]]` rows are read
by a separate parser (`load_order.cpp::Read`). Different files, different
parsers. The legacy `[plugin].default_position` / `[plugin].default_priority`
keys were renamed into this table in the Phase-7 zone-rework subset and are no
longer read.
