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
-- fired). The site fires every tick AFTER kcdx's hook installs, so by the
-- "ready" event the counter is >0. CAP-03 PASS asserts the SAME observable
-- the old pak callback did: the hook INSTALLED (h:applied()==true) AND
-- FIRED at least once (fire_count > 0).

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

-- :applied()/:reason() are only final after the apply pass, which runs
-- after this plugin.lua returns. By "ready" the per-frame site has had
-- many ticks to fire, so the counter reflects whether the dispatch chain
-- worked end-to-end.
kcdx.on("ready", function()
    local applied = h:applied()
    local pass = (applied == true) and (fire_count > 0)
    kcdx.test.report("CAP-03", pass,
        pass and ("update-callee hook installed (applied()==true) and fired "
                  .. fire_count .. " time(s) during boot")
             or  ("expected applied()==true and fire_count>0; got applied="
                  .. tostring(applied) .. " fire_count=" .. fire_count
                  .. " reason=" .. tostring(h:reason())))
end)
