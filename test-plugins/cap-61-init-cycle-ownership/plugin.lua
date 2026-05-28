-- CAP-61 plugin.lua — init-cycle ownership: the kcdx-owned ctor bracket.
--
-- The Lua-side falsifiable assertion is INTENTIONALLY conservative: it pins
-- the auto-detectable claim that the kcdx loader pipeline reached the first
-- update tick end-to-end past the C_ModManager construction point. The
-- discriminating bracket-fired-vs-native-fallback signal is in the engine
-- log (MOD_ABSORB ctor_bracket_complete vs ctor_bracket_install_failed /
-- ctor_bracket_failed) and is read by the agent post-launch.
--
-- Why this layering is honest: the ctor bracket either fires (kcdx fully
-- replaces ModManager_ctor) or it doesn't (install fails + native ctor +
-- native SELECT run). In BOTH cases, if CSystem::Init proceeds past the
-- ctor, kcdx plugins still load + run; from inside Lua, a `ready` fire
-- alone cannot distinguish the two paths. The log lines can, and the agent
-- reads them. The Lua row pins the "the engine did not crash inside the
-- bracket and did not deadlock on the readiness event" claim — itself a
-- real falsifiable signal (a bracket-runtime crash or a worker-hang
-- prevents this row from PASSING and the suite from emitting a
-- `suite: X/Y passing` line at all, surfacing the catastrophe).

local SELF_NAME = "ts.cap_61_init_cycle_ownership"

kcdx.on("ready", function()
    local rejected, reason = kcdx.plugin.is_rejected(SELF_NAME)

    if rejected == false then
        kcdx.test.report("CAP-61-bracket-reached-ready", true,
            "kcdx.on('ready') fired for '" .. SELF_NAME .. "' — the kcdx "
            .. "loader pipeline reached the first update tick end-to-end "
            .. "past C_ModManager construction. is_rejected returned "
            .. "(false, nil), so this plugin loaded normally. The "
            .. "bracket-fired-vs-native discrimination is recorded by "
            .. "the agent from the MOD_ABSORB ctor_bracket_* log lines: "
            .. "ctor_bracket_complete = the kcdx ctor bracket fired; "
            .. "ctor_bracket_install_failed / ctor_bracket_failed = the "
            .. "bracket was inactive and the native ctor + SELECT ran "
            .. "instead.")
    else
        kcdx.test.report("CAP-61-bracket-reached-ready", false,
            "FAIL: kcdx.plugin.is_rejected(\"" .. SELF_NAME .. "\") "
            .. "returned (" .. tostring(rejected)
            .. ", reason=" .. tostring(reason) .. "); want (false, nil) "
            .. "— this plugin should NOT be rejected (no [load_order], "
            .. "no version restrictions, every capability used is "
            .. "RequireZone::Either). A reject here means a manifest "
            .. "or zone regression rejected this plugin and the row "
            .. "cannot prove the bracket-reached-ready claim.")
    end
end)

kcdx.log.info("CAP61",
    "init-cycle-ownership cap-61 registered: at kcdx.on('ready') will "
    .. "assert kcdx.plugin.is_rejected(\"" .. SELF_NAME .. "\") returns "
    .. "(false, nil). The bracket-fired discrimination lives in the "
    .. "MOD_ABSORB ctor_bracket_* engine-log lines, read by the agent "
    .. "post-launch.")
