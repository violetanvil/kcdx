-- CAP-38 sig-mismatch gate — Lua half (parity-is-tested, lua-api-surface.md).
--
-- Pair of cap-38-sig-mismatch-gate/ (the C++ DLL). Both surfaces carry the
-- SAME footgun and the SAME gate: a named target carrying a verified
-- Address-Library ABI + an explicit signature= was SILENTLY trusted. The
-- gate's core behavior is (c): WARN + keep the explicit sig (consult the
-- verified ABI to DETECT the conflict, emit a teaching WARN, then proceed
-- with the explicit sig as authored). The C++ half drives kcdxHookInterface;
-- THIS half drives kcdx.hook — same model, the Lua spelling.
--
-- Named target: `kcdx.lua_settable` (engine seed id 1186, verified ABI
-- "void (ptr L, i32 idx)"). WRONG explicit signature: "void (ptr L)" (1
-- arg → arg-count delta vs the verified 2-arg ABI → NOT
-- SignaturesCompatible → the gate fires).
--
-- Row:
--   * CAP-38-lua-gate-proceeds (AUTO, boot-only, asserted at
--     kcdx.on("ready")): the install PROCEEDED with the explicit sig —
--     handle:applied()==true. behavior-c keeps the explicit sig
--     authoritative; the install succeeds. FALSIFIABLE against a
--     hypothetical (a)-reject impl: a reject would leave applied()==false
--     with a non-empty :reason() and this row FAILS. (The install IS the
--     proof, the cap-33/34/35 idiom — the hook never needs to fire for the
--     resolve-and-apply assertion. The C++ half additionally proves the
--     hook FIRES per the explicit sig.)
--
-- The gate-WARN line is the [manual] CAP-38-lua-gate-warn row: the
-- orchestrator greps the engine log for category HOOK_SIG_GATE, action
-- explicit_overrides_verified (keys target / plugin / explicit_sig /
-- verified_sig / used). This plugin does NOT scrape the log itself.
--
-- SAFETY — hooking the hot lua_settable with a wrong 1-arg sig is safe: the
-- before observer mutates nothing and does not skip the original; the
-- detour preserves lua_settable's real registers, so it runs untouched.

local h = kcdx.hook{
    name      = "cap38_lua_gate",
    target    = "kcdx.lua_settable",      -- verified ABI "void (ptr L, i32 idx)"
    signature = "void (ptr L)",            -- WRONG on purpose (1 arg)
    before    = function(L)
        -- Pure observer: read nothing, mutate nothing, do not skip. The
        -- wrong sig mis-describes the ABI (the gate's whole point) but the
        -- observer never acts on the missing arg.
        return
    end,
}

if not h then
    kcdx.log.error("CAP38_LUA",
        "kcdx.hook on named target 'kcdx.lua_settable' + wrong sig "
        .. "'void (ptr L)' returned nil at registration — the gate "
        .. "REJECTED the mismatch synchronously instead of WARN+proceed "
        .. "(behavior-c violated)")
    kcdx.test.report("CAP-38-lua-gate-proceeds", false,
        "kcdx.hook returned nil — gate rejected the named-target + wrong-sig "
        .. "install synchronously (expected behavior-c: WARN + proceed)")
    return
end

kcdx.on("ready", function()
    local applied = h:applied()
    local reason  = h:reason()
    local pass = applied == true
    kcdx.test.report("CAP-38-lua-gate-proceeds", pass,
        string.format(
            "named target 'kcdx.lua_settable' + WRONG explicit sig "
            .. "'void (ptr L)' (verified ABI 'void (ptr L, i32 idx)'): "
            .. "applied()=%s, reason=%q. behavior-c keeps the explicit sig "
            .. "authoritative and the install PROCEEDS; a hypothetical "
            .. "(a)-reject impl would leave applied()==false with a non-"
            .. "empty reason and FAIL this row. The gate-WARN is the "
            .. "[manual] CAP-38-lua-gate-warn row (grep HOOK_SIG_GATE "
            .. "explicit_overrides_verified).",
            tostring(applied), tostring(reason)))
end)
