# Lua callback main-thread runtime guard

## Status

Known gap. The live `lua_State` is single-threaded (`lua_lock`/`lua_unlock` are
no-ops in CryEngine's bundled Lua 5.1), and `kcdx::scripting`'s dynamic dispatchers
call `lua_pcall` against `g_lua_state` with **no runtime check** that they are on
the main thread. A registered callback fired from a worker/render/audio thread
races the engine's Lua activity → corrupt VM state. Today the only protection is
the authoring contract ("only hook main-thread functions"), which leaves the
foot-gun to the author — the engine could guard it (AP12-adjacent UX punt). This
is a known correctness gap in shipped behavior (AP13), not a designed feature
awaiting demand.

## Trigger to revisit

- A callback is observed firing off-thread (Aftermath dump / BugSplat with the
  dispatcher on a non-main thread), OR
- a hook site whose thread is uncertain needs to be made safe by construction, OR
- this is picked up directly as a correctness hardening pass.

## Design

Add a `GetCurrentThreadId()` check in the `dynamic_hook_pre/post/mid`
dispatchers: capture the main thread ID at init (the thread that runs the
first-tick `ApplyZone`), and on each dispatch, if `GetCurrentThreadId() !=
g_main_thread_id`, **skip the Lua invocation** and log under a stable category
(`LUA_THREAD`) so the author sees the skipped off-thread fire rather than a
silent corruption. Skip-and-log is the safe degrade; the author still learns the
hook site is off-thread.

## Files that need to change

- `src/scripting.cpp` — capture `g_main_thread_id` at init; gate
  `dynamic_hook_pre/post/mid` on the thread-ID match; log skipped off-thread fires.
- `.claude/rules/lua-callback-threading.md` — flip the "known-safe / known-unsafe
  to hook" lists from author-responsibility-only to "engine guards, author still
  prefers main-thread sites"; drop the AP13 deferral framing once the guard ships.

## Related

- `.claude/rules/lua-callback-threading.md` — the main-thread contract.
- `anti-patterns.md` AP6 (off-thread callback) + AP13 (this gap was previously
  recorded as a someday-maybe).
