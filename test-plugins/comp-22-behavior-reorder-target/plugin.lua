-- COMP-22 reorder target — declares `reorder_target`, loads LAST (priority
-- 90). The §6 branch-a reorder fixture's later-loading owner.
--
-- The comp-20 declarer (priority 30) sets this plugin's full name
-- (ts.comp_22_behavior_reorder_target.reorder_target) at load — BEFORE this
-- plugin's declare has run — so that set resolves to the reorder error (the
-- owner loads later than the setter). This plugin exists only to make that
-- owner a REAL installed+enabled later plugin that DOES declare the name, so
-- the resolver picks the reorder branch (not absent, not typo). The assertion
-- lives on the comp-20 declarer; this plugin reports nothing.
--
-- A self-set here would resolve normally (this plugin runs last, after its own
-- declare) — but the fixture's whole point is the EARLIER comp-20 declarer's
-- set, so this plugin just declares and stays quiet.
kcdx.behavior.declare("reorder_target", {
    description    = "comp-22 reorder fixture target (declared by the LATER plugin)",
    default        = "reorder-default",
    implementation = function(value) end,
})

kcdx.log.info("COMP22",
    "reorder-target plugin declared reorder_target (priority 90, loads last) "
    .. "— the comp-20 declarer's earlier set on it drives the §6 branch-a "
    .. "reorder error")
