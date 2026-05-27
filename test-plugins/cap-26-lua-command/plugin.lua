-- CAP-26 plugin.lua — kcdx.command register + kcdx.console.execute fire +
-- args-table (array + .raw) round-trip, ALL boot-only.
--
-- This is the LUA analog of the C++ CAP-13 (cap-13-console-command):
-- CAP-13 drives kcdxConsoleInterface::RegisterCommand + ExecuteString from
-- a DLL; CAP-26 drives kcdx.command + kcdx.console.execute from pure Lua.
-- Together they prove the two authoring surfaces are at parity on the
-- console-command round-trip (the authoring surface is one learnable model
-- in two languages, with mirrored kcdx.* naming and call-shape).
--
-- Flow (deterministic, boot-only — no player gesture):
--   1. At plugin load: kcdx.command{ name="cap26_cmd", callback=... } registers
--      the command from plugin.lua while IConsole is NOT yet up (plugin.lua
--      runs in RunAll, before console::Init) — so the command is DEFERRED-
--      queued (the not-ready arm of Thunk_RegisterCommand) and FLUSHED when
--      console::Init arms the surface; the callback records what it received
--      into plugin-local upvalues.
--   2. On kcdx.on("input_loaded", ...) (first update tick, after the apply
--      pass — the same deterministic boot trigger CAP-13 uses via
--      InputLoaded): kcdx.console.execute("cap26_cmd 42 hello") fires the
--      command synchronously on the main thread. The callback runs
--      same-stack BEFORE execute() returns (ExecuteString is synchronous —
--      main-thread-safe), so by the time we assert, the recorded state is set.
--   3. Assert the callback fired AND the args matched AND execute()
--      returned true; report PASS/FAIL accordingly.
--
-- Fired line is "cap26_cmd 42 hello": three space-separated tokens, no
-- quotes, no spaces-in-arg — so CryEngine tokenizes it unambiguously as
-- command-name + exactly two args. The callback receives args[1]=="42",
-- args[2]=="hello", #args==2, and args.raw is the full line (contains
-- "cap26_cmd"). args[1..] EXCLUDE GetArg(0)/the command name (per the
-- kcdx.command contract — lua_bind_command.cpp).

-- Plugin-local state the command callback records into (upvalues — NOT
-- globals; only one plugin global, `kcdx`, exists per the authoring rules).
local fired      = false   -- did the callback ever run?
local got_args   = nil     -- the args array the callback saw (shallow copy)
local got_count  = nil     -- #args the callback saw
local got_raw    = nil     -- args.raw the callback saw

kcdx.command{
    name        = "cap26_cmd",
    description = "CAP-26 self-test command: fired at input_loaded via "
                  .. "kcdx.console.execute to prove the Lua command round-trip.",
    callback    = function(args)
        fired     = true
        got_count = #args
        got_raw   = args.raw
        got_args  = { args[1], args[2] }
    end,
}

kcdx.on("input_loaded", function()
    -- Fire the command exactly as if typed into the ~ console. Synchronous:
    -- the callback above runs before execute() returns.
    local executed = kcdx.console.execute("cap26_cmd 42 hello")

    -- (1) execute() must return true — IConsole is live at input_loaded.
    if executed ~= true then
        kcdx.test.report("CAP-26-command-roundtrip", false,
            "kcdx.console.execute returned " .. tostring(executed)
            .. " (expected true — IConsole should be live at input_loaded)")
        return
    end

    -- (2) The callback must have fired — a never-firing callback means a
    -- broken round-trip (registration, dispatch, or the args bridge).
    if not fired then
        kcdx.test.report("CAP-26-command-roundtrip", false,
            "kcdx.console.execute returned true but the command callback "
            .. "never fired — broken register/dispatch round-trip")
        return
    end

    -- (3) The args must match the fired line exactly.
    local a1 = got_args and got_args[1]
    local a2 = got_args and got_args[2]
    local ok = got_count == 2
           and a1 == "42"
           and a2 == "hello"
           and type(got_raw) == "string"
           and string.find(got_raw, "cap26_cmd", 1, true) ~= nil

    if ok then
        kcdx.test.report("CAP-26-command-roundtrip", true,
            "kcdx.command + kcdx.console.execute round-trip ok: "
            .. "#args=2, args[1]='42', args[2]='hello', raw='"
            .. tostring(got_raw) .. "'")
    else
        kcdx.test.report("CAP-26-command-roundtrip", false,
            "args mismatch: #args=" .. tostring(got_count)
            .. " args[1]=" .. tostring(a1)
            .. " args[2]=" .. tostring(a2)
            .. " raw=" .. tostring(got_raw)
            .. " (expected #args=2, args[1]='42', args[2]='hello', "
            .. "raw containing 'cap26_cmd')")
    end
end)

kcdx.log.info("CAP26",
    "registered cap26_cmd; will self-fire via kcdx.console.execute at "
    .. "input_loaded and assert the args round-trip")
