# kcdx load order

How plugins are ordered, what the user can change, and what the engine
refuses to change because the request is impossible.

## Mental model

One global ordered list. One immovable sentinel: `game.exe`.
The list naturally has two zones:

```
[ kcdx_builtin.bugsplat_filename_fix ]  zone=before_game,  priority=30
[ some_author.cool_fix               ]  zone=before_game,  priority=50
─── game.exe ───────────────────────────  (immovable sentinel)
[ mods.inventory_in_dialogue         ]  zone=after_game,   priority=0
[ mods.cool_pak_mod                  ]  zone=after_game,   priority=0
[ ts.cap_04_midhook                  ]  zone=after_game,   priority=10
[ some_author.tweak_mod              ]  zone=after_game,   priority=50
[ some_author.late_arriving_mod      ]  zone=after_game,   priority=90
```

Each row is identified by the qualified `<author>.<plugin>` form the engine
derives from the plugin's manifest (`[plugin].author` + `[plugin].name`) — the
same `<author>.<plugin>`-prefixed model every shared-namespace surface uses.

This mirrors the SKSE / MO2 / Vortex model. Rows are draggable; the
sentinel is an immovable divider; users reorder freely within their
capability limits.

### kcdx IS the mod loader — plugins AND vanilla pak mods in one list

kcdx is the KCD2 mod loader. It discovers, orders, and loads **both**:

- **kcdx plugins** — a folder with a `kcdx.toml`, found in either
  `kcdx-plugins/` OR the game's `mods/` directory (a kcdx plugin works dropped
  in either place).
- **vanilla pak mods** — a folder in `mods/` with a `mod.manifest` and **no**
  `kcdx.toml` (the ordinary KCD2 mod format).

Both kinds resolve into the SAME ordered list above — so a user with a mix of
vanilla pak mods and kcdx plugins reorders them all from one place, the same
way. The `kcdx.toml`'s presence is the sole classifier: present → kcdx plugin
(its pak content loads the same way a vanilla mod's would, PLUS kcdx's extra
capabilities); absent → vanilla pak mod.

A vanilla pak mod appears in the list as a **`mods.<modid>`** row. This is the
same `<author>.<plugin>` namespace model the rows above use, applied to vanilla
mods — `mods` is the namespace, the `<modid>` from the mod's `mod.manifest` is
the name. By default a pak mod sits at `zone=after_game`, `priority=0` (an early
`after_game` block, so vanilla mods lead the author plugins), and within that
block the pak mods keep their `mods/mod_order.txt` relative order. The human mod
name (the `mod.manifest` `<name>`) rides along as a trailing `#` comment on the
row, so you see the real mod name even though the row is keyed by `<modid>`.

### Upgrading a vanilla pak mod into a kcdx plugin

A mod author turns a vanilla pak mod into a kcdx plugin by dropping a
`kcdx.toml` at the mod root. Nothing else changes: the same pak content loads
the same way, PLUS the mod can now use kcdx's capabilities (hooks, byte
patches, console commands, Lua). The upgrade is purely additive — the
`kcdx.toml`'s presence is the only thing that reclassifies the folder from
vanilla pak mod to kcdx plugin.

Why the sentinel: WHGame.dll's `DllMain` is the canonical timing anchor
for the "before_game" window — it is the only natural "phase break" in
KCD2's startup. Some fixes (BugSplat filename, custom ICompatibility
flags, etc.) MUST be in place before WHGame.dll's `DllMain` runs to
take effect; everything else runs after the engine has finished
booting. The sentinel materializes that phase break as a row the user
can position other rows around.

`before_game` is a TIMING window, not a target-DLL gate. The LDR
notification mechanism that drives the window applies a resolved
before_game patch to **any** DLL mapped during it — WHGame.dll, other
game-bin DLLs, third-party preloads — not just WHGame.dll. The sentinel
names *when* the window closes, not *which* DLL the window's patches
may target.

## Sort key

```
(Zone asc, plugin_effective_priority asc, orderIndex asc, plugin_name asc,
 Source asc, entry.priority asc, entry.name asc)
```

- **Zone** decides the side of the sentinel. `before_game = 0`,
  `after_game = 1`.
- **Plugin effective priority** orders rows within their zone.
  `0..100`; `0` = earliest, `100` = latest, `50` = middle (default).
- **orderIndex** breaks priority ties by `mods/mod_order.txt` line position,
  so vanilla pak mods at the same priority keep their `mod_order.txt` relative
  order. A pak mod not listed in `mod_order.txt` sorts after the listed ones;
  a kcdx plugin carries the maximum `orderIndex`, so among plugins this
  tiebreaker is a no-op and their relative order still breaks on name.
