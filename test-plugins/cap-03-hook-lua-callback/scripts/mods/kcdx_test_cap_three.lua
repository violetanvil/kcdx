-- CAP-03 — pak Lua side.
--
-- Registers Cap03Test.OnUpdateCallee, which kcdx's [[hook]] +
-- lua_callback chain calls back every tick that the engine's
-- CGame::Update runs (the hook target is a direct callee of update).
-- We bump a counter on each fire.
--
-- After OnSystemStarted (engine has ticked many times by then), the
-- EventSystemListener inspects the counter and calls kcdx.test.report
-- based on whether fire_count > 0.

Cap03Test = {}
Cap03Test.fire_count = 0
Cap03Test.reported = false

-- The hook callback. kcdx's dispatcher calls this with
-- (this_ptr) — the single `this` arg passed in rcx to the hooked
-- engine method (a __fastcall void method on the CGame::Update path).
-- We don't dereference the pointer — object layout isn't known and
-- the test only needs to verify the dispatch chain fired at all.
function Cap03Test.OnUpdateCallee(this_ptr)
    Cap03Test.fire_count = Cap03Test.fire_count + 1
end

-- After OnSystemStarted, the engine has been ticking for a while and
-- our per-frame hook has had hundreds of opportunities to fire.
-- Inspect the counter and report.
function Cap03Test:checkAndReport()
    if self.reported then return end
    if not kcdx or not kcdx.dev or not kcdx.dev.is_enabled() then return end
    self.reported = true

    if Cap03Test.fire_count > 0 then
        kcdx.test.report("CAP-03", true,
            "update-callee hook fired " .. Cap03Test.fire_count
            .. " time(s) during boot")
    else
        kcdx.test.report("CAP-03", false,
            "update-callee hook never fired - lua_callback chain broken?")
    end
end

function Cap03Test:EventSystemListener(actionName, eventName, argTable)
    if actionName == "System"
        and (eventName == "OnSystemStarted"
          or eventName == "OnLoadingComplete"
          or eventName == "OnGameplayStarted") then
        self:checkAndReport()
    end
end

if System and System.LogAlways then
    System.LogAlways("[KCDX_TEST_CAP_THREE] script-load: "
        .. "Cap03Test.OnUpdateCallee registered")
end
if UIAction and UIAction.RegisterEventSystemListener then
    UIAction.RegisterEventSystemListener(
        Cap03Test, "", "", "EventSystemListener")
end
