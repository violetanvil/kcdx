-- CAP-107 plugin.lua — Phase 10 VERIFICATION PROBE: combat_started/ended anchor.
--
-- ============================================================================
-- EXPERT / VERIFICATION-ONLY — raw-RVA expert escape hatch, NOT the shipped
-- author surface. The common path is a NAME (engine carries address + ABI). A
-- raw RVA in production code is AP1; here it is the deliberate labeled expert
-- form for a one-off Phase-10 anchor verification, used SO THAT no DB seed row
-- is authored before the anchor is live-confirmed.
-- ============================================================================
--
-- THE QUESTION: can a combat_started/combat_ended event be derived by hooking
-- the combat-state property GETTER and edge-detecting its returned state byte?
--
-- WHY THE GETTER, NOT A WRITER (pilot-combat-pin.md VERDICT: NO STATIC PIN of
-- the writer): the combat-state value is written by an inlined templated
-- SetValue — one of 2,523 indistinguishable [+8] byte writes, with no function
-- or vtable slot to hook — and the change-signal dispatches through a runtime-
-- bound listener list (no static fire fn). The ONLY statically-pinnable surface
-- is the getter:
--   FUN_181a7dac0  u8 __fastcall(ptr prop /*rcx*/)  -- returns state byte in al
--   RVA 0x1a7dac0, C_ModelProperty vtable slot 1; the value byte is at [prop+8].
--
-- ============================================================================
-- CAVEAT (load-bearing): this getter is on the READ path with ~192 callers. It
-- fires on READ CADENCE, not at the true SetValue moment. This probe is an
-- EDGE-DETECTOR over polled reads, NOT a true transition event. Acceptable for a
-- v1 combat_started/combat_ended verification; kcdx has NO write-watch primitive,
-- so this is the buildable path. The state is graded (cmp al,1 / cmp al,2 — a
-- 3-state enum); we edge-detect "in combat at all" (>= 1) for start/end.
-- ============================================================================
--
-- WHY THE RAW-VA `address =` FORM + an AFTER hook: the getter RETURNS the value
-- (in al), so we need its return — that is an `after` callback (receives the
-- original's return value). The raw-VA entry locator is `address = <pointer
-- userdata>` in opts (the `rva = "..."` string is the callsite surface, not
-- this one). The VA is a POINTER USERDATA (module base + RVA), never a Lua
-- integer (CryEngine Lua is LUA_NUMBER=float; pointer-magnitude integers round).
-- No name => `signature` is required.
--
-- PER-PROP edge-detection: the getter is shared across every combat-actor's
-- property instance, so we key the prior-value table by the prop POINTER's
-- display address (the rcx arg). NOTE (unverified, the live run reveals it):
-- per the hook docs the `after` callback receives the return value first; whether
-- the captured args (prop/rcx) also arrive positionally after it is not spelled
-- out for `after`. This probe is written to DEGRADE GRACEFULLY: if the prop arg
-- is present we edge-detect per-prop (the precise form); if it is absent
-- (after gives the return value only) we fall back to a SINGLE global prior-value
-- edge-detector keyed under one bucket. Either way a 0<->>=1 crossing is observed
-- and the first one PASSes — the per-prop refinement only sharpens which actor
-- transitioned, it is not load-bearing for the anchor-verification PASS.
--
-- OUTCOME -> MEANING (pre-committed, flat):
--   a 0->>=1 or >=1->0 crossing is observed during combat enter/leave -> the
--       getter + edge-detect path DOES surface combat transitions -> CAP-107 PASS.
--   the getter fires but no crossing is ever seen across combat enter/leave ->
--       the polled-read edge-detect does not catch the transition (cadence/state
--       mismatch); row stays PENDING (no false PASS) -> the writer-trace path is
--       needed instead.

-- PASS is STICKY + TERMINAL (CAP-03 PASS_LATCH idiom): one observed transition
-- confirms the path; it cannot un-confirm. Persistent _G key survives a
-- plugin.lua re-eval across save-loads in the single shared Lua state.
local PASS_LATCH = "__kcdx_cap107_passed"
local function already_passed()
    return rawget(_G, PASS_LATCH) == true
end

-- Per-prop prior-value table, keyed by the prop pointer's display address. The
-- display address is lossy for round-tripping into an API, but it is a stable,
-- unique KEY for a table — exactly what we need here (we never feed it back to a
-- kcdx call). When the prop arg is unavailable we use a single fallback bucket.
local prior_state = {}
local FALLBACK_KEY = "__no_prop_arg__"

local GETTER_RVA = 0x1a7dac0
local target_va = kcdx.memory.get_module_base_address("WHGame.dll"):add(GETTER_RVA)

-- Normalize the returned state byte to "in combat at all" (>= 1). Graded enum:
-- 0 = not in combat, >= 1 = some combat state.
local function in_combat(state_byte)
    return (state_byte ~= nil and state_byte >= 1)
end

local function report_transition(kind, key, from_v, to_v)
    if already_passed() then return end
    rawset(_G, PASS_LATCH, true)
    kcdx.test.report("CAP-107-probe-combat", true,
        "observed combat-state " .. kind .. " transition (" .. tostring(from_v)
        .. "->" .. tostring(to_v) .. ") at the combat-state getter "
        .. "FUN_181a7dac0 (RVA 0x1a7dac0), prop-key=" .. tostring(key)
        .. " — getter-hook edge-detect surfaces combat transitions. CAVEAT: "
        .. "edge over polled reads, not the true SetValue moment.")
end

local h = kcdx.hook.after("WHGame.dll",
    -- `after`: first param is the original's return value (the state byte in al).
    -- Per the degrade-gracefully note above, the prop arg MAY follow; capture it
    -- if present.
    function(ret, prop)
        local key = FALLBACK_KEY
        if prop ~= nil and type(prop) ~= "number" and not prop:is_null() then
            key = prop:get_address()  -- a stable unique table key (display addr)
        end

        local now = in_combat(ret)
        local was = prior_state[key]   -- nil on first sighting of this prop
        prior_state[key] = now

        if was == nil then return ret  -- first read for this prop: seed, no edge
        end
        if now ~= was then
            kcdx.log.debug("CAP107", "combat-state edge: key=%s %s->%s (ret=%s)",
                tostring(key), tostring(was), tostring(now), tostring(ret))
            if now and not was then
                report_transition("started", key, was, now)   -- 0 -> >=1
            elseif was and not now then
                report_transition("ended", key, was, now)     -- >=1 -> 0
            end
        end
        return ret  -- after must return the (unchanged) value
    end,
    {
        name      = "cap107_combat_getter",
        address   = target_va,
        signature = "u8 (ptr prop)",
    })

if h == nil then
    kcdx.test.report("CAP-107-probe-combat", false,
        "kcdx.hook returned nil — registration failed (raw-RVA expert hatch "
        .. "rejected at register time)")
    return
end

-- Backstop: a never-INSTALLED hook is a real FAIL (the probe could not be
-- placed). An installed-but-no-transition-yet hook leaves the row PENDING until
-- the dev enters/leaves combat — the after callback self-reports PASS on the
-- first observed edge.
kcdx.on("input_loaded", function()
    if already_passed() then return end
    if h:applied() ~= true then
        kcdx.test.report("CAP-107-probe-combat", false,
            "hook did not install: applied()=" .. tostring(h:applied())
            .. " reason=" .. tostring(h:reason()))
    end
end)

kcdx.log.info("CAP107", "VERIFICATION PROBE registered: hooking combat-state "
    .. "getter @ RVA 0x1a7dac0 (raw-RVA expert hatch, after). Edge-detects "
    .. "0<->>=1 over polled reads; PASSes on first transition. Enter then leave "
    .. "combat in-game. CAVEAT: read-cadence edge, not the true SetValue moment.")
