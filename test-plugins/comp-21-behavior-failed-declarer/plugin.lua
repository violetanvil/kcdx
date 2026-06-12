-- COMP-21 failed declarer — deliberately ERRORS before declaring anything.
--
-- The §6 branch-b failed-declarer fixture: this plugin.lua raises a Lua error
-- at the TOP of the file, BEFORE any kcdx.behavior.declare runs. The engine's
-- per-file load guard isolates the error (the line is logged at ERROR; the
-- load continues for every other plugin), and this plugin registers ZERO
-- behaviors. The loader records this plugin's script-failure so a later
-- consumer's set on its full behavior name resolves to the FAILED-DECLARER
-- error ("'<owner>' failed to load") instead of the typo or reorder branch.
--
-- This is the load OUTCOME the resolver consults first (design §6): a declarer
-- that errored before its declares ran is a different fix (fix/remove the
-- declarer) than a typo (check the name) or a reorder (move the plugin).
--
-- It reports NO test row itself — by construction it never reaches a report
-- call. The assertion lives in comp-21-behavior-consumer.

-- The deliberate failure. Anything below it never runs (this is the point —
-- the declare that WOULD register `should_never_register` is unreachable).
error("comp-21 deliberate plugin.lua load failure (fixture) — this declarer "
    .. "errors before any kcdx.behavior.declare runs, so it registers no "
    .. "behaviors; a consumer setting its name gets the failed-declarer error")

-- Unreachable — present only to show the behavior that DID NOT register.
kcdx.behavior.declare("should_never_register", {
    description    = "comp-21 unreachable declare (the error above prevents it)",
    default        = false,
    implementation = function(value) end,
})
