-- CAP-05 + CAP-11 test pak.
--
-- Pak Lua test plugin. Exercises:
--   * kcdx.dev.is_enabled()           (silent-skip when off)
--   * kcdx.lua.cfunction_address(fn)  -> kcdx.memory.pointer userdata (CAP-11)
--   * kcdx.memory.dynamic_hook(...)   -> installs detour from pak Lua (CAP-05)
--   * kcdx.test.report(name, pass, reason) -> records pass/fail
--
-- Hooks kcdx.cap05.probe (registered by cap-05's OWN companion DLL,
-- cap-05.dll), triggers a call to it from pak Lua, verifies the
-- pre_callback fired. The fixture is self-owned: it no longer depends on
-- the archived hello-plugin sample (test-suite.md — a test owns its
-- fixtures).

KcdxTestPaklua = {}
KcdxTestPaklua.installed = false

-- Run the actual test logic. Called from the event listener when
-- OnSystemStarted (or later) fires. Guarded for single-run.
function KcdxTestPaklua:runTests(label)
    if self.installed then return end

    -- Wait for kcdx global to be live. It's populated on first-update-tick
    -- which runs AFTER OnSystemStarted. Don't mark `installed` here —
    -- if kcdx isn't ready yet, retry on the next event (OnLoadingComplete,
    -- OnGameplayStarted, etc).
    if not kcdx or not kcdx.dev or not kcdx.dev.is_enabled
       or not kcdx.dev.is_enabled() then
        return
    end
    -- Same check for the subsystems we'll touch.
    if not kcdx.test or not kcdx.test.report
       or not kcdx.lua or not kcdx.lua.cfunction_address
       or not kcdx.memory or not kcdx.memory.dynamic_hook then
        return
    end

    -- All preconditions met; lock out further runs and proceed.
    self.installed = true

    -- Need cap-05's OWN cfunction (registered by cap-05.dll). If it's
    -- absent, the companion DLL didn't load/register — a REAL failure to
    -- report (not an external-sample dependency).
    if not kcdx.cap05 or not kcdx.cap05.probe then
        kcdx.test.report("CAP-11", false,
            "kcdx.cap05.probe not registered (cap-05.dll missing or "
            .. "RegisterFunction failed?)")
        kcdx.test.report("CAP-05", false,
            "no cfunction to address (cap-05.dll missing?)")
        return
    end

    -- CAP-11: cfunction_address returns a pointer userdata
    local p = kcdx.lua.cfunction_address(kcdx.cap05.probe)
    if type(p) ~= "userdata" then
        kcdx.test.report("CAP-11", false,
            "cfunction_address returned " .. type(p) ..
            ", expected userdata")
        kcdx.test.report("CAP-05", false,
            "skipped: CAP-11 prerequisite failed")
        return
    end
    kcdx.test.report("CAP-11", true,
        "cfunction_address(kcdx.cap05.probe) returned pointer userdata "
        .. "(triggered at " .. tostring(label) .. ")")

    -- CAP-05: install a hook from pak Lua targeting the address
    -- the userdata wraps.
    local fired = false
    local h, herr = kcdx.memory.dynamic_hook({
        name         = "kcdx_test_paklua_hook",
        target       = p,
        return_type  = "void",
        param_types  = {"ptr"},  -- lua_State* in rcx
        pre_callback = function(retval, L_ptr)
            fired = true
            return true  -- let original run
        end,
    })
    if not h then
        kcdx.test.report("CAP-05", false,
            "dynamic_hook failed: " .. tostring(herr))
        return
    end
    self.hook_handle = h  -- keep alive

    -- Trigger kcdx.cap05.probe ourselves. Should fire the pre_callback.
    kcdx.cap05.probe("cap-05-trigger")

    if fired then
        kcdx.test.report("CAP-05", true,
            "pak Lua installed dynamic_hook + pre_callback fired on trigger")
    else
        kcdx.test.report("CAP-05", false,
            "hook installed but pre_callback did not fire on trigger")
    end
end

-- Broad event-system listener. Matches the verify-pak pattern that's
-- known to fire reliably. CryEngine calls this method on EVERY
-- engine event (no filters); we dispatch inside.
function KcdxTestPaklua:EventSystemListener(actionName, eventName, argTable)
    if actionName == "System"
        and (eventName == "OnSystemStarted"
          or eventName == "OnLoadingComplete"
          or eventName == "OnGameplayStarted") then
        -- pcall protects against any error escaping the listener
        -- (CryEngine swallows errors silently otherwise).
        pcall(function() self:runTests(eventName) end)
    end
end

if UIAction and UIAction.RegisterEventSystemListener then
    UIAction.RegisterEventSystemListener(
        KcdxTestPaklua, "", "", "EventSystemListener")
end
