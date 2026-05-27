-- CAP-10 plugin.lua — the LUA HALF that proves the C++ RegisterFunction
-- round-trip. This is the CAP-10 FALSIFIABLE VERDICT.
--
-- The C++ side (cap-10.cpp) only REGISTERS kcdx.cap10test.stub (a function
-- that reads an int arg and returns arg+42) and reports FAIL only on a real
-- registration failure. It deliberately does NOT report PASS — a successful
-- RegisterFunction whose function is unreachable or wrong from Lua would
-- still be a broken feature. That false-PASS (PASS purely on the bool
-- return of RegisterFunction — a verdict that can't go red) is exactly what
-- this plugin closes: the verdict
-- is the actual Lua-side CALL, not the registration.
--
-- DLL-load-before-own-plugin.lua: the DLL load wave runs before RunAll
-- executes this plugin.lua, so kcdx.cap10test.stub (registered by
-- cap-10.dll in kcdxPlugin_Load) is reachable here. The call is synchronous
-- and same-stack — by the time stub(100) returns, we have the result.
--
-- Shape follows cap-26-lua-command/plugin.lua: register (here: the DLL did)
-- then fire-and-assert at input_loaded, all boot-only (no player gesture).

kcdx.on("input_loaded", function()
    -- Guard: the function must be present. Absent => cap-10.dll missing or
    -- RegisterFunction failed (cap-10.cpp already reported that real FAIL,
    -- but we name it here so the row is never silently skipped).
    if not kcdx.cap10test or not kcdx.cap10test.stub then
        kcdx.test.report("CAP-10", false,
            "kcdx.cap10test.stub not registered — cap-10.dll missing or "
            .. "RegisterFunction failed")
        return
    end

    -- THE VERDICT: call the C++-registered function and assert the
    -- arg+return round-trip. 100 -> 142 (arg+42). A wrong/nil value means
    -- the function is unreachable, in the wrong namespace, or the
    -- arg/return marshaling dropped a value.
    local r = kcdx.cap10test.stub(100)

    if r == 142 then
        kcdx.test.report("CAP-10", true,
            "kcdx.cap10test.stub(100) returned 142 (arg+42) — the "
            .. "C++-registered function is callable from Lua and the "
            .. "arg+return round-trip correctly.")
    else
        kcdx.test.report("CAP-10", false,
            "kcdx.cap10test.stub(100) returned " .. tostring(r)
            .. " (expected 142) — the C++-registered function is not "
            .. "round-tripping: registration no-op'd, wrong namespace, or "
            .. "the arg/return marshaling dropped a value.")
    end
end)

kcdx.log.info("CAP10",
    "will call kcdx.cap10test.stub(100) at input_loaded and assert == 142 "
    .. "(the C++ RegisterFunction round-trip verdict)")
