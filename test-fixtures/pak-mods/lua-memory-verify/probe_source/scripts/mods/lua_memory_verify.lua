-- kcdx.memory verification (Phase 5c.7a-rev2 raw-C-API).
-- Output prefixed [KCDX_V] for grep extraction from kcd.log.

-- Defensive: if System.LogAlways throws at top-level pak-init time
-- (because System global isn't ready), the whole script gets dropped
-- silently. Wrap in pcall so we can at least see whether System
-- exists.
local _ok, _err = pcall(function()
    if not System then
        error("System global missing at pak-init time")
    end
    System.LogAlways("[KCDX_V] === verify script loaded ===")
    System.LogAlways("[KCDX_V] script-load: _G.kcdx = " .. tostring(_G.kcdx))
    System.LogAlways("[KCDX_V] script-load: _G.KCDX = " .. tostring(_G.KCDX))
end)
if not _ok then
    -- Try alternate logger if System.LogAlways isn't available.
    if System and System.LogAlways then
        System.LogAlways("[KCDX_V] PRELUDE-PCALL-FAILED: " .. tostring(_err))
    end
    -- Re-raise so CryEngine logs the error its own way; this also
    -- ensures we'd see it in kcd.log under one of CryEngine's
    -- error channels even if System.LogAlways is broken.
    error(_err)
end

-- Phase 5f: TOML hook 'phase5f_lua_test' calls Phase5fTest.Greet
-- when the hooked engine function fires. Register the global here at
-- script-load so it exists before kcdx's first-update-tick (which is
-- when the hook installs); kcdx's dispatcher resolves the name lazily
-- on each fire so even if we'd registered later it would still work.
Phase5fTest = {}
Phase5fTest.fire_count = 0
function Phase5fTest.Greet()
    Phase5fTest.fire_count = Phase5fTest.fire_count + 1
    -- Only log the first few fires so we don't flood kcd.log if the
    -- hook target fires every game tick.
    if Phase5fTest.fire_count <= 3 then
        System.LogAlways("[KCDX_V] Phase5fTest.Greet fired (count="
            .. Phase5fTest.fire_count .. ")")
    end
    return true  -- let the original run
end
System.LogAlways("[KCDX_V] Phase5fTest.Greet registered ("
    .. tostring(Phase5fTest.Greet) .. ")")

-- Mirror cheat mod's lifecycle pattern (verified via reverse-engineering
-- the cheat pak): register a single EventSystemListener function, then
-- dispatch from it into our own per-event methods. The engine does NOT
-- magically call `LuaMemoryVerify:onSystemStarted()` by name; it calls
-- whatever function name we passed to UIAction.RegisterEventSystemListener.

LuaMemoryVerify = {}
LuaMemoryVerify.tested = false

