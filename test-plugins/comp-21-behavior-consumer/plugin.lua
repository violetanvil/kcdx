-- COMP-21 consumer — sets the FAILED declarer's behavior, asserts the
-- §6 branch-b failed-declarer teaching error.
--
-- comp-21-behavior-failed-declarer (priority 20) loads FIRST and its
-- plugin.lua errors before declaring anything; this consumer (priority 40)
-- loads LATER, so by set time the loader has recorded that declarer's
-- script-failure. The set on its full name resolves to the FAILED-DECLARER
-- branch — the resolver consults the declarer's load OUTCOME first.

local FAILED = "ts.comp_21_behavior_failed_declarer."

-- Set at load (pcall-captured so the raise is an observation, never an abort).
local ok_set, err_set =
    pcall(kcdx.behavior.set, FAILED .. "should_never_register", true)

-- comp-21-set-failed-declarer — the set on the failed declarer's name RAISED
-- the failed-declarer teaching error naming the load failure as the fix.
-- FALSIFIABLE: the set SUCCEEDED (resolved a name a failed declarer never
-- registered), the error names a REORDER or a typo instead of the load
-- failure, or the error does not name the failed owner -> FAIL. The
-- discriminator is the declarer's load OUTCOME (it errored), not order or a
-- name miss.
kcdx.on("input_loaded", function()
    local row = "comp-21-set-failed-declarer"
    if ok_set then
        kcdx.test.report(row, false, "set('" .. FAILED
            .. "should_never_register') SUCCEEDED — a set on a behavior a "
            .. "FAILED declarer never registered must RAISE, not resolve")
    elseif type(err_set) ~= "string"
        or not string.find(err_set, "failed to load", 1, true) then
        kcdx.test.report(row, false, "the set raised but the error does not "
            .. "say the declarer 'failed to load' (the §6 branch-b "
            .. "failed-declarer wording) — the resolver must consult the "
            .. "load OUTCOME first; got: " .. tostring(err_set))
    elseif string.find(err_set, "below it", 1, true)
        or string.find(err_set, "loads after", 1, true) then
        kcdx.test.report(row, false, "the error suggests a REORDER for a "
            .. "FAILED declarer — no reorder fixes a load failure; the fix is "
            .. "to fix or remove the declarer (got: " .. tostring(err_set)
            .. ")")
    elseif not string.find(err_set,
        "comp_21_behavior_failed_declarer", 1, true) then
        kcdx.test.report(row, false, "the failed-declarer error does not name "
            .. "the failed owner (comp_21_behavior_failed_declarer); got: "
            .. tostring(err_set))
    else
        kcdx.test.report(row, true, "a set on a FAILED declarer's behavior "
            .. "raised the §6 branch-b failed-declarer teaching error "
            .. "('<owner> failed to load — fix or remove it'), naming the "
            .. "failed owner and the right fix — the resolver consulted the "
            .. "declarer's load OUTCOME first, distinguishing a failed load "
            .. "from a typo or a reorder")
    end
    kcdx.log.info("COMP21",
        "behavior failed-declarer consumer reported its row (set on a "
        .. "deliberately-failed declarer -> the failed-load teaching error)")
end)
