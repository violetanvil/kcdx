-- CAP-105 plugin.lua — Phase 10 VERIFICATION PROBE: item_picked_up anchor.
--
-- ============================================================================
-- EXPERT / VERIFICATION-ONLY — raw-RVA expert escape hatch, NOT the shipped
-- author surface. The common path is a NAME (the engine carries address + ABI).
-- A raw RVA in a production plugin is AP1; here it is the deliberate, labeled
-- expert form for a one-off Phase-10 anchor verification, used SO THAT no DB
-- seed row has to be authored before the anchor is live-confirmed.
-- ============================================================================
--
-- THE QUESTION: does a player item-pickup route through the single global
-- entity-script dispatch chokepoint (CScriptTable::CallFunction @ RVA 0xb9ceb4,
-- vtable slot 22) with funcName == "OnPickup"?
--
-- THE TARGET (pure-static pin, pilot-pickup-pin.md):
--   FUN_180b9ceb4  u64 __fastcall(ptr scriptTable /*rcx*/, ptr callDesc /*rdx*/)
--   callDesc->funcName is a const char* at [callDesc + 0x00].
--
-- WHY THE RAW-VA `address =` FORM (not a `rva = "..."` string): per the hook
-- API, the function-ENTRY raw locator is a raw virtual address passed as
-- `address = <pointer userdata>` in opts (the `rva = "<module> @ rva 0x…"`
-- string form is for kcdx.hook.callsite's call-instruction redirect, a different
-- surface). We build the VA as a POINTER USERDATA — module base + RVA — never a
-- Lua integer: CryEngine Lua is LUA_NUMBER=float, so a pointer-magnitude integer
-- silently rounds (integers beyond 2^24 lose precision). The pointer userdata is
-- exact. Because there is no name, we MUST pass `signature` in opts.
--
-- OUTCOME -> MEANING (pre-committed, flat):
--   funcName "OnPickup" observed at this slot  -> the item-pickup callback DOES
--       route through the entity-script chokepoint -> CAP-105 PASS; the anchor
--       binding is confirmed (promotes the pin to seed-ready, user-gated AP18).
--   funcNames seen but never "OnPickup"        -> pickup may use the C++
--       CItemSystem path instead; row stays PENDING (no false PASS). The logged
--       On<X> list tells which callbacks DO route here.
--   no On<X> funcName ever logged              -> the dispatch is not firing as
--       pinned (or the hook did not install) -> investigate; row stays PENDING.

-- PASS is STICKY + TERMINAL (CAP-03 PASS_LATCH idiom). Once OnPickup is observed
-- once, the anchor is confirmed and cannot un-confirm. The latch is a persistent
-- _G key so it survives a plugin.lua re-eval across save-loads in the single
-- shared Lua state.
local PASS_LATCH = "__kcdx_cap105_passed"
local function already_passed()
    return rawget(_G, PASS_LATCH) == true
end

-- callDesc->funcName: a const char* at offset +0x00 of the callDesc pointer.
-- callDesc:deref() reads the pointer-width value at [callDesc+0] (the char* )
-- and wraps it as a new pointer; :get_string() reads the C string there.
local FUNCNAME_OFFSET = 0x00

-- The expert-hatch raw VA: module base of WHGame.dll + the pinned RVA, as an
-- exact pointer userdata.
local CALLFUNCTION_RVA = 0xb9ceb4
local target_va = kcdx.memory.get_module_base_address("WHGame.dll"):add(CALLFUNCTION_RVA)

local function read_func_name(call_desc)
    -- call_desc is the rdx arg (a pointer userdata). funcName lives at +0x00.
    if call_desc == nil or call_desc:is_null() then return nil end
    local name_ptr = call_desc:add(FUNCNAME_OFFSET):deref()  -- the const char*
    if name_ptr == nil or name_ptr:is_null() then return nil end
    return name_ptr:get_string()
end

local h = kcdx.hook.before("WHGame.dll",
    function(script_table, call_desc)  -- ptr rcx, ptr rdx (callDesc)
        local func_name = read_func_name(call_desc)
        -- FILTER: this slot dispatches EVERY script call. Act only on the
        -- entity-script-EVENT family (funcName starting "On") to avoid spamming
        -- a hot dispatch log (volume caveat, pilot-pickup-pin.md).
        if type(func_name) ~= "string" then return end
        if func_name:sub(1, 2) ~= "On" then return end

        kcdx.log.debug("CAP105", "entity-script dispatch: funcName=%s", func_name)

        if func_name == "OnPickup" then
            if already_passed() then return end
            rawset(_G, PASS_LATCH, true)
            kcdx.test.report("CAP-105-probe-pickup", true,
                "observed funcName='OnPickup' at CScriptTable::CallFunction "
                .. "(RVA 0xb9ceb4, slot 22) — item pickup DOES route through the "
                .. "entity-script dispatch chokepoint. Anchor binding confirmed.")
        end
    end,
    {
        name      = "cap105_scripttable_callfunction",
        address   = target_va,
        signature = "u64 (ptr scriptTable, ptr callDesc)",
    })

if h == nil then
    kcdx.test.report("CAP-105-probe-pickup", false,
        "kcdx.hook returned nil — registration failed (raw-RVA expert hatch "
        .. "rejected at register time)")
    return
end

-- Backstop: if by input_loaded the hook never INSTALLED (apply-pass rejection),
-- report the real FAIL with the reason — the probe could not be placed. We do
-- NOT FAIL on "not yet observed OnPickup": the pickup gesture happens later in
-- the launch, and the before callback self-reports PASS when it sees OnPickup.
-- An installed-but-not-yet-observed hook leaves the row PENDING (honest), which
-- is the correct state until the dev picks up an item.
kcdx.on("input_loaded", function()
    if already_passed() then return end
    if h:applied() ~= true then
        kcdx.test.report("CAP-105-probe-pickup", false,
            "hook did not install: applied()=" .. tostring(h:applied())
            .. " reason=" .. tostring(h:reason()))
    end
end)

kcdx.log.info("CAP105", "VERIFICATION PROBE registered: hooking CScriptTable::"
    .. "CallFunction @ RVA 0xb9ceb4 (raw-RVA expert hatch). Filters On<X> "
    .. "funcNames; PASSes on first OnPickup. Pick up an item in-game.")
