-- CAP-22 plugin.lua — installs the kcdx.hook mode="callsite" redirects the
-- companion DLL verifies on InputLoaded. This is the surface under test:
-- redirecting ONE specific call instruction (its E8 rel32 displacement) so
-- only that caller is affected; every other caller of the same callee is
-- untouched.
--
-- The DLL exposes, per redirected caller, the rva-locator STRING of the
-- exact E8 call-to-Helper site inside that caller's body
-- (kcdx.cap22.site_*() -> "cap-22.dll @ rva 0x..."). We pass it via the
-- kcdx.hook.callsite(module, callsite, mode, callback, [opts]) sub-verb:
-- `callsite` is the target_callsite table { rva = <that string> }, `mode` is
-- the wrapping behavior ("before"/"after"/"around"/"replace"), and the
-- signature (the CALLED function's ABI — Helper: int Cap22_Helper(int) ->
-- "i32 (i32 x)") rides in the trailing [opts] table.
--
-- The DLL owns the value assertions on InputLoaded (after ApplyZone),
-- including the two isolation checks: the control caller of the SAME
-- Helper, and a direct Helper call, must both be UNAFFECTED.

local MOD   = "WHGame.dll"  -- required positional (callsite rva carries its own module)
local cap22 = kcdx.cap22    -- DLL-registered call-site rva-locator accessors
local SIG = "i32 (i32 x)"   -- the CALLED function (Helper) ABI

-- CAP-22-before: redirect the call site; massage Helper's arg (x -> x+1).
-- Helper still runs (from this site) with the changed arg.
kcdx.hook.callsite(MOD, { rva = cap22.site_before() }, "before",
    function(x) return x + 1 end,
    { name = "cap22_before", signature = SIG })

-- CAP-22-after: redirect the call site; transform Helper's return (+1000).
kcdx.hook.callsite(MOD, { rva = cap22.site_after() }, "after",
    function(ret) return ret + 1000 end,
    { name = "cap22_after", signature = SIG })

-- CAP-22-around: redirect the call site; wrap the call — call Helper, then
-- double the result.
kcdx.hook.callsite(MOD, { rva = cap22.site_around() }, "around",
    function(orig, x) return 2 * orig(x) end,
    { name = "cap22_around", signature = SIG })

-- CAP-22-replace: redirect the call site; Helper is NOT called from this
-- site — return a constant.
kcdx.hook.callsite(MOD, { rva = cap22.site_replace() }, "replace",
    function(x) return 42 end,
    { name = "cap22_replace", signature = SIG })

kcdx.log.info("CAP22", "installed all cap-22 callsite redirects "
    .. "(before/after/around/replace on four distinct E8 sites; "
    .. "control caller + direct Helper left untouched)")
