# kcdx load order

How plugins are ordered, what the user can change, and what the engine
refuses to change because the request is impossible.

## Mental model

One global ordered list of plugins. One immovable sentinel: `game.exe`.
The list naturally has two zones:

```
[ kcdx_builtin.bugsplat_filename_fix ]  zone=before_game,  priority=30
[ some_author.cool_fix               ]  zone=before_game,  priority=50
─── game.exe ───────────────────────────  (immovable sentinel)
[ ts.cap_04_midhook                  ]  zone=after_game,   priority=10
[ some_author.tweak_mod              ]  zone=after_game,   priority=50
[ some_author.late_arriving_mod      ]  zone=after_game,   priority=90
```

Each row is identified by the qualified `<author>.<plugin>` form the engine
derives from the plugin's manifest (`[plugin].author` + `[plugin].name`); see
[`naming-namespaces.md`](../.claude/rules/naming-namespaces.md).

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

## Author hints — the per-plugin `[load_order]` table

A `[load_order]` table per plugin's `kcdx.toml`, with two optional keys:

```toml
[load_order]
# Where should this plugin sit by default?
#   "before_game"  — applied before WHGame.dll's DllMain
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
renamed into this table in the Phase-7 zone-rework subset.)

## User overrides — `kcdx-engine/load_order.toml`

Hand-edit it, or let a future kcdx launcher write it. Plugins not
listed inherit author defaults.

```toml
# kcdx load order. Edit via the launcher UI, or hand-edit here.
# Plugins not listed get author-default position + priority.

[[plugin]]
# Identify the plugin by its qualified <author>.<plugin> form, mirroring how
# every other shared-namespace surface names a plugin (naming-namespaces.md).
name      = "kcdx_builtin.bugsplat_filename_fix"
zone      = "before_game"   # one of: before_game, after_game
priority  = 30               # 0..100; lower applies earlier
enabled   = true             # set false to skip without renaming folder

[[plugin]]
name      = "some_author.tweak_mod"
zone      = "after_game"
priority  = 50
enabled   = true
```

Per-field rules:

- **`name`** — required. The qualified `<author>.<plugin>` form (author
  taken from `[plugin].author`, plugin from `[plugin].name`). A row with
  no `name`, or a `name` that doesn't match any discovered plugin's
  qualified form, is skipped with a WARN line.
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
5. **Apply** entries in sort order. Zone is the source of truth for
   timing:
   - `zone=before_game` `[[patch]]` entries apply during kcdx.asi's
     `DllMain` (against modules already mapped — ntdll, kernel32,
     dinput8, kcdx.asi itself) or when their target module is mapped
     later, BEFORE that module's own `DllMain` runs. This is via an
     `LdrRegisterDllNotification` callback installed during kcdx's
     `DllMain`.
   - `zone=after_game` entries apply at the first update tick, the
     same point patches have always applied historically.

`before_game` is unconditionally honored — no env var, no feature
flag. The load order says when; the engine obeys.

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
