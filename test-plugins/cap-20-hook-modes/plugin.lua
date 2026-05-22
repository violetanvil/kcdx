-- CAP-20 plugin.lua — installs the kcdx.hook hooks the companion DLL
-- verifies on InputLoaded. This is the surface under test: the new
-- mode-as-key kcdx.hook (before/after/around/replace), chaining, wstr
-- marshaling, and load-order conflict resolution.
--
-- Each target's address comes from the DLL via kcdx.cap20.addr_*() (an
-- exact lightuserdata). RegisterFunction installs plugin C functions
-- under the kcdx table (kcdx.<table>.<fn>), NOT at global scope — so the
-- DLL's RegisterFunction(self,"cap20","addr_before",...) lives at
-- kcdx.cap20.addr_before. We hook via kcdx.hook{ address=<that>, <mode>=fn }.
-- Signature is "i32 (i32 seed)" for the Add_* targets, "i32 (wstr s)"
-- for WLen. The DLL calls the targets after ApplyZone and asserts.

local cap20 = kcdx.cap20   -- the DLL-registered target-address accessors
local SIG = "i32 (i32 seed)"

-- CAP-20-before: massage the arg (seed -> seed+1). Original still runs.
kcdx.hook{
    name    = "cap20_before",
    address = cap20.addr_before(),
    signature = SIG,
    before  = function(seed) return seed + 1 end,
}

-- CAP-20-after: transform the return value (+1000).
kcdx.hook{
    name    = "cap20_after",
    address = cap20.addr_after(),
    signature = SIG,
    after   = function(ret) return ret + 1000 end,
}

-- CAP-20-replace: original never runs; return a constant.
kcdx.hook{
    name    = "cap20_replace",
    address = cap20.addr_replace(),
    signature = SIG,
    replace = function(seed) return 42 end,
}

-- CAP-20-around: wrap the original — call it, double the result.
kcdx.hook{
    name    = "cap20_around",
    address = cap20.addr_around(),
    signature = SIG,
    around  = function(orig, seed) return 2 * orig(seed) end,
}

-- CAP-20-chain: two before hooks on ONE target. Both fire, in load
-- order (intra-plugin: registration order). First adds 1, second
-- doubles. (10+1)*2 = 22, then original +100 = 122.
local chainAddr = cap20.addr_chain()
kcdx.hook{ name = "cap20_chain_a", address = chainAddr, signature = SIG,
           before = function(seed) return seed + 1 end }
kcdx.hook{ name = "cap20_chain_b", address = chainAddr, signature = SIG,
           before = function(seed) return seed * 2 end }

-- CAP-20-wstr: read + mutate a wide-string arg. Replace 'abc' with
-- 'abcd' so the native WLen sees length 4.
kcdx.hook{
    name    = "cap20_wstr",
    address = cap20.addr_wlen(),
    signature = "i32 (wstr s)",
    before  = function(s) return s .. "d" end,
}

-- CAP-20-conflict: TWO replace hooks on one target, returning DIFFERENT
-- constants (7 then 99). They cannot coexist (replace is exclusive in
-- v1), so load order decides: the first (cap20_conflict_a) wins and
-- returns 7; the second (cap20_conflict_b) is REJECTED. The DLL asserts
-- the observed result is 7 (not 99) — value-distinguishable proof that
-- the first won and the second lost. The REJECTION side
-- (hConflictB:applied()==false + reason) is asserted in Lua from the
-- "ready" callback below (CAP-20-conflict-rejected).
local conflictAddr = cap20.addr_conflict()
local hConflictA = kcdx.hook{
    name = "cap20_conflict_a", address = conflictAddr, signature = SIG,
    replace = function(seed) return 7 end,
}
local hConflictB = kcdx.hook{
    name = "cap20_conflict_b", address = conflictAddr, signature = SIG,
    replace = function(seed) return 99 end,
}

