-- CAP-104 plugin.lua — the six shipped console-driven behavior-catalog entries.
--
-- Each shipped catalog behavior (kcdx.behavior.<bare>) toggles a known game
-- CVar via kcdx.console.execute. This plugin exercises all six end-to-end:
-- SET the behavior (a load-time act, recorded now, applied at the boundary),
-- then assert post-boundary that the backing CVar actually changed to the
-- toggled value — proving the entry's kcdx.console.execute toggle ran and the
-- engine applied it.
--
-- TIMING (three windows in the first-update-tick boot block, in order):
--   1. plugin.lua load (top level): the behavior SETS run here — a set is a
--      load-time act, recorded into the pending table. The CVar read surface is
--      NOT armed yet, so `before` CANNOT be captured here.
--   2. "ready" (the first ApplyZone pass): fires AFTER cvar::Init armed the CVar
--      read surface and BEFORE the apply boundary fired the toggles. This is the
--      ONLY window to capture each CVar's pre-toggle `before` value
--      (kcdx.cvar.get_int) — the verified read surface, the same nil-on-miss
--      contract the existence probe used to prove all six CVars are readable.
--   3. "input_loaded" (post-boundary): the apply boundary already ran the
--      console.execute toggles. Assert each CVar reflects the toggled value,
--      then RESTORE the original `before` on EVERY path.
--
-- SAVE AND RESTORE (a hard requirement): for each entry the fixture captures
-- `before` at "ready", sets the behavior to a value that changes the CVar from
-- `before`, asserts the change at "input_loaded", and RESTORES `before` via
-- kcdx.console.execute — wrapped so a failed assert STILL restores. A
-- regression test that permanently flips the developer's CVars is a
-- side-effecting defect; this test leaves the game exactly as found.
--
-- This plugin's engine-derived stamp prefix: ts.cap_104_catalog_entries.
-- (Only relevant in that this plugin SETS the engine-catalog behaviors by their
-- reserved kcdx.behavior.<bare> full names — a catalog name is settable from
-- any stop, the §6 window law — never this plugin's own prefix.)

-- The six entries under test: the catalog behavior's reserved full name, its
-- backing CVar, and the row id. Each is a BOOLEAN behavior whose implementation
-- runs `kcdx.console.execute("<cvar> " .. (value and "<on>" or "0"))`.
--   on_value: the CVar value the behavior writes when set true (1, or 2 for DoF).
-- The toggled (set) value is chosen per entry at "ready" from the captured
-- `before`, so the CVar is guaranteed to CHANGE: if before == 0 we set true
-- (CVar -> on_value, non-zero); else we set false (CVar -> 0). Either way the
-- expected post-toggle CVar value differs from `before`.
local ENTRIES = {
    { row = "cap-104-motion_blur",
      behavior = "kcdx.behavior.motion_blur",          cvar = "r_MotionBlur",          on_value = 1 },
    { row = "cap-104-depth_of_field",
      behavior = "kcdx.behavior.depth_of_field",       cvar = "r_DepthOfField",        on_value = 2 },
    { row = "cap-104-display_info",
      behavior = "kcdx.behavior.display_info",          cvar = "r_DisplayInfo",         on_value = 1 },
    { row = "cap-104-chromatic_aberration",
      behavior = "kcdx.behavior.chromatic_aberration",  cvar = "r_ChromaticAberration", on_value = 1 },
    { row = "cap-104-show_compass",
      behavior = "kcdx.behavior.show_compass",          cvar = "wh_ui_showCompass",     on_value = 1 },
    { row = "cap-104-skip_intro_logos",
      behavior = "kcdx.behavior.skip_intro_logos",      cvar = "g_skipIntro",           on_value = 1 },
}

-- Per-entry runtime state filled across the two post-load windows.
--   before        : the CVar value captured at "ready" (pre-toggle). nil = the
--                   read MISSED (CVar not readable) — an honest miss, never a
--                   fabricated 0; a missing `before` FAILs the row and means
--                   "do not attempt a restore to a value we never had".
--   set_value     : the boolean the behavior was set to (chosen from `before`).
--   expected_cvar : the CVar value the toggle should produce (differs from before).
--   set_ok/set_err: the pcall result of the load-time set.
local state = {}  -- keyed by row id

-- Row results captured across windows, reported at input_loaded (post-boundary,
-- where every per-entry assert + the restore-clean check resolves).
local results = {}
local function rec(row, pass, reason)
    results[#results + 1] = { row = row, pass = pass, reason = reason }
end

-- The domain must be a TABLE before any member is read (mirrors cap-100/cap-103).
local domain_ok = type(kcdx.behavior) == "table"
local verbs_ok = domain_ok
    and type(kcdx.behavior.set) == "function"
    and type(kcdx.console) == "table"
    and type(kcdx.console.execute) == "function"
    and type(kcdx.cvar) == "table"
    and type(kcdx.cvar.get_int) == "function"

local ALL_ROWS = {
    "cap-104-motion_blur", "cap-104-depth_of_field", "cap-104-display_info",
    "cap-104-chromatic_aberration", "cap-104-show_compass",
    "cap-104-skip_intro_logos", "cap-104-restore-clean",
}

if not verbs_ok then
    -- A required surface (behavior.set / console.execute / cvar.get_int) did
    -- not register — every row is a real FAIL, never a silent skip.
    local detail = "behavior=" .. type(kcdx.behavior)
        .. ", console=" .. type(kcdx.console)
        .. ", cvar=" .. type(kcdx.cvar)
    for _, row in ipairs(ALL_ROWS) do
        rec(row, false,
            "a required surface did not register (" .. detail
            .. ") — kcdx.behavior.set / kcdx.console.execute / kcdx.cvar.get_int "
            .. "must all be live for the catalog-entry rows to run")
    end
else
    -- =====================================================================
    -- Window 1 — load-time SETS (the load-time act). Each set is recorded;
    -- the apply boundary (later, pre-InputLoaded) fires the implementation.
    -- The set VALUE is chosen at "ready" from `before`, so the set itself is
    -- deferred to the ready handler (below) — a set is legal at the main stop
    -- and at "ready" alike (both precede the boundary; "ready" fires at the
    -- first ApplyZone pass, before RunApplyBoundary). Doing the set at "ready"
    -- lets each set's value depend on the freshly-read `before`.
    -- =====================================================================
    for _, e in ipairs(ENTRIES) do
        state[e.row] = { before = nil, set_value = nil, expected_cvar = nil,
                         set_ok = nil, set_err = nil }
    end
end

-- =====================================================================
-- "ready" — the CVar read surface is armed (cvar::Init ran) and the apply
-- boundary has NOT yet fired (RunApplyBoundary is later). Capture each
-- pre-toggle `before`, choose the set value so the CVar will CHANGE, and
-- issue the load-window set. (A set at "ready" is still a pre-boundary record;
-- the boundary applies it once, like a top-level set.)
-- =====================================================================
kcdx.on("ready", function()
    if not verbs_ok then return end  -- already recorded as FAILs

    for _, e in ipairs(ENTRIES) do
        local st = state[e.row]
        -- Capture the pre-toggle CVar value. nil = an honest miss (the read
        -- surface or the CVar is unavailable) — never coalesced to a fake 0,
        -- so the row FAILs truthfully and no bogus restore is attempted.
        st.before = kcdx.cvar.get_int(e.cvar)
        if st.before ~= nil then
            -- Choose the set value so the toggle MOVES the CVar off `before`:
            -- before == 0 -> set true (CVar -> on_value, non-zero);
            -- before ~= 0 -> set false (CVar -> 0).
            st.set_value     = (st.before == 0)
            st.expected_cvar = st.set_value and e.on_value or 0
            st.set_ok, st.set_err =
                pcall(kcdx.behavior.set, e.behavior, st.set_value)
        end
    end
end)

-- =====================================================================
-- "input_loaded" — post-boundary. The apply boundary already ran each set
-- behavior's implementation (the kcdx.console.execute toggle). For each entry:
-- assert the CVar now reads the toggled value (pcall-guarded so a raise still
-- reaches the restore), then RESTORE the captured `before` on EVERY path. A
-- final restore-clean row re-reads every CVar to confirm the original value
-- is back.
-- =====================================================================
kcdx.on("input_loaded", function()
    if not verbs_ok then
        for _, r in ipairs(results) do
            kcdx.test.report(r.row, r.pass, r.reason)
        end
        return
    end

    for _, e in ipairs(ENTRIES) do
        local st = state[e.row]

        -- The assert is wrapped so that NO error path skips the restore. The
        -- inner function records the row's verdict; whatever it does, the
        -- restore below runs unconditionally afterward.
        local function assert_entry()
            if st.before == nil then
                rec(e.row, false, "the pre-toggle read of '" .. e.cvar
                    .. "' at ready MISSED (kcdx.cvar.get_int returned nil) — "
                    .. "the CVar was not readable, so the entry's effect "
                    .. "could not be verified (no fabricated baseline)")
                return
            end
            if not st.set_ok then
                rec(e.row, false, "set('" .. e.behavior .. "', "
                    .. tostring(st.set_value) .. ") RAISED: "
                    .. tostring(st.set_err) .. " — a catalog behavior must be "
                    .. "settable from any stop (the §6 window law)")
                return
            end
            local now, errNow = kcdx.cvar.get_int(e.cvar)
            if now == nil then
                rec(e.row, false, "post-boundary read of '" .. e.cvar
                    .. "' MISSED (returned nil" .. (errNow ~= nil
                        and (", err=" .. tostring(errNow)) or "")
                    .. ") — cannot confirm the toggle applied")
                return
            end
            if now ~= st.expected_cvar then
                rec(e.row, false, "after set('" .. e.behavior .. "', "
                    .. tostring(st.set_value) .. "), '" .. e.cvar
                    .. "' reads " .. tostring(now) .. " (expected "
                    .. tostring(st.expected_cvar) .. ", changed from the "
                    .. "captured " .. tostring(st.before) .. ") — the entry's "
                    .. "kcdx.console.execute toggle did NOT apply at the "
                    .. "boundary")
                return
            end
            rec(e.row, true, "set('" .. e.behavior .. "', "
                .. tostring(st.set_value) .. ") drove '" .. e.cvar .. "' from "
                .. tostring(st.before) .. " to " .. tostring(now)
                .. " (the toggled value) at the apply boundary — the entry's "
                .. "kcdx.console.execute effect applied")
        end

        -- Run the assert; capture any raise so the restore ALWAYS runs.
        local assert_ran, assert_err = pcall(assert_entry)
        if not assert_ran then
            rec(e.row, false, "the assertion for '" .. e.row .. "' RAISED ("
                .. tostring(assert_err) .. ") — recorded FAIL; the CVar is "
                .. "still restored below")
        end

        -- RESTORE — unconditional, on every path. Only attempt it when we
        -- actually captured a baseline (st.before ~= nil); restoring to a
        -- value we never had would itself flip the CVar.
        if st.before ~= nil then
            st.restore_ok =
                pcall(kcdx.console.execute, e.cvar .. " " .. tostring(st.before))
        end
    end

    -- =====================================================================
    -- cap-104-restore-clean — after every per-entry assert + restore, each
    -- backing CVar reads its ORIGINAL captured `before` again. This proves the
    -- restore ran on every entry and the dev's game is left exactly as found.
    -- FALSIFIABLE: any CVar whose baseline was captured does NOT read its
    -- original value post-restore -> FAIL (a permanent CVar flip — a
    -- side-effecting defect). An entry whose baseline MISSED (never captured)
    -- is reported as such, not silently passed.
    -- =====================================================================
    do
        local row = "cap-104-restore-clean"
        local bad = nil
        local missed = {}
        for _, e in ipairs(ENTRIES) do
            local st = state[e.row]
            if st.before == nil then
                missed[#missed + 1] = e.cvar
            else
                local back = kcdx.cvar.get_int(e.cvar)
                if back ~= st.before then
                    bad = "'" .. e.cvar .. "' reads " .. tostring(back)
                        .. " after restore (expected the original "
                        .. tostring(st.before) .. ") — the restore did NOT "
                        .. "return it; the dev's game was left modified"
                    break
                end
            end
        end
        if bad then
            rec(row, false, bad)
        elseif #missed == #ENTRIES then
            rec(row, false, "NO backing CVar baseline was captured ("
                .. table.concat(missed, ", ") .. " all MISSED) — the read "
                .. "surface never came up, so restore-clean cannot be proven")
        elseif #missed > 0 then
            rec(row, false, "some backing CVar baselines MISSED ("
                .. table.concat(missed, ", ") .. ") — restore could not be "
                .. "verified for them; the captured CVars did restore, but "
                .. "the clean-state claim is not fully provable")
        else
            rec(row, true, "every backing CVar (" .. #ENTRIES
                .. " entries) reads its original captured value again after "
                .. "the per-entry restores — the test left the dev's game "
                .. "exactly as found (no permanent CVar flip)")
        end
    end

    for _, r in ipairs(results) do
        kcdx.test.report(r.row, r.pass, r.reason)
    end
    kcdx.log.info("CAP104",
        "shipped catalog-entry self-test reported " .. #results
        .. " rows: six console-driven catalog behaviors each SET so their "
        .. "backing CVar changed, the change asserted post-boundary (the "
        .. "kcdx.console.execute toggle applied), then the original CVar value "
        .. "RESTORED on every path; the restore-clean row confirms the dev's "
        .. "game was left exactly as found.")
end)
