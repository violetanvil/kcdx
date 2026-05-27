-- CAP-33 plugin.lua — author-declared named targets (targets.toml) + alias.
--
-- The targets.toml sidecar (loaded by the engine BEFORE this script runs)
-- declared three targets under this plugin's namespace
-- (ts.cap_33_author_targets, derived from [plugin].author + [plugin].name — the author never types
-- the prefix):
--   * openlibs_by_pattern  — a PATTERN locator + signature (the share-guarantee
--                            row), luaL_openlibs (seed id 1190). The 16-byte entry
--                            AOB is .text-UNIQUE (1 match, minted + confirmed by
--                            an AOB scan against the binary) and the function
--                            is entry-hooked by NOBODY, so the by-name pattern
--                            scan resolves it end-to-end — the share-guarantee proof.
--   * luaopen_math_by_id   — luaopen_math by address_id=1172 (RVA, no scan →
--                            immune to the prologue overwrite). A VERIFIED leaf
--                            that NOTHING entry-hooks (distinct from the bytes
--                            target id 1124). The prefix + alias proofs point
--                            HERE so they pass.
--   * bool_leaf_safe_site  — lua_toboolean by address_id=1124, a DISTINCT
--                            verified leaf that NOTHING hooks (pristine
--                            prologue) for the kcdx.bytes resolution proof.
--
-- Every hook below is a no-op passthrough whose ONLY job is to resolve a NAME
-- and apply. We assert :applied() at kcdx.on("ready") (after the apply pass).
-- The id-located hook target is luaopen_math — lualibs[] entry 6, invoked
-- EXACTLY ONCE during luaL_openlibs at Lua boot and never again, and entry-hooked
-- by nobody (the live run hooks only CGame_per_frame_ui_pump id 1003 + the
-- IsInCombat callsites 1006/1007; production hooks lua_pcall/CGame::Update/
-- l_alloc). The empty before-hook installs cleanly and is the lowest-frequency
-- verified target available. The install IS the proof the name resolved; the
-- hook never needs to fire.
--
-- WHY NOT id 1003 (the earlier target): cap-03 ALREADY entry-hooks id 1003
-- (CGame_per_frame_ui_pump) via the legacy [[hook]] first-wins path, so a second
-- hook on it loses with "already hooked by 'cap03_update_callee'". Repointed to
-- the unhooked luaopen_math so the prefix/alias install can succeed.

-- ====================================================================
-- (1) CAP-33-pattern-by-name — THE SHARE-GUARANTEE HEADLINE.
-- kcdx.hook{ target = "<own pattern target>" } with NO signature=. The pure
-- "author names a target BY AOB PATTERN and hooks it by name" proof: the
-- pattern carries the ADDRESS (a .text-unique entry AOB) and the target's
-- signature carries the ABI, both delivered by the bare name with zero hex at
-- the call site (the disassembler test's sharpest edge — one expert names an
-- AOB site once, every other author hooks it by name with no hex).
-- The target is luaL_openlibs (seed id 1190) — its 16-byte entry AOB is
-- .text-unique (confirmed by an AOB scan against the binary) and NOTHING
-- entry-hooks it, so the prologue stays pristine and the by-name pattern scan
-- resolves end-to-end. If the binder did not get the signature FROM the target
-- it would reject ("a signature is required"); install with no inline sig IS
-- the proof the name supplied both.
-- ====================================================================
local hPattern = kcdx.hook{
    name   = "cap33_pattern_by_name",
    target = "openlibs_by_pattern",      -- author-declared PATTERN target; carries the sig
    before = function(self) return self end,  -- no-op passthrough
}

-- ====================================================================
-- (2) CAP-33-engine-tier — the ENGINE tier of self>engine>other still works.
-- A bare name that is an engine seed ("luaL_loadfile", id 1002) resolves to
-- the engine's own target. Proves the author's targets COEXIST with the
-- engine name table (precedence, not partition).
-- ====================================================================
local hEngine = kcdx.hook{
    name   = "cap33_engine_tier",
    target = "luaL_loadfile",           -- ENGINE seed name (resolves + carries ABI)
    before = function(L, filename) return L, filename end,
}

-- ====================================================================
-- (3) CAP-33-prefixed — the explicit "<pluginname>.<target>" form.
-- Unambiguous from anywhere and never warns. Resolves the VERIFIED-ID target
-- luaopen_math_by_id (address_id=1172 — RVA, no scan), so this proves PREFIX
-- resolution end-to-end and can PASS (immune to the pattern row's blocker, and
-- on a function NOTHING entry-hooks so the install is not first-wins-blocked).
-- Carries no signature= (the target does). Empty before-hook (returns nothing →
-- original runs unchanged; the install IS the proof).
-- ====================================================================
local hPrefixed = kcdx.hook{
    name   = "cap33_prefixed",
    target = "ts.cap_33_author_targets.luaopen_math_by_id",  -- explicit prefix → verified-id target
    before = function() end,                              -- no-op (returns nothing)
}

