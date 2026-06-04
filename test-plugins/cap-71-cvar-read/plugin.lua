-- CAP-71 plugin.lua — kcdx.cvar.* CVar-read surface regression (Lua).
--
-- kcdx.cvar.get_int(name) / .get_bool(name) / .get_float(name) read a game
-- CVar's value by name. The author supplies the CVar string they already hold
-- (a modding wiki, the `~` console, a config); the engine resolves the console
-- + the ICVar accessor. Returns:
--   get_int   — a number on a successful read; nil on a miss (CVar absent /
--               surface not ready). (nil, err) ONLY on a bad arg (non-string).
--   get_bool  — true/false (int != 0) on success; nil on a miss.
--   get_float — a number on success; nil on a miss.
-- The miss value is nil — a missing CVar is observable, NEVER a fabricated
-- value (the no-garbage-write contract: a failed read never returns a number).
--
-- The CVar surface is ready once the console comes up (input_loaded), so every
-- value read gates on kcdx.on("input_loaded", ...) — exactly like cap-68 gates
-- its callable/overlay rows. The bad-arg row needs NO ready surface (the binder
-- rejects a non-string before touching the console), so it runs at load.
--
-- Test mode: boot-only. sys_pakPriority is a CONFIRMED boot-present int CVar
-- (verified live in the asset-system recon); the reads fire at input_loaded
-- with no player gesture, and each result is fully machine-checkable (a number /
-- bool / nil) — there is NO perceptual overlay to eyeball (unlike cap-68/69's
-- print). launch-to-menu confirms every row.
--
-- Known-good read target: "sys_pakPriority" — a real boot-present int CVar
-- (recon-CONFIRMED). Its value is a small mode (kcdx's launcher sets 0 via
-- user.cfg; default 2 otherwise), so the rows assert the read SUCCEEDED and the
-- value is a plausible small mode (0..3), NOT a hardcoded magic int.

local CVAR_INT   = "sys_pakPriority"            -- confirmed boot-present int CVar
local CVAR_BOGUS = "kcdx_nonexistent_cvar_xyz"  -- deliberately absent

-- ====================================================================
-- (1) CAP-71-badarg — a non-string arg returns (nil, teaching-err).
-- ====================================================================
-- BOOT-ONLY: the binder checks the arg type FIRST and returns (nil, err)
-- before the console surface is touched, so this is checkable at load with no
-- ready surface. err names the expected `string` argument. Mirrors CAP-68-badarg.
-- Assert r==nil, err is a string, and err contains the literal "string".
do
    local r, err = kcdx.cvar.get_int(123)

    local ok = r == nil
           and type(err) == "string"
           and string.find(err, "string", 1, true) ~= nil

    if ok then
        kcdx.test.report("CAP-71-badarg", true,
            "kcdx.cvar.get_int(123) (non-string) returned (nil, err): r==nil, "
            .. "err is a string, and err names the expected `string` argument "
            .. "(string.find(err, \"string\") matched) — a bad arg is "
            .. "(nil, teaching-err), NOT a silent no-op / NOT a fabricated value")
    else
        kcdx.test.report("CAP-71-badarg", false,
            "bad-arg contract mismatch: r=" .. tostring(r) .. " (want nil) "
            .. "err type=" .. type(err) .. " (want string) err="
            .. tostring(err) .. " (want a string naming the `string` argument)")
    end
end

