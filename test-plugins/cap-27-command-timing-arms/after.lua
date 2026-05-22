-- CAP-27 after.lua — the IMMEDIATE arm + the self-fire/assert for BOTH arms.
--
-- The lua_after slot runs at hooks.cpp:431 — AFTER console::Init (hooks.cpp:409
-- arms IConsole and flushes the deferred queue) and AFTER ApplyZone. So by the
-- time this file runs:
--   * console g_ready is TRUE  -> a kcdx.command here hits the IMMEDIATE arm
--     of Thunk_RegisterCommand (console.cpp:263): RegisterCommandNow directly,
--     no queue. This is the NEW coverage — the Lua mirror of CAP-13's C++
--     immediate path.
--   * cap27_deferred (registered in plugin.lua) has ALREADY been flushed into
--     g_slots by FlushPendingCommands at Init.
--   * IConsole is live -> kcdx.console.execute works here.
-- So the whole self-fire + assert happens in after.lua; no input_loaded wait
-- is needed (cleaner than CAP-26, since everything is ready by lua_after).

local state = require("state")

-- (1) IMMEDIATE arm: register cap27_immediate now (post-Init, g_ready=true ->
-- RegisterCommandNow direct, NOT queued).
kcdx.command{
    name        = "cap27_immediate",
    description = "CAP-27 IMMEDIATE-arm self-test command: registered from "
                  .. "the lua_after slot (post-console::Init, g_ready=true) so "
                  .. "it takes RegisterCommandNow directly; the Lua mirror of "
                  .. "CAP-13's C++ immediate path.",
    callback    = function(args)
        local s = state.immediate
        s.fired = true
        s.count = #args
        s.raw   = args.raw
        s.args  = { args[1], args[2] }
    end,
}

-- (2) Self-fire BOTH commands. Each fired line is THREE space-separated
-- tokens (command-name + exactly two args), no quotes / no spaces-in-arg —
-- so CryEngine tokenizes each unambiguously as name + args[1] + args[2].
-- ExecuteString is synchronous, so each callback runs same-stack before its
-- execute() returns.
local exec_immediate = kcdx.console.execute("cap27_immediate 9 beta")
local exec_deferred  = kcdx.console.execute("cap27_deferred 7 alpha")

-- ============================================================================
-- CAP-27-immediate — the IMMEDIATE arm (the primary new coverage).
-- PASS iff: execute("cap27_immediate ...") returned true (IConsole live) AND
-- cap27_immediate's callback fired (proves RegisterCommandNow landed it in
-- g_slots and CryEngine dispatched it) AND its args matched ("9","beta").
-- ============================================================================
do
    local s = state.immediate
    if exec_immediate ~= true then
        kcdx.test.report("CAP-27-immediate", false,
            "kcdx.console.execute(\"cap27_immediate ...\") returned "
            .. tostring(exec_immediate) .. " (expected true — IConsole is live "
            .. "by lua_after, after console::Init)")
    elseif not s.fired then
        kcdx.test.report("CAP-27-immediate", false,
            "execute returned true but cap27_immediate's callback never fired "
            .. "— the IMMEDIATE arm (RegisterCommandNow from lua_after) did not "
            .. "register/dispatch")
    else
        local a1 = s.args and s.args[1]
        local a2 = s.args and s.args[2]
        local ok = s.count == 2
               and a1 == "9"
               and a2 == "beta"
               and type(s.raw) == "string"
               and string.find(s.raw, "cap27_immediate", 1, true) ~= nil
        if ok then
            kcdx.test.report("CAP-27-immediate", true,
                "IMMEDIATE arm ok (lua_after, post-Init, RegisterCommandNow "
                .. "direct): cap27_immediate fired with #args=2, args[1]='9', "
                .. "args[2]='beta', raw='" .. tostring(s.raw) .. "'")
        else
            kcdx.test.report("CAP-27-immediate", false,
                "cap27_immediate fired but args mismatch: #args="
                .. tostring(s.count) .. " args[1]=" .. tostring(a1)
                .. " args[2]=" .. tostring(a2) .. " raw=" .. tostring(s.raw)
                .. " (expected #args=2, args[1]='9', args[2]='beta', "
                .. "raw containing 'cap27_immediate')")
        end
    end
end

-- ============================================================================
-- CAP-27-coexist — the QUEUE-THEN-FLUSH boundary: the deferred command
-- (registered from plugin.lua, flushed at Init) AND the immediate command
-- (registered from after.lua) BOTH dispatch with their own correct args,
-- proving they coexist in g_slots without clobber.
-- PASS iff: BOTH executes returned true AND BOTH callbacks fired with their
-- distinct args (deferred: "7","alpha"; immediate: "9","beta").
-- ============================================================================
do
    local d = state.deferred
    local i = state.immediate
    if exec_deferred ~= true then
        kcdx.test.report("CAP-27-coexist", false,
            "kcdx.console.execute(\"cap27_deferred ...\") returned "
            .. tostring(exec_deferred) .. " (expected true)")
    elseif not d.fired then
        kcdx.test.report("CAP-27-coexist", false,
            "execute returned true but cap27_deferred's callback never fired "
            .. "— the deferred command did not flush into g_slots at "
            .. "console::Init (or was clobbered by the immediate registration)")
    elseif not i.fired then
        kcdx.test.report("CAP-27-coexist", false,
            "cap27_deferred fired but cap27_immediate never fired — the two "
            .. "arms do not coexist in g_slots")
    else
        local d1, d2 = d.args and d.args[1], d.args and d.args[2]
        local i1, i2 = i.args and i.args[1], i.args and i.args[2]
        local ok = d.count == 2 and d1 == "7" and d2 == "alpha"
               and i.count == 2 and i1 == "9" and i2 == "beta"
        if ok then
            kcdx.test.report("CAP-27-coexist", true,
                "deferred+immediate coexist: cap27_deferred (queued in "
                .. "plugin.lua, flushed at Init) dispatched args[1]='7', "
                .. "args[2]='alpha' AND cap27_immediate (registered in "
                .. "after.lua) dispatched args[1]='9', args[2]='beta' — both "
                .. "landed in g_slots without clobber")
        else
            kcdx.test.report("CAP-27-coexist", false,
                "both fired but args crossed/mismatched: deferred #args="
                .. tostring(d.count) .. " args=(" .. tostring(d1) .. ","
                .. tostring(d2) .. ") immediate #args=" .. tostring(i.count)
                .. " args=(" .. tostring(i1) .. "," .. tostring(i2) .. ") "
                .. "(expected deferred=('7','alpha'), immediate=('9','beta'))")
        end
    end
end

kcdx.log.info("CAP27",
    "after.lua: registered cap27_immediate (IMMEDIATE arm), self-fired both "
    .. "commands via kcdx.console.execute, reported CAP-27-immediate + "
    .. "CAP-27-coexist")
