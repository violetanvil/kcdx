-- CAP-102 sibling Lua plugin — the cross-language half of the C++
-- kcdxBehaviorInterface test. The ONE behavior registry serves both languages;
-- this plugin runs at its MAIN stop (plugin.lua, the first-tick Lua wave) and:
--
--   1. DECLARES two behaviors the C++ plugin consumes:
--      - lua_togglable (bool, default false, WITH a revert) — the C++ plugin
--        Gets a handle, toggles it post-load, and asserts the old handle is
--        generation-checked Stale.
--      - lua_consumed (int, default 0, WITH a revert) — the C++ plugin SETs it
--        to 99 at its main stop (a LUA-DECLARED behavior set from C++).
--
--   2. SETS two C++-DECLARED behaviors (declared by the C++ plugin at its early
--      stop) — proving a C++-declared behavior is settable from Lua:
--      - cpp_scalar = true  (the C++ implementation fires with true at the
--        boundary; the C++ plugin asserts it).
--      - cpp_crosslang = 42 (the C++ implementation fires with 42).
--
-- All four acts are LOAD-TIME (declares + sets at the main stop), so they run at
-- the top level of plugin.lua. This plugin reports ONE row of its own (its sets
-- + declares executed without error); the value-effect assertions live in the
-- C++ plugin (it owns the boundary observation). The own-row is falsifiable: it
-- FAILs if any declare/set raised.
--
-- Engine-derived stamp prefix: ts.cap_102_cpp_behavior_lua.
-- The C++ plugin's prefix: ts.cap_102_cpp_behavior.

local OWN_PREFIX = "ts.cap_102_cpp_behavior_lua."
local CPP_PREFIX = "ts.cap_102_cpp_behavior."

local errors = {}  -- collected failure strings; empty == PASS

-- The value lua_consumed's implementation received when the C++ plugin SET it
-- (the value-effect half of C++→Lua parity). The C++ plugin sets it to 99 at
-- its main stop (a post-load toggle on this revert declarer) → the engine fires
-- this implementation inline with the new value BEFORE input_loaded. Recorded
-- here, asserted at input_loaded (post-boundary) so the C++-side Set is observed
-- to have driven this Lua-declared implementation — not merely resolved/recorded.
local lua_consumed_impl_ran   = false
local lua_consumed_impl_value = nil

