-- COMP-13 observer plugin.lua — asserts that the subject was REJECTED by
-- zone_gate.
--
-- The subject (ts.comp_13_zone_gate_subject) declares zone=before_game,
-- which trips the synthetic After-required API entry in zone_gate's
-- capability table (kcdx.zone_gate_test_after_only,
-- src/zone_gate.cpp kCapabilities[]). zone_gate runs in LoadAllConfigs
-- at config-load time, well before any plugin.lua. By the time this
-- observer's "ready" handler fires (at end of the after_game apply
-- pass), the subject's engineAccepted has long been flipped to false
-- and its plugin.lua has been skipped at every init site.
--
-- We assert that kcdx.plugin.is_rejected returns (true, reason) for the
-- subject and the reason contains the substring "after_game" (the
-- API's required zone, per the binder's teaching-error string at
-- src/zone_gate.cpp::Check() lines 114-122):
--
--   "declared zone='before_game' but calls kcdx.zone_gate_test_after_only
--    (requires zone='after_game' in kcdx 0.2.0)"
--
-- The literal 'after_game' appears in the `requires zone='after_game'`
-- clause. We match it with plain-find (string.find with the no-pattern
-- flag true) — the single quotes around it in the reason text don't
-- affect substring matching, and we don't want regex escaping noise.

kcdx.on("ready", function()
    local subject = "ts.comp_13_zone_gate_subject"
    local rejected, reason = kcdx.plugin.is_rejected(subject)

    -- Build the assertion: rejected==true, reason is a string, reason
    -- contains "after_game" (the API's required zone). Plain-find
    -- (4th arg `true`) — substring search, no Lua patterns.
    local reason_s = tostring(reason)
    local has_after_game = type(reason) == "string"
                       and string.find(reason_s, "after_game", 1, true) ~= nil

    if rejected == true and has_after_game then
        kcdx.test.report("COMP-13-zone-reject", true,
            "subject '" .. subject .. "' rejected by zone_gate with reason '"
            .. reason_s .. "' (contains 'after_game' — the API's required zone, "
            .. "consistent with the synthetic kcdx.zone_gate_test_after_only "
            .. "entry; the gate runs at config-load before any plugin.lua, so "
            .. "the rejection persisted to ready time)")
    else
        kcdx.test.report("COMP-13-zone-reject", false,
            "subject rejection assertion failed: rejected=" .. tostring(rejected)
            .. " (want true) reason=" .. tostring(reason) .. " (want a string "
            .. "containing 'after_game'); has_after_game=" .. tostring(has_after_game))
    end
end)

kcdx.log.info("COMP13",
    "observer registered: at kcdx.on('ready') asserts kcdx.plugin.is_rejected "
    .. "for the subject ts.comp_13_zone_gate_subject")
