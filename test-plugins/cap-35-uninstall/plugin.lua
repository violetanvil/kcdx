-- CAP-35 plugin.lua — Lua handle:uninstall().
--
-- The permanent regression for the Lua-side handle:uninstall() method
-- and its underlying engine path:
--   * step 1 — kcdx::hook_chain::Uninstall(handleId): erases the
--     ChainEntry for sig/callsite chains; clears midHandleId + midCallback
--     for mid chains; trampoline retained (session-lifetime per the
--     dual-Lua-VM safety stance hook_chain.cpp:1700-1768).
--   * step 2 — H_uninstall in src/lua_registry.cpp: the metatable
--     method. Kind::Hook routes through hook_chain::Uninstall + SetStatus
--     (Status::Removed); non-Hook kinds raise a teaching luaL_error
--     (lua_registry.cpp:177-189). H_tostring's Removed case (step 1, lua_
--     registry.cpp:204) renders "removed" in tostring(h) post-uninstall.
--
-- Target choice: luaopen_math via the 2-segment kcdx.<seedname> form
-- (target="kcdx.luaopen_math") — engine-seed reference under the reserved
-- "kcdx" author root. The same shape cap-34's CAP-34-explicit-1dot-kcdx
-- row uses; the seed's signature ("i32 (ptr L)", confirmed in seed.csv
-- row 97 — formerly 1172 before the 1–157 curated-set renumber; by-name
-- resolution is renumber-immune) is carried by the name so no inline
-- signature= is needed.
-- luaopen_math is an UNHOOKED verified leaf called EXACTLY ONCE during
-- Lua boot and never again — established prior art (cap-33/cap-34/comp-12
-- all install no-op hooks on it without colliding with production hooks).
-- The hooks never fire during this test run: the install IS the proof the
-- name resolved + applied, and the metatable methods ARE the surface
-- under test.

-- ====================================================================
-- (1) CAP-35-uninstall-basic — install one no-op before-hook; at ready
-- assert :applied()==true, then :uninstall(), then :applied()==false.
-- The core lifecycle proof. FALSIFIABLE: if step 2's SetStatus(Removed)
-- did not flip the status atomic, the post-uninstall :applied() would
-- still read true (or nil if Pending) — the row FAILs.
-- ====================================================================
local hBasic = kcdx.hook.before("WHGame.dll", "kcdx.luaopen_math",  -- 2-segment engine-seed form
    function() end,                                                -- no-op (never fires this run)
    { name = "cap35_uninstall_basic" })

-- ====================================================================
-- (2) CAP-35-uninstall-idempotent — install a second no-op before-hook;
-- at ready, call :uninstall() TWICE in a row, assert both calls returned
-- the handle (the self-return chaining contract, lua_registry.cpp:191
-- `lua_pushvalue(L, 1); return 1`), assert :applied()==false at the end.
-- FALSIFIABLE: if the second call raises or fails to return self, the
-- chaining contract is broken — the row FAILs. The engine layer is
-- idempotent (hook_chain.cpp:1709 returns true on handleId=0 / unknown;
-- hook_chain.cpp:1767 returns true if no match found) so the second
-- :uninstall() on an already-Removed entry must be a safe no-op.
-- ====================================================================
local hIdem = kcdx.hook.before("WHGame.dll", "kcdx.luaopen_math",
    function() end,
    { name = "cap35_uninstall_idempotent" })

