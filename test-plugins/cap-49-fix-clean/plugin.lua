-- CAP-49 clean fixture plugin.lua — the no-false-reject proof.
--
-- This plugin.lua runs ONLY if its (fully valid, breadth-exercising) manifest
-- passed strict validation. Reaching this report line at all is the proof:
-- if strict validation false-rejected the manifest, LoadOneFile would have
-- returned early, the plugin would never have registered, plugin.lua would
-- never have run, and cap-49-clean-loads would sit PENDING (FAIL by absence).
--
-- FALSIFIABLE: the feature-broken state is "validation false-rejects a
-- valid manifest → this clean row never reports → FAIL". The pass is gated on
-- the plugin actually loading + this callback firing, not on a constant.

kcdx.on("ready", function()
    kcdx.test.report("cap-49-clean-loads", true,
        "a fully valid manifest (breadth of recognized [plugin] keys + "
        .. "[entrypoints] + [load_order] + [[plugin.dependencies]] + "
        .. "compatible_game_versions + test_names) loaded and ran its "
        .. "plugin.lua — strict manifest validation did NOT false-reject it. "
        .. "Feature-broken state: validation false-rejects a valid manifest "
        .. "→ this row never reports → FAIL.")
end)

kcdx.log.info("CAP49", "clean fixture loaded; reports cap-49-clean-loads at ready")
