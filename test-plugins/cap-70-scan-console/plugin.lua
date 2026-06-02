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

-- The CAP-70-dispatch pattern is a DETOUR-IMMUNE, .text-unique deep-interior
-- AOB (WHGame.dll+0x9800, 4035 bytes past its containing function's entry) that
-- still resolves to 1 match at input_loaded, AFTER the apply pass, regardless of
-- which plugins co-reside. See the row's own block below for why.

-- ====================================================================
-- (1) CAP-70-dispatch — kcdx_scan with a KNOWN-GOOD pattern dispatches.
-- ====================================================================
-- Uses a DEEP-INTERIOR .text-UNIQUE AOB (WHGame.dll+0x9800, verified EXACTLY 1
-- match against WHGame.dll). This site is DETOUR-IMMUNE BY CONSTRUCTION: it sits
-- ~4 KB (4035 bytes) into its containing function — far outside the 5-byte
-- function-entry zone any co-resident kcdx.hook before/after detour can reach (a
-- 5-byte JMP detour only clobbers a function's FIRST bytes), and it is operand-
-- free (register-and-small-displacement arithmetic, no call/jmp rel32, no rip-
-- relative operand → build-stable bytes). No suite plugin byte-rewrites it. So
-- it stays count==1 at input_loaded, AFTER the ApplyZone byte-patch pass.
--
-- WHY THIS MATTERS: an entry-prologue AOB (the old choice) is CO-RESIDENT-
-- HOSTAGE — any plugin that hooks that function installs a 5-byte JMP detour
-- over the prologue and destroys the match before input_loaded. A deep-interior
-- operand-free AOB is hook-immune by construction: no entry detour can land in
-- the window. Two earlier choices each picked a co-resident-mutated site: the
-- outfit-swap AOB (whose tail cap-39 rewrites at the apply pass) and the
-- luaL_openlibs entry AOB (which a co-resident plugin entry-hooks before
-- input_loaded). This site escapes both traps.
--
-- The command line is a single-quoted Lua string containing the double-quoted
-- pattern arg, so the pattern reaches kcdx_scan as one quoted token:
--   kcdx_scan WHGame.dll "41 03 EC 33 DF 41 03 45 CC C4 E2 50 F2 FA 03 C3"
--
-- AUTO-PASS (machine): execute returned true — the console surface accepted +
-- dispatched the kcdx_scan command. This holds regardless of how CryEngine
-- tokenizes the quoted pattern arg (the dispatch is proven either way).
-- FALSIFIABLE: kcdx_scan not registered, or the surface not ready → execute
-- returns false → FAIL.
kcdx.on("input_loaded", function()
    local line =
        'kcdx_scan WHGame.dll "41 03 EC 33 DF 41 03 45 CC C4 E2 50 F2 FA 03 C3"'
    local ok = kcdx.console.execute(line)

    if ok == true then
        kcdx.test.report("CAP-70-dispatch", true,
            "kcdx.console.execute('" .. line .. "') returned true — the "
            .. "engine console surface accepted + dispatched the kcdx_scan "
            .. "command. Manual eyeball (open ~): the deep-interior .text-unique "
            .. "site (detour-immune, byte-rewritten by nobody) prints "
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

-- ====================================================================
-- (3) CAP-70-result — assert the kcdx_scan FINDING, not just dispatch.
-- ====================================================================
-- CAP-70-dispatch proves the CONSOLE surface dispatched the command, but a
-- registered command that finds NOTHING still returns execute==true — so the
-- dispatch row cannot catch a scan that stopped resolving. This row closes that
-- gap: it asserts the scan FINDING programmatically.
--
-- The result seam is shared: the kcdx.scan{} Lua verb and the kcdx_scan console
-- command both route their resolve through scan_engine::RunScan, so asserting
-- via kcdx.scan{} against the same verified site exercises the identical resolve
-- path with a machine-checkable return value (kcdx_scan paints to the overlay;
-- kcdx.scan{} returns a table). Same deep-interior .text-unique AOB
-- (the verified single-match, detour-immune site), so a regression in the
-- resolve path that the console row would only show as a wrong overlay count
-- fails THIS row hard.
--
-- WHY THIS SITE (detour-immunity): the assertion runs at input_loaded, AFTER the
-- apply pass — so the AOB must survive whatever the co-resident suite plugins do.
-- An entry-prologue AOB is co-resident-hostage (any plugin hooking that function
-- installs a 5-byte JMP detour over the prologue and the leading bytes vanish). A
-- deep-interior operand-free AOB (WHGame.dll+0x9800, 4 KB into its function) is
-- hook-immune by construction: no 5-byte entry detour reaches the window, and
-- nothing byte-rewrites it. count==1 holds regardless of which plugins co-reside.
--
-- Precision: matches[1].offset is the module-relative offset (scan offset 0, so
-- offset == relOffset == the RVA), pushed C-side via lua_pushinteger but read
-- back into a Lua NUMBER. CryEngine's Lua is LUA_NUMBER=float (24-bit mantissa):
-- integers round-trip exactly only below 2^24 (16,777,216). This site's offset
-- (~0x9800 = 38,912) is BELOW 2^24, so it would round-trip exactly — but the
-- offset still SHIFTS per game build, so an exact compare is flaky-by-
-- construction across versions regardless of the float grid. So this row does
-- NOT assert an exact RVA; it asserts the precision-safe, falsifiable shape —
-- count==1 at the verified site, the match resolves on WHGame.dll, and offset is
-- a positive number. Identical discipline to CAP-32-resolve, which also refuses
-- a per-build absolute address.
--
-- FALSIFIABLE: count != 1 → the verified site stopped resolving / the scan
-- resolve path regressed; module != "WHGame.dll" or offset not a positive
-- number → resolved the wrong site / a malformed match table. Either → FAIL.
kcdx.on("input_loaded", function()
    local r = kcdx.scan{
        name    = "cap70_deep_interior_verify",
        pattern = "41 03 EC 33 DF 41 03 45 CC C4 E2 50 F2 FA 03 C3",
        module  = "WHGame.dll",
    }

    if type(r) ~= "table" then
        kcdx.test.report("CAP-70-result", false,
            "kcdx.scan returned " .. type(r) .. " (expected a table) for the "
            .. "verified deep-interior AOB — a resolved scan is ALWAYS a "
            .. "table, never nil; a nil return means bad input reached the binder")
    else
        local m1 = r.matches and r.matches[1]
        local ok = r.count == 1
               and m1 ~= nil
               and m1.module == "WHGame.dll"
               and type(m1.offset) == "number"
               and m1.offset > 0

        if ok then
            kcdx.test.report("CAP-70-result", true,
                "kcdx_scan resolve path found the verified deep-interior site: "
                .. "count==1, matches[1].module==\"WHGame.dll\", "
                .. "matches[1].offset==" .. tostring(m1.offset) .. " (a positive "
                .. "number, module-relative — not asserting a per-build absolute "
                .. "RVA, which exceeds the float-exact 2^24 grid and shifts per "
                .. "build). A registered-but-finds-nothing scan would FAIL here, "
                .. "where the execute==true dispatch row cannot.")
        else
            kcdx.test.report("CAP-70-result", false,
                "kcdx_scan resolve mismatch on the verified deep-interior AOB: "
                .. "count=" .. tostring(r.count) .. " (want 1 — count!=1 means "
                .. "the site stopped resolving / the scan regressed) matches[1]="
                .. tostring(m1) .. " module=" .. tostring(m1 and m1.module)
                .. " (want \"WHGame.dll\") offset=" .. tostring(m1 and m1.offset)
                .. " type=" .. tostring(m1 and type(m1.offset))
                .. " (want a positive number; wrong module/offset means it "
                .. "resolved the wrong site)")
        end
    end
end)

kcdx.log.info("CAP70",
    "registered kcdx_scan console-dispatch self-test; will fire "
    .. "kcdx.console.execute for the known-good pattern (CAP-70-dispatch) and "
    .. "the missing-argv case (CAP-70-badargv) at input_loaded and assert "
    .. "each dispatched (execute==true), plus CAP-70-result which asserts the "
    .. "scan FINDING (count==1 at the verified deep-interior site) via "
    .. "kcdx.scan{} over the shared resolve path. Open ~ to eyeball the "
    .. "painted dispatch output.")
