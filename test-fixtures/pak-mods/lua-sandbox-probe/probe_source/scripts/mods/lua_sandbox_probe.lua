-- kcdx sandbox probe — confirms what pak-mod Lua can reach.
-- Every line is prefixed [KCDX_PROBE] so it's grep-friendly out of kcd.log.

System.LogAlways("[KCDX_PROBE] === pak-mod Lua sandbox probe starting ===")
System.LogAlways("[KCDX_PROBE] _VERSION = " .. tostring(_VERSION))

-- core globals: just print what's there
System.LogAlways("[KCDX_PROBE] _G.package        = " .. tostring(_G.package))
System.LogAlways("[KCDX_PROBE] _G.debug          = " .. tostring(_G.debug))
System.LogAlways("[KCDX_PROBE] _G.os             = " .. tostring(_G.os))
System.LogAlways("[KCDX_PROBE] _G.io             = " .. tostring(_G.io))
System.LogAlways("[KCDX_PROBE] _G.loadstring     = " .. tostring(_G.loadstring))
System.LogAlways("[KCDX_PROBE] _G.loadfile       = " .. tostring(_G.loadfile))
System.LogAlways("[KCDX_PROBE] _G.dofile         = " .. tostring(_G.dofile))
System.LogAlways("[KCDX_PROBE] _G.System         = " .. tostring(_G.System))
System.LogAlways("[KCDX_PROBE] _G.System.LogAlways = " .. tostring(_G.System and _G.System.LogAlways))

-- package fields, if present
if _G.package then
    System.LogAlways("[KCDX_PROBE] package.loadlib   = " .. tostring(_G.package.loadlib))
    System.LogAlways("[KCDX_PROBE] package.cpath     = " .. tostring(_G.package.cpath))
    System.LogAlways("[KCDX_PROBE] package.path      = " .. tostring(_G.package.path))
    System.LogAlways("[KCDX_PROBE] package.loaders   = " .. tostring(_G.package.loaders))
    System.LogAlways("[KCDX_PROBE] package.preload   = " .. tostring(_G.package.preload))
else
    System.LogAlways("[KCDX_PROBE] package missing — pak sandbox stripped it")
end

-- debug fields, if present
if _G.debug then
    System.LogAlways("[KCDX_PROBE] debug.getregistry = " .. tostring(_G.debug.getregistry))
    System.LogAlways("[KCDX_PROBE] debug.getinfo     = " .. tostring(_G.debug.getinfo))
    System.LogAlways("[KCDX_PROBE] debug.sethook     = " .. tostring(_G.debug.sethook))
    System.LogAlways("[KCDX_PROBE] debug.getupvalue  = " .. tostring(_G.debug.getupvalue))
else
    System.LogAlways("[KCDX_PROBE] debug missing — pak sandbox stripped it")
end

-- os fields: dump uses pairs but we want known names too
if _G.os then
    System.LogAlways("[KCDX_PROBE] os.execute        = " .. tostring(_G.os.execute))
    System.LogAlways("[KCDX_PROBE] os.remove         = " .. tostring(_G.os.remove))
    System.LogAlways("[KCDX_PROBE] os.getenv         = " .. tostring(_G.os.getenv))
    System.LogAlways("[KCDX_PROBE] os.clock          = " .. tostring(_G.os.clock))
    System.LogAlways("[KCDX_PROBE] os.time           = " .. tostring(_G.os.time))
end

-- THE load-bearing test: can pak Lua actually CALL package.loadlib?
-- The dump shows package.loadlib exists in the console-Lua context; this
-- proves whether it's also reachable+invocable from pak Lua. Wrap in
-- pcall — if the sandbox blocks the call (returns nil + error), pcall
-- catches it cleanly.
if _G.package and _G.package.loadlib then
    local ok, result, errmsg = pcall(_G.package.loadlib, "kernel32.dll", "GetProcAddress")
    System.LogAlways("[KCDX_PROBE] loadlib pcall ok       = " .. tostring(ok))
    System.LogAlways("[KCDX_PROBE] loadlib pcall result   = " .. tostring(result))
    System.LogAlways("[KCDX_PROBE] loadlib pcall errmsg   = " .. tostring(errmsg))
else
    System.LogAlways("[KCDX_PROBE] package.loadlib unreachable, cannot test invocation")
end

-- Compile-eval test: can pak Lua execute arbitrary string-encoded code?
-- (loadstring is the classic Lua 5.1 way; load() is the 5.2+ replacement.)
if _G.loadstring then
    local fn, errmsg = _G.loadstring("return 42")
    if fn then
        local ok, val = pcall(fn)
        System.LogAlways("[KCDX_PROBE] loadstring works, return = " .. tostring(val))
    else
        System.LogAlways("[KCDX_PROBE] loadstring failed to compile: " .. tostring(errmsg))
    end
else
    System.LogAlways("[KCDX_PROBE] loadstring missing — pak sandbox blocked code compilation")
end

System.LogAlways("[KCDX_PROBE] === probe complete ===")