local function guard(label, fn)
    local ok, err = pcall(fn)
    if not ok then
        errors[#errors + 1] = label .. ": " .. tostring(err)
    end
end

-- 1. Declare the two behaviors the C++ plugin consumes (both togglable).
guard("declare lua_togglable", function()
    kcdx.behavior.declare("lua_togglable", {
        description    = "a Lua-declared togglable bool (C++ stale-handle test)",
        default        = false,
        implementation = function(value) end,        -- effect not asserted here
        revert         = function(old_value) end,    -- presence makes it togglable
    })
end)

guard("declare lua_consumed", function()
    kcdx.behavior.declare("lua_consumed", {
        description    = "a Lua-declared int the C++ plugin sets to 99",
        default        = 0,
        -- Record the value the C++-side Set drives this implementation with, so
        -- the input_loaded row below can assert the cross-language Set fired the
        -- Lua-declared implementation (the value-effect half of C++→Lua parity).
        implementation = function(value)
            lua_consumed_impl_ran   = true
            lua_consumed_impl_value = value
        end,
        revert         = function(old_value) end,
    })
end)

-- lua_raiser: a togglable bool whose implementation RAISES on the post-load
-- toggle (not on the load-time apply). The C++ plugin mints a value handle on
-- it, then toggles it from C++ — the toggle's implementation raises, so the
-- registry CLEARS the record to unset AND must BUMP the value generation (the
-- get()-answered value changed from the recorded value to the default). The C++
-- stale-handle-on-raise row asserts the old handle goes Stale: without the
-- generation bump on the impl-raise path, the old handle would read as FRESH and
-- dereference the now-default ref — a silent wrong value (the exact defect the
-- generation counter exists to prevent). Set at load so it is APPLIED when the
-- C++ toggle runs (the revert+impl path, not the never-applied path).
guard("declare lua_raiser", function()
    kcdx.behavior.declare("lua_raiser", {
        description    = "Lua-declared togglable bool; impl raises on the toggle",
        default        = false,
        implementation = function(value)
            -- Raise ONLY on the toggle value (true), not the load-time apply
            -- (false) — so the boundary applies it cleanly and it IS applied
            -- when the C++ toggle runs.
            if value == true then error("lua_raiser: deliberate toggle raise") end
        end,
        revert         = function(old_value) end,  -- succeeds
    })
end)

guard("set lua_raiser load", function()
    -- Apply it at load with the non-raising value (false) so the boundary
    -- applies it; the C++ toggle to true is what triggers the impl-raise.
    kcdx.behavior.set("lua_raiser", false)
end)

-- 2. Set the two C++-declared behaviors FROM LUA (the cross-language Set). The
--    C++ plugin declared cpp_scalar / cpp_crosslang at its early stop; they are
--    plugin-tier behaviors settable from THIS main stop. Use the explicit
--    cross-plugin prefixed names (another plugin's behaviors).
guard("set cpp_scalar", function()
    kcdx.behavior.set(CPP_PREFIX .. "cpp_scalar", true)
end)

guard("set cpp_crosslang", function()
    kcdx.behavior.set(CPP_PREFIX .. "cpp_crosslang", 42)
end)

-- Report the own-row at "ready" (the declares + sets are synchronous load-time
-- records; no need to wait for the boundary for THIS plugin's own assertion).
kcdx.on("ready", function()
    local pass = (#errors == 0)
    local reason
    if pass then
        reason = "all cross-language declares + sets executed without error: "
            .. "declared lua_togglable + lua_consumed + lua_raiser (this plugin's "
            .. "tier) and load-set lua_raiser=false, set cpp_scalar=true + "
            .. "cpp_crosslang=42 (the C++ plugin's tier, from Lua) — the "
            .. "value-effect assertions are the C++ plugin's + this plugin's "
            .. "impl-fired row; FAILS if any declare/set raised"
    else
        reason = "one or more cross-language declares/sets RAISED: "
            .. table.concat(errors, " | ")
    end
    kcdx.test.report("CAP-102-lua-sibling-crosslang", pass, reason)
end)

-- The value-effect half of C++→Lua: assert the C++ plugin's Set of this
-- Lua-declared behavior actually DROVE its implementation. The C++ plugin sets
-- lua_consumed=99 at its main stop (kcdxPlugin_PostGameLoad), which runs BEFORE
-- the apply boundary — so the Set RECORDS 99 (a load-window record, lua_consumed
-- is not yet applied) and the apply boundary then invokes this implementation
-- ONCE with the recorded 99. The boundary runs before input_loaded, so by
-- input_loaded (post-boundary) the impl has fired. The C++ plugin's
-- CAP-102-crosslang-cpp-sets-lua row proves the Set RESOLVED + RECORDED (Get
-- reads 99 back); THIS row proves the recorded value DROVE the Lua-declared
-- implementation with 99 — together, the full cross-language Set. FALSIFIABLE:
-- FAILS if the impl never ran (the C++ Set did not reach the Lua-declared
-- implementation) or ran with a value other than 99.
kcdx.on("input_loaded", function()
    local ran  = lua_consumed_impl_ran
    local val  = lua_consumed_impl_value
    local pass = ran and val == 99
    local reason
    if pass then
        reason = "the C++ plugin's Set(lua_consumed=99) DROVE this Lua-declared "
            .. "behavior's implementation: it fired with value=99 (the "
            .. "value-effect half of C++→Lua parity — a C++ Set on a "
            .. "Lua-declared behavior runs the Lua implementation with the "
            .. "C++-supplied value)"
    else
        reason = "the C++ Set on lua_consumed did NOT drive this implementation "
            .. "correctly: impl ran=" .. tostring(ran) .. " value="
            .. tostring(val) .. " (PASS requires ran=true AND value==99)"
    end
    kcdx.test.report("CAP-102-crosslang-cpp-sets-lua-impl-fired", pass, reason)
end)
