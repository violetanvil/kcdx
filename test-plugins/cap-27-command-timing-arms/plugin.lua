-- CAP-27 plugin.lua — the DEFERRED arm of the kcdx.command registration
-- timing test.
--
-- plugin.lua runs in RunAll (hooks.cpp:331), which is BEFORE console::Init
-- (hooks.cpp:409). So at this point console g_ready is FALSE: this
-- kcdx.command call hits the DEFERRED arm of Thunk_RegisterCommand
-- (console.cpp:263) — the command is QUEUED (g_pendingCommands) and later
-- flushed by FlushPendingCommands when console::Init arms the surface
-- (console.cpp:322). This is the SAME arm CAP-26 exercises.
--
-- We only REGISTER here. The self-fire + assert for BOTH commands happens in
-- after.lua (the lua_after slot), which runs AFTER console::Init — by then
-- this deferred command has been flushed into g_slots and IConsole is live,
-- so after.lua can fire it and confirm the flush landed (the CAP-27-coexist
-- assertion).
--
-- The callback records what it received into the shared state module (see
-- state.lua) so after.lua — a separate chunk — can read it.

local state = require("state")

kcdx.command{
    name        = "cap27_deferred",
    description = "CAP-27 DEFERRED-arm self-test command: registered from "
                  .. "plugin.lua (pre-console::Init, g_ready=false) so it is "
                  .. "queued then flushed at Init; fired from after.lua via "
                  .. "kcdx.console.execute to prove the flush landed and "
                  .. "coexists with the immediate command.",
    callback    = function(args)
        local s = state.deferred
        s.fired = true
        s.count = #args
        s.raw   = args.raw
        s.args  = { args[1], args[2] }
    end,
}

kcdx.log.info("CAP27",
    "registered cap27_deferred from plugin.lua (DEFERRED arm: pre-Init, "
    .. "g_ready=false -> queued; flushed at console::Init). Self-fire + "
    .. "assert run in after.lua.")