- **Plugin name** breaks remaining ties for determinism.
- **Source** (`Engine < User`) preserves "engine fixes lead" for ties
  at the plugin level.
- **Entry priority + name** order multiple entries inside one plugin.

A vanilla pak mod sorts by this same key: `zone=after_game`, `priority=0`,
`orderIndex` from its `mod_order.txt` line. To move a pak mod above or below
others, change its priority in `load_order.toml` (below) exactly as you would
for a plugin.

The priority range is deliberately sparse: `0..100`. That gives
users / authors room to insert "definitely before X" without
renumbering siblings.

## Author hints — the per-plugin `[load_order]` table

A `[load_order]` table per plugin's `kcdx.toml`, with two optional keys:

```toml
[load_order]
# Where should this plugin sit by default?
#   "before_game"  — applied during the LDR window (before WHGame.dll's
#                    DllMain; targets any DLL mapped during the window)
#                    (only valid if all entries are zone-flexible)
#   "after_game"   — applied at first update tick (default for user plugins)
#   ""  (omitted)  — kcdx derives from capabilities. Engine builtins
#                    default to before_game; user plugins default to
#                    after_game when their entries permit it.
zone     = "before_game"

# Where in the chosen zone?  0 = earliest, 100 = latest, 50 = middle.
priority = 30
```

Author hints are advisory. The user can override either field via
`load_order.toml` (see below). Both keys are optional; an absent `[load_order]`
table defaults `zone` to `""` (derive) and `priority` to `50`.

This per-plugin `[load_order]` table (in the plugin's own `kcdx.toml`) is
DISTINCT from the engine-wide override file `kcdx-engine/load_order.toml`
described below, whose top-level `[[plugin]]` rows a separate parser reads.
(The legacy `[plugin].default_position` / `[plugin].default_priority` keys were
renamed into this table in the zone rework.)

## User overrides — `kcdx-engine/load_order.toml`

Hand-edit it, or let a future kcdx launcher write it. Plugins not
listed inherit author defaults.

```toml
# kcdx load order. Edit via the launcher UI, or hand-edit here.
# Plugins not listed get author-default position + priority.

[[plugin]]
# Identify the plugin by its qualified <author>.<plugin> form, mirroring how
# every other shared-namespace surface names a plugin.
name      = "kcdx_builtin.bugsplat_filename_fix"
zone      = "before_game"   # one of: before_game, after_game
priority  = 30               # 0..100; lower applies earlier
enabled   = true             # set false to skip without renaming folder

[[plugin]]
name      = "some_author.tweak_mod"
zone      = "after_game"
priority  = 50
enabled   = true

[[plugin]]
name      = "mods.cool_pak_mod"   # Cool Pak Mod
zone      = "after_game"
priority  = 20                     # bumped above the default-0 pak block
enabled   = true
```

A **vanilla pak mod** is reordered exactly like a plugin: edit its
`mods.<modid>` row — change `zone`, `priority`, or `enabled`, keyed by the
`<modid>` from the mod's `mod.manifest`. kcdx owns the resolved order; the
trailing `#` comment carries the human mod name so you can tell which mod a
`<modid>` row is. kcdx discovers each pak mod on first run, seeds its initial
position from `mods/mod_order.txt`, and writes its `mods.<modid>` row here so
you can edit it; `mod_order.txt` is only the seed kcdx reads and keeps in sync
— your `load_order.toml` edits are the authority.

Per-field rules:

- **`name`** — required. For a plugin: the qualified `<author>.<plugin>` form
  (author taken from `[plugin].author`, plugin from `[plugin].name`). For a
  vanilla pak mod: `mods.<modid>` (the `<modid>` from its `mod.manifest`). A
  row with no `name`, or a `name` that doesn't match any discovered plugin or
  pak mod, is skipped with a WARN line.
- **`zone`** — optional. Missing → author default. Out-of-vocab values
  (anything other than `before_game` / `after_game`) get a WARN and
  fall back to author default.
- **`priority`** — optional. Missing → author default. Out-of-range
  values (`< 0` or `> 100`) get a WARN and fall back to author
  default.
- **`enabled`** — optional. Default `true`. `enabled = false` skips
  every side effect of the plugin: its `plugin.lua` never runs and its
  `kcdxPlugin_Preload` / `kcdxPlugin_Load` are never called, so none of
  its `kcdx.*` registrations (`kcdx.bytes` / `kcdx.hook` / `kcdx.code` /
  `kcdx.command` / `kcdx.on` / `kcdx.scan`) or `kcdxBytesInterface` /
  `kcdxHookInterface` / `kcdxTrampolineInterface` installs ever happen.
  The plugin's DLL is still mapped into the process (the loader reads its
  manifest version + exports), but none of its entry points fire. This is
  the single, authoritative way to disable a plugin without uninstalling
  it.

