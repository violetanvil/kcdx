-- COMP-03 plugin A — winner half of the cross-plugin hook-on-hook pair.
--
-- Migration off legacy [[hook]] bytes= first-wins onto a
-- cross-plugin kcdx.hook{replace}. See kcdx.toml for the full design:
-- A ([load_order].priority=10) sorts before B (20) in the apply pass, so A's
-- replace does first-touch and WINS; B is CanCoexist-rejected. A reports
-- NOTHING — the GetConflictReport assertion lives in plugin B's DLL.
--
-- target = the named IsInCombat callsite wrapper (Address Library id 1007,
-- RVA 0x566040, function entry). The name resolves the address; the seed
-- carries no signature for id 1007, so we supply `bool (ptr self)`:
--   * 1 ptr arg — the AOB's `mov rax,[rcx+8]` reads through rcx (this/obj).
--   * bool return — the AOB ends `3C 01` (cmp al,1): the result is a byte
--     tested as a boolean.
-- replace returning false (0) reproduces the legacy `xor eax,eax; ret`
-- detour: the engine writes the bool back to eax/al, zeroing it.

local h = kcdx.hook{
    name      = "comp03_a",
    target    = "IsInCombat_callsite_with_stack_frame",  -- id 1007; name resolves the address
    signature = "bool (ptr self)",                       -- seed carries none for 1007
    replace   = function(self)
        -- The original never runs; return false (0) — the migration of the
        -- legacy `31 C0 C3` (xor eax,eax; ret) byte detour.
        return false
    end,
}

-- A registers and wins; it does NOT report. If registration itself failed
-- (parse/locator error → nil), log loudly so the silent-no-win is visible
-- in A's own log — B's report will independently FAIL (it would see only
-- one entry, or B applied) which is the authoritative signal.
if not h then
    kcdx.log.error("COMP03_A",
        "kcdx.hook{replace} on 'IsInCombat_callsite_with_stack_frame' "
        .. "returned nil at registration — A never queued its replace, so "
        .. "it cannot win the conflict; plugin B's GetConflictReport will "
        .. "FAIL COMP-03 (it expects A applied + B rejected)")
end
