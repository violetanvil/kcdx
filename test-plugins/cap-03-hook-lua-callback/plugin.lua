-- CAP-03 plugin.lua — Phase 4b Batch 1: CAP-03 migrated from
-- [[hook]] + pak-Lua-callback to kcdx.hook{before}; same site, same
-- observable (the hook callback fires), pure-Lua now.
--
-- The target is an un-named direct callee of CGame::Update
-- (0x180865FB4 in KCD2 1.5.1164953) — no Address Library name, so the
-- locator stays the SAME pattern= AOB the old [[hook]] block used, with
-- signature="void (ptr)" (the void __fastcall(this_ptr*) ABI: return_type
-- "void", param_types ["ptr"]). This is the labeled expert hatch for a
-- target the library can't name yet (cornerstones.md / AP12-OK), NOT a gap.
--
-- The before callback does NOT dereference the ptr (object layout unknown;
-- the test only confirms the dispatch chain fired). CAP-03 PASS asserts the
-- SAME observable the old pak callback did: the hook FIRED at least once.
--
-- REPORT TIMING — self-report on first fire, NOT poll at a fixed lifecycle
-- event. A probe (PROBE B/B.2, 2026-05-25) established the ground truth: the
-- hook installs and the detour fires correctly + repeatedly on the main
-- thread — but the FIRST fire lands AFTER kcdx.on("input_loaded") (and well
-- after "ready"). The target is the menu/UI pump; the game only starts
-- calling it once the menu is actually rendering, which is a frame or more
-- past input_loaded. So ANY fixed lifecycle sampling point can precede the
-- first fire and read fire_count=0 — that was the bug in the first two
-- migration attempts (reported at ready, then input_loaded, both too early).
-- The correct observable is event-driven: the moment the hook fires, report
-- PASS. The hook firing IS the thing under test; we report when it happens.

local PATTERN =
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 80 B9 C1 05 00 00 00 48 8B D9"

local reported = false

local function report_fired(fire_count)
    if reported then return end
    reported = true
    kcdx.test.report("CAP-03", true,
        "update-callee hook fired (count=" .. fire_count .. ") — the before "
        .. "callback ran on the hooked CGame::Update callee. Phase 4b Batch 1 "
        .. "migration off legacy [[hook]]+pak; same site, same observable, "
        .. "kcdx.hook{before} mechanism.")
end

local fire_count = 0

local h = kcdx.hook{
    name      = "cap03_update_callee",
    pattern   = PATTERN,            -- un-named site: expert AOB hatch (AP12-OK)
    signature = "void (ptr)",       -- void __fastcall(this_ptr*); ptr not deref'd
    before    = function(this_ptr)  -- single `this` arg; we only count fires
        fire_count = fire_count + 1
        report_fired(fire_count)    -- self-report PASS on the first fire
    end,
}

if h == nil then
    kcdx.test.report("CAP-03", false,
        "kcdx.hook returned nil — registration failed")
    return
end

-- Backstop: if by input_loaded the hook never INSTALLED (apply-pass
-- rejection), report FAIL with the reason — that is a real failure (the hook
-- couldn't be placed). We do NOT FAIL here on fire_count==0: the first fire
-- legitimately lands after input_loaded (see the timing note above), and the
-- before callback self-reports PASS whenever it fires. If applied()==true but
-- it simply hasn't fired yet, the row stays PENDING until it does (the honest
-- "installed, awaiting first fire" state) — it flips to PASS the moment the
-- menu pump runs.
kcdx.on("input_loaded", function()
    if reported then return end
    if h:applied() ~= true then
        reported = true
        kcdx.test.report("CAP-03", false,
            "hook did not install: applied()=" .. tostring(h:applied())
            .. " reason=" .. tostring(h:reason()))
    end
    -- applied()==true but not yet fired: leave PENDING; the before callback
    -- reports PASS on its first fire (which the probe confirmed happens
    -- shortly after input_loaded as the menu renders).
end)
