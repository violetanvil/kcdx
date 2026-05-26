-- CAP-03 plugin.lua — Phase 4b Batch 1: CAP-03 migrated from
-- [[hook]] + pak-Lua-callback to kcdx.hook{before}; same site, same
-- observable (callback fires), pure-Lua now.
--
-- The target is an un-named direct callee of CGame::Update
-- (0x180865FB4 in KCD2 1.5.1164953) — no Address Library name, so the
-- locator stays the SAME pattern= AOB the old [[hook]] block used, with
-- signature="void (ptr)" (the void __fastcall(this_ptr*) ABI: return_type
-- "void", param_types ["ptr"]). This is the labeled expert hatch for a
-- target the library can't name yet (cornerstones.md / AP12-OK), NOT a gap.
--
-- The before callback bumps a fire counter — it does NOT dereference the
-- ptr (object layout unknown; the test only confirms the dispatch chain
-- fired). CAP-03 PASS asserts the SAME observable the old pak callback did:
-- the hook INSTALLED (h:applied()==true) AND FIRED at least once
-- (fire_count > 0).
--
-- REPORT TIMING: the old pak test reported from a CryEngine EventSystem
-- listener on OnSystemStarted/OnGameplayStarted — well after the engine had
-- ticked many times. The kcdx-native equivalent is "input_loaded" (the
-- latest boot-time lifecycle event, "fires every boot" — the standard
-- auto-pass trigger), NOT "ready". "ready" fires ~immediately after the
-- apply pass — the CGame::Update callee has barely run by then (the first
-- migration attempt reported at "ready" and saw fire_count=0 despite
-- applied()==true). input_loaded gives the per-frame callee maximal time to
-- fire before the assertion.

local PATTERN =
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 80 B9 C1 05 00 00 00 48 8B D9"

local fire_count = 0

local h = kcdx.hook{
    name      = "cap03_update_callee",
    pattern   = PATTERN,            -- un-named site: expert AOB hatch (AP12-OK)
    signature = "void (ptr)",       -- void __fastcall(this_ptr*); ptr not deref'd
    before    = function(this_ptr)  -- single `this` arg; we only count fires
        fire_count = fire_count + 1
    end,
}

if h == nil then
    kcdx.test.report("CAP-03", false,
        "kcdx.hook returned nil — registration failed")
    return
end

-- Report at input_loaded (the latest boot-time lifecycle event) so the
-- per-frame CGame::Update callee has had maximal time to fire. :applied()
-- is final by here (the apply pass ran long ago); fire_count reflects whether
-- the dispatch chain worked end-to-end. Guard against input_loaded firing
-- more than once (it can re-fire) — report only the first time.
local reported = false
kcdx.on("input_loaded", function()
    if reported then return end
    reported = true
    local applied = h:applied()
    local pass = (applied == true) and (fire_count > 0)
    kcdx.test.report("CAP-03", pass,
        pass and ("update-callee hook installed (applied()==true) and fired "
                  .. fire_count .. " time(s) by input_loaded")
             or  ("expected applied()==true and fire_count>0; got applied="
                  .. tostring(applied) .. " fire_count=" .. fire_count
                  .. " reason=" .. tostring(h:reason())))
end)