"Revert to defaults" in a future launcher just deletes
`load_order.toml`. kcdx regenerates effective values from author
hints next launch.

## Behavior dependency edges — `kcdx-engine/behavior_edges.toml`

The load-order unit also owns the **behavior dependency edge store**
(`kcdx-engine/behavior_edges.toml`) — an engine-managed file (not user-edited)
that records which plugins set which named behaviors (`kcdx.behavior.set`). At
the next launch, kcdx re-checks these edges against the resolved order **before
any plugin runs**: a consumer that now loads before its declarer is reported up
front, and the persisted edge sharpens the resolution error from the second
launch on. The store is rebuilt from each launch's observed sets and
self-invalidates (a dropped or uninstalled edge is pruned). It is the
load-order-side persistence of the named-behavior ordering story; the
author-facing surface is documented in [`lua/behavior.md`](lua/behavior.md)
("The ordering errors" / "Fixing a bad order").

## Auto-order — the engine can FIX a bad order, on demand

When a behavior consumer is ordered **before** the declarer it depends on, the
recorded edges (above) carry everything needed to repair it: kcdx can compute a
corrected order that puts each consumer **after** its declarer and write it back
to `load_order.toml`. This is a **callable correction**, not an automatic one —
the engine never silently reorders your list. You invoke it (a future pre-launch
launcher button is the intended trigger), and it does three things:

- **Computes a corrected order** that satisfies every recorded dependency
  (consumer below declarer), moving **only** the rows that must move — an
  unrelated plugin keeps its position.
- **Reports a cycle instead of guessing.** If two plugins depend on each other
  in a circle (A must load both before and after B), no single order can satisfy
  them — kcdx names the plugins in the cycle and leaves your order **unchanged**
  rather than picking an arbitrary (and necessarily wrong) one. You resolve the
  circular dependency between those plugins, then re-run it.
- **Applies the correction by writing `load_order.toml` priority rows** — it
  adjusts each moved plugin's `priority` so the order sorts correctly.

**It takes effect at the NEXT launch, not immediately.** The load order is
consumed at boot — by the time you could trigger this, the running session's
order is already in use — so the corrected order applies the next time you start
the game. (This is also why it is a *pre-launch* surface: the launcher, not an
in-game console command.)

## Capability gating

Each plugin's declared entries (its `kcdx.*` Lua calls / `kcdx*Interface`
C++ methods — behavior ships in CODE, not TOML) determine
which zone it CAN sit in:

| Surface              | `before_game`-capable in principle? | Why |
|----------------------|-------------------------------------|-----|
| `kcdx.bytes`         | Yes (mechanism is loader-safe)      | Pure VirtualProtect + memcpy; loader-safe under LDR notification. ⚠️ But see §"before_game is STUBBED" below — no registry-apply path is wired for before_game yet. |
| `kcdx.hook`          | No                                  | MinHook init runs in the worker thread; the detour chain needs WHGame.dll's `.text` proximity. (before_game hooks are deferred work.) |
| `kcdx.hook mode=mid` | No                                  | MinHook + JIT (+ a Lua callback, which needs the VM). |
| `kcdx.code`          | No                                  | JIT branch-pool / trampoline alloc needs ±2 GB of WHGame.dll's `.text`. |
| `kcdx.command`       | No (registered by the plugin)       | Needs `gEnv->pConsole`. |
| `kcdx.on`            | No (registered by the plugin)       | Needs the messaging subsystem. |

(Surface mapping from the deleted legacy TOML tables: `[[patch]]`→`kcdx.bytes`,
`[[hook]]`→`kcdx.hook`, `[[mid_hook]]`→`kcdx.hook mode=mid`,
`[[trampoline]]`→`kcdx.code`.)

A plugin with at least one after_game-requiring entry has
`MinZone = AfterGame`. Engine derives this once at config-load time
and gates resolution against it:

- If author hint / user override puts the plugin in `before_game`
  but its `MinZone` is `AfterGame`, kcdx **downgrades** to
  `after_game` at priority `50` and logs a WARN naming the reason.
  The plugin still loads — it just runs in a position where its
  entries actually function.

A future launcher should prevent this state from being saved in the
first place: when the user drags a mid-hook plugin into before_game,
the UI rejects the move and shows the engine-derived reason.

## Lifecycle

1. **`config::LoadAllConfigs`** walks discovery roots, parses every
   `kcdx.toml` (identity + metadata only — behavior ships in plugin code), and loads each
   plugin's behavior code (`plugin.lua` / DLL). The plugin's `kcdx.*`
   Lua calls / `kcdx*Interface` C++ methods queue intent into the apply
   registry (`lua_registry`), each entry stamped with its plugin's name.
