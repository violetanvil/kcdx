-- CAP-59 plugin.lua — kcdx.hook name-resolved sub-verb regression.
--
-- The sub-verb shape on hooks:
--
--   kcdx.hook.before(module, target, callback)
--                    ^       ^
--                    |       the function NAME — resolved via self > engine >
--                    |       other (a bare seed name hits the engine tier).
--                    |
--                    the required module positional.
--
-- This plugin exercises (claim 1) a NAME-resolved before-hook installs AND
-- fires, and (claim 2) a hook on a non-function-kind target does NOT apply
-- (the kind mismatch surfaces — a function detour cannot legally install on a
-- data_slot-kind target).

-- ====================================================================
-- (1) CAP-59-fires — kcdx.hook.before("WHGame.dll", "lua_pcall", fn) on a
--     curated engine-seed entry installs AND the callback fires.
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
-- The NAME path: a bare seed name as the target positional walks
-- self > engine > other; the bare `lua_pcall` hits the engine seed and
-- the name carries the address AND verified signature
-- ("i32 (ptr L, i32 nargs, i32 nresults, i32 errfunc)") — no hand-written
-- ABI. (cap-03 also hooks a curated name by the before sub-verb; cap-59
-- proves the FIRE end-to-end on a continuously-called leaf.)
--
-- FALSIFIABLE: if the name fails to resolve, the before sub-verb returns
-- (nil, err) → no callback closure to fire → PENDING (caught by the
-- InputLoaded backstop below as a loud FAIL). If install succeeds but the
-- detour is not wired → no fire → backstop FAIL.
-- ====================================================================

local g_fired   = false  -- guards the one-shot self-report
local g_handle  = nil    -- captured from the install path; nil if before() failed

do
    -- The name-resolved sub-verb: kcdx.hook.before resolves the bare
    -- engine-seed name and carries its verified ABI. Returns the handle
    -- (or (nil, err) on a bad call).
    local h, err = kcdx.hook.before("WHGame.dll", "lua_pcall",
        function(L, nargs, nresults, errfunc)
            -- L is the lua_State*; the other args come from lua_pcall's
            -- signature. We don't read them; the callback's job is to fire
            -- once and self-report.
            if g_fired then return end       -- one-shot guard
            g_fired = true
            kcdx.test.report("CAP-59-fires", true,
                "kcdx.hook.before(\"WHGame.dll\", \"lua_pcall\", fn) installed "
                .. "AND fired — the name path resolved the bare engine-seed "
                .. "name (self > engine > other walk hit the engine tier), "
                .. "carried its verified ABI, wired the detour, and the "
                .. "callback received its first fire on the next lua_pcall call")
        end,
        { name = "cap59_fires" })

    g_handle = h
    if g_handle == nil then
        kcdx.test.report("CAP-59-fires", false,
            "kcdx.hook.before(\"WHGame.dll\", \"lua_pcall\", fn) returned nil "
            .. "at registration: " .. tostring(err) .. " — the name path "
            .. "rejected a known-good curated engine-seed name + verified ABI")
        return
    end
end

-- ====================================================================
-- (2) CAP-59-no-abi-rejected — a hook on a non-function-kind target is
--     REJECTED. We DECLARE a target ourselves with kind="data_slot" (which
--     the declared store honors AS AUTHORED). A data_slot kind carries NO
--     signature (it opts out of the pattern-without-signature gate), and a
--     function detour NEEDS an ABI to marshal — so the before sub-verb
--     rejects it SYNCHRONOUSLY with a "no signature / needs an ABI" teaching
--     error. This is the kind mismatch surfacing through the irreducible-
--     signature requirement: a non-function target cannot legally take a
--     function hook because it has no ABI to marshal.
--
-- The pattern is intentionally bogus (DE AD BE EF...): the entry registers on
-- name/version-key syntax + the kind-opts-out-of-signature gate, regardless of
-- whether the bogus pattern would scan-match.
-- ====================================================================

local declared = kcdx.declare("WHGame.dll", "cap59_data_slot_target", {
    -- Per-version map with one entry; kind="data_slot" opts the entry out of
    -- the pattern-without-signature gate (declared_targets:
    -- NeedsSignatureButHasNone returns false when kindTag != "function" and
    -- != ""), so the entry carries NO signature.
    ["1.5.1164953"] = {
        pattern = "DE AD BE EF DE AD BE EF",
        kind    = "data_slot",
    },
})

if declared ~= true then
    kcdx.test.report("CAP-59-no-abi-rejected", false,
        "kcdx.declare(\"WHGame.dll\", \"cap59_data_slot_target\", ...) "
        .. "did not return true — the test's prerequisite (a self-declared "
        .. "kind=data_slot entry with no signature) is not set up. See the "
        .. "dev log under DECLARED_TARGET / DECLARED_TARGET_BIND for the "
        .. "reject reason")
else
    -- A function hook on the no-signature data_slot target: the before
    -- sub-verb resolves the name (declared-store hit) but finds no ABI, so it
    -- REJECTS synchronously with (nil, err) naming the missing signature. A
    -- non-function-kind target cannot legally take a function detour.
    local h, err = kcdx.hook.before("WHGame.dll", "cap59_data_slot_target",
        function() end, { name = "cap59_data_slot_hook" })

    local errStr = tostring(err)
    -- The teaching error names the missing ABI (the load-bearing contract);
    -- the exact prose can drift, so assert the stable "no signature" / "ABI"
    -- substrings rather than the full message.
    local saysNoAbi =
        errStr:find("no signature", 1, true) ~= nil
        or errStr:find("needs an ABI", 1, true) ~= nil
        or errStr:lower():find("signature", 1, true) ~= nil
    local pass = (h == nil) and (type(err) == "string") and saysNoAbi
    if pass then
        kcdx.test.report("CAP-59-no-abi-rejected", true,
            "kcdx.hook.before on the kind=\"data_slot\" target was REJECTED "
            .. "synchronously (\"" .. errStr .. "\") — a non-function-kind "
            .. "target carries no ABI, so a function detour cannot marshal it; "
            .. "the irreducible-signature requirement surfaces the kind "
            .. "mismatch at the binder")
    else
        kcdx.test.report("CAP-59-no-abi-rejected", false,
            "kcdx.hook.before on the data_slot target did NOT reject — got "
            .. "h=" .. tostring(h) .. " err=" .. errStr .. ". A function "
            .. "detour installed against a non-function (no-ABI) target would "
            .. "silently mis-marshal — the falsifiable failure mode")
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
            "the kcdx.hook.before(\"WHGame.dll\", \"lua_pcall\", fn) callback "
            .. "did NOT fire between plugin load and input_loaded — the install "
            .. "path wired no detour (handle=" .. tostring(g_handle)
            .. (g_handle and (", :applied()=" .. tostring(g_handle:applied())
                .. ", :reason()=" .. tostring(g_handle:reason())) or "")
            .. "). lua_pcall fires continuously from plugin-load onward "
            .. "(every Lua-from-C callsite); a missed fire by input_loaded "
            .. "means the name-resolved install never reached the "
            .. "dispatch path")
    end
end)

kcdx.log.info("CAP59",
    "registered the kcdx.hook.before(\"WHGame.dll\", \"lua_pcall\", fn) "
    .. "name-resolved install (CAP-59-fires self-reports from the first "
    .. "callback fire) + the data_slot no-ABI rejection probe "
    .. "(CAP-59-no-abi-rejected self-reports inline)")
