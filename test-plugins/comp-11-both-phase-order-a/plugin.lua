-- COMP-11 plugin A (the ASSERTER) — plugin.lua: the BEFORE slot + the
-- collector subscriptions + the input_loaded assert.
--
-- A is the LOWEST-priority plugin (default_priority 30 < B's 70), so this
-- plugin.lua runs FIRST in RunAll — before ANY phase token publishes. That is
-- what makes the collector deterministic (option B): we subscribe to both
-- plugins' token events at the TOP here, BEFORE publishing our own before
-- token, so kcdx.publish (synchronous) cannot fire a token before the
-- collector is live. See kcdx.toml for the full subscribe-before-first-token
-- argument.
--
-- The token event is the BARE name "phase_token"; the engine prepends the
-- publishing plugin's name, so a token from A arrives as
-- "ts_comp_11_both_phase_order_a:phase_token" and one from B as
-- "ts_comp_11_both_phase_order_b:phase_token". The event NAME tells WHICH
-- plugin; the payload { slot = "before" | "after" } tells WHICH slot. The
-- collector reconstructs the ordered [plugin, slot] sequence as
-- "<plugin>.<slot>" strings.

-- The ordered run-record. Each subscriber callback appends ONE entry the
-- instant its token fires, so the list order == the actual run order of the
-- slots across both phases.
local sequence = {}

local function record(plugin, payload)
    local slot = type(payload) == "table" and payload.slot or "?"
    sequence[#sequence + 1] = plugin .. "." .. slot
end

-- (1) Collector subscriptions — registered FIRST, before any publish below.
-- A subscribes to BOTH plugins' phase_token events by their stamped names.
kcdx.on("ts_comp_11_both_phase_order_a:phase_token", function(payload)
    record("a", payload)
end)
kcdx.on("ts_comp_11_both_phase_order_b:phase_token", function(payload)
    record("b", payload)
end)

-- (2) The input_loaded assert — registered now, fires on the first update
-- tick, AFTER RunAll (both before slots) + ApplyZone + RunAfterEntrypoints
-- (both after slots) per the sub-3 hooks.cpp ordering. By then `sequence`
-- holds all four tokens in the order their slots ran.
--
-- Expected = [a.before, b.before, a.after, b.after]:
--   a.before before b.before  -> within the BEFORE phase, prio 30 < 70.
--   both befores before any after -> the PHASE BOUNDARY (RunAll before
--                                     RunAfterEntrypoints).
--   a.after before b.after    -> within the AFTER phase, prio 30 < 70.
-- This single ordered comparison proves BOTH the phase boundary AND the
-- per-phase priority interleave; no weaker subset of tokens can false-pass it.
local expected = { "a.before", "b.before", "a.after", "b.after" }

local function seq_eq(got, want)
    if #got ~= #want then return false end
    for i = 1, #want do
        if got[i] ~= want[i] then return false end
    end
    return true
end

kcdx.on("input_loaded", function()
    local got = table.concat(sequence, ", ")
    local want = table.concat(expected, ", ")
    local ok = seq_eq(sequence, expected)
    kcdx.test.report("COMP-11-both-phase-order", ok,
        ok and ("both-phase run-order correct: [" .. got .. "] — every "
                .. "before-slot ran before every after-slot (phase boundary) "
                .. "AND within each phase the lower-priority (30) plugin's "
                .. "slot ran before the higher (70) (priority interleave)")
            or ("run-order [" .. got .. "] != expected [" .. want .. "] — "
                .. "phase boundary or priority interleave violated (or a token "
                .. "was missed)"))
end)

-- (3) The BEFORE slot's own token. Published AFTER the subscriptions above, so
-- the collector catches it (A hears its own event). This is "a.before".
kcdx.publish("phase_token", { slot = "before" })

kcdx.log.info("COMP11A",
    "plugin.lua (before slot): subscribed collector to both phase_token "
    .. "events + registered input_loaded assert, then published a.before "
    .. "(lowest priority 30 -> runs first, subscribes before any token)")
