# Single-plugin hot reload (`kcdx_reload_plugin <name>`)

**Status:** out-of-scope for the v0.2 restructure; tracked here per [restructure/00-original-plan.md "Out of scope"](restructure/00-original-plan.md). Revisit when authors actively ask for it or kcdx ships beyond ~50 active plugin authors where iteration friction is felt across the community.

## Goal

A console command that re-runs ONE plugin's `plugin.lua` against the current Lua state without restarting the game. Eliminates the 10-30 second restart cycle from the author iteration loop for simple changes (Lua logic tweaks, callback edits, behavior toggles).

## Why not now

- The deferred-apply + registration-pass model assumes registration happens once at startup. Hot-reloading one plugin requires re-running its registration phase against an already-running engine, which means:
  - **Unhook existing entries** the prior plugin run installed. Safely unhooking is its own engineering surface — MinHook supports it, but chained hooks (multiple plugins on the same site) need careful ordering to remove correctly.
  - **Re-apply the new registrations** through conflict_engine without disturbing other plugins' entries on shared sites.
  - **Reset Lua state** owned by the plugin (functions, tables registered in the registry, captured upvalues) without leaking memory.
- None of these are insurmountable; they're just a real chunk of work that doesn't pay off until many authors are iterating against kcdx daily.

## Revisit triggers

- TC authors file feature requests naming hot reload as the friction point they want killed
- The active-plugin-author community grows past ~50 and the iteration loop becomes a quality-of-life pain point
- An iteration cycle measurement (game restart time × edits per session) crosses some threshold worth optimizing

## Approach when built

Likely shape (subject to design when revisited):

- `kcdx_reload_plugin <name>` console command takes the plugin's stable name
- Engine identifies all registry entries owned by that plugin; unhooks each in reverse-registration order
- Engine clears the plugin's Lua state (registry references, declared behaviors)
- Engine re-runs the plugin's `plugin.lua` (and any `[entrypoints]` files) under the existing per-plugin coroutine + crash_guard
- New registrations queue; engine runs conflict_engine + apply pass for just the reloaded plugin's entries
- Other plugins are not touched; their entries continue running through the reload

## Constraints carried forward

- Not all entries are safely unhookable at runtime (e.g. cosave callbacks fire from outside the registration plugin's scope). The reload either documents which entries don't reload (and require restart) or refuses to reload those plugins entirely.
- Behavior consumers (plugins that called `kcdx.behavior.set` against a behavior the reloaded plugin DECLARED) may see undefined values during the reload window. Solution: reloaded plugin's `behavior.declare` re-runs first; consumers' settings re-apply automatically since the behavior name still resolves.
- Trampolines allocated for the prior run get freed back to the branch pool when their hooks unhook.

## Related

- [restructure/00-original-plan.md](restructure/00-original-plan.md) §"Out of scope" — where this is referenced
- [.claude/rules/hook-engine.md](../../.claude/rules/hook-engine.md) — chained-hook removal semantics this feature depends on
- [src/hook_chain.cpp](../../src/hook_chain.cpp) — the existing chain mediator that would need an unhook path