-- CAP-20-dyncall: regression for the shared JitTrampoline arg+return
-- round-trip (kcdx.memory.dynamic_call). This is the path whose
-- LUA_NUMBER=float arg/return marshaling was latent-broken and never
-- tested (only void + bogus-target paths existed) — see
-- docs/known-issues/cap-20-around-wraps-original-wrong-result.md. Call a
-- known i32(i32) target directly via dynamic_call (runs unhooked here —
-- hooks apply later at ApplyZone) and assert 10 -> 110. Reported inline
-- from plugin.lua (dynamic_call is synchronous; no deferral needed).
do
    local target = cap20.addr_dyncall()
    local call = kcdx.memory.dynamic_call{
        target = target, return_type = "i32", param_types = {"i32"},
    }
    if call then
        local got = call(10)
        kcdx.test.report("CAP-20-dyncall", got == 110,
            got == 110
              and "dynamic_call i32(i32): 10 -> 110 (arg+return round-trip)"
              or  ("dynamic_call i32(i32) returned " .. tostring(got)
                   .. " (expected 110) — JitTrampoline float marshal regression"))
    else
        kcdx.test.report("CAP-20-dyncall", false,
            "kcdx.memory.dynamic_call returned nil")
    end
end

-- CAP-20-addrname (the Address Library NAME locator, sub-4b) is verified
-- in cap-20.cpp at the RESOLVE layer: the DLL asserts
-- ResolveAddressByName("name") == ResolveAddress(id) for a known entry
-- (exact uintptr_t, no float loss; collision-proof; no live hook needed).
-- Install+dispatch by locator is already covered by the 8 sub-tests
-- above.

-- CAP-20-addrname-miss: a kcdx.hook whose address_id NAME does NOT
-- resolve. This hook is EXPECTED to fail to apply — that is the test.
-- The name "cap20_addrname_miss" is deliberately not a real Address
-- Library entry, so its failure can never be confused with a regression
-- on a real target. We capture its handle and assert (in "ready")
-- :applied()==false with a non-empty :reason(). before=function is a
-- no-op stub: the hook never installs, so it never runs.
local hAddrnameMiss = kcdx.hook{
    name       = "cap20_addrname_miss",
    address_id = "cap20_addrname_miss",   -- not a real Address Library name
    signature  = SIG,
    before     = function(seed) return seed end,
}

-- CAP-20 deferred failure-path asserts. These need the post-apply
-- moment: handle:applied()/:reason() only become final during ApplyZone,
-- which runs AFTER this plugin.lua returns. kcdx.on("ready", ...) fires
-- once, after this plugin's zone apply pass completes — so every handle
-- captured above now reads a final status. (docs/outstanding-work/
-- ready-event-and-handle-assert.md)
kcdx.on("ready", function()
    -- (a) CAP-20-addrname-miss: a bad address_id NAME must fail to apply
    -- with a clear reason.
    do
        local applied = hAddrnameMiss:applied()
        local reason  = hAddrnameMiss:reason()
        local pass = (applied == false) and reason ~= nil and reason ~= ""
        kcdx.test.report("CAP-20-addrname-miss", pass,
            pass and ("bad address_id name failed to apply with reason: "
                      .. reason)
                 or  ("expected applied()==false + non-empty reason; got "
                      .. "applied=" .. tostring(applied)
                      .. " reason=" .. tostring(reason)))
    end

    -- (b) CAP-20-conflict-rejected: the load-order-losing replace
    -- (hConflictB) must be REJECTED — applied()==false + reason.
    do
        local applied = hConflictB:applied()
        local reason  = hConflictB:reason()
        local pass = (applied == false) and reason ~= nil and reason ~= ""
        kcdx.test.report("CAP-20-conflict-rejected", pass,
            pass and ("load-order-losing replace rejected with reason: "
                      .. reason)
                 or  ("expected applied()==false + non-empty reason; got "
                      .. "applied=" .. tostring(applied)
                      .. " reason=" .. tostring(reason)))
    end

    -- (c) CAP-20-ready: prove the ready cb fired AT ALL + a SUCCEEDED
    -- handle reads applied()==true. hConflictA won the conflict, so it
    -- applied. If ready never fires, this row never reports (shows
    -- PENDING) — which itself signals the feature didn't work.
    do
        local applied = hConflictA:applied()
        local pass = (applied == true)
        kcdx.test.report("CAP-20-ready", pass,
            pass and "ready callback fired; winning handle applied()==true"
                 or  ("ready fired but winning handle applied="
                      .. tostring(applied) .. " (expected true)"))
    end
end)

-- Also exercises kcdx.log.* (the grouped logging domain): a positional
-- "do a thing" call per the surface convention. The handles are held in
-- locals so they aren't GC'd before ApplyZone resolves them.
local _ = { hConflictA, hConflictB, hAddrnameMiss }
kcdx.log.info("CAP20", "installed all cap-20 hooks (4 modes + chain + wstr + conflict)")
