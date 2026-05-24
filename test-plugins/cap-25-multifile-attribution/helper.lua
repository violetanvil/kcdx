-- CAP-25 helper.lua (the require'd SIBLING) — THIS IS THE FILE UNDER TEST.
--
-- plugin.lua does require("helper"); the kcdx searcher resolves it to this
-- sibling and, at its compile point, kcdx calls RegisterScriptOwner(this
-- path, "ts.cap_25_multifile_attribution"). So this source is attributed
-- to the plugin in g_scriptOwners — which is exactly the fix this test
-- regression-guards. Everything below runs FROM INSIDE this require'd source.
--
-- What this file does:
--   (a) SUBSCRIBE (at helper-load time, synchronous during plugin.lua's
--       require → registered at plugin load) to this plugin's OWN published
--       event, using the "<author>.<plugin>.<event>" 3-segment dot form (the
--       bare-name-is-mine rule: a subscriber always names "<author>.<plugin>.<event>";
--       here the helper subscribes to its own plugin, so the prefix is this
--       plugin's own [plugin].author + [plugin].name).
--   (b) PUBLISH the BARE event from a DEFERRED kcdx.on("input_loaded", ...)
--       callback. This is the HARDEST identity case: when the publish runs,
--       NO plugin.lua frame and NO helper top-level frame is live — the
--       publish executes from the engine's dispatch of the input_loaded
--       callback. The engine stamps a published bare event under the
--       PUBLISHER's RESOLVED plugin name → "<resolved-owner>:event".
--
-- The subscription callback owns the CAP-25 row: it asserts the payload
-- arrived intact (payload.marker == "CAP25_OK") and reports the result.
--
-- DETERMINISTIC ORDERING (mirrors comp-09): the subscription is registered
-- synchronously at helper-load (during plugin.lua's require, i.e. at plugin
-- load), and the publish fires from input_loaded (first update tick, AFTER
-- all plugin.lua loaded). So the subscription is guaranteed present when the
-- publish fires: subscribe-at-load, publish-from-input_loaded.
--
-- WHY A FIRING CALLBACK IS UNFORGEABLE PROOF OF ATTRIBUTION
-- (the falsifiable assertion — the whole reason this fixture is shaped this
--  way; identity-probe outcome map):
--   * The helper SUBSCRIBES to "ts.cap_25_multifile_attribution.multifile_event"
--     and PUBLISHES the bare "multifile_event". The engine stamps the
--     published bare event under the PUBLISHER's resolved <author>.<plugin>.
--   * fix WORKS (helper source IS attributed to the plugin): publish
--     resolves owner = "ts.cap_25_multifile_attribution", stamps
--     "ts.cap_25_multifile_attribution.multifile_event", which MATCHES the
--     subscription → the callback FIRES with the payload → PASS.
--   * fix BROKEN (helper source resolves to "<anon>"): publish stamps
--     "<anon>.multifile_event", which does NOT match the subscription →
--     the callback NEVER FIRES → the row stays PENDING/FAIL.
--   So a firing callback carrying payload.marker == "CAP25_OK" is
--   unforgeable proof that the require'd helper's kcdx.* calls resolved to
--   the plugin — the regression that proves the completeness criterion.

local SELF  = "ts.cap_25_multifile_attribution"  -- this plugin's qualified <author>.<plugin> identity
local EVENT = "multifile_event"                     -- the bare event name
local FULL  = SELF .. "." .. EVENT                  -- the subscription string (3-segment dot)

-- (a) SUBSCRIBE at helper-load time (synchronous during plugin.lua's
-- require). The callback owns the CAP-25 row. It fires ONLY if the publish
-- below stamped the event under SELF (i.e. the helper's publish resolved to
-- this plugin, not "<anon>").
kcdx.on(FULL, function(payload)
    local ok = type(payload) == "table" and payload.marker == "CAP25_OK"
    kcdx.test.report("CAP-25-multifile-attribution", ok,
        ok and ("require'd helper's kcdx.* calls resolved to the plugin: "
                .. "the publish from inside the helper's deferred callback "
                .. "stamped '" .. FULL .. "' (matched the subscription) and "
                .. "the payload arrived intact (marker=CAP25_OK)")
            or ("subscription fired but payload mismatch — marker="
                .. tostring(type(payload) == "table" and payload.marker
                    or payload)
                .. " (expected CAP25_OK)"))
end)

-- (b) PUBLISH the BARE event from a DEFERRED input_loaded callback (first
-- update tick, after all plugin.lua loaded → the subscription above is
-- already registered). The publish runs from the engine's dispatch with no
-- plugin.lua / helper top-level frame live — the hardest identity case.
kcdx.on("input_loaded", function()
    local fired = kcdx.publish(EVENT, { marker = "CAP25_OK" })
    kcdx.log.info("CAP25",
        "helper published bare '" .. EVENT .. "' from input_loaded -> "
        .. tostring(fired) .. " subscriber(s) (expect 1 iff the helper "
        .. "resolved to the plugin and stamped '" .. FULL .. "')")
end)

kcdx.log.info("CAP25",
    "helper loaded via require; subscribed to '" .. FULL .. "' and "
    .. "registered the input_loaded deferred publisher of bare '"
    .. EVENT .. "'")

return { loaded = true }
