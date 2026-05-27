-- COMP-09 plugin B (SUBSCRIBER) — exercises kcdx.publish cross-plugin
-- pub/sub.
--
-- B subscribes at plugin.lua-LOAD time to plugin A's custom event. A
-- published event is stamped with its publisher's <author>.<plugin>, so B
-- uses the "<author>.<plugin>.<event>" dot form: A's author is "ts", its
-- plugin name is "comp_09_pubsub_a", and the bare event is "outfit_changed",
-- so the full subscription name is "ts.comp_09_pubsub_a.outfit_changed".
--
-- A publishes from its input_loaded handler (after all plugin.lua loaded),
-- so this subscription is guaranteed registered before the publish fires
-- (deterministic ordering).
--
-- The callback owns the COMP-09 row. It asserts:
--   (a) it RECEIVED the payload (a table), and
--   (b) the table arrived intact BY REFERENCE — payload.x == 42 and
--       payload.name == "Noble" (the values A published).
-- A correct fire ALSO proves A's publisher namespace resolved: the event
-- only reaches this callback if it was stamped exactly
-- "ts.comp_09_pubsub_a.outfit_changed" (the name subscribed to here).
--
-- Identity-probe outcome map:
--   fires with payload.x==42 + payload.name=="Noble" -> PASS: publish
--     identity resolved (OwningPluginForCurrentCall stamped A correctly,
--     even though publish ran from inside A's input_loaded callback).
--   never fires / wrong payload -> a RegisterScriptOwner identity-resolution
--     gap (the event stamped under the wrong namespace, or the payload was
--     not passed by reference) — surface before the pub/sub layer lands.

kcdx.on("ts.comp_09_pubsub_a.outfit_changed", function(payload)
    local ok = type(payload) == "table"
        and payload.x == 42
        and payload.name == "Noble"
    kcdx.test.report("COMP-09-pubsub", ok,
        ok and ("received A's published 'outfit_changed' with payload "
                .. "x=42 name='Noble' (by reference; publisher namespace "
                .. "resolved)")
            or ("publish received but payload mismatch: x="
                .. tostring(type(payload) == "table" and payload.x or payload)
                .. " name="
                .. tostring(type(payload) == "table" and payload.name or "?")))
end)

kcdx.log.info("COMP09B",
    "subscribed to ts.comp_09_pubsub_a.outfit_changed (awaiting A's "
    .. "publish on input_loaded)")
