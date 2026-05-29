-- CAP-28 plugin.lua — kcdx.bytes smart-resolver shape regression.
--
-- The smart resolver is the __index-driven Lua shape that resolves a named
-- target to its install function on demand:
--
--   kcdx.bytes.outfit_swap_callsite_aob{ replacement = "90 90 90" }
--                       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
--                       the name; __index returns a userdata whose
--                       __call installs with target=<name>.
--
-- Failure modes the surface promises:
--   * A TYPO at the name → __index returns nil (typo fails fast at the
--     name access, not later at install time).
--   * Otherwise: same install outcome as the equivalent flat-table form.

-- ====================================================================
-- (1) CAP-28-typo-fails-fast — `kcdx.bytes.<bad_name>{...}` returns nil
--     at the name access; calling the resulting nil raises Lua's
--     "attempt to call a nil value" error naming the typoed slot.
--     We pcall-catch that error and assert the message contains the
--     typoed name (the engine steers the author to the fix).
-- ====================================================================
do
    local TYPO = "cap28_definitely_not_a_real_target_xyzzy"
    local ok, err = pcall(function()
        -- The smart resolver: typing kcdx.bytes.<typo> hits __index, which
        -- probes every population source (engine seed / declared store /
        -- author-target store). On a miss, __index returns nil; the trailing
        -- {...} call then raises "attempt to call a nil value".
        return kcdx.bytes[TYPO]{ replacement = "90 90 90" }
    end)

    -- ok==false means the call raised — that is the EXPECTED outcome (the
    -- typo-fails-fast contract). err is the Lua-formatted error string;
    -- assert it carries the typoed name so the author sees WHICH slot was
    -- wrong.
    local raised      = (ok == false)
    local err_str     = tostring(err)
    local names_typo  = raised and string.find(err_str, TYPO, 1, true) ~= nil

    if raised and names_typo then
        kcdx.test.report("CAP-28-typo-fails-fast", true,
            "kcdx.bytes." .. TYPO .. "{...} raised \"" .. err_str
            .. "\" — typo fails fast at the name access AND the error names "
            .. "the typoed slot (smart-resolver __index returned nil; the "
            .. "subsequent call raised attempt-to-call-nil)")
    else
        local why
        if not raised then
            why = "the typo DID NOT raise — kcdx.bytes." .. TYPO
                .. "{...} resolved or silently succeeded. The smart resolver "
                .. "is supposed to return nil for an unknown name; if it "
                .. "returned a real installer, a typo would install at some "
                .. "unrelated site or silently no-op"
        else
            why = "the typo raised but the error did not contain the typoed "
                .. "name \"" .. TYPO .. "\"; got: " .. err_str
                .. ". The author cannot tell WHICH slot was wrong"
        end
        kcdx.test.report("CAP-28-typo-fails-fast", false, why)
    end
end

-- ====================================================================
-- (2) CAP-28-install-parity — `kcdx.bytes.<name>{...}` installs with the
--     SAME outcome as `kcdx.bytes{target=<name>, ...}`. Two registrations
--     in this plugin against the same site cap-01 covers, distinct
--     names; both must reach :applied()==true at ready.
-- ====================================================================
--
-- The site: outfit_swap_callsite_aob (id 1004), the verified-safe rewrite
-- 44 8A F0 -> 45 31 F6 cap-01 already drives. Two same-replacement writers
-- on one site is conflict_engine WriteOnWriteFull — not a rejection;
-- whichever applies first writes, the other idempotent-skips, both reach
-- Status::Applied. So the assertion holds in every apply interleaving.

local h_smart = kcdx.bytes.outfit_swap_callsite_aob{
    name        = "cap28_smart_resolver_form",
    offset      = 13,
    original    = "44 8A F0",
    replacement = "45 31 F6",
    idempotent  = true,
}

local h_flat = kcdx.bytes{
    name        = "cap28_flat_table_form",
    target      = "outfit_swap_callsite_aob",
    offset      = 13,
    original    = "44 8A F0",
    replacement = "45 31 F6",
    idempotent  = true,
}

if not h_smart or not h_flat then
    kcdx.test.report("CAP-28-install-parity", false,
        "registration failed: h_smart=" .. tostring(h_smart)
        .. ", h_flat=" .. tostring(h_flat)
        .. " — at least one form returned nil at registration "
        .. "(the smart resolver and the flat-table form should both "
        .. "produce a handle for a valid named target)")
    return
end

kcdx.on("ready", function()
    local s_applied = h_smart:applied()
    local f_applied = h_flat:applied()
    local pass      = (s_applied == true) and (f_applied == true)

    if pass then
        kcdx.test.report("CAP-28-install-parity", true,
            "both forms applied: kcdx.bytes.<name>{...} :applied()==true, "
            .. "kcdx.bytes{target=<name>,...} :applied()==true — the "
            .. "smart-resolver shape and the flat-table shape install the "
            .. "same way (coexisting on the same site via conflict_engine "
            .. "WriteOnWriteFull idempotent-skip)")
    else
        kcdx.test.report("CAP-28-install-parity", false,
            "install parity broken: smart-resolver :applied()="
            .. tostring(s_applied) .. " (reason: "
            .. tostring(h_smart:reason()) .. "), flat-table :applied()="
            .. tostring(f_applied) .. " (reason: "
            .. tostring(h_flat:reason()) .. ") — one shape worked but the "
            .. "other did not, breaking the parity contract")
    end
end)