-- ====================================================================
-- (4) CAP-33-alias — kcdx.alias declares a local handle, then hook via it.
-- kcdx.alias substitutes the long prefixed name before resolving; the alias is
-- plugin-scoped and cannot shadow. The hook gives no signature= — the alias
-- resolves to the VERIFIED-ID target luaopen_math_by_id, which carries the ABI,
-- so this proves ALIAS resolution end-to-end and can PASS. Empty before-hook
-- (returns nothing → original runs unchanged).
-- ====================================================================
local aliasOk, aliasErr = kcdx.alias("up", "ts.cap_33_author_targets.luaopen_math_by_id")
if aliasOk ~= true then
    kcdx.log.error("CAP33", "kcdx.alias failed: " .. tostring(aliasErr))
end
local hAlias = kcdx.hook{
    name   = "cap33_alias",
    target = "up",                      -- the local alias → the verified-id target
    before = function() end,            -- no-op (returns nothing)
}

-- ====================================================================
-- (5) CAP-33-bytes-by-name — kcdx.bytes{ target = "<name>" } resolution.
-- Resolves the author-declared VERIFIED-ID target "bool_leaf_safe_site" to
-- lua_toboolean's entry VA (address_id=1124) — a DISTINCT verified leaf that
-- NOTHING hooks, so the prologue is pristine and the original-byte verify is
-- correct (no detour bytes). Byte 0 of the lua_toboolean entry is 0x48 (read
-- directly from WHGame.dll: "48 83 EC 28 ..."), so writing 0x48 over 0x48 is
-- an IDEMPOTENT NO-OP that verifies AND applies without changing behaviour. We
-- assert RESOLUTION via :applied()==true (the write succeeds only if the name
-- resolved to a real, writable VA) rather than a destructive edit.
-- ====================================================================
local hBytes = kcdx.bytes{
    name        = "cap33_bytes_by_name",
    target      = "bool_leaf_safe_site",   -- author-declared VERIFIED-ID target
    original    = "48",                    -- verified byte 0 of lua_toboolean entry (WHGame.dll)
    replacement = "48",                    -- same byte: idempotent no-op
}

-- Handles resolve to a final :applied() only AFTER the zone apply pass, which
-- runs after this plugin.lua returns. Read them in kcdx.on("ready").
kcdx.on("ready", function()
    -- (1) SHARE-GUARANTEE HEADLINE — the author-declared PATTERN target resolves BY NAME.
    do
        local applied = hPattern:applied()
        kcdx.test.report("CAP-33-pattern-by-name", applied == true,
            applied == true
              and ("kcdx.hook{ target=\"openlibs_by_pattern\" } applied with NO "
                   .. "signature= — the author-declared PATTERN target supplied "
                   .. "BOTH address (.text-unique AOB) and ABI by name "
                   .. "(the share guarantee: name once, hook by name forever)")
              or  ("expected applied()==true (pattern target resolves by name + "
                   .. "carries the ABI); got applied=" .. tostring(applied)
                   .. " reason=" .. tostring(hPattern:reason())))
    end

    -- (2) engine tier.
    do
        local applied = hEngine:applied()
        kcdx.test.report("CAP-33-engine-tier", applied == true,
            applied == true
              and "kcdx.hook{ target=\"luaL_loadfile\" } (engine seed) resolved — engine tier coexists with author targets"
              or  ("expected applied()==true for the engine seed name; got "
                   .. "applied=" .. tostring(applied)
                   .. " reason=" .. tostring(hEngine:reason())))
    end

    -- (3) explicit prefix → verified-id target.
    do
        local applied = hPrefixed:applied()
        kcdx.test.report("CAP-33-prefixed", applied == true,
            applied == true
              and "explicit \"ts.cap_33_author_targets.luaopen_math_by_id\" (address_id=1172) resolved directly"
              or  ("expected applied()==true for the prefixed form; got "
                   .. "applied=" .. tostring(applied)
                   .. " reason=" .. tostring(hPrefixed:reason())))
    end

    -- (4) alias → verified-id target.
    do
        local applied = hAlias:applied()
        kcdx.test.report("CAP-33-alias", applied == true and aliasOk == true,
            (applied == true and aliasOk == true)
              and "kcdx.alias(\"up\", \"...luaopen_math_by_id\") + kcdx.hook{ target=\"up\" } resolved via the alias"
              or  ("expected aliasOk==true AND applied()==true; got aliasOk="
                   .. tostring(aliasOk) .. " applied=" .. tostring(applied)
                   .. " reason=" .. tostring(hAlias:reason())))
    end

    -- (5) kcdx.bytes target=<name> → verified-id target.
    do
        local applied = hBytes:applied()
        kcdx.test.report("CAP-33-bytes-by-name", applied == true,
            applied == true
              and "kcdx.bytes{ target=\"bool_leaf_safe_site\" } (address_id=1124) resolved the author-target to a writable VA; idempotent no-op write applied"
              or  ("expected applied()==true (name resolved to a writable VA); "
                   .. "got applied=" .. tostring(applied)
                   .. " reason=" .. tostring(hBytes:reason())))
    end
end)

kcdx.log.info("CAP33",
    "registered author-target hooks (pattern-by-name BLOCKED, engine-tier, "
    .. "prefixed+alias on verified-unhooked id 1172 luaopen_math) + kcdx.bytes target=bool_leaf_safe_site "
    .. "(verified id 1124); applied() asserted at ready")
