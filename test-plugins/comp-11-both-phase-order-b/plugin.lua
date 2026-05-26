-- COMP-11 plugin B (the PUBLISHER) — plugin.lua: the BEFORE slot.
--
-- B is the HIGHER-priority plugin ([load_order].priority 70 > A's 30), so this
-- plugin.lua runs AFTER A's plugin.lua in RunAll. By the time this runs, A's
-- collector subscriptions are already live (A is lowest priority -> A's
-- plugin.lua ran first), so this published token is caught. This token is
-- "b.before"; it lands in the collector's sequence right after "a.before".
--
-- B is a pure publisher: it only publishes its phase tokens. The bare event is
-- "phase_token" with payload { slot = ... }; the engine stamps it under B's
-- name ("ts.comp_11_both_phase_order_b.phase_token"), which is the name A
-- subscribed to.

kcdx.publish("phase_token", { slot = "before" })

kcdx.log.info("COMP11B",
    "plugin.lua (before slot): published b.before (priority 70 -> runs after "
    .. "a.before; A's collector is already subscribed)")
