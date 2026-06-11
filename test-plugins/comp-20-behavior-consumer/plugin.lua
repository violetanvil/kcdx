-- COMP-20 consumer plugin.lua — the behavior-only consumer (§9 consumer
-- leg). Loads AFTER the declarer (priority 60 vs 30); its whole surface is
-- kcdx.behavior.set/get + the suite report — deliberately NO kcdx.declare,
-- no kcdx.dll.declare, no hash-checked verb, and no game-version
-- declaration of any kind (see kcdx.toml). The declarer-side assertions
-- (impl fired once with these values, etc.) are the declarer's rows; THIS
-- row proves the consumer story: a plain plugin fully loads and drives
-- behaviors with nothing but set + full names.

local D = "ts.comp_20_behavior_declarer."

-- All sets at load (the load window), full stamped names, pcall-captured so
-- a raise is an observation, never a load abort.
local sets = {
    { name = D .. "boundary_first",  value = true },
    { name = D .. "hardcore_combat", value = "hardcore-on" },
    { name = D .. "conflict_target", value = "consumer-wins" },
    { name = D .. "raise_behavior",  value = "consumer-set-this" },
    { name = D .. "drain_setter",    value = true },
    { name = D .. "drain_target",    value = 10 },
}
for _, s in ipairs(sets) do
    s.ok, s.err = pcall(kcdx.behavior.set, s.name, s.value)
end

-- comp-20-behavior-only-consumer — reports at input_loaded (post-boundary):
-- every load-time set RECORDED (no raise — the declarer loaded first, so
-- every full name resolved), and this consumer's own US-1/US-2 set is the
-- value the boundary applied (get reads "hardcore-on" post-boundary). The
-- "no version declaration" half is structural: this plugin's script and
-- manifest carry none, yet it fully loaded and its sets applied.
-- FALSIFIABLE: any load-time set raised, or post-boundary
-- get(hardcore_combat) reads anything but "hardcore-on" -> FAIL.
kcdx.on("input_loaded", function()
    local row = "comp-20-behavior-only-consumer"
    for _, s in ipairs(sets) do
        if not s.ok then
            kcdx.test.report(row, false, "the load-time set('" .. s.name
                .. "') RAISED: " .. tostring(s.err)
                .. " — a behavior-only consumer setting a loaded "
                .. "declarer's full name must record")
            return
        end
    end
    local okGet, v = pcall(kcdx.behavior.get, D .. "hardcore_combat")
    if not okGet then
        kcdx.test.report(row, false, "post-boundary get('" .. D
            .. "hardcore_combat') RAISED: " .. tostring(v))
    elseif v ~= "hardcore-on" then
        kcdx.test.report(row, false, "post-boundary get('" .. D
            .. "hardcore_combat') reads " .. tostring(v)
            .. " (expected this consumer's applied 'hardcore-on')")
    else
        kcdx.test.report(row, true, "a behavior-only consumer (no "
            .. "kcdx.declare, no hash-checked verb, no game-version "
            .. "declaration of any kind) fully loaded, recorded six sets "
            .. "against the declarer's full stamped names at load, and its "
            .. "US-1/US-2 set applied at the boundary (get reads "
            .. "'hardcore-on' post-boundary) — the §9 consumer leg")
    end
    kcdx.log.info("COMP20",
        "behavior-only consumer reported its row (six load-time sets + the "
        .. "post-boundary get round-trip)")
end)
