-- COMP-11 plugin B (the PUBLISHER) — after.lua: the AFTER slot.
--
-- The lua_after slot runs in RunAfterEntrypoints (hooks.cpp ordering),
-- AFTER RunAll (both before slots) + ApplyZone, iterating plugins in load-order
-- priority asc. B is priority 70 > A's 30, so this after.lua runs AFTER A's
-- after.lua — this token is "b.after", the LAST entry in the collector's
-- sequence. A's collector (subscribed in A's plugin.lua) catches it.

kcdx.publish("phase_token", { slot = "after" })

kcdx.log.info("COMP11B",
    "after.lua (after slot): published b.after (priority 70 -> runs last, "
    .. "after a.after in RunAfterEntrypoints)")