-- The CVar surface is ready at input_loaded. CAP-71-callable / -float / -miss /
-- -bool all read a value, so they fire from the input_loaded callback.
kcdx.on("input_loaded", function()
    -- ================================================================
    -- (2) CAP-71-callable — get_int reads a CONFIRMED real int CVar.
    -- ================================================================
    -- BOOT-ONLY (at the input_loaded fire, no player gesture). Asserts
    -- kcdx.cvar.get_int is a function AND reading sys_pakPriority (a CVar that
    -- DEMONSTRABLY exists at boot) returns a number — NOT nil, NOT an error —
    -- whose value is a plausible small pakPriority mode (0..3). FAILS if the
    -- binding is missing / returns the wrong type / the read fails for a CVar
    -- that demonstrably exists / the value is out of the plausible mode range.
    do
        local is_fn = type(kcdx.cvar.get_int) == "function"
        -- Call under pcall so a binder error becomes a falsifiable FAIL rather
        -- than aborting the chunk. A valid read must yield a number.
        local called, v = pcall(kcdx.cvar.get_int, CVAR_INT)
        local ok = is_fn
               and called == true
               and type(v) == "number"
               and v >= 0 and v <= 3   -- a plausible small pakPriority mode

        if ok then
            kcdx.test.report("CAP-71-callable", true,
                "kcdx.cvar.get_int(\"" .. CVAR_INT .. "\") returned the number "
                .. tostring(v) .. " at input_loaded — the binding exists, has "
                .. "the right return type, and read a plausible small mode "
                .. "(0..3) for a CVar that demonstrably exists. Cross-surface "
                .. "parity: the C++ CAP-72-callable reads the SAME name via "
                .. "GetCVarInt — both call the same engine cvar:: core, so the "
                .. "two rows MUST report the same observed value (compare them).")
        else
            kcdx.test.report("CAP-71-callable", false,
                "callable/read mismatch for \"" .. CVAR_INT .. "\": "
                .. "type(kcdx.cvar.get_int)=" .. type(kcdx.cvar.get_int)
                .. " (want function) pcall ok=" .. tostring(called)
                .. " (want true — no binder error) value type=" .. type(v)
                .. " value=" .. tostring(v)
                .. " (want a number in 0..3 — the read of a CVar that exists "
                .. "must succeed and return a plausible small mode)")
        end
    end

    -- ================================================================
    -- (3) CAP-71-float — get_float reads sys_pakPriority as a float.
    -- ================================================================
    -- BOOT-ONLY. Reads the SAME confirmed-present CVar via get_float. A CVar's
    -- value is readable as a float even when it is nominally an int (the engine
    -- reads the float field via GetFVal); an int CVar's float reading may be the
    -- int-as-float or 0.0 — both fine. So this asserts the READ SUCCEEDS
    -- (returns a number, not nil), NOT a specific value.
    -- WHY this CVar, not an e_* float CVar: only sys_pakPriority is recon-
    -- CONFIRMED boot-present (results-driven — read a confirmed CVar, never a
    -- guessed name). get_float on it exercises the GetFVal accessor path on a
    -- CVar that demonstrably exists.
    do
        local called, v = pcall(kcdx.cvar.get_float, CVAR_INT)
        local ok = called == true and type(v) == "number"

        if ok then
            kcdx.test.report("CAP-71-float", true,
                "kcdx.cvar.get_float(\"" .. CVAR_INT .. "\") returned the "
                .. "number " .. tostring(v) .. " at input_loaded — the GetFVal "
                .. "accessor path read a CVar that demonstrably exists "
                .. "(value-agnostic: the float reading of a nominally-int CVar "
                .. "may be the int-as-float or 0.0; the row asserts the read "
                .. "SUCCEEDED, not a specific value)")
        else
            kcdx.test.report("CAP-71-float", false,
                "get_float read mismatch for \"" .. CVAR_INT .. "\": "
                .. "pcall ok=" .. tostring(called) .. " (want true) value type="
                .. type(v) .. " value=" .. tostring(v)
                .. " (want a number — the float read of a CVar that exists "
                .. "must succeed)")
        end
    end

    -- ================================================================
    -- (4) CAP-71-bool — get_bool reads sys_pakPriority as a bool.
    -- ================================================================
    -- BOOT-ONLY. get_bool is (get_int != 0). Reads the SAME confirmed CVar; the
    -- read must SUCCEED (return a boolean, not nil). Value-agnostic: the bool is
    -- whatever (int != 0) yields for the live pakPriority value.
    do
        local called, v = pcall(kcdx.cvar.get_bool, CVAR_INT)
        local ok = called == true and type(v) == "boolean"

        if ok then
            kcdx.test.report("CAP-71-bool", true,
                "kcdx.cvar.get_bool(\"" .. CVAR_INT .. "\") returned the "
                .. "boolean " .. tostring(v) .. " at input_loaded — get_bool "
                .. "(= int != 0) read a CVar that demonstrably exists "
                .. "(value-agnostic: the bool reflects the live mode value)")
        else
            kcdx.test.report("CAP-71-bool", false,
                "get_bool read mismatch for \"" .. CVAR_INT .. "\": "
                .. "pcall ok=" .. tostring(called) .. " (want true) value type="
                .. type(v) .. " value=" .. tostring(v)
                .. " (want a boolean — the bool read of a CVar that exists "
                .. "must succeed)")
        end
    end

    -- ================================================================
    -- (5) CAP-71-miss — a bogus CVar name returns nil (no garbage write).
    -- ================================================================
    -- BOOT-ONLY. Reading a CVar that does NOT exist returns nil — the
    -- observable-miss / no-garbage-write contract. FAILS if a bogus name
    -- returns a non-nil value (a fabricated read) — the silent-success defect
    -- the out-param+bool engine shape exists to prevent (a miss must be
    -- distinguishable from a real 0).
    do
        local called, v = pcall(kcdx.cvar.get_int, CVAR_BOGUS)
        -- The read must not error (a bogus NAME is a valid string arg — it is a
        -- miss, not a bad arg) AND must return nil (not a fabricated number).
        local ok = called == true and v == nil

        if ok then
            kcdx.test.report("CAP-71-miss", true,
                "kcdx.cvar.get_int(\"" .. CVAR_BOGUS .. "\") returned nil at "
                .. "input_loaded — a CVar that does NOT exist reads as nil (the "
                .. "observable-miss contract), NEVER a fabricated value. A "
                .. "bogus name is a valid string (a miss, not a bad arg), so it "
                .. "does not error — it returns nil")
        else
            kcdx.test.report("CAP-71-miss", false,
                "miss contract mismatch for bogus \"" .. CVAR_BOGUS .. "\": "
                .. "pcall ok=" .. tostring(called) .. " (want true — a bogus "
                .. "name is a valid string, a miss not a bad arg) value="
                .. tostring(v) .. " (want nil — a non-existent CVar must read "
                .. "nil, NOT a fabricated value)")
        end
    end
end)

kcdx.log.info("CAP71",
    "kcdx.cvar.* self-test armed: CAP-71-badarg (non-string -> (nil, err) "
    .. "naming `string`) reported at load; CAP-71-callable (get_int reads a "
    .. "plausible small mode for sys_pakPriority), CAP-71-float (get_float "
    .. "reads it), CAP-71-bool (get_bool reads it), CAP-71-miss (a bogus name "
    .. "reads nil) report from input_loaded")
