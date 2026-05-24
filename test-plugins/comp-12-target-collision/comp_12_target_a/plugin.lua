-- COMP-12 plugin A — self-wins asserter.
--
-- A's targets.toml declared bare `combat_check` = luaopen_math by
-- address_id=1172 (verified RVA 0x009299AC + ABI "i32 (ptr L)"; located by id,
-- so it resolves and applies regardless of any hook on the function). Plugin B
-- (sibling) declared bare `combat_check` = a bogus non-matching pattern. Both
-- own the bare name → a cross-plugin collision.
--
-- A hooks the BARE name `combat_check`. Per self > engine > other
-- (naming-namespaces.md) the calling plugin's OWN target wins, so A's hook
-- must resolve A's GOOD locator and APPLY. Had B's target leaked instead,
-- A's hook would resolve B's bogus pattern, fail to match, and NOT apply —
-- so applied()==true is a falsifiable proof that SELF won.
--
-- luaopen_math is a VERIFIED leaf that NOTHING entry-hooks (the live run hooks
-- only CGame_per_frame_ui_pump id 1003 + the IsInCombat callsites 1006/1007),
-- so A's empty before-hook is free to INSTALL — the apply outcome reflects
-- precedence, not a first-wins collision with some other hook. It is lualibs[]
-- entry 6, invoked exactly ONCE at Lua boot, so the no-op detour is harmless.
-- (Earlier this targeted id 1003, which cap-03 already entry-hooks via the
-- legacy first-wins path — A's install lost there for a reason unrelated to
-- precedence; repointed to the unhooked luaopen_math.)

local hSelf = kcdx.hook{
    name   = "comp12_self",
    target = "combat_check",   -- BARE name: A owns one, B owns one → self wins
    before = function() end,   -- no-op (returns nothing → original runs unchanged)
}

kcdx.on("ready", function()
    local applied = hSelf:applied()
    kcdx.test.report("COMP-12-self-wins", applied == true,
        applied == true
          and ("bare `combat_check` resolved A's OWN target (self>engine>other): "
               .. "the hook applied — A's good locator won, not B's bogus one")
          or  ("expected applied()==true (self's good target wins the bare "
               .. "collision); got applied=" .. tostring(applied)
               .. " reason=" .. tostring(hSelf:reason())
               .. " — if reason mentions a non-matching pattern, B's target "
               .. "leaked into A's resolution (precedence broken)"))
end)

kcdx.log.info("COMP12",
    "plugin A hooked bare `combat_check`; asserting self's target won the "
    .. "cross-plugin collision at ready (the once-per-session bare-collision "
    .. "warn is observable in the NAMESPACE log)")
