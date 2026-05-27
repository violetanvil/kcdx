-- CAP-49 observer plugin.lua — asserts each of the four cap-49 reject
-- fixtures is queryable as REJECTED via kcdx.plugin.is_rejected(name).
--
-- The four reject fixtures each carry a VALID, DISTINCT author+name and
-- reject on a DIFFERENT key/table/type at parse time:
--   ts.cap_49_fix_unknown_key   — unknown key `colour` in [plugin]
--   ts.cap_49_fix_wrong_type    — `version_independent` typed as a string
--   ts.cap_49_fix_misplaced_key — engine `dev_mode` inside the plugin [kcdx]
--   ts.cap_49_fix_stray_table   — stray top-level [[patch]] table
--
-- The engine records each reject under both the folder path (always) and the
-- "<author>.<plugin>" name key (because identity was valid at reject time),
-- then folds that map into zone_gate::IsRejected/RejectReason — the same
-- backing kcdx.plugin.is_rejected reads. So each fixture below MUST come back
-- (true, reason).
--
-- FALSIFIABLE (AP15): if validation stops rejecting a class, the engine never
-- records that reject → is_rejected returns false → the row FAILs. The pass
-- condition reads the ACTUAL accessor output (the feature under test), not a
-- constant. A reject fixture coming back is_rejected==false is exactly the
-- silent-accept regression this feature exists to prevent.

local fixtures = {
    { row = "cap-49-reject-unknown-key",
      name = "ts.cap_49_fix_unknown_key",
      class = "an unknown key in [plugin]" },
    { row = "cap-49-reject-wrong-type",
      name = "ts.cap_49_fix_wrong_type",
      class = "a wrong-typed value on a known [plugin] key" },
    { row = "cap-49-reject-misplaced-key",
      name = "ts.cap_49_fix_misplaced_key",
      class = "a misplaced engine key in the plugin's [kcdx]" },
    { row = "cap-49-reject-stray-table",
      name = "ts.cap_49_fix_stray_table",
      class = "a stray top-level [[patch]] table" },
}

kcdx.on("ready", function()
    for _, fx in ipairs(fixtures) do
        local rejected, reason = kcdx.plugin.is_rejected(fx.name)
        local reason_s = tostring(reason)
        local reason_ok = type(reason) == "string" and #reason > 0

        if rejected == true and reason_ok then
            kcdx.test.report(fx.row, true,
                "reject fixture '" .. fx.name .. "' (" .. fx.class
                .. ") is queryable as rejected: is_rejected returned "
                .. "(true, reason='" .. reason_s .. "'). Parse-time reject "
                .. "recorded + name-keyed + folded into the is_rejected "
                .. "backing as designed.")
        else
            kcdx.test.report(fx.row, false,
                "FAIL: reject fixture '" .. fx.name .. "' (" .. fx.class
                .. ") is_rejected returned (" .. tostring(rejected)
                .. ", reason=" .. reason_s .. "); want (true, non-empty "
                .. "reason). Feature-broken state: validation stops rejecting "
                .. fx.class .. " → the engine never records the reject → "
                .. "is_rejected returns false → silent-accept regression.")
        end
    end
end)

kcdx.log.info("CAP49",
    "observer registered: at kcdx.on('ready') asserts kcdx.plugin.is_rejected "
    .. "(true, reason) for the four cap-49 reject fixtures")
