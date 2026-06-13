-- CAP-106 plugin.lua — Phase 10 VERIFICATION PROBE: perk_unlocked anchor.
--
-- ============================================================================
-- EXPERT / VERIFICATION-ONLY — raw-RVA expert escape hatch, NOT the shipped
-- author surface. The common path is a NAME (engine carries address + ABI). A
-- raw RVA in production code is AP1; here it is the deliberate labeled expert
-- form for a one-off Phase-10 anchor verification, used SO THAT no DB seed row
-- is authored before the anchor is live-confirmed.
-- ============================================================================
--
-- THE QUESTION: does a NATURAL-PROGRESSION perk unlock (quest reward / level-up /
-- the RTTR C_AddPerkEffect effect — not only the Lua AddPerk command) fire the
-- perk-collection insert FUN_18046b704?
--
-- THE TARGET (pure-static walk, pilot-perk-static.md):
--   FUN_18046b704  bool __fastcall(ptr perkCollection /*rcx*/, ptr perkId /*rdx*/,
--                                  ptr ctx /*r8*/)
--   the add-if-absent insert + the on-changed notify (call [delegate-vtable+8]).
--
-- WHY THE RAW-VA `address =` FORM: the function-ENTRY raw locator is a raw VA
-- passed as `address = <pointer userdata>` in opts (the `rva = "..."` string is
-- the callsite surface, not this one). The VA is built as a POINTER USERDATA
-- (module base + RVA), never a Lua integer — CryEngine Lua is LUA_NUMBER=float
-- and a pointer-magnitude integer rounds. No name => `signature` is required.
--
-- OUTCOME -> MEANING (pre-committed, flat):
--   the insert fires (callback runs) -> 0x46b704 IS on the perk-grant path
--       reached by the triggered unlock -> CAP-106 PASS.
--   the insert never fires for a natural-progression unlock -> the progression
--       grant takes a DIFFERENT insert path; row stays PENDING (no false PASS).
--
-- A before-callback on the INSERT ENTRY suffices — entry firing already proves
-- the grant path reached this function; we do not need to read the GUID arg to
-- answer the anchor-binding question (reading it would deref a runtime pointer
-- for no added signal). Entry-fired IS the observable.

-- PASS is STICKY + TERMINAL (CAP-03 PASS_LATCH idiom): one fire confirms the
-- path; it cannot un-confirm. Persistent _G key survives a plugin.lua re-eval
-- across save-loads in the single shared Lua state.
local PASS_LATCH = "__kcdx_cap106_passed"
local function already_passed()
    return rawget(_G, PASS_LATCH) == true
end

local g_fires = 0

local INSERT_RVA = 0x46b704
local target_va = kcdx.memory.get_module_base_address("WHGame.dll"):add(INSERT_RVA)

local h = kcdx.hook.before("WHGame.dll",
    function(perk_collection, perk_id, ctx)  -- ptr rcx, ptr rdx (GUID), ptr r8
        g_fires = g_fires + 1
        kcdx.log.debug("CAP106", "perk-collection insert fired (count=%d)", g_fires)
        if already_passed() then return end
        rawset(_G, PASS_LATCH, true)
        kcdx.test.report("CAP-106-probe-perk", true,
            "perk-collection insert FUN_18046b704 (RVA 0x46b704) fired — "
            .. "confirms 0x46b704 is on the perk-grant path (count="
            .. g_fires .. ").")
    end,
    {
        name      = "cap106_perk_grant",
        address   = target_va,
        signature = "bool (ptr perkCollection, ptr perkId, ptr ctx)",
    })

if h == nil then
    kcdx.test.report("CAP-106-probe-perk", false,
        "kcdx.hook returned nil — registration failed (raw-RVA expert hatch "
        .. "rejected at register time)")
    return
end

-- Backstop: a never-INSTALLED hook is a real FAIL (the probe could not be
-- placed). An installed-but-not-yet-fired hook leaves the row PENDING until the
-- dev unlocks a perk — the before callback self-reports PASS on first fire.
kcdx.on("input_loaded", function()
    if already_passed() then return end
    if h:applied() ~= true then
        kcdx.test.report("CAP-106-probe-perk", false,
            "hook did not install: applied()=" .. tostring(h:applied())
            .. " reason=" .. tostring(h:reason()))
    end
end)

kcdx.log.info("CAP106", "VERIFICATION PROBE registered: hooking perk-collection "
    .. "insert @ RVA 0x46b704 (raw-RVA expert hatch). PASSes on first fire. "
    .. "Unlock a perk via natural progression in-game.")
