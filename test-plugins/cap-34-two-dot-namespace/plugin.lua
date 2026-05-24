-- CAP-34 plugin.lua — the 2-dot namespace model (`<author>.<plugin>.<bare>`).
--
-- The targets.toml sidecar declared ONE bare target under this plugin's
-- namespace (author=ts, name=cap_34_two_dot_namespace → registered as
-- `ts.cap_34_two_dot_namespace.ui_pump_self`, locating luaopen_math by
-- address_id=1172). The five assertions below exercise the resolver tiers
-- the 2-dot refactor cares about:
--
--   (1) CAP-34-explicit-2dot       — the explicit 3-segment author.plugin.bare
--                                   form for THIS plugin's own target.
--   (2) CAP-34-explicit-1dot-kcdx  — the 2-segment kcdx.<seedname> form (engine
--                                   seed under reserved author='kcdx').
--   (3) CAP-34-bare-self           — the bare name resolves to THIS plugin's
--                                   own target via the self-tier of
--                                   self>engine>other (no prefix typed).
--   (4) CAP-34-alias-2dot          — kcdx.alias substitutes a 3-segment target
--                                   string + the hook resolves via the alias.
--   (5) CAP-34-cross-plugin-2dot   — a 3-segment reference to ANOTHER plugin's
--                                   target (cap-33's luaopen_math_by_id).
--
-- Every hook below is a no-op before-hook whose ONLY job is to resolve a NAME
-- and apply. We assert :applied() at kcdx.on("ready") (after the apply pass).
-- Target choice (luaopen_math, id 1172) and rationale mirror cap-33's prefix/
-- alias rows; the install IS the proof, the hook never needs to fire. Cap-33
-- already installs multiple before-hooks on this leaf, so the chain semantics
-- here are an established pattern (CAP-20-chain: "two before hooks on one
-- target chain in load order").

-- ====================================================================
-- (1) CAP-34-explicit-2dot — the explicit 3-segment author.plugin.bare form.
-- The pure "explicit 2-dot resolution" proof: `ts.cap_34_two_dot_namespace.
-- ui_pump_self` resolves directly via the parsed 3-segment form. Unambiguous
-- from anywhere; never warns. No signature= (the target carries the ABI).
-- ====================================================================
local hExplicit2Dot = kcdx.hook{
    name   = "cap34_explicit_2dot",
    target = "ts.cap_34_two_dot_namespace.ui_pump_self",  -- 3-segment author.plugin.bare
    before = function() end,                              -- no-op
}

-- ====================================================================
-- (2) CAP-34-explicit-1dot-kcdx — the 2-segment kcdx.<seedname> form.
-- Engine seed names live under the reserved author='kcdx' (so the canonical
-- form for a seed is `kcdx.<seedname>` — 2 segments because the engine's
-- "plugin" half of the namespace collapses into the reserved root). Proves
-- the kcdx.* root resolves under the 2-dot model.
-- ====================================================================
local hExplicit1DotKcdx = kcdx.hook{
    name   = "cap34_explicit_1dot_kcdx",
    target = "kcdx.luaL_loadfile",      -- 2-segment kcdx.<seedname> (engine root)
    before = function(L, filename) return L, filename end,
}

-- ====================================================================
-- (3) CAP-34-bare-self — the bare name resolves to THIS plugin's target via
-- self-tier. No prefix typed — the resolver finds (owningAuthor=ts,
-- owningPlugin=cap_34_two_dot_namespace, bareName=ui_pump_self) before
-- falling through to engine or other-plugin. Proves bare-name self-tier
-- resolution works under the 2-dot model.
-- ====================================================================
local hBareSelf = kcdx.hook{
    name   = "cap34_bare_self",
    target = "ui_pump_self",            -- BARE — no author/plugin prefix
    before = function() end,            -- no-op
}

-- ====================================================================
-- (4) CAP-34-alias-2dot — kcdx.alias("short", "<3-segment>") then hook via the
-- alias. kcdx.alias substitutes the long author.plugin.bare string before
-- resolving; the alias is plugin-scoped. Proves alias substitution composes
-- with the 3-segment form.
-- ====================================================================
local aliasOk, aliasErr = kcdx.alias("short", "ts.cap_34_two_dot_namespace.ui_pump_self")
if aliasOk ~= true then
    kcdx.log.error("CAP34", "kcdx.alias failed: " .. tostring(aliasErr))
end
local hAlias2Dot = kcdx.hook{
    name   = "cap34_alias_2dot",
    target = "short",                   -- the local alias → the 3-segment target
    before = function() end,            -- no-op
}

-- ====================================================================
-- (5) CAP-34-cross-plugin-2dot — a 3-segment reference to ANOTHER plugin's
-- target. Resolves `ts.cap_33_author_targets.luaopen_math_by_id` (cap-33's
-- verified-id target). Proves cross-plugin 3-segment lookup works under the
-- 2-dot model. CAVEAT: this needs cap-33-author-targets installed in the same
-- suite; in the regression suite it always is. If cap-34 is ever run in
-- isolation, this row fails gracefully via :applied()==false — fine, no other
-- assertion depends on it.
-- ====================================================================
local hCrossPlugin = kcdx.hook{
    name   = "cap34_cross_plugin_2dot",
    target = "ts.cap_33_author_targets.luaopen_math_by_id",  -- 3-segment, other-plugin's bare
    before = function() end,                                  -- no-op
}

-- Handles resolve to a final :applied() only AFTER the zone apply pass, which
-- runs after this plugin.lua returns. Read them in kcdx.on("ready").
kcdx.on("ready", function()
    -- (1) explicit 3-segment author.plugin.bare.
    do
        local applied = hExplicit2Dot:applied()
        kcdx.test.report("CAP-34-explicit-2dot", applied == true,
            applied == true
              and ("kcdx.hook{ target=\"ts.cap_34_two_dot_namespace.ui_pump_self\" } "
                   .. "applied — explicit 3-segment author.plugin.bare form resolves "
                   .. "directly under the 2-dot model")
              or  ("expected applied()==true for the explicit 3-segment form; got "
                   .. "applied=" .. tostring(applied)
                   .. " reason=" .. tostring(hExplicit2Dot:reason())))
    end

    -- (2) explicit 2-segment kcdx.<seedname> (engine root).
    do
        local applied = hExplicit1DotKcdx:applied()
        kcdx.test.report("CAP-34-explicit-1dot-kcdx", applied == true,
            applied == true
              and ("kcdx.hook{ target=\"kcdx.luaL_loadfile\" } applied — the "
                   .. "2-segment kcdx.<seedname> form (reserved 'kcdx' author "
                   .. "for engine seed) resolves")
              or  ("expected applied()==true for the kcdx.<seedname> form; got "
                   .. "applied=" .. tostring(applied)
                   .. " reason=" .. tostring(hExplicit1DotKcdx:reason())))
    end

    -- (3) bare name → self-tier (this plugin's own target).
    do
        local applied = hBareSelf:applied()
        kcdx.test.report("CAP-34-bare-self", applied == true,
            applied == true
              and ("kcdx.hook{ target=\"ui_pump_self\" } applied — bare-name "
                   .. "self-tier resolution found (author=ts, plugin="
                   .. "cap_34_two_dot_namespace, bare=ui_pump_self)")
              or  ("expected applied()==true for bare-name self-tier resolution; "
                   .. "got applied=" .. tostring(applied)
                   .. " reason=" .. tostring(hBareSelf:reason())))
    end

    -- (4) alias → 3-segment target.
    do
        local applied = hAlias2Dot:applied()
        kcdx.test.report("CAP-34-alias-2dot", applied == true and aliasOk == true,
            (applied == true and aliasOk == true)
              and ("kcdx.alias(\"short\", \"ts.cap_34_two_dot_namespace.ui_pump_self\") "
                   .. "+ kcdx.hook{ target=\"short\" } applied — alias substitution "
                   .. "composes with the 3-segment form")
              or  ("expected aliasOk==true AND applied()==true; got aliasOk="
                   .. tostring(aliasOk) .. " applied=" .. tostring(applied)
                   .. " reason=" .. tostring(hAlias2Dot:reason())))
    end

    -- (5) cross-plugin 3-segment reference.
    do
        local applied = hCrossPlugin:applied()
        kcdx.test.report("CAP-34-cross-plugin-2dot", applied == true,
            applied == true
              and ("kcdx.hook{ target=\"ts.cap_33_author_targets.luaopen_math_by_id\" } "
                   .. "applied — 3-segment cross-plugin reference resolves "
                   .. "(other-plugin tier of self>engine>other)")
              or  ("expected applied()==true for cross-plugin 3-segment lookup; got "
                   .. "applied=" .. tostring(applied)
                   .. " reason=" .. tostring(hCrossPlugin:reason())))
    end
end)

kcdx.log.info("CAP34",
    "registered 2-dot namespace hooks (explicit-2dot, explicit-1dot-kcdx, "
    .. "bare-self, alias-2dot, cross-plugin-2dot) on verified-unhooked id 1172 "
    .. "luaopen_math + id 1002 luaL_loadfile; applied() asserted at ready")
