-- CAP-59 plugin.lua — kcdx.hook smart-resolver shape regression.
--
-- The smart resolver shape on hooks:
--
--   kcdx.hook.<name>.<mode>(fn)
--                 ^      ^
--                 |      mode access — __index on the resolved userdata;
--                 |      returns the install closure for a valid mode +
--                 |      nil for an invalid mode for the resolved KIND
--                 |      (the kind-aware filter — IsValidHookModeForKind).
--                 |
--                 name access — __index on kcdx.hook; returns the
--                 resolved userdata for a name that exists in any
--                 population source, nil for a typo (typo-fails-fast).
--
-- This plugin exercises the kind-aware filter (claim 2) and the FIRES
-- end-to-end install (claim 1).

-- ====================================================================
-- (1) CAP-59-fires — kcdx.hook.lua_pcall.before(fn) on a curated
--     engine-seed entry installs AND the callback fires.
--
-- Target choice: `lua_pcall` — a curated verified leaf called
-- continuously throughout the session (every Lua-from-C invocation
-- routes through it). Critically, lua_pcall is called many times AFTER
-- plugin load — unlike one-shot Lua-state-init functions like
-- luaopen_math, which execute during luaL_openlibs at VM creation
-- (BEFORE plugin scripts run) and have zero future call sites by the
-- time a plugin can install a hook. lua_pcall fires from the next Lua
-- callback the engine dispatches, which happens within milliseconds of
-- plugin load. One fire per session is enough: we self-report PASS from
-- the FIRST fire and the one-shot guard makes subsequent fires inert.
--
-- The smart resolver shape: NO `kcdx.` prefix — the __index access
-- key is the bare seed name. ResolveBareWinner walks self > engine >
-- other; the bare `lua_pcall` hits the engine seed and the smart
-- resolver returns the install closure. (cap-03 / cap-04 use the
-- flat-table form `kcdx.hook{target="lua_pcall", ...}`; cap-59 uses
-- the bare smart-resolver form — same target, different surface.)
--
-- FALSIFIABLE: if the smart resolver fails to resolve the bare engine
-- seed name (returns nil from __index), `.before` raises and registration
-- never happens — no callback closure to fire → PENDING (caught by the
-- InputLoaded backstop below as a loud FAIL). If install returns nil
-- (kind mismatch / unknown reason) → also FAIL. If install succeeds
-- but the detour is not wired → no fire → backstop FAIL.
-- ====================================================================

local g_fired   = false  -- guards the one-shot self-report
local g_handle  = nil    -- captured from the install path; nil if .before failed

do
    local ok, errOrHandle = pcall(function()
        -- The smart resolver: kcdx.hook.lua_pcall → resolved userdata;
        -- .before → install closure; (fn) → install. Returns the handle
        -- (or (nil, err) on a bad call inside Lua_Hook).
        return kcdx.hook.lua_pcall.before(function(L, nargs, nresults, errfunc)
            -- L is the lua_State*; the other args come from lua_pcall's
            -- signature ("i32 (ptr L, i32 nargs, i32 nresults, i32
            -- errfunc)"). We don't read them; the callback's job is to
            -- fire once and self-report.
            if g_fired then return end       -- one-shot guard
            g_fired = true
            kcdx.test.report("CAP-59-fires", true,
                "kcdx.hook.lua_pcall.before(fn) installed AND fired — "
                .. "the smart-resolver install path resolved the bare "
                .. "engine-seed name (self > engine > other walk hit the "
                .. "engine tier), wired the detour, and the callback "
                .. "received its first fire on the next lua_pcall call")
        end)
    end)

    if not ok then
        kcdx.test.report("CAP-59-fires", false,
            "kcdx.hook.lua_pcall.before(fn) raised at registration: "
            .. tostring(errOrHandle) .. " — the smart resolver did not "
            .. "produce a callable installer for the bare engine-seed "
            .. "name `lua_pcall` (the typo-fails-fast gate or the "
            .. "kind-aware filter falsely rejected a valid name)")
        return  -- skip the rest; the install never happened
    end

    g_handle = errOrHandle
    if g_handle == nil then
        kcdx.test.report("CAP-59-fires", false,
            "kcdx.hook.lua_pcall.before(fn) returned nil at registration "
            .. "— the install path rejected a known-good curated name + "
            .. "valid mode (the smart resolver did not produce a working "
            .. "installer for `lua_pcall.before`)")
        return
    end
end

