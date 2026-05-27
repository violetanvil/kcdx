-- COMP-11 plugin A (the ASSERTER) — after.lua: the AFTER slot.
--
-- The lua_after slot runs in RunAfterEntrypoints (hooks.cpp ordering),
-- which runs AFTER RunAll (both plugin.lua before slots) + ApplyZone, and
-- iterates plugins in load-order priority asc. A is priority 30 < B's 70, so
-- this after.lua runs BEFORE B's after.lua — this token is "a.after", and it
-- lands in the collector's sequence after both before tokens and before
-- "b.after".
--
-- The collector subscription that catches this was registered in A's
-- plugin.lua (before slot), which already ran in the earlier RunAll phase — so
-- the subscriber is long live by the time this publishes. No timing hole.

kcdx.publish("phase_token", { slot = "after" })

kcdx.log.info("COMP11A",
    "after.lua (after slot): published a.after (priority 30 -> runs before "
    .. "b.after in RunAfterEntrypoints)")
