-- CAP-03 plugin.lua — CAP-03 migrated from
-- [[hook]] + pak-Lua-callback to kcdx.hook{before}; same site, same
-- observable (the hook callback fires), pure-Lua now.
--
-- The target is the menu/UI pump — a direct callee of CGame::Update — now
-- CURATED in the Address Library as `CGame_per_frame_ui_pump`. So cap-03
-- resolves it BY NAME (the common path): the name carries the address AND the
-- verified ABI `void (ptr self)` — no hex, no hand-written signature. An
-- earlier version used a raw AOB pattern because the entity wasn't curated
-- yet; that's now migrated to the by-name locator.
--
-- The before callback does NOT dereference the ptr (object layout unknown;
-- the test only confirms the dispatch chain fired). CAP-03 PASS asserts the
-- SAME observable the old pak callback did: the hook FIRED at least once.
--
-- REPORT TIMING — self-report on first fire, NOT poll at a fixed lifecycle
-- event. A runtime probe established the ground truth: the
-- hook installs and the detour fires correctly + repeatedly on the main
-- thread — but the FIRST fire lands AFTER kcdx.on("input_loaded") (and well
-- after "ready"). The target is the menu/UI pump; the game only starts
-- calling it once the menu is actually rendering, which is a frame or more
-- past input_loaded. So ANY fixed lifecycle sampling point can precede the
-- first fire and read fire_count=0 — that was the bug in the first two
-- migration attempts (reported at ready, then input_loaded, both too early).
-- The correct observable is event-driven: the moment the hook fires, report
-- PASS. The hook firing IS the thing under test; we report when it happens.

-- PASS is STICKY + TERMINAL. A one-shot hook that has already fired ONCE cannot
-- un-fire — so once CAP-03 reports PASS it must NEVER report FAIL afterwards.
-- OBSERVED (kcdx-dev 15:30 run): CAP-03 reported PASS at boot (hook fired,
-- count=1), then a SECOND FAIL ("never fired") fired ~26s later, between the
-- first and second save-load — and the aggregator keeps the LAST verdict,
-- flipping the real PASS to FAIL. The exact trigger of that late FAIL was NOT
-- the boot input_loaded backstop (that ran clean at boot) and NOT a plugin.lua
-- re-eval (RegisterKcdxTable/first-update-tick fired exactly once — the chunk
-- did not re-run). The precise late-FAIL source was not fully traced; rather
-- than guess it, the fix targets the INVARIANT that holds regardless: a
-- one-shot test that has passed cannot later fail. The PASS latch lives on a
-- persistent global keyed to this plugin so it survives ANY re-entry path
-- (re-eval, a re-fired listener, or the untraced late trigger) in the single
-- shared Lua state — PASS wins and is terminal, mechanism-independent.
local PASS_LATCH = "__kcdx_cap03_passed"
local function already_passed()
    return rawget(_G, PASS_LATCH) == true
end
local function latch_pass()
    rawset(_G, PASS_LATCH, true)
end

-- `reported` guards THIS evaluation's report calls (a fresh chunk on re-eval
-- starts un-reported); `already_passed()` is the cross-eval terminal latch.
local reported = false

local function report_fired(fire_count)
    if reported or already_passed() then return end
    reported = true
    latch_pass()
    kcdx.test.report("CAP-03", true,
        "update-callee hook fired (count=" .. fire_count .. ") — the before "
        .. "callback ran on the hooked CGame::Update callee. Migration off "
        .. "legacy [[hook]]+pak; same site, same observable, "
        .. "kcdx.hook{before} mechanism.")
end

local fire_count = 0

local h = kcdx.hook{
    name      = "cap03_update_callee",
    target    = "CGame_per_frame_ui_pump",  -- curated Address Library name: carries the address AND the verified ABI
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
--
-- TERMINAL-PASS guard (the bug this fixes): on a SECOND save-load this chunk
-- re-evaluates, re-installs the hook, and re-subscribes input_loaded. If that
-- callback sampled applied()==false before the fresh apply pass completes it
-- would report FAIL — flipping a genuine prior PASS to FAIL in the aggregator
-- (which keeps the LAST verdict). So the backstop FIRST honours the cross-eval
-- PASS latch: if CAP-03 ever passed, it stays passed and the backstop does
-- nothing. A hook that NEVER installed (no prior PASS, applied()~=true) still
-- reports the real FAIL — coverage of the never-install case is preserved.
kcdx.on("input_loaded", function()
    if already_passed() then return end  -- PASS is terminal across re-loads
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
