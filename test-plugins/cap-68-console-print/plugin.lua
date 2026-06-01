-- CAP-68 plugin.lua — kcdx.console.print line-to-the-~-overlay regression.
--
-- kcdx.console.print(text) is a GROUPED-DOMAIN positional verb: one string arg,
-- the line to paint to the in-game `~` console overlay. It returns:
--   true   — the line was accepted (the surface was ready and printed it),
--   false  — the console surface isn't ready, or the print path is unavailable
--            on this build,
--   (nil, err) — ONLY on a bad arg (a non-string), per the kcdx-binder error
--            convention. err is a string naming the expected `string` argument.
--
-- The console surface is armed by input_loaded, so the calls that need a ready
-- surface (callable, overlay) run inside kcdx.on("input_loaded"). The bad-arg
-- row needs NO ready surface (the binder rejects a non-string before touching
-- the surface), so it runs synchronously at load.

-- ====================================================================
-- (1) CAP-68-badarg — a non-string arg returns (nil, teaching-err).
-- ====================================================================
-- BOOT-ONLY: the binder (src/lua_bind_command.cpp Lua_ConsolePrint) checks the
-- arg type FIRST and returns (nil, err) before the surface is touched, so this
-- is checkable synchronously at load with no overlay and no event wait.
-- err names the expected argument: "...expects a single string argument...".
-- Assert r==nil, err is a string, and err contains the literal "string"
-- (string.find plain=true).
do
    local r, err = kcdx.console.print(123)

    local ok = r == nil
           and type(err) == "string"
           and string.find(err, "string", 1, true) ~= nil

    if ok then
        kcdx.test.report("CAP-68-badarg", true,
            "non-string arg returned (nil, err): r==nil, err is a string, and "
            .. "err names the expected `string` argument "
            .. "(string.find(err, \"string\") matched) — a bad arg is "
            .. "(nil, teaching-err), NOT a silent no-op / NOT a boolean")
    else
        kcdx.test.report("CAP-68-badarg", false,
            "bad-arg contract mismatch: r=" .. tostring(r) .. " (want nil) "
            .. "err type=" .. type(err) .. " (want string) err="
            .. tostring(err) .. " (want a string naming the `string` argument)")
    end
end

-- The console surface is armed by input_loaded. CAP-68-callable + CAP-68-overlay
-- both need a ready surface, so they fire from the input_loaded callback.
kcdx.on("input_loaded", function()
    -- ================================================================
    -- (2) CAP-68-callable — the binding is a function returning a boolean.
    -- ================================================================
    -- BOOT-ONLY (confirmed at the input_loaded fire, no player gesture).
    -- Asserts kcdx.console.print is a function AND a call with a VALID string
    -- returns a boolean (not nil, not an error) once the surface is up. FAILS
    -- if the binding is missing (type ~= "function") or returns the wrong type
    -- (a print binding that errored or returned nil for a valid string).
    do
        local is_fn = type(kcdx.console.print) == "function"
        -- Call inside pcall so a binder error becomes a falsifiable FAIL rather
        -- than aborting the chunk. A valid string must yield a boolean.
        local called, ret = pcall(kcdx.console.print, "CAP68_CALLABLE_CHECK")
        local ok = is_fn
               and called == true
               and type(ret) == "boolean"

        if ok then
            kcdx.test.report("CAP-68-callable", true,
                "kcdx.console.print is a function and a valid-string call "
                .. "returned a boolean (" .. tostring(ret) .. ") once the "
                .. "console surface was armed at input_loaded — the binding "
                .. "exists and has the right return type")
        else
            kcdx.test.report("CAP-68-callable", false,
                "callable contract mismatch: type(kcdx.console.print)="
                .. type(kcdx.console.print) .. " (want function) pcall ok="
                .. tostring(called) .. " (want true — no binder error) ret type="
                .. type(ret) .. " value=" .. tostring(ret)
                .. " (want a boolean)")
        end
    end

    -- ================================================================
    -- (3) CAP-68-overlay — a valid print at input_loaded returns true;
    --     the printed line is the manual overlay observable.
    -- ================================================================
    -- `console` mode. The assertable part self-reports: kcdx.console.print of a
    -- known marker line returns TRUE (the surface accepted it). The PERCEPTUAL
    -- part — the marker line appearing on the `~` console overlay — is the
    -- manual eyeball (overlay-paint has no machine-readable signal). Auto-pass =
    -- "call returned true"; the user confirms the printed line by opening `~`.
    do
        local ok = kcdx.console.print("CAP68_CONSOLE_PRINT_OVERLAY_OK")

        if ok == true then
            kcdx.test.report("CAP-68-overlay", true,
                "kcdx.console.print(\"CAP68_CONSOLE_PRINT_OVERLAY_OK\") returned "
                .. "true at input_loaded — the console surface accepted the "
                .. "line (auto-pass). MANUAL: open the ~ console and confirm "
                .. "the line CAP68_CONSOLE_PRINT_OVERLAY_OK is painted on the "
                .. "overlay (the perceptual observable, no machine signal)")
        else
            kcdx.test.report("CAP-68-overlay", false,
                "kcdx.console.print returned " .. tostring(ok)
                .. " (want true) for the overlay marker at input_loaded — the "
                .. "surface refused the print, so the line will not paint. "
                .. "FAILS if the surface is not ready / the print path is "
                .. "unavailable on this build")
        end
    end
end)

kcdx.log.info("CAP68",
    "kcdx.console.print self-test armed: CAP-68-badarg (non-string -> "
    .. "(nil, err) naming `string`) reported at load; CAP-68-callable "
    .. "(function returning a boolean) + CAP-68-overlay (valid print returns "
    .. "true, line paints the ~ overlay) report from input_loaded")
