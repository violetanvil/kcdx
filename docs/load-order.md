# kcdx load order

How plugins are ordered, what the user can change, and what the engine
refuses to change because the request is impossible.

## Mental model

One global ordered list of plugins. One immovable sentinel: `game.exe`.
The list naturally has two zones:

```
[ kcdx.bugsplat-filename-fix    ]  zone=before_game,  priority=30
[ some-author.cool-fix          ]  zone=before_game,  priority=50
─── game.exe ──────────────────────  (immovable sentinel)
[ kcdx.cap-04-midhook           ]  zone=after_game,   priority=10
[ some-author.tweak-mod         ]  zone=after_game,   priority=50
[ some-author.late-arriving-mod ]  zone=after_game,   priority=90
```

This mirrors the SKSE / MO2 / Vortex model. Plugins are rows; the
sentinel is an immovable divider; users drag freely within their
capability limits.

Why the sentinel: WHGame.dll's `DllMain` is the only natural "phase
break" in KCD2's startup. Some fixes (BugSplat filename, custom
ICompatibility flags, etc.) MUST run before WHGame.dll's `DllMain` to
take effect; everything else runs after the engine has finished
booting. The sentinel materializes that phase break as a row the user
can position other rows around.

## Sort key

```
(Zone asc, plugin_effective_priority asc, plugin_name asc,
 Source asc, entry.priority asc, entry.name asc)
```

- **Zone** decides the side of the sentinel. `before_game = 0`,
  `after_game = 1`.
- **Plugin effective priority** orders plugins within their zone.
  `0..100`; `0` = earliest, `100` = latest, `50` = middle (default).
- **Plugin name** breaks priority ties for determinism.
- **Source** (`Engine < User`) preserves "engine fixes lead" for ties
  at the plugin level.
- **Entry priority + name** order multiple entries inside one plugin.

The priority range is deliberately sparse: `0..100`. That gives
users / authors room to insert "definitely before X" without
renumbering siblings.

## Author hints — `[plugin]` table

Two optional fields per plugin's `kcdx.toml`:

```toml
[plugin]
name              = "myauthor.example"
version           = "1.0.0"

# Where should this plugin sit by default?
#   "before_game"  — applied before WHGame.dll's DllMain
#                    (only valid if all entries are zone-flexible)
#   "after_game"   — applied at first update tick (default for user plugins)
#   ""  (omitted)  — kcdx derives from capabilities. Engine builtins
#                    default to before_game; user plugins default to
#                    after_game when their entries permit it.
default_position  = "before_game"

# Where in the chosen zone?  0 = earliest, 100 = latest, 50 = middle.
default_priority  = 30
```

Author hints are advisory. The user can override either field via
`load_order.toml` (see below). Both fields are optional; defaults are
`""` (derive) and `50`.

## User overrides — `kcdx-engine/load_order.toml`

Hand-edit it, or let a future kcdx launcher write it. Plugins not
listed inherit author defaults.

```toml
# kcdx load order. Edit via the launcher UI, or hand-edit here.
# Plugins not listed get author-default position + priority.

[[plugin]]
name      = "kcdx.bugsplat-filename-fix"
zone      = "before_game"   # one of: before_game, after_game
priority  = 30               # 0..100; lower applies earlier
enabled   = true             # set false to skip without renaming folder

[[plugin]]
name      = "some-author.tweak-mod"
zone      = "after_game"
priority  = 50
enabled   = true
```

Per-field rules:

- **`name`** — required. Must match the plugin's `[plugin].name`
  exactly. A row with no `name` is skipped with a WARN line.
- **`zone`** — optional. Missing → author default. Out-of-vocab values
  (anything other than `before_game` / `after_game`) get a WARN and
  fall back to author default.
- **`priority`** — optional. Missing → author default. Out-of-range
  values (`< 0` or `> 100`) get a WARN and fall back to author
  default.
- **`enabled`** — optional. Default `true`. `enabled = false` is a
  soft-disable: the plugin's entries don't apply, but the plugin DLL
  (if any) still loads — useful for plugins whose DLL has side
  effects you want to keep while suppressing TOML-declared work.
  Power-user / no-launcher fallback: rename the folder
  `<plugin>.disabled/`. `.disabled` wins.

"Revert to defaults" in a future launcher just deletes
`load_order.toml`. kcdx regenerates effective values from author
hints next launch.

## Capability gating

Each plugin's declared entries determine which zone it CAN sit in:

| Entry type     | Allowed in `before_game`? | Why |
|----------------|---------------------------|-----|
| `[[patch]]`    | Yes                       | Pure VirtualProtect + memcpy. Loader-safe under LDR notification. |
| `[[hook]]`     | No                        | MinHook init runs in worker thread; trampoline pool needs WHGame.dll's `.text` proximity. |
| `[[mid_hook]]` | No                        | MinHook + JIT + Lua VM. |
| `[[trampoline]]` | No                      | JIT branch-pool needs ±2 GB of WHGame.dll's `.text`. |
| `[[command]]`  | No (registered by DLL)    | Needs `gEnv->pConsole`. |
| `[[event]]`    | No (registered by DLL)    | Needs messaging subsystem. |

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
   `kcdx.toml`, fills `g_patches` / `g_hooks` / `g_mid_hooks` /
   `g_trampolines`. Each entry is stamped with its plugin's name.
2. **`load_order::Read`** loads `kcdx-engine/load_order.toml` if
   present. Missing file = quiet path; every plugin gets author
   defaults.
3. **`load_order::Resolve`** computes the `Effective(zone, priority,
   enabled)` row per plugin. Applies capability gating; logs any
   downgrades.
4. **Sort** the entry vectors by the global key above.
5. **Apply** entries in sort order. Today (v0.1, before PR 2),
   everything still applies at first update tick. Zone is only
   informational at this point.

After PR 2 ships the `LdrRegisterDllNotification` path,
`before_game`-zoned `[[patch]]` entries apply during kcdx's `DllMain`
or when their target module is mapped, BEFORE that module's own
`DllMain` runs.

## What kcdx logs

Each apply pass emits one line per plugin showing the resolved order:

```
load_order: resolved 5 plugin(s) (2 user override row(s) applied)
  kcdx.bugsplat-filename-fix: zone=before_game priority=30 enabled=true
  some-author.cool-fix:       zone=before_game priority=50 enabled=true
  kcdx.cap-04-midhook:        zone=after_game  priority=10 enabled=true
  some-author.tweak-mod:      zone=after_game  priority=50 enabled=true
  some-author.late-mod:       zone=after_game  priority=90 enabled=true
```

When a plugin's request is downgraded, the line also names the
reason:

```
  some-author.midhook-plugin: zone=after_game priority=50 enabled=true
    (plugin 'some-author.midhook-plugin' requested zone=before_game but
     declares entries (hook/mid_hook/trampoline) that require after_game;
     reassigned to after_game at priority 50)
```

## What kcdx does NOT do

- It does not refuse to load a plugin because of a zone mismatch.
  Mismatches downgrade; they don't reject.
- It does not implicitly chain hooks. First plugin to hook a function
  wins; second plugin to target the same function aborts (see
  `.claude/rules/hook-engine.md` §"First-hook-wins"). Load order
  doesn't change that — it just decides who's first.
- It does not validate that two `[[patch]]` entries in different
  plugins don't overlap. That's the conflict engine's job; load order
  decides who wins when they DO overlap (lower effective priority
  wins, with `Source::Engine` breaking ties).
