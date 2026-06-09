-- CAP-38 sig-mismatch gate — Lua half (both surfaces of one capability are
-- tested — the authoring surface is one model in two languages, mirrored).
--
-- Pair of cap-38-sig-mismatch-gate/ (the C++ DLL). Both surfaces carry the
-- SAME footgun and the SAME gate: a named target carrying a verified
-- Address-Library ABI + an explicit signature= was SILENTLY trusted. The
-- gate's core behavior is (c): keep + proceed (consult the verified ABI to
-- DETECT the conflict, emit a teaching diagnostic, then proceed with the
-- explicit sig as authored). The diagnostic SEVERITY splits by
-- ClassifyConflict: cap-38's return-width delta (i32 vs void) is a HARD
-- conflict → the gate logs at ERROR (a known crash risk on a live engine
-- function); a soft same-shape conflict stays at WARN. The C++ half drives
-- kcdxHookInterface; THIS half drives kcdx.hook — same model, the Lua spelling.
--
-- Named target: `kcdx.luaopen_table` (engine seed id 1173, verified ABI
-- "i32 (ptr L)"). WRONG explicit signature: "void (ptr L)" (same arg count,
-- RETURN-WIDTH delta — i32 collapsed to void → NOT SignaturesCompatible →
-- the gate fires). The conflict is a return-width delta, not arg-count, so
-- no hypothetical arg-count chain issue applies even on coexistence.
--
-- Row:
--   * CAP-38-lua-gate-proceeds (AUTO, boot-only, asserted at
--     kcdx.on("ready")): the install PROCEEDED with the explicit sig —
--     handle:applied()==true. behavior-c keeps the explicit sig
--     authoritative; the install succeeds. FALSIFIABLE against a
--     hypothetical (a)-reject impl: a reject would leave applied()==false
--     with a non-empty :reason() and this row FAILS. (The install IS the
--     proof, the cap-33/34/35 idiom — the hook never needs to fire for the
--     resolve-and-apply assertion.)
--
-- The gate's HARD-conflict line is the [manual] CAP-38-lua-gate-warn row:
-- a post-run log grep confirms it in the engine log. cap-38's mismatch is a
-- RETURN-WIDTH delta (i32 verified vs void explicit) → ClassifyConflict
-- returns Hard → the gate emits at ERROR level (not WARN). The EXACT line:
-- LEVEL=Error, category HOOK_SIG_GATE, action
-- `explicit_overrides_verified_hard` (keys target / plugin / explicit_sig /
-- verified_sig / used / severity=hard / crash_risk=true / note). Pre-fix the
-- gate emitted action `explicit_overrides_verified` at WARN — so this row
-- catches both a silent-trust regression AND a downgrade-to-WARN regression.
-- This plugin does NOT scrape the log itself.
--
-- SAFETY — the target is gameplay-COLD. luaopen_table is lualibs[] entry 2,
-- called EXACTLY ONCE at Lua library-init (boot) and NEVER during gameplay.
-- cap-38's hook installs at the first-update-tick AFTER that single call →
-- the wrong-ABI thunk is installed but NEVER FIRES → no register/stack
-- corruption, no crash. This is the cap-33 cold-leaf idiom (cap-33 documents
-- the same property for luaopen_math: "the install IS the proof, the hook
-- never needs to fire"). The OLD target was the HOT lua_settable, whose
-- wrong-ABI thunk DID fire on the live save-load path and crashed the game
-- (the 0xC8 root cause): the wrong THUNK corrupts regardless of how polite
-- the observer is — the observer's politeness was never the safety property,
-- a cold no-fire target is.

-- Named target carries verified ABI "i32 (ptr L)"; the [opts] signature is
-- WRONG on purpose (return-width delta) — the sig-mismatch gate detects the
-- conflict, logs at ERROR (hard), and the install PROCEEDS with the explicit
-- sig (behavior-c).
local h = kcdx.hook.before("WHGame.dll", "kcdx.luaopen_table",
    function(L)
        -- No-op observer. Never relied upon to fire — the target is
        -- gameplay-cold (luaopen_table runs once at boot, before this hook
        -- installs), so this thunk is installed but never invoked.
        return
    end,
    { name = "cap38_lua_gate", signature = "void (ptr L)" })

if not h then
    kcdx.log.error("CAP38_LUA",
        "kcdx.hook on named target 'kcdx.luaopen_table' + wrong sig "
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
            "named target 'kcdx.luaopen_table' + WRONG explicit sig "
            .. "'void (ptr L)' (verified ABI 'i32 (ptr L)' — return-width "
            .. "delta): applied()=%s, reason=%q. behavior-c keeps the explicit sig "
            .. "authoritative and the install PROCEEDS; a hypothetical "
            .. "(a)-reject impl would leave applied()==false with a non-"
            .. "empty reason and FAIL this row. The gate-WARN is the "
            .. "[manual] CAP-38-lua-gate-warn row (grep HOOK_SIG_GATE "
            .. "explicit_overrides_verified).",
            tostring(applied), tostring(reason)))
end)
