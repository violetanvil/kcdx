-- CAP-47 plugin.lua — leave a crash breadcrumb the engine self-check can find.
--
-- The behavior under test (the per-detour fire ring + the owner-named
-- inventory) is ENGINE machinery; the engine self-reports the row in
-- src/hooks.cpp. This plugin's only job is to make a kcdx.hook detour FIRE at
-- boot so the ring is non-empty and the chain carries a real owner plugin
-- name (ts.cap_47_crash_breadcrumb) — proving Parts 2 + 3 end-to-end.
--
-- Mechanism (the cap-04 / cap-20-dyncall idiom): allocate a tiny int(int)
-- stub via kcdx.code, install a `before` hook on it, then call the stub via
-- kcdx.memory.dynamic_call from kcdx.on("ready") — which fires at InputLoaded,
-- AFTER ApplyZone has installed the hook. The call routes through the stub's
-- MinHook detour -> DispatchPre -> modification_inventory::RecordFire, leaving
-- exactly one breadcrumb. We do NOT mutate the arg or skip the original; the
-- before callback is a pure observer (the breadcrumb is a side effect of the
-- detour firing at all).
--
-- The 9-byte stub (verbatim from cap-04, verified-safe self-contained):
--   +0:  48 89 C8        mov rax, rcx     ; rax = seed (arg in rcx, win64)
--   +3:  48 83 C0 64     add rax, 0x64    ; rax += 100
--   +7:  90              nop
--   +8:  C3              ret
-- => int fn(int seed) -> seed + 100.

local STUB_BYTES = "48 89 C8 48 83 C0 64 90 C3"  -- 9 bytes; mov/add/nop/ret
local STUB_SIZE  = 16    -- > 9, NOP-pads the tail past MinHook's 5-byte patch
local SIG        = "i32 (i32 seed)"

-- Allocate the stub. pool="branch" is REQUIRED: the function-entry detour
-- writes a rel32 jmp that must reach within +/-2 GB of WHGame.dll's .text.
local region, allocErr = kcdx.code{
    name   = "cap47_breadcrumb_stub",
    bytes  = STUB_BYTES,
    size   = STUB_SIZE,
    pool   = "branch",
    export = "stub",   -- bare; engine stamps ts.cap_47_crash_breadcrumb.stub
}
if region == nil then
    kcdx.log.error("CAP47",
        "kcdx.code stub alloc returned nil: " .. tostring(allocErr)
        .. " — no breadcrumb will be left; the engine cap-47 self-report "
        .. "will retry every tick and never PASS")
    return
end

-- Install a `before` hook on the stub's entry. Pure observer: pass the arg
-- through unchanged (return it) so the original `mov/add/ret` runs untouched.
-- The kcdx.code region pointer is the raw target positional; the signature
-- rides in [opts] (a raw VA carries no name-supplied ABI).
local hook = kcdx.hook.before("WHGame.dll", region,  -- kcdx.code pointer at the stub entry (+0)
    function(seed) return seed end,                  -- no-op passthrough
    { name = "cap47_breadcrumb_fire", signature = SIG })

-- At "ready" (InputLoaded — AFTER ApplyZone installs the hook above), CALL the
-- stub so the detour fires and RecordFire writes the breadcrumb. dynamic_call
-- is the Lua-side primitive to invoke a raw target (a kcdx.memory.pointer has
-- no call method); cap-20-dyncall uses the same path.
kcdx.on("ready", function()
    if hook == nil or hook:applied() ~= true then
        kcdx.log.error("CAP47",
            "breadcrumb hook did not apply (applied="
            .. tostring(hook and hook:applied())
            .. ") — calling the stub will not leave a breadcrumb")
    end
    local call = kcdx.memory.dynamic_call{
        target = region, return_type = "i32", param_types = {"i32"},
    }
    if call == nil then
        kcdx.log.error("CAP47",
            "kcdx.memory.dynamic_call returned nil — cannot trigger the "
            .. "breadcrumb hook")
        return
    end
    -- One call -> a DispatchPre RecordFire AND a DispatchPost RecordFire: a
    -- non-empty chain records at BOTH chokepoints, so one call leaves a Pre
    -- breadcrumb and a Post breadcrumb (two ring entries, not one). Result is
    -- 10+100=110 but the VALUE is irrelevant here; the FIRE is the point (the
    -- engine self-check reads the ring, not the call result; its nFires>=1
    -- assertion is satisfied by either entry).
    local _ = call(10)
    kcdx.log.info("CAP47",
        "called the breadcrumb stub at ready; the before detour fired and "
        .. "left a Pre and a Post fire-ring breadcrumb (the newest ring entry "
        .. "is the Post fire; plugin=ts.cap_47_crash_breadcrumb "
        .. "hook=cap47_breadcrumb_fire either way) for the engine cap-47 "
        .. "self-report")
end)