2. **`load_order::Read`** loads `kcdx-engine/load_order.toml` if
   present. Missing file = quiet path; every plugin gets author
   defaults.
3. **`load_order::Resolve`** computes the `Effective(zone, priority,
   enabled)` row per plugin. Applies capability gating; logs any
   downgrades.
4. **`lua_registry::ApplyZone(zone)`** is the apply driver: it snapshots
   the pending registry entries, filters to the given zone (by owning
   plugin), **sorts by the unified key above**, and dispatches each entry
   to its per-kind handler (`Kind::Bytes` → `patch::ApplyPatch`,
   `Kind::Hook` → `hook_chain`). Zone is the source of truth for timing:
   - `zone=after_game` entries apply at the first update tick — this is
     the SOLE live invocation today (`ApplyZone(AfterGame)`). It is the
     same point patches have always applied historically.
   - `zone=before_game` is the TARGET for an `ApplyZone(BeforeGame)`
     invocation during kcdx.dll's `DllMain` / at LDR-notification time —
     but ⚠️ **that invocation is NOT BUILT yet** (see §"before_game is
     STUBBED" below). The only before_game machinery that runs today is
     `ldr_notify`, which iterates the legacy `patch::g_patches` vector —
     permanently EMPTY since the legacy byte-patch parser was removed — so
     before_game application currently applies NOTHING.

`after_game` is unconditionally honored — no env var, no feature flag.
The load order says when; the engine obeys. before_game timing is
designed (the load order can declare it) but not yet wired to an apply
path — deferred.

## ⚠️ before_game is STUBBED — not yet wired

**A plugin CAN declare `zone = "before_game"`, but its entries do NOT
apply yet.** There is no live before_game registry-apply path in kcdx
today:

- `lua_registry::ApplyZone` is only ever invoked with `AfterGame`. There
  is no `ApplyZone(BeforeGame)` call site.
- `ldr_notify`'s before_game applicator (`ApplyEntriesForModule` /
  `ApplyAlreadyLoaded`) iterates only `patch::g_patches`, which has had no
  populator since the legacy `[[patch]]` parser was removed —
  so it is permanently empty and applies nothing.
- The `kcdx-engine/builtin/bugsplat-filename-fix` builtin (`zone =
  before_game`) is a **MANIFEST-ONLY STUB**: it declares the zone but
  ships NO behavior (no entrypoints, no Lua, no patch) and is
  ship-disabled (`enabled = false`). It is a placeholder a later rewrite
  lands in place.
- The ONLY before_game thing actually running is
  `early_hook::bugsplat::Arm` — the BugSplat ctor install in
  `dllmain.cpp` (`RunBeforeGameZoneInDllMain`), NOT a load-order entry.

before_game application is **aspirational / deferred** —
the full spec covers the LDR-notification install path, the foreign-module/
export locator, and the bugsplat consumer.
A `zone = "before_game"` declaration is honored by the load-order
resolver (the row sorts to the before_game side) but does not yet reach
an apply.

## What kcdx logs

Each apply pass emits one line per plugin showing the resolved order:

```
load_order: resolved 5 plugin(s) (2 user override row(s) applied)
  kcdx_builtin.bugsplat_filename_fix: zone=before_game priority=30 enabled=true
  some_author.cool_fix:               zone=before_game priority=50 enabled=true
  ts.cap_04_midhook:                  zone=after_game  priority=10 enabled=true
  some_author.tweak_mod:              zone=after_game  priority=50 enabled=true
  some_author.late_mod:               zone=after_game  priority=90 enabled=true
```

When a plugin's request is downgraded, the line also names the
reason:

```
  some_author.midhook_plugin: zone=after_game priority=50 enabled=true
    (plugin 'some_author.midhook_plugin' requested zone=before_game but
     declares entries (kcdx.hook / kcdx.hook mode=mid / kcdx.code) that
     require after_game; reassigned to after_game at priority 50)
```

## What kcdx does NOT do

- It does not refuse to load a plugin because of a zone mismatch.
  Mismatches downgrade; they don't reject.
- Load order decides hook coexistence, not load-time rejection. Multiple
  compatible `kcdx.hook` callbacks on one target coexist in a single
  load-order-ordered chain (`hook_chain`); only genuinely incompatible
  hooks at one site reject the later-in-load-order one. `kcdx.hook`
  conflicts resolve by load order through the chain engine: load order
  decides who is first in the chain and who wins an incompatibility.
- It does not validate that two `kcdx.bytes` patches in different
  plugins don't overlap up front. That's the conflict engine's job; load
  order decides who wins when they DO overlap (lower effective priority
  wins, with `Source::Engine` breaking ties).
