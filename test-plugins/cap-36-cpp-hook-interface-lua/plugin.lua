-- CAP-36 sibling Lua plugin — the Lua half of CAP-36-cpp-hook-crosslang.
--
-- Pair of test-plugins/cap-36-cpp-hook-interface/ (the C++ DLL). The C++
-- plugin defines a native stub Cap36_Crosslang(int seed) returning
-- seed+100, registers two kcdx C functions for this Lua plugin to
-- consume:
--
--   kcdx.cap36.addr_crosslang()        -> lightuserdata, the stub VA
--   kcdx.cap36.set_lua_fired(seed)     -> notify the C++ plugin that the
--                                         Lua before-hook fired AND the
--                                         seed value it observed (the
--                                         second-in-chain Lua hook sees
--                                         the post-C++-mutation seed, so
--                                         the seed value is itself a
--                                         falsifiable witness for
--                                         "both entries on one chain").
--
-- Both halves of the chain install a `before` hook on the stub:
--   C++ plugin (load-order priority 30): adds 1 to seed
--   THIS plugin (load-order priority 70): multiplies seed by 2
--   stub then runs: seed + 100
--
-- For input 10 the expected return is ((10+1)*2)+100 = 122. Unique
-- among the firing-pattern alternatives:
--   122 -> both halves fired in load order (PASS)
--   111 -> C++ only (Lua never fired)
--   120 -> Lua only (C++ never fired)
--   121 -> reversed order
--   110 -> no hooks
--
-- The crosslang row itself is REPORTED by the C++ plugin in its
-- PostGameLoad, after it re-invokes Cap36_Crosslang and observes the
-- return value + this plugin's set_lua_fired notification.
--
-- This plugin's dependency on cap_36_cpp_hook_interface (declared in
-- kcdx.toml) forces the C++ plugin to load FIRST so the C functions are
-- registered + reachable when this plugin.lua runs.

local cap36 = kcdx.cap36
if not cap36 or not cap36.addr_crosslang or not cap36.set_lua_fired then
    -- The C++ plugin did not register its Lua surface. Log loudly so a
    -- regression that drops the C-side RegisterFunction calls is
    -- discoverable (the C++ plugin's crosslang row will FAIL anyway —
    -- this log line surfaces the diagnostic next to the cause).
    kcdx.log.error("CAP36_LUA",
        "cap_36_cpp_hook_interface did not register kcdx.cap36.addr_"
        .. "crosslang / .set_lua_fired — sibling Lua plugin cannot "
        .. "install the crosslang chain's Lua half; C++ plugin's "
        .. "CAP-36-cpp-hook-crosslang row will FAIL")
    return
end

local addr = cap36.addr_crosslang()
kcdx.log.info("CAP36_LUA",
    "installing Lua before-hook on Cap36_Crosslang (addr handed over by "
    .. "C++ plugin via kcdx.cap36.addr_crosslang)")

kcdx.hook{
    name      = "cap36_crosslang_lua",
    address   = addr,
    signature = "i32 (i32 seed)",
    before    = function(seed)
        -- Notify the C++ plugin that the Lua callback fired AND the
        -- seed value we observed. The C++ before fires FIRST (priority
        -- 30 < 70) and writes args[0]=seed+1, so this Lua callback
        -- should see seed=11 (proving both entries land on the same
        -- chain — if they ran on separate detours, the Lua side would
        -- see the un-mutated seed=10 from the stub's actual call site).
        cap36.set_lua_fired(seed)
        return seed * 2
    end,
}

kcdx.log.info("CAP36_LUA",
    "Lua before-hook registered (priority 70, fires after C++ priority "
    .. "30); the C++ plugin's PostGameLoad re-invokes the stub and "
    .. "reports CAP-36-cpp-hook-crosslang PASS iff observed=122 + Lua "
    .. "callback fired + seed-in-Lua==11")
