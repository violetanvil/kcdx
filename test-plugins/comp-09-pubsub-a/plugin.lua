-- COMP-09 plugin A (PUBLISHER) — exercises kcdx.publish cross-plugin
-- pub/sub (Phase 2b sub-9).
--
-- A publishes a custom event with a TABLE payload. The subscriber half
-- (comp-09-pubsub-b) listens via kcdx.on("ts.comp_09_pubsub_a.<event>",
-- fn) and reports COMP-09 from its callback.
--
-- DETERMINISTIC ORDERING: A publishes from inside its own
-- kcdx.on("input_loaded", ...) handler — which fires on the first update
-- tick, AFTER every plugin.lua has loaded and B has subscribed. A top-level
-- publish here would race B's load. Publishing from a dispatched callback
-- ALSO exercises the identity-from-inside-a-callback probe: the
-- "<author>.<plugin>.<event>" stamp must resolve correctly when publish is called
-- from within a kcdx.on callback, not just from top-level plugin.lua.
--
-- The bare event name is "outfit_changed"; the engine prepends A's plugin
-- name, so subscribers hear "ts.comp_09_pubsub_a.outfit_changed".

kcdx.on("input_loaded", function()
    -- Publish a table payload BY REFERENCE. B asserts payload.x == 42 and
    -- payload.name == "Noble", proving the table arrived intact (not
    -- serialized — the same Lua table, shared by reference).
    local fired = kcdx.publish("outfit_changed", {
        x    = 42,
        name = "Noble",
    })
    kcdx.log.info("COMP09A",
        "published 'outfit_changed' from input_loaded -> "
        .. tostring(fired) .. " subscriber(s)")
end)

kcdx.log.info("COMP09A",
    "registered input_loaded publisher (publishes after all plugin.lua "
    .. "loaded; subscriber is comp-09-pubsub-b)")