-- ====================================================================
-- (3) CAP-35-uninstall-tostring — install a third no-op before-hook;
-- at ready capture tostring(h) BEFORE :uninstall() (should contain
-- "applied" per H_tostring's status switch, lua_registry.cpp:201),
-- call :uninstall(), capture tostring(h) AFTER (should contain "removed"
-- per the Removed case lua_registry.cpp:204 step 1 added). Assert
-- before≠after AND after contains "removed". FALSIFIABLE: if step 1's
-- H_tostring Removed branch wasn't added, the post-uninstall tostring
-- would still render "applied" (or some other status), and the substring
-- check would fail.
-- ====================================================================
local hTostr = kcdx.hook.before("WHGame.dll", "kcdx.luaopen_math",
    function() end,
    { name = "cap35_uninstall_tostring" })

-- ====================================================================
-- (4) CAP-35-uninstall-chain-survives — install TWO before-hooks on the
-- same target under DISTINCT names. At ready, assert both :applied()==
-- true. Uninstall ONLY the first one. Assert first :applied()==false,
-- second STILL :applied()==true. The multi-hook-on-same-target pattern
-- from CAP-20-chain (established as "two before hooks on one target
-- chain in load order"). Proves the Option-A semantics step 1 implements
-- (hook_chain.cpp:1745-1765 entries.erase removes only the one entry;
-- the chain trampoline + remaining entries stay healthy — the empty-
-- chain DispatchPre/Post guards at lines 749/809 make even a fully-
-- drained chain a safe no-op shim). FALSIFIABLE: if step 1 erased the
-- wrong entry, removed both entries, or tore down the chain wholesale,
-- the surviving hook's :applied()==true assertion would fail.
-- ====================================================================
local hChainA = kcdx.hook.before("WHGame.dll", "kcdx.luaopen_math",
    function() end,
    { name = "cap35_uninstall_chain_a" })
local hChainB = kcdx.hook.before("WHGame.dll", "kcdx.luaopen_math",
    function() end,
    { name = "cap35_uninstall_chain_b" })

-- ====================================================================
-- (5) CAP-35-uninstall-bytes-error — the non-Hook teaching-error proof
-- for step 2's default branch (lua_registry.cpp:177-189). Register a
-- kcdx.bytes with a well-formed-but-deliberately-non-matching pattern
-- (16 bytes of DE AD BE EF...) and a 1-byte 0x90-over-0x90 replacement
-- (idempotent no-op even IF it somehow matched). The pattern parses
-- cleanly so the binder appends a Kind::Bytes Entry and returns a
-- handle; the apply pass scans WHGame.dll, finds 0 matches, flips
-- status to Failed (per lua_bind_bytes.cpp ApplyBytesEntry). The Entry
-- still exists with Kind::Bytes — H_uninstall switches on Kind not
-- Status (lua_registry.cpp:161-189), so calling :uninstall() on it
-- routes to the default branch and raises:
--   "handle:uninstall() is not yet supported for kcdx.bytes handles ..."
-- Wrap in pcall (the test plugin must survive the raise); assert pcall
-- returned false; assert the error message contains "kcdx.bytes" AND
-- "not yet supported" (the exact substrings step 2's luaL_error emits).
-- FALSIFIABLE: if H_uninstall's default branch silently flipped status
-- instead of raising, pcall would return true — the row FAILs. NO live
-- memory is written: the apply finds nothing, no VirtualProtect, no
-- memcpy; safe under every condition.
-- ====================================================================
local hBytes, hBytesErr = kcdx.bytes{
    name        = "cap35_uninstall_bytes_error",
    -- well-formed AOB the binder accepts at registration; will scan
    -- WHGame.dll and find 0 matches at apply (status -> Failed). The
    -- LENGTH (16 bytes) is unrelated to original/replacement length —
    -- those are bytes written at the resolved site, the pattern is the
    -- locator.
    pattern     = "DE AD BE EF DE AD BE EF DE AD BE EF DE AD BE EF",
    module      = "WHGame.dll",
    original    = "90",                      -- 1 byte; same length as replacement
    replacement = "90",                      -- 1 byte; idempotent NOP-over-NOP
    idempotent  = true,
}
if hBytes == nil then
    -- Defensive: if the binder rejected at registration, we never get a
    -- handle to test :uninstall() on. Log loudly so the failure mode is
    -- discoverable rather than silenced (fix at the cause, never quietly
    -- swallow it).
    kcdx.log.error("CAP35",
        "kcdx.bytes registration unexpectedly rejected at binder: "
        .. tostring(hBytesErr) .. " (CAP-35-uninstall-bytes-error will "
        .. "report FAIL with this reason)")
end

-- ====================================================================
-- (6) CAP-35-off-thread-skip — parser test for the new `off_thread` knob
-- (the Lua off_thread parser landing).
-- Register a no-op hook with `off_thread = "skip"` on `kcdx.luaopen_math`
-- (same prior-art target the basic rows use; the hook never fires this
-- run, the install IS the proof). PASS iff the binder returned a non-nil
-- handle — the string parsed cleanly + the payload field was threaded.
-- FALSIFIABLE: if the parser rejects "skip" (typo, missing case, wrong
-- comparison), kcdx.hook returns nil + err → the assert fires → no
-- report → the matrix row sits PENDING.
-- ====================================================================
local hSkip = kcdx.hook.before("WHGame.dll", "kcdx.luaopen_math",
    function() end,
    { name = "cap35_off_thread_skip", off_thread = "skip" })
kcdx.test.report("CAP-35-off-thread-skip", hSkip ~= nil,
    (hSkip ~= nil)
      and "off_thread=\"skip\" parsed; binder returned a handle"
      or  ("expected a handle, got nil — off_thread parser rejected "
           .. "\"skip\" (the new knob is not wired)"))

-- ====================================================================
-- (7) CAP-35-off-thread-bogus — teaching-error test. Register with an
-- INVALID off_thread value ("bogus" — not one of marshal/skip/error).
-- The binder must return (nil, err) where err names the field. PASS
-- iff returned handle is nil AND error message contains the substring
-- "off_thread". FALSIFIABLE: a too-permissive parser (silent default-
-- to-Marshal, or no validation) returns a non-nil handle → assert
-- fails. A right-shaped error message without "off_thread" in it also
-- fails — the teaching contract is "name the field so the author can
-- find + fix it" (errors teach — the message names the offending input).
-- ====================================================================
local hBogus, hBogusErr = kcdx.hook.before("WHGame.dll", "kcdx.luaopen_math",
    function() end,
    { name = "cap35_off_thread_bogus", off_thread = "bogus" })
do
    local errStr   = tostring(hBogusErr)
    local nilOk    = (hBogus == nil)
    local mentions = (string.find(errStr, "off_thread", 1, true) ~= nil)
    local pass     = nilOk and mentions
    kcdx.test.report("CAP-35-off-thread-bogus", pass,
        pass
          and ("teaching-error PASS — bogus off_thread rejected at "
               .. "binder; error mentions 'off_thread': " .. errStr)
          or  ("expected (nil, err containing 'off_thread'); got handle="
               .. tostring(hBogus) .. " err=" .. errStr))
end

-- ====================================================================
-- Handles resolve to a final :applied() only AFTER the zone apply pass,
-- which runs after this plugin.lua returns. Read them + drive the
-- uninstall lifecycle in kcdx.on("ready").
-- ====================================================================
kcdx.on("ready", function()
    ----------------------------------------------------------------
    -- (1) basic lifecycle.
    ----------------------------------------------------------------
    do
        local appliedBefore = hBasic:applied()
        hBasic:uninstall()
        local appliedAfter = hBasic:applied()
        local pass = (appliedBefore == true) and (appliedAfter == false)
        kcdx.test.report("CAP-35-uninstall-basic", pass,
            pass
              and ("handle:uninstall() lifecycle PASS — applied()==true "
                   .. "pre-uninstall, applied()==false post-uninstall "
                   .. "(step 2 SetStatus(Removed) flipped the status atomic)")
              or  ("expected pre=true, post=false; got pre="
                   .. tostring(appliedBefore) .. " post="
                   .. tostring(appliedAfter)
                   .. " reason=" .. tostring(hBasic:reason())))
    end

    ----------------------------------------------------------------
    -- (2) idempotent double-uninstall.
    ----------------------------------------------------------------
    do
        local appliedBefore = hIdem:applied()
        local r1 = hIdem:uninstall()
        local r2 = hIdem:uninstall()
        local appliedAfter = hIdem:applied()
        -- The handle returned from :uninstall() must be the SAME userdata
        -- as the original (self-return chaining contract). rawequal
        -- compares userdata identity without invoking __eq.
        local r1IsSelf = rawequal(r1, hIdem)
        local r2IsSelf = rawequal(r2, hIdem)
        local pass = (appliedBefore == true) and r1IsSelf and r2IsSelf
                       and (appliedAfter == false)
        kcdx.test.report("CAP-35-uninstall-idempotent", pass,
            pass
              and ("double :uninstall() PASS — both calls returned self "
                   .. "(chaining contract); applied()==false post both "
                   .. "(engine layer is idempotent on unknown / Removed ids)")
              or  ("expected pre=true, r1==self, r2==self, post=false; got "
                   .. "pre=" .. tostring(appliedBefore)
                   .. " r1==self=" .. tostring(r1IsSelf)
                   .. " r2==self=" .. tostring(r2IsSelf)
                   .. " post=" .. tostring(appliedAfter)))
    end

    ----------------------------------------------------------------
    -- (3) tostring transition.
    ----------------------------------------------------------------
    do
        local appliedBefore = hTostr:applied()
        local sBefore = tostring(hTostr)
        hTostr:uninstall()
        local sAfter = tostring(hTostr)
        -- H_tostring renders the status word verbatim ("applied" /
        -- "removed"). Pre must contain "applied" (the start state);
        -- post must contain "removed" (the step 1 Removed-branch case).
        local preHasApplied = (string.find(sBefore, "applied", 1, true) ~= nil)
        local postHasRemoved = (string.find(sAfter, "removed", 1, true) ~= nil)
        local pass = (appliedBefore == true) and preHasApplied
                       and postHasRemoved and (sBefore ~= sAfter)
        kcdx.test.report("CAP-35-uninstall-tostring", pass,
            pass
              and ("tostring(h) status transition PASS — before='"
                   .. sBefore .. "' (contains 'applied'), after='"
                   .. sAfter .. "' (contains 'removed'); H_tostring's "
                   .. "Removed case + SetStatus(Removed) wire end-to-end")
              or  ("expected pre-applied=true + pre contains 'applied' + "
                   .. "post contains 'removed' + before!=after; got "
                   .. "pre-applied=" .. tostring(appliedBefore)
                   .. " before='" .. tostring(sBefore)
                   .. "' after='" .. tostring(sAfter) .. "'"))
    end

    ----------------------------------------------------------------
    -- (4) chain survives — uninstall A, B stays applied.
    ----------------------------------------------------------------
    do
        local appliedA0 = hChainA:applied()
        local appliedB0 = hChainB:applied()
        hChainA:uninstall()
        local appliedA1 = hChainA:applied()
        local appliedB1 = hChainB:applied()
        local pass = (appliedA0 == true) and (appliedB0 == true)
                       and (appliedA1 == false) and (appliedB1 == true)
        kcdx.test.report("CAP-35-uninstall-chain-survives", pass,
            pass
              and ("chain survives single-entry uninstall PASS — A pre=true, "
                   .. "B pre=true; after A:uninstall(): A=false, B=true "
                   .. "(step 1 entries.erase removed only the one entry; "
                   .. "trampoline + B's entry retained)")
              or  ("expected A pre=true, B pre=true, A post=false, B post=true; "
                   .. "got A pre=" .. tostring(appliedA0)
                   .. " B pre=" .. tostring(appliedB0)
                   .. " A post=" .. tostring(appliedA1)
                   .. " B post=" .. tostring(appliedB1)
                   .. " A reason=" .. tostring(hChainA:reason())
                   .. " B reason=" .. tostring(hChainB:reason())))
    end

    ----------------------------------------------------------------
    -- (5) kcdx.bytes :uninstall() raises a teaching error.
    ----------------------------------------------------------------
    do
        if hBytes == nil then
            -- Registration was rejected at binder time (defensive branch
            -- above already logged the rejection reason). Report FAIL
            -- with the captured binder error so the row is diagnosable.
            kcdx.test.report("CAP-35-uninstall-bytes-error", false,
                "kcdx.bytes registration was rejected at the binder; no "
                .. "handle to call :uninstall() on. binder_err="
                .. tostring(hBytesErr))
        else
            local ok, errMsg = pcall(function()
                hBytes:uninstall()
            end)
            -- ok==false => the teaching luaL_error fired (expected).
            -- ok==true  => H_uninstall did NOT raise — step 2's default
            --              branch is wrong (FAIL).
            local raised = (ok == false)
            -- Substring checks against the exact text step 2's luaL_error
            -- emits: "handle:uninstall() is not yet supported for
            -- kcdx.bytes handles ..." (lua_registry.cpp:183-188).
            local errStr = tostring(errMsg)
            local hasKind  = (string.find(errStr, "kcdx.bytes", 1, true) ~= nil)
            local hasNYI   = (string.find(errStr, "not yet supported", 1, true) ~= nil)
            local pass = raised and hasKind and hasNYI
            kcdx.test.report("CAP-35-uninstall-bytes-error", pass,
                pass
                  and ("kcdx.bytes :uninstall() teaching-error PASS — "
                       .. "pcall returned false (error raised); error "
                       .. "contains 'kcdx.bytes' AND 'not yet supported' "
                       .. "(step 2 default branch refuses to lie about "
                       .. "removed status when the patch engine has no "
                       .. "revert path)")
                  or  ("expected raised==true + err contains 'kcdx.bytes' "
                       .. "+ err contains 'not yet supported'; got "
                       .. "raised=" .. tostring(raised)
                       .. " has_kind=" .. tostring(hasKind)
                       .. " has_nyi=" .. tostring(hasNYI)
                       .. " err='" .. errStr .. "'"))
        end
    end
end)

kcdx.log.info("CAP35",
    "registered handle:uninstall() lifecycle hooks (basic, idempotent, "
    .. "tostring, chain-survives x2) on kcdx.luaopen_math + a bogus-pattern "
    .. "kcdx.bytes for the non-Hook teaching-error path; assertions fire "
    .. "at kcdx.on(\"ready\") after the apply pass")
