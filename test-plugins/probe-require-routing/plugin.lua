-- PROBE plugin.lua (THROWAWAY) — answers ONE checkable unknown:
--   Does require("helper") resolve and load the SIBLING helper.lua in
--   this plugin's own folder, on CryEngine's shared lua_State?
--
-- THEORY-INDEPENDENT (.claude/rules/results-driven.md): we log the RAW
-- GROUND TRUTH first — the actual type of `package`, the actual
-- package.path string, the actual type of `require`, the actual
-- searcher count — BEFORE attempting require. We do NOT test a theory
-- about what package.path "should" be; we observe what it IS. Then we
-- attempt require inside pcall so a failure is captured, not fatal, and
-- log the FULL error string verbatim either way.
--
-- The DIAGNOSTIC LOG (category REQUIRE_PROBE) is the deliverable; the
-- PASS/FAIL is secondary. Outcome -> meaning map is in the deliverable
-- writeup, not here (the log carries the raw facts; interpretation is the
-- user's after launch).

local CAT = "REQUIRE_PROBE"

-- ----- Ground-truth facts, logged BEFORE any require attempt. -----

-- (1) Does the global `package` table exist, and of what type?
local package_type = type(package)
kcdx.log.info(CAT, "package type = " .. package_type)

-- (2) The RAW package.path value (the Lua-file searcher's search string),
--     or the literal "nil" if absent. tostring() so a nil/non-string is
--     still printed, not an error.
local package_path
if package_type == "table" then
    package_path = tostring(package.path)
else
    package_path = "(package is not a table)"
end
kcdx.log.info(CAT, "package.path = " .. package_path)

-- (3) Does `require` exist as a function?
kcdx.log.info(CAT, "require type = " .. type(require))

-- (4) The Lua-file/C searcher chain. Lua 5.1 names it package.loaders
--     (5.2+ renamed it package.searchers). Log which exists and how many
--     entries — a live file searcher is what makes require("helper")
--     able to find a .lua at all.
if package_type == "table" then
    local loaders = package.loaders     -- Lua 5.1 name
    local searchers = package.searchers -- 5.2+ name (likely nil on 5.1)
    local function count(t)
        if type(t) ~= "table" then return "(not a table)" end
        local n = 0
        for _ in pairs(t) do n = n + 1 end
        return tostring(n)
    end
    kcdx.log.info(CAT, "package.loaders type = " .. type(loaders)
        .. ", count = " .. count(loaders))
    kcdx.log.info(CAT, "package.searchers type = " .. type(searchers)
        .. ", count = " .. count(searchers))
else
    kcdx.log.info(CAT, "package.loaders / package.searchers: "
        .. "package is not a table — cannot inspect")
end

-- ----- The attempt: require("helper"), failure captured not fatal. -----

local ok, result = pcall(require, "helper")

if ok then
    -- require returned WITHOUT error. Inspect what it returned: did we
    -- actually get the sibling module's sentinel table back?
    local got_sentinel = type(result) == "table"
        and result.loaded == true
        and result.marker == "PROBE_HELPER_OK"
    kcdx.log.info(CAT, "require('helper') returned ok; result type = "
        .. type(result)
        .. ", marker = "
        .. (type(result) == "table" and tostring(result.marker) or "(not a table)"))

    if got_sentinel then
        -- The sibling .lua resolved, loaded, and returned its sentinel.
        kcdx.test.report("PROBE-require-sibling", true,
            "require('helper') returned the sibling sentinel "
            .. "(marker=PROBE_HELPER_OK) — native require resolved a "
            .. "sibling .lua. package.path was: " .. package_path)
    else
        -- require succeeded but did NOT give us our sibling module — it
        -- resolved SOMETHING ELSE named "helper" (e.g. another plugin's,
        -- or a cached/preload entry). FAIL: the sibling did not load.
        kcdx.test.report("PROBE-require-sibling", false,
            "require('helper') returned ok but NOT our sibling sentinel "
            .. "(got type=" .. type(result) .. ", marker="
            .. (type(result) == "table" and tostring(result.marker)
                or "(not a table)")
            .. ") — a DIFFERENT 'helper' resolved, not this plugin's file")
    end
else
    -- require ERRORED. `result` is the FULL error string — log it
    -- verbatim; this is the load-bearing diagnostic (does the message
    -- say "module 'helper' not found", and does it list the paths it
    -- searched?).
    local err = tostring(result)
    kcdx.log.info(CAT, "require('helper') ERRORED: " .. err)
    kcdx.test.report("PROBE-require-sibling", false,
        "require('helper') errored: " .. err)
end
