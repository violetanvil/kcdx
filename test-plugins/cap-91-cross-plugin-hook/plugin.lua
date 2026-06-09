-- CAP-91 plugin.lua — hook a function BY ITS kcdx.functions.* reference value.
--
-- The extensibility keystone: a kcdx.hook sub-verb accepts a kcdx.functions.*
-- REFERENCE VALUE as its target (2nd positional), not only a name string. The
-- engine dispatches by arg-2 type — a reference userdata yields its resolved
-- name + carried signature, routed through the same name path a string target
-- takes. One author hooks another's function BY NAME with no disassembly: the
-- reference carries the address AND the ABI, so the consumer writes zero hex.
--
-- Two falsifiable rows:
--   (1) a GAME-DLL reference value installs AND fires (the wired half);
--   (2) a declare-only PLUGIN reference is rejected predictably (no address to
--       fire on yet — the binder must not fake-install a never-firing hook).

-- ====================================================================
-- (1) cap-91-game-ref-hook-fires — hook a GAME-DLL reference value; it fires.
-- ====================================================================
local g_fired  = false   -- one-shot self-report guard
local g_handle = nil      -- captured from the install path

do
    -- The target is a REFERENCE VALUE, not a string: kcdx.functions.WHGame
    -- .lua_pcall resolves to a function-reference userdata carrying the
    -- canonical name + the verified ABI. Passed as the 2nd positional, the
    -- engine reads the reference and routes it through the name path.
    local ref = kcdx.functions.WHGame.lua_pcall
    if ref == nil then
        kcdx.test.report("cap-91-game-ref-hook-fires", false,
            "kcdx.functions.WHGame.lua_pcall is nil — the function-reference "
            .. "namespace did not mint a reference; cannot hook by value")
    else
        local h, err = kcdx.hook.before("WHGame.dll", ref,
            function(L, nargs, nresults, errfunc)
                if g_fired then return end       -- one-shot guard
                g_fired = true
                kcdx.test.report("cap-91-game-ref-hook-fires", true,
                    "kcdx.hook.before(\"WHGame.dll\", "
                    .. "kcdx.functions.WHGame.lua_pcall, fn) installed AND fired "
                    .. "— the engine dispatched the target by arg-2 type, read "
                    .. "the GAME reference's resolved name + verified ABI, wired "
                    .. "the detour, and the callback received its first fire "
                    .. "(hooked BY REFERENCE VALUE, zero hex)")
            end,
            { name = "cap91_game_ref_hook" })
        g_handle = h
        if g_handle == nil then
            kcdx.test.report("cap-91-game-ref-hook-fires", false,
                "kcdx.hook.before on the GAME reference value returned nil at "
                .. "registration: " .. tostring(err) .. " — the binder did not "
                .. "accept the kcdx.functions.* reference as a target (arg-2 "
                .. "type dispatch broken, or the reference's name did not "
                .. "resolve)")
        end
    end
end

-- ====================================================================
-- (2) cap-91-plugin-ref-hook-shape — a declare-only PLUGIN reference is
--     rejected predictably (no address to fire on yet).
--
-- A kcdx.dll.declare'd plugin function carries a signature but NO address until
-- a PDB / C-export backs it (the address path is a later additive step). A
-- callback hook NEEDS an address to detour — so the binder must REJECT a hook
-- on a declare-only plugin reference, never fake-install a hook that can never
-- fire. The reject route: the plugin reference's name ("DeclaredFn") is not a
-- curated/address-resolvable name, so the binder's name-resolution finds no
-- address and rejects with a teaching error.
-- ====================================================================
do
    local PLUGIN_NS = "a.test"
    local DECL_FN   = "DeclaredFn"
    -- Declare the function (signature only; no address — the declare store does
    -- not carry an address).
    local ok = kcdx.dll.declare(PLUGIN_NS, {
        [DECL_FN] = { signature = "bool (ptr self)" },
    })
    if ok ~= true then
        kcdx.test.report("cap-91-plugin-ref-hook-shape", false,
            "kcdx.dll.declare(\"" .. PLUGIN_NS .. "\", {...}) returned "
            .. tostring(ok) .. " — the declaration prerequisite was rejected")
    else
        local ref = kcdx.functions[PLUGIN_NS][DECL_FN]
        if ref == nil then
            kcdx.test.report("cap-91-plugin-ref-hook-shape", false,
                "kcdx.functions[\"" .. PLUGIN_NS .. "\"]." .. DECL_FN
                .. " is nil — declare did not populate the namespace, so there "
                .. "is no plugin reference to test the hook-shape against")
        else
            -- Hook the declare-only plugin reference. It has a signature but no
            -- address (has_address=false) — the binder must reject (no address
            -- to detour), not return a live handle for a hook that can never
            -- fire.
            local h, err = kcdx.hook.before("a.test", ref,
                function(self) end, { name = "cap91_plugin_ref_hook" })
            -- PASS: the binder rejected (h == nil + a teaching error). A
            -- declare-only plugin reference has no resolvable address, so a
            -- callback hook on it cannot be installed.
            local pass = (h == nil) and (type(err) == "string")
            if pass then
                kcdx.test.report("cap-91-plugin-ref-hook-shape", true,
                    "a callback hook on a declare-only PLUGIN reference "
                    .. "(signature, no address yet) was REJECTED with a "
                    .. "teaching error (\"" .. tostring(err) .. "\") — the "
                    .. "binder did not fake-install a hook that can never fire; "
                    .. "the plugin-function ADDRESS path (PDB / C-export) is a "
                    .. "later additive step")
            else
                kcdx.test.report("cap-91-plugin-ref-hook-shape", false,
                    "kcdx.hook.before on a declare-only plugin reference "
                    .. "returned h=" .. tostring(h) .. " err=" .. tostring(err)
                    .. " — a live handle here claims an install with NO address "
                    .. "to fire on (a silent never-firing hook), the falsifiable "
                    .. "failure mode. Until a PDB/C-export backs the declared "
                    .. "function, a callback hook on it must be rejected")
            end
        end
    end
end

-- ====================================================================
-- InputLoaded backstop for cap-91-game-ref-hook-fires — if the lua_pcall
-- callback never fires by input_loaded, the one-shot guard never trips and the
-- row never self-reports. Convert that to a loud FAIL. lua_pcall fires
-- continuously from plugin-load onward, so a missed fire is a real regression
-- in the reference-value install path.
-- ====================================================================
kcdx.on("input_loaded", function()
    if not g_fired then
        kcdx.test.report("cap-91-game-ref-hook-fires", false,
            "the kcdx.hook.before(.., kcdx.functions.WHGame.lua_pcall, fn) "
            .. "callback did NOT fire between plugin load and input_loaded — "
            .. "the reference-value install wired no detour (handle="
            .. tostring(g_handle)
            .. (g_handle and (", :applied()=" .. tostring(g_handle:applied())
                .. ", :reason()=" .. tostring(g_handle:reason())) or "")
            .. "). lua_pcall fires continuously from plugin-load onward; a "
            .. "missed fire means the reference-value target was accepted but "
            .. "never reached the dispatch path")
    end
end)

kcdx.log.info("CAP91",
    "registered the reference-value hook keystone: a GAME reference "
    .. "(kcdx.functions.WHGame.lua_pcall) hook that fires + the honest "
    .. "rejection of a declare-only PLUGIN reference (no address yet)")