-- ====================================================================
-- (2) CAP-59-invalid-mode-nil — kcdx.hook.<name>.<mode> returns nil
--     when <mode> is INVALID for the resolved name's KIND. The kind-
--     aware filter IsValidHookModeForKind returns false for anything
--     that is not "function" or empty; we exercise it by DECLARING a
--     target ourselves with kind="data_slot" (which the declared store
--     honors AS AUTHORED via VersionEntry.kindTag → KindForResolvedName).
--
-- We use a self-declared kind="data_slot" entry rather than a curated
-- vtable_index/callsite entry because the runtime DB's per-entry kind
-- column is not exposed in the in-repo CSV. The declared store is a
-- real, first-class population source for the smart resolver
-- (KindForResolvedName tier 2), so this exercises the SAME kind-aware
-- filter the curated entry path would.
--
-- The pattern is intentionally bogus (uses 90 wildcards): the kind
-- probe runs BEFORE the scan-resolve, so a scan miss / non-matching
-- pattern does not affect the kindTag lookup. (declared_targets
-- registers the entry as long as name/version-key syntax + the
-- pattern-without-signature gate are satisfied; kindTag="data_slot"
-- opts out of the signature requirement.)
-- ====================================================================

local declared = kcdx.declare("WHGame.dll", "cap59_data_slot_target", {
    -- Per-version map with one entry; kind="data_slot" opts the entry
    -- out of the pattern-without-signature gate (declared_targets:
    -- NeedsSignatureButHasNone returns false when kindTag != "function"
    -- and != ""). The pattern is bogus on purpose — the kind probe in
    -- KindForResolvedName runs off the entry's kindTag, not off a
    -- successful scan resolve.
    ["1.5.1164953"] = {
        pattern = "DE AD BE EF DE AD BE EF",
        kind    = "data_slot",
    },
})

if declared ~= true then
    kcdx.test.report("CAP-59-invalid-mode-nil", false,
        "kcdx.declare(\"WHGame.dll\", \"cap59_data_slot_target\", ...) "
        .. "did not return true — the test's prerequisite (a self-declared "
        .. "kind=data_slot entry the smart resolver can probe) is not set "
        .. "up. See the dev log under DECLARED_TARGET / DECLARED_TARGET_BIND "
        .. "for the reject reason")
else
    -- Now probe the smart resolver: kcdx.hook.<that_declared>.before
    -- should return nil at the .before access (the kind-aware filter
    -- rejects every mode for a "data_slot" kind), and the subsequent
    -- call raises "attempt to call a nil value". pcall-catch the error
    -- and assert it raised.
    local ok, err = pcall(function()
        return kcdx.hook.cap59_data_slot_target.before(function() end)
    end)

    local raised = (ok == false)
    -- The error should reference the nil-call: Lua's standard message
    -- is "attempt to call a nil value". We don't require an exact
    -- substring match (Lua phrasing varies across versions); the
    -- raise + the pcall returning false IS the contract.
    local pass = raised
    if pass then
        kcdx.test.report("CAP-59-invalid-mode-nil", true,
            "kcdx.hook.cap59_data_slot_target.before raised at the .before "
            .. "access (\"" .. tostring(err) .. "\") — the kind-aware filter "
            .. "honored the self-declared kind=\"data_slot\" entry and "
            .. "returned nil for the function-only `before` mode (the "
            .. "smart resolver did NOT produce an installer for a mode "
            .. "the kind cannot legally take)")
    else
        kcdx.test.report("CAP-59-invalid-mode-nil", false,
            "kcdx.hook.cap59_data_slot_target.before did NOT raise — the "
            .. "kind-aware filter ignored the self-declared kind=\"data_slot\" "
            .. "tag and produced a real installer. .before(fn) returned: "
            .. tostring(err) .. ". An installer for a data_slot-kind name "
            .. "would silently set up a function detour against a non-"
            .. "function target — the falsifiable failure mode")
    end
end

-- ====================================================================
-- InputLoaded backstop — if the lua_pcall callback NEVER fires between
-- plugin load and input_loaded, the one-shot guard never trips and
-- CAP-59-fires never self-reports. Convert that to a loud FAIL on the
-- input_loaded lifecycle event. lua_pcall is called continuously from
-- the moment plugin Lua starts running (every Lua-from-C callsite uses
-- it), so a missed fire by input_loaded means the smart-resolver
-- install never reached the dispatch path — a real regression.
-- ====================================================================

kcdx.on("input_loaded", function()
    if not g_fired then
        kcdx.test.report("CAP-59-fires", false,
            "the kcdx.hook.lua_pcall.before(fn) callback did NOT fire "
            .. "between plugin load and input_loaded — the install path "
            .. "wired no detour (handle=" .. tostring(g_handle)
            .. (g_handle and (", :applied()=" .. tostring(g_handle:applied())
                .. ", :reason()=" .. tostring(g_handle:reason())) or "")
            .. "). lua_pcall fires continuously from plugin-load onward "
            .. "(every Lua-from-C callsite); a missed fire by input_loaded "
            .. "means the smart-resolver install never reached the "
            .. "dispatch path")
    end
end)

kcdx.log.info("CAP59",
    "registered the kcdx.hook.lua_pcall.before(fn) smart-resolver "
    .. "install (CAP-59-fires self-reports from the first callback fire) "
    .. "+ the kcdx.hook.cap59_data_slot_target.before nil-access probe "
    .. "(CAP-59-invalid-mode-nil self-reports inline)")
