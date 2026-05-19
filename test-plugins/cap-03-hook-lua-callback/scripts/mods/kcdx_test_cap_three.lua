-- CAP-03 — pak Lua side.
--
-- Registers Cap03Test.OnLuaLoadFile, which kcdx's [[hook]] +
-- lua_callback chain calls back when luaL_loadfile fires. We bump
-- a counter on each fire.
--
-- After OnSystemStarted (boot's luaL_loadfile fires have happened by
-- then), the EventSystemListener inspects the counter and calls
-- kcdx.test.report(CAP-03, ...) based on whether fire_count > 0.

Cap03Test = {}
Cap03Test.fire_count = 0
Cap03Test.reported = false

-- The hook callback. kcdx's dispatcher calls this with
-- (retval_unused, L_pointer, filename_pointer) — the param shapes
-- declared in the [[hook]] entry's param_types.
function Cap03Test.OnLuaLoadFile(retval, L_ptr, filename_ptr)
    Cap03Test.fire_count = Cap03Test.fire_count + 1
    return true  -- let original luaL_loadfile run
end

-- After OnSystemStarted, the engine has finished loading its Scripts/
-- dir and our hook has had plenty of opportunity to fire. Inspect the
-- counter and report.
function Cap03Test:checkAndReport()
    if self.reported then return end
    if not kcdx or not kcdx.dev or not kcdx.dev.is_enabled() then return end
    self.reported = true

    if Cap03Test.fire_count > 0 then
        kcdx.test.report("CAP-03", true,
            "luaL_loadfile hook fired " .. Cap03Test.fire_count
            .. " time(s) during boot")
    else
        kcdx.test.report("CAP-03", false,
            "luaL_loadfile hook never fired - lua_callback chain broken?")
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
        .. "Cap03Test.OnLuaLoadFile registered")
end
if UIAction and UIAction.RegisterEventSystemListener then
    UIAction.RegisterEventSystemListener(
        Cap03Test, "", "", "EventSystemListener")
end
