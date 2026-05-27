# Cross-plugin opt-in scanning for `kcdx.scan`

## Status

Deferred — genuine scope-creep beyond the `kcdx.scan` feature, surfaced as a
real next step (not buried). The user explicitly carved this out as its own
feature when `kcdx.scan` landed.

Today `kcdx.scan{ ... module = "..." }` scans exactly ONE module per call
(default `WHGame.dll`), and the resolve pipeline (`scan_engine::ResolveScan`)
resolves against that single named module. A TC author cannot, in one call,
AOB-scan the game image AND another plugin's DLL — nor scan across "the game
plus all opted-in mods" at once.

## The capability

A `kcdx.scan` form that scans the game module AND opted-in plugin modules in
one call, with per-match attribution showing WHICH module each hit came from.
Shape (illustrative — settle the surface in the feature's audit):

```lua
local r = kcdx.scan{
    name         = "find_it",
    pattern      = "48 8B ...",
    scan_plugins = true,   -- scan WHGame.dll + every opted-in plugin DLL
}
-- r.matches[i].module already carries the owning module (landed in sub-1),
-- so a match in another plugin's DLL is attributable out of the box.
```

A scan is opt-in per scanned plugin (a plugin must FLAG itself scannable in
its own manifest), so one plugin cannot blindly AOB-scan another's DLL without
that plugin consenting.

## What's needed

1. **A `[plugin]` manifest key to flag a plugin scannable** (e.g.
   `scannable = true`). This is the consent gate — a plugin opts its own DLL
   into being a scan target. Default off. Per `toml-schema.md` conventions.
2. **`scan_engine` generalized to scan multiple modules in one
   `ResolveScan`.** Today it resolves against one named module; it needs to
   iterate the game module + the set of opted-in plugin modules, accumulating
   matches across all of them. `src/scan_engine.{h,cpp}`.
3. **Per-match module attribution — already landed.** Each `ScanMatch` already
   carries `module` (sub-1 of the `kcdx.scan` feature), surfaced to Lua as
   `matches[i].module`. So the multi-module result generalizes the existing
   per-match shape without an attribution change — the binder already returns
   the owning module per match.
4. **The Lua binder + C++ mirror surface for the new arg** (`scan_plugins=` or
   the settled spelling), full parity per `lua-api-surface.md`, plus the doc
   entry (`docs/lua/scan.md` + the C++ mirror) and a regression test row that
   exercises a match in an opted-in plugin DLL (extend or sibling cap-32).

## Trigger to revisit

When EITHER holds:

- A TC author needs to AOB-scan another plugin's DLL (e.g. to hook into a
  framework mod's code by pattern rather than by a published symbol).
- An author wants ONE scan across the game image + loaded mods (a "find this
  byte sequence anywhere in the live process" diagnostic).

## Related

- `src/lua_bind_scan.cpp` — the `kcdx.scan` binder (single-module today;
  the `module` field is where the multi-module opt-in plugs in).
- `src/scan_engine.{h,cpp}` — `ResolveScan` (single-module resolve to
  generalize) + `ScanMatch.module` (the attribution that already generalizes).
- `test-plugins/cap-32-scan/` — the current single-module regression; the
  multi-module row extends it.
- `.claude/rules/lua-api-surface.md` — full Lua+C++ parity for the new arg.
- `.claude/rules/toml-schema.md` — the `scannable` manifest key conventions.