function LuaMemoryVerify:runTests(label)
    if self.tested then return end
    System.LogAlways("[KCDX_V] [" .. label .. "] _G.kcdx = " .. tostring(_G.kcdx))
    if _G.kcdx == nil or _G.kcdx.memory == nil then
        return
    end
    self.tested = true
    System.LogAlways("[KCDX_V] [" .. label .. "] === kcdx.memory available, testing ===")

    local ok, err = pcall(function()
        System.LogAlways("[KCDX_V] kcdx.version = " .. tostring(kcdx.version))

        local base = kcdx.memory.get_module_base_address()
        System.LogAlways("[KCDX_V] WHGame.dll base = 0x"
            .. string.format("%X", base:get_address()))
        System.LogAlways("[KCDX_V] base:is_valid() = " .. tostring(base:is_valid()))

        local mz = base:get_word()
        System.LogAlways("[KCDX_V] base:get_word() = 0x" .. string.format("%X", mz)
            .. (mz == 0x5A4D and "  (correct: MZ)" or "  (UNEXPECTED!)"))

        local pat = "48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0"
        local hit = kcdx.memory.scan_pattern(pat)
        System.LogAlways("[KCDX_V] scan_pattern(outfit) = 0x"
            .. string.format("%X", hit:get_address())
            .. (hit:is_null() and "  (NULL — pattern not found)" or "  (ok)"))

        local heap = kcdx.memory.allocate(64)
        System.LogAlways("[KCDX_V] allocate(64) = 0x"
            .. string.format("%X", heap:get_address())
            .. (heap:is_null() and "  (NULL — alloc failed)" or "  (ok)"))
        if not heap:is_null() then
            heap:set_dword(0xDEADBEEF)
            local r = heap:get_dword()
            System.LogAlways("[KCDX_V] write+read 0xDEADBEEF = 0x"
                .. string.format("%X", r)
                .. (r == 0xDEADBEEF and "  (ok)" or "  (CORRUPTED!)"))
            kcdx.memory.free(heap)
            System.LogAlways("[KCDX_V] free(heap) returned ok")
        end

        -- Chained method calls + arithmetic
        local p = kcdx.memory.pointer(0x1000)
        local q = p:add(0x100):sub(0x40)
        System.LogAlways("[KCDX_V] pointer(0x1000):add(0x100):sub(0x40) = 0x"
            .. string.format("%X", q:get_address())
            .. (q:get_address() == 0x10C0 and "  (ok)" or "  (WRONG)"))

        -- dynamic_hook surface tests (Phase 5c.7b.2).
        System.LogAlways("[KCDX_V] kcdx.memory.dynamic_hook = "
            .. tostring(kcdx.memory.dynamic_hook))

        -- (1) Missing-name should fail cleanly.
        local h1, err1 = kcdx.memory.dynamic_hook({
            target = 0x12345678,
            pre_callback = function() end,
        })
        System.LogAlways("[KCDX_V] dynamic_hook no-name: h="
            .. tostring(h1) .. " err=" .. tostring(err1))

        -- (2) No callbacks should fail cleanly.
        local h2, err2 = kcdx.memory.dynamic_hook({
            name = "test_no_cbs",
            target = 0x12345678,
        })
        System.LogAlways("[KCDX_V] dynamic_hook no-cbs: h="
            .. tostring(h2) .. " err=" .. tostring(err2))

        -- (3) Bogus target (unmapped address) should fail at MinHook stage.
        local h3, err3 = kcdx.memory.dynamic_hook({
            name = "test_bogus_target",
            target = 0x1,
            pre_callback = function() return true end,
        })
        System.LogAlways("[KCDX_V] dynamic_hook bogus-target: h="
            .. tostring(h3) .. " err=" .. tostring(err3))

        -- (4) Valid-target install. NOTE: kcdx.memory.scan_pattern
        -- runs against the LIVE in-memory image, AFTER kcdx has
        -- already applied its first-update-tick patches/hooks. An
        -- AOB written against the pristine binary may no longer
        -- match — that's correct and documented.
        --
        -- For this test we use the outfit-swap AOB as a deliberate
        -- "pattern doesn't match anymore" check: mempatch overwrites
        -- the last 3 bytes of this 16-byte pattern, so the scan
        -- correctly returns null in the post-patch state. This is
        -- not a scan_pattern bug — it's an authoring constraint we
        -- now expose in the docs (see patch_engine.cpp::ScanModuleFirst
        -- comment).
        local outfit_pat = "48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0"
        local outfit_addr = kcdx.memory.scan_pattern(outfit_pat)
        if outfit_addr:is_null() then
            System.LogAlways("[KCDX_V] outfit pattern NULL (expected — "
                .. "mempatch already patched the last 3 bytes of this AOB; "
                .. "scan operates on post-patch live image)")
        else
            System.LogAlways("[KCDX_V] outfit pattern unexpectedly found at 0x"
                .. string.format("%X", outfit_addr:get_address())
                .. " (mempatch outfit-swap apply may have failed?)")
        end

        -- (5) dynamic_call surface tests (Phase 5c.7c).
        System.LogAlways("[KCDX_V] kcdx.memory.dynamic_call = "
            .. tostring(kcdx.memory.dynamic_call))

        -- 5a. Missing target should fail cleanly.
        local c1, cerr1 = kcdx.memory.dynamic_call({
            return_type = "void",
            param_types = {},
        })
        System.LogAlways("[KCDX_V] dynamic_call no-target: c="
            .. tostring(c1) .. " err=" .. tostring(cerr1))

        -- 5b. Build a trampoline against a bogus target. The JIT step
        -- doesn't actually invoke the target — it just emits the code.
        -- Should succeed (returns a callable userdata).
        local c2, cerr2 = kcdx.memory.dynamic_call({
            target      = 0x12345678,
            return_type = "i32",
            param_types = {"i32", "i32"},
        })
        System.LogAlways("[KCDX_V] dynamic_call bogus-target: c="
            .. tostring(c2) .. " err=" .. tostring(cerr2))
        -- Stash so it doesn't GC; don't invoke (would crash on bogus target).
        LuaMemoryVerify.test_call = c2

        -- (6) kcdx.lua.cfunction_address tests (Phase 5c.7d).
        System.LogAlways("[KCDX_V] kcdx.lua = " .. tostring(kcdx.lua))
        System.LogAlways("[KCDX_V] kcdx.lua.cfunction_address = "
            .. tostring(kcdx.lua and kcdx.lua.cfunction_address))

        -- (6.0) Numeric-precision probe. Fires kcdx.lua._probe_numbers
        -- which logs sizeof + push/pull round-trips across the float-/
        -- double-precision boundaries into kcdx-dev.log (LUA.NUMBER_PROBE/*).
        -- Tells us how CryEngine compiled its Lua 5.1 numeric build
        -- BEFORE we pick a fix for cfunction_address's pointer truncation.
        if kcdx.lua._probe_numbers then
            System.LogAlways("[KCDX_V] running kcdx.lua._probe_numbers()...")
            kcdx.lua._probe_numbers()
            System.LogAlways("[KCDX_V] kcdx.lua._probe_numbers() done; "
                .. "see kcdx-dev.log LUA.NUMBER_PROBE/* lines")
        else
            System.LogAlways("[KCDX_V] kcdx.lua._probe_numbers missing — "
                .. "rebuild kcdx.asi")
        end

        -- 6a. Non-cfunction argument should fail cleanly.
        local addr_nil, errl = kcdx.lua.cfunction_address("not a function")
        System.LogAlways("[KCDX_V] cfunction_address non-fn: addr="
            .. tostring(addr_nil) .. " err=" .. tostring(errl))

        -- 6b. Real cfunction: System.LogAlways. cfunction_address now
        -- returns a kcdx.memory.pointer userdata (Phase 5g cleanup,
        -- post-NUMBER_PROBE finding — LUA_NUMBER=float makes integer
        -- VAs lossy). Verify type + that :get_address() reads a value
        -- inside WHGame.dll. (The :get_address() return is still lossy
        -- by design; we accept the float-grid round for diagnostics
        -- but the userdata itself preserves the exact value.)
        local logalways_p = kcdx.lua.cfunction_address(System.LogAlways)
        System.LogAlways("[KCDX_V] cfunction_address(System.LogAlways): "
            .. "type=" .. type(logalways_p)
            .. " tostring=" .. tostring(logalways_p))
        if type(logalways_p) == "userdata" then
            local logalways_addr_lossy = logalways_p:get_address()
            System.LogAlways("[KCDX_V] cfunction_address(System.LogAlways):"
                .. "get_address() = 0x"
                .. string.format("%X", logalways_addr_lossy)
                .. "  (lossy float-grid)")
            local base_p = kcdx.memory.get_module_base_address()
            local base_va = base_p:get_address()
            local in_module = (logalways_addr_lossy >= base_va)
                and (logalways_addr_lossy < base_va + 0x10000000)
            System.LogAlways("[KCDX_V]   address within WHGame.dll? "
                .. tostring(in_module))
        end

        -- 6c. Pure-Lua function: not a cfunction, expect nil.
        local pure = function() return 42 end
        local addr_pure, errp = kcdx.lua.cfunction_address(pure)
        System.LogAlways("[KCDX_V] cfunction_address(pure-lua-fn): addr="
            .. tostring(addr_pure) .. " err=" .. tostring(errp))

        -- (7) kcdxScriptingInterface tests (Phase 5e).
        -- hello-plugin registers kcdx.hello.greet and kcdx.hello.add
        -- during its kcdxPlugin_Load. Verify they're reachable from
        -- pak Lua and produce the expected results.
        System.LogAlways("[KCDX_V] kcdx.hello = " .. tostring(kcdx.hello))
        if kcdx.hello then
            System.LogAlways("[KCDX_V] kcdx.hello.greet = "
                .. tostring(kcdx.hello.greet))
            System.LogAlways("[KCDX_V] kcdx.hello.add = "
                .. tostring(kcdx.hello.add))

            local greet_result = kcdx.hello.greet("Michael")
            System.LogAlways("[KCDX_V] kcdx.hello.greet('Michael') = "
                .. tostring(greet_result))

            local sum = kcdx.hello.add(3, 4)
            System.LogAlways("[KCDX_V] kcdx.hello.add(3, 4) = "
                .. tostring(sum)
                .. (sum == 7 and "  (ok)" or "  (WRONG)"))
        else
            System.LogAlways("[KCDX_V] kcdx.hello NOT registered — "
                .. "is hello-plugin running an updated build?")
        end

        -- (8) End-to-end Phase 5c.7b dispatch demo: hook System.LogAlways
        -- at its C function entry and observe every call. Demonstrates:
        --   - kcdx.lua.cfunction_address resolves a real C function (Phase 5c.7d)
        --   - kcdx.memory.dynamic_hook installs at function entry (Phase 5c.7b)
        --   - dispatch fires the Lua pre-callback (Phase 5c.6/7a)
        --   - re-entrancy guard prevents infinite recursion when the
        --     callback itself calls System.LogAlways (Phase 5g)
        --
        -- We track fire count + last seen message pointer in a global
        -- so the periodic check below can verify the hook fired without
        -- recursing through the dispatch.
        Phase5gDemo = {}
        Phase5gDemo.fires = 0

        -- Hook target choice: kcdx.hello.greet. We control both sides
        -- of this — hello-plugin registers Lua_Greet via Phase 5e's
        -- RegisterFunction, kcdx's scripting_interface installs a
        -- LuaDispatchShim closure into kcdx.hello.greet. The address
        -- cfunction_address returns IS the address the Lua VM invokes
        -- when pak Lua calls kcdx.hello.greet(). No CryEngine
        -- indirection. Direct verification path.
        --
        -- Phase 5g end-to-end test: cfunction_address now returns
        -- a kcdx.memory.pointer userdata which we pass DIRECTLY into
        -- dynamic_hook.target. dynamic_hook already understands
        -- pointer userdata, so the exact VA flows through with no
        -- numeric encoding — bypassing the LUA_NUMBER=float trap that
        -- silently rounded VAs to 16 MB boundaries in earlier runs.
        System.LogAlways("[5gINV STEP-6a] tostring(kcdx.hello.greet) = "
            .. tostring(kcdx.hello.greet))
        System.LogAlways("[5gINV STEP-6a] type(kcdx.hello.greet) = "
            .. type(kcdx.hello.greet))

        local greet_p = kcdx.lua.cfunction_address(kcdx.hello.greet)
        System.LogAlways("[5gINV STEP-6b] cfunction_address returned: "
            .. "type=" .. type(greet_p)
            .. " tostring=" .. tostring(greet_p))
        if type(greet_p) == "userdata" then
            local greet_addr_lossy = greet_p:get_address()
            System.LogAlways(string.format(
                "[5gINV STEP-6b] greet_p:get_address() = 0x%X (lossy)",
                greet_addr_lossy))
        end

        local h, herr = kcdx.memory.dynamic_hook({
            name        = "phase5g_greet_intercept",
            -- Pass the pointer userdata DIRECTLY. dynamic_hook reads
            -- the exact uintptr_t out of the userdata payload — no
            -- precision loss. (Passing greet_p:get_address() would
            -- fail with MH_ERROR_NOT_EXECUTABLE because the int form
            -- is float-grid-rounded to non-executable memory.)
            target      = greet_p,
            return_type = "void",
            param_types = {"ptr"},  -- lua_State* in rcx
            pre_callback = function(retval, L_ptr)
                Phase5gDemo.fires = Phase5gDemo.fires + 1
                if Phase5gDemo.fires <= 3 then
                    System.LogAlways(string.format(
                        "[5gDEMO] kcdx.hello.greet intercept #%d",
                        Phase5gDemo.fires))
                end
                return true
            end,
        })

        if h then
            Phase5gDemo.handle = h
            System.LogAlways("[KCDX_V] (5g demo) hook installed; triggering "
                .. "5 explicit calls to kcdx.hello.greet")
            for trigger_i = 1, 5 do
                local r = kcdx.hello.greet("trigger_" .. trigger_i)
                System.LogAlways("[5gTRIGGER] greet returned: " .. tostring(r))
            end
            System.LogAlways("[KCDX_V] (5g demo) after 5 explicit calls: "
                .. "Phase5gDemo.fires = " .. Phase5gDemo.fires
                .. (Phase5gDemo.fires > 0 and "  (DISPATCH WORKS)"
                                          or "  (dispatch SILENT)"))
        else
            System.LogAlways("[KCDX_V] (5g demo) dynamic_hook FAILED: "
                .. tostring(herr))
        end
    end)
    if not ok then
        System.LogAlways("[KCDX_V] tests THREW: " .. tostring(err))
    end
    System.LogAlways("[KCDX_V] [" .. label .. "] === tests done ===")
end

function LuaMemoryVerify:EventSystemListener(actionName, eventName, argTable)
    -- log every event the first time we see it so we can see which
    -- fire when. After tests run once we stop logging spam.
    if not self.tested then
        System.LogAlways("[KCDX_V] event: " .. tostring(actionName)
            .. " / " .. tostring(eventName))
    end
    if actionName == "System" then
        if eventName == "OnSystemStarted"
            or eventName == "OnLoadingComplete"
            or eventName == "OnGameplayStarted" then
            self:runTests(eventName)
        end
    end
end

-- Register with the engine event bus. This is the bit my earlier
-- verify pak was missing — without this, none of my methods get
-- invoked by the engine.
UIAction.RegisterEventSystemListener(LuaMemoryVerify, "", "", "EventSystemListener")
System.LogAlways("[KCDX_V] registered with UIAction.RegisterEventSystemListener")
