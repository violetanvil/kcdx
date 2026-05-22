-- CAP-22 plugin.lua — installs the kcdx.hook mode="callsite" redirects the
-- companion DLL verifies on InputLoaded. This is the surface under test:
-- redirecting ONE specific call instruction (its E8 rel32 displacement) so
-- only that caller is affected; every other caller of the same callee is
-- untouched.
--
-- The DLL exposes, per redirected caller, the rva-locator STRING of the
-- exact E8 call-to-Helper site inside that caller's body
-- (kcdx.cap22.site_*() -> "cap-22.dll @ rva 0x..."). We pass it as
-- target_callsite = { rva = <that string> }. mode = "callsite" is the
-- SCOPE selector; the behavior (before/after/around/replace) is attached
-- under its own key, and operates on the CALLED function's ABI (Helper:
-- int Cap22_Helper(int) -> signature "i32 (i32 x)").
--
-- The DLL owns the value assertions on InputLoaded (after ApplyZone),
-- including the two isolation checks: the control caller of the SAME
-- Helper, and a direct Helper call, must both be UNAFFECTED.

local cap22 = kcdx.cap22   -- DLL-registered call-site rva-locator accessors
local SIG = "i32 (i32 x)"  -- the CALLED function (Helper) ABI

-- CAP-22-before: redirect the call site; massage Helper's arg (x -> x+1).
-- Helper still runs (from this site) with the changed arg.
kcdx.hook{
    name            = "cap22_before",
    mode            = "callsite",
    target_callsite = { rva = cap22.site_before() },
    signature       = SIG,
    before          = function(x) return x + 1 end,
}

-- CAP-22-after: redirect the call site; transform Helper's return (+1000).
kcdx.hook{
    name            = "cap22_after",
    mode            = "callsite",
    target_callsite = { rva = cap22.site_after() },
    signature       = SIG,
    after           = function(ret) return ret + 1000 end,
}

-- CAP-22-around: redirect the call site; wrap the call — call Helper, then
-- double the result.
kcdx.hook{
    name            = "cap22_around",
    mode            = "callsite",
    target_callsite = { rva = cap22.site_around() },
    signature       = SIG,
    around          = function(orig, x) return 2 * orig(x) end,
}

-- CAP-22-replace: redirect the call site; Helper is NOT called from this
-- site — return a constant.
kcdx.hook{
    name            = "cap22_replace",
    mode            = "callsite",
    target_callsite = { rva = cap22.site_replace() },
    signature       = SIG,
    replace         = function(x) return 42 end,
}

kcdx.log.info("CAP22", "installed all cap-22 callsite redirects "
    .. "(before/after/around/replace on four distinct E8 sites; "
    .. "control caller + direct Helper left untouched)")
