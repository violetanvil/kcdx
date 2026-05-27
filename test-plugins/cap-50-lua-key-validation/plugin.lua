-- CAP-50 — kcdx.* binder input validation: FAIL LOUD on bad author input.
--
-- Batch D of the fail-state-logging sweep (fail-state-logging.md / AP14). The
-- {table} binders signal bad args via the (nil, err) RETURN pattern, so each
-- row reads the returned err string directly — no pcall is needed for these
-- (the binder does not raise; it returns). The assertions read the LOAD-
-- BEARING parts of the teaching message (does it name the bad key / index?),
-- not the exact prose, so a wording tweak does not flip the row but a
-- regression that stops naming the offending input DOES.
--
-- All three run synchronously at plugin load (the binders validate at the
-- call). Reported inline; boot-only.

-- ====================================================================
-- (#11) cap-50-unknown-key — an UNRECOGNIZED option key is rejected, and
-- the error NAMES it. We pass a clear typo (`signagure` for `signature`)
-- alongside a valid locator. Pre-fix the binder read only the keys it knew
-- and silently ignored the sibling typo — the hook would then register (or
-- fail later for a different reason) with the author's signature intent
-- vanished. Post-fix the binder rejects up front with (nil, err) naming the
-- bad key.
--
-- FALSIFIABLE (AP15): the feature-broken state is "the unknown-key gate stops
-- rejecting → the typo'd key is silently ignored → kcdx.hook returns a handle
-- (h ~= nil) OR returns an error that does NOT name 'signagure' → FAIL". The
-- pass reads the actual err string the gate produced.
do
    local h, err = kcdx.hook{
        name      = "cap50_unknown_key",
        target    = "luaL_loadfile",   -- a valid name-based locator
        signagure = "i32 (ptr L, cstr filename)",  -- TYPO: should be `signature`
        before    = function(L, filename) return L, filename end,
    }
    local pass = (h == nil)
                 and type(err) == "string"
                 and err:find("signagure", 1, true) ~= nil
                 and (err:find("unrecognized", 1, true) ~= nil
                      or err:find("not a recognized", 1, true) ~= nil)
    kcdx.test.report("cap-50-unknown-key", pass,
        pass and ("unrecognized key rejected with (nil, err) naming it: " .. err)
             or  ("expected (nil, err) where err names 'signagure' AND says "
                  .. "unrecognized/not-a-recognized; got h=" .. tostring(h)
                  .. " err=" .. tostring(err)
                  .. " — a SILENT-ACCEPT (handle returned / err not naming the "
                  .. "key) means the unknown-key gate stopped rejecting"))
end

-- ====================================================================
-- (#12) cap-50-param-types-nonstring — a NON-STRING param_types entry is an
-- ERROR, not an end-of-list marker. {"ptr", 5} would pre-fix TRUNCATE at the
-- 5 (the first non-string), building a 1-arg thunk for a function the author
-- declared as 2-arg — wrong arity marshaled into a native call (a crash risk,
-- not benign). Post-fix the binder rejects naming the bad index. We do NOT
-- invoke the resulting handle (there is none on the reject path); the assert
-- is on the synchronous (nil, err) reject shape.
--
-- target is a harmless integer VA: the call is rejected at param_types
-- validation BEFORE any JIT / invocation, so the bogus target never matters.
--
-- FALSIFIABLE (AP15): the feature-broken state is "the param_types walk
-- silently truncates at the non-string entry → dynamic_call returns a callable
-- handle for the WRONG arity (c ~= nil) → FAIL". The pass reads the err the
-- reject produced.
do
    local c, err = kcdx.memory.dynamic_call{
        target      = 0x12345678,         -- bogus VA; never reached (rejected first)
        return_type = "void",
        param_types = { "ptr", 5 },       -- 5 is non-string → must REJECT, not truncate
    }
    local pass = (c == nil)
                 and type(err) == "string"
                 and err:find("param_types", 1, true) ~= nil
    kcdx.test.report("cap-50-param-types-nonstring", pass,
        pass and ("non-string param_types entry rejected with (nil, err): " .. err)
             or  ("expected (nil, err) naming param_types (the non-string entry "
                  .. "rejected, not silently truncated); got c=" .. tostring(c)
                  .. " err=" .. tostring(err)
                  .. " — a callable handle here means the walk truncated to the "
                  .. "wrong arity (a crash-risk marshal into the native call)"))
end

-- ====================================================================
-- (#10) cap-50-null-return-distinguishable — get_module_base_address on a
-- definitely-absent module RUNS and returns a NULL pointer userdata (not nil,
-- not an error). The Lua-observable is "the call ran and returned a null
-- pointer"; the WARN line naming the bad module is engine-log-only (an author
-- cannot read the engine log from Lua, so a null return looks identical
-- pass/fail from Lua — the WARN is agent-log-confirmed, not Lua-assertable).
--
-- What IS Lua-assertable and falsifiable: the call returns a pointer userdata
-- whose :is_null() is true. :is_null() compares the C-side address to 0
-- exactly (no float round-trip — lua-precision.md), so it is a reliable
-- "ran, resolved to nothing" signal.
--
-- FALSIFIABLE (AP15): the feature-broken state is "the call errors (returns
-- nil / raises) OR returns a non-null pointer for an absent module → FAIL".
-- The pass reads the feature's actual output (the returned pointer + its
-- null-ness), not a constant.
do
    local p = kcdx.memory.get_module_base_address("definitely_not_a_real_module.dll")
    local pass = (p ~= nil)
                 and (type(p) == "userdata")
                 and (p:is_null() == true)
    kcdx.test.report("cap-50-null-return-distinguishable", pass,
        pass and ("get_module_base_address('definitely_not_a_real_module.dll') "
                  .. "ran and returned a NULL pointer userdata (is_null()==true) "
                  .. "— the diagnostic WARN naming the absent module is "
                  .. "agent-log-confirmed in kcdx-dev.log")
             or  ("expected a null pointer userdata (the call ran, found "
                  .. "nothing); got p=" .. tostring(p) .. " type=" .. type(p)
                  .. " — an error/nil here means the absent-module path no "
                  .. "longer returns an observable null pointer"))
end

kcdx.log.info("CAP50",
    "kcdx.* binder validation self-test ran at load: cap-50-unknown-key "
    .. "(kcdx.hook rejects + names a typo'd key), cap-50-param-types-nonstring "
    .. "(dynamic_call rejects a non-string param_types entry, no silent "
    .. "truncation), cap-50-null-return-distinguishable (get_module_base_"
    .. "address returns an observable null pointer for an absent module; WARN "
    .. "agent-log-confirmed)")
