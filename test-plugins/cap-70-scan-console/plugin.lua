-- CAP-70 plugin.lua — drive the engine-owned kcdx_scan `~`-console command
-- through kcdx.console.execute and assert the console surface dispatched it.
--
-- kcdx_scan is a CONSOLE COMMAND (registered unconditionally at console::Init,
-- src/console_commands_scan.cpp), NOT a kcdx.* Lua surface — so the way a test
-- exercises it is the way a user types it: through CryEngine's
-- IConsole::ExecuteString, reached from Lua via kcdx.console.execute(line).
-- ExecuteString is SYNCHRONOUS on the main thread (cap-26 proves this exact
-- round-trip against a Lua-registered command); kcdx.console.execute returns
-- true iff the console surface accepted + dispatched the line.
--
-- Both rows fire at kcdx.on("input_loaded") — the console surface is armed by
-- then (the same deterministic boot trigger cap-26 uses). The auto-pass for
-- BOTH rows is the MACHINE-CHECKABLE "execute returned true" (the command
-- exists and was dispatched). The kcdx_scan output painted to the `~` overlay
-- is human-perceptual (no machine signal) — that is the manual eyeball.

-- ====================================================================
-- (1) CAP-70-dispatch — kcdx_scan with a KNOWN-GOOD pattern dispatches.
-- ====================================================================
-- Reuses cap-32's LIVE-PROVEN outfit-swap AOB (already verified to resolve to
-- count==1 on WHGame.dll), so the manual `[scan] 1 matches:` overlay observable
-- rests on a verified site, not a fresh RE guess.
--
-- The command line is a single-quoted Lua string containing the double-quoted
-- pattern arg, so the pattern reaches kcdx_scan as one quoted token:
--   kcdx_scan WHGame.dll "48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0"
--
-- AUTO-PASS (machine): execute returned true — the console surface accepted +
-- dispatched the kcdx_scan command. This holds regardless of how CryEngine
-- tokenizes the quoted pattern arg (the dispatch is proven either way).
-- FALSIFIABLE: kcdx_scan not registered, or the surface not ready → execute
-- returns false → FAIL.
kcdx.on("input_loaded", function()
    local line =
        'kcdx_scan WHGame.dll "48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0"'
    local ok = kcdx.console.execute(line)

    if ok == true then
        kcdx.test.report("CAP-70-dispatch", true,
            "kcdx.console.execute('" .. line .. "') returned true — the "
            .. "engine console surface accepted + dispatched the kcdx_scan "
            .. "command. Manual eyeball (open ~): the outfit-swap site prints "
            .. "'[scan] 1 matches:' + a 'WHGame.dll+0x...' line.")
    else
        kcdx.test.report("CAP-70-dispatch", false,
            "kcdx.console.execute returned " .. tostring(ok)
            .. " (expected true). Either kcdx_scan is not registered, or the "
            .. "console surface is not armed at input_loaded — the command did "
            .. "not dispatch.")
    end
end)

-- ====================================================================
-- (2) CAP-70-badargv — kcdx_scan with NO args dispatches the fail-loud path.
-- ====================================================================
-- Drives `kcdx_scan` alone (missing module + pattern). kcdx_scan's argc<3 arm
-- prints the teaching usage line to the overlay and returns — fail-loud, never
-- a silent no-op, never a crash.
--
-- AUTO-PASS (machine): execute returned true — the registered command
-- DISPATCHED and ran its bad-argv path. execute returning true for a registered
-- command is independent of the command's OWN argv validation (execute reports
-- whether the surface dispatched the line, not whether the command liked its
-- args); so this row asserts the command EXISTS and the bad-argv path runs
-- without crashing the dispatch.
-- FALSIFIABLE: execute returns false (command not registered) or the game
-- destabilises (the bad-argv path crashed) → FAIL.
kcdx.on("input_loaded", function()
    local line = "kcdx_scan"
    local ok = kcdx.console.execute(line)

    if ok == true then
        kcdx.test.report("CAP-70-badargv", true,
            "kcdx.console.execute('" .. line .. "') returned true — kcdx_scan "
            .. "is registered and its missing-argv (argc<3) fail-loud path "
            .. "dispatched without crashing. Manual eyeball (open ~): the "
            .. "usage line 'kcdx_scan <module> \"<AOB pattern>\" ...' is "
            .. "painted.")
    else
        kcdx.test.report("CAP-70-badargv", false,
            "kcdx.console.execute returned " .. tostring(ok)
            .. " (expected true) — kcdx_scan did not dispatch (not registered, "
            .. "or surface not armed).")
    end
end)

kcdx.log.info("CAP70",
    "registered kcdx_scan console-dispatch self-test; will fire "
    .. "kcdx.console.execute for the known-good pattern (CAP-70-dispatch) and "
    .. "the missing-argv case (CAP-70-badargv) at input_loaded and assert "
    .. "each dispatched (execute==true). Open ~ to eyeball the painted output.")
