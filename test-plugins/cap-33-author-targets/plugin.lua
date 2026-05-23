-- CAP-33 plugin.lua — author-declared named targets (targets.toml) + alias.
--
-- The targets.toml sidecar (loaded by the engine BEFORE this script runs)
-- declared two targets under this plugin's namespace
-- (cap_33_author_targets, derived from [plugin].name — the author never types
-- the prefix):
--   * loadfile_by_pattern  — a PATTERN locator + a signature (the §36 row)
--   * loadbuffer_safe_site  — a PATTERN locator on the luaL_loadbuffer entry AOB
--
-- Every hook below is a no-op passthrough whose ONLY job is to resolve a NAME
-- and apply. We assert :applied() at kcdx.on("ready") (after the apply pass).
-- All targets are luaL_loadfile, which fires only during pre-update Scripts/
-- loading — dormant post-install, so installing a detour is harmless (the
-- same safe-target reasoning cap-20 uses for CAP-20-target). The install IS
-- the proof the name resolved; the hook never needs to fire.

local SIG = "i32 (ptr L, cstr filename)"

-- ====================================================================
-- (1) CAP-33-pattern-by-name — THE §36 HEADLINE.
-- kcdx.hook{ target = "<own pattern target>" } with NO signature=. The name
-- carries BOTH the address (resolved from the author-declared AOB through the
-- patch engine) AND the verified ABI (the target's signature). If the binder
-- had not gotten the signature from the target it would have rejected with
-- "no verified signature"; applied()==true is the end-to-end proof that a
-- pattern site named ONCE is hookable BY NAME with zero hex/ABI here.
-- ====================================================================
local hPattern = kcdx.hook{
    name   = "cap33_pattern_by_name",
    target = "loadfile_by_pattern",     -- author-declared PATTERN target; carries the sig
    before = function(L, filename) return L, filename end,  -- no-op passthrough
}

-- ====================================================================
-- (2) CAP-33-engine-tier — the ENGINE tier of self>engine>other still works.
-- A bare name that is an engine seed ("luaL_loadfile", id 1002) resolves to
-- the engine's own target. Proves the author's targets COEXIST with the
-- engine name table (precedence, not partition — naming-namespaces.md).
-- ====================================================================
local hEngine = kcdx.hook{
    name   = "cap33_engine_tier",
    target = "luaL_loadfile",           -- ENGINE seed name (resolves + carries ABI)
    before = function(L, filename) return L, filename end,
}

-- ====================================================================
-- (3) CAP-33-prefixed — the explicit "<pluginname>.<target>" form.
-- Unambiguous from anywhere and never warns; resolves the SAME pattern target
-- as (1). Carries no signature= (the target does).
-- ====================================================================
local hPrefixed = kcdx.hook{
    name   = "cap33_prefixed",
    target = "cap_33_author_targets.loadfile_by_pattern",  -- explicit prefix
    before = function(L, filename) return L, filename end,
}

-- ====================================================================
-- (4) CAP-33-alias — kcdx.alias declares a local handle, then hook via it.
-- kcdx.alias substitutes the long prefixed name before resolving; the alias is
-- plugin-scoped and cannot shadow. The hook gives no signature= — the alias
-- resolves to the pattern target, which carries the ABI.
-- ====================================================================
local aliasOk, aliasErr = kcdx.alias("lf", "cap_33_author_targets.loadfile_by_pattern")
if aliasOk ~= true then
    kcdx.log.error("CAP33", "kcdx.alias failed: " .. tostring(aliasErr))
end
local hAlias = kcdx.hook{
    name   = "cap33_alias",
    target = "lf",                      -- the local alias → the pattern target
    before = function(L, filename) return L, filename end,
}

-- ====================================================================
-- (5) CAP-33-bytes-by-name — kcdx.bytes{ target = "<name>" } resolution.
-- Resolves the author-declared PATTERN target "loadbuffer_safe_site" to
-- luaL_loadbuffer's entry VA — a DISTINCT function from the hooked
-- luaL_loadfile, so the byte write lands on memory NO cap-33 hook touches (no
-- hook/bytes overlap on one address). The write is an IDEMPOTENT NO-OP: byte 0
-- of the entry is 0x48 (the AOB in targets.toml begins "48 83 EC 38"), so
-- writing 0x48 over 0x48 applies cleanly without changing behaviour. We assert
-- RESOLUTION via :applied()==true (the byte write succeeds only if the name
-- resolved to a real, writable VA) rather than a destructive edit.
-- ====================================================================
local hBytes = kcdx.bytes{
    name        = "cap33_bytes_by_name",
    target      = "loadbuffer_safe_site",  -- author-declared PATTERN target
    original    = "48",                    -- verified byte 0 of luaL_loadbuffer entry
    replacement = "48",                    -- same byte: idempotent no-op
}

-- Handles resolve to a final :applied() only AFTER the zone apply pass, which
-- runs after this plugin.lua returns. Read them in kcdx.on("ready").
kcdx.on("ready", function()
    -- (1) §36 HEADLINE.
    do
        local applied = hPattern:applied()
        kcdx.test.report("CAP-33-pattern-by-name", applied == true,
            applied == true
              and ("kcdx.hook{ target=\"loadfile_by_pattern\" } applied with NO "
                   .. "signature= — the author-declared PATTERN target supplied "
                   .. "BOTH address and ABI by name (cornerstones §36)")
              or  ("expected applied()==true (pattern target resolves by name "
                   .. "+ carries the ABI); got applied=" .. tostring(applied)
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

    -- (3) explicit prefix.
    do
        local applied = hPrefixed:applied()
        kcdx.test.report("CAP-33-prefixed", applied == true,
            applied == true
              and "explicit \"cap_33_author_targets.loadfile_by_pattern\" resolved directly"
              or  ("expected applied()==true for the prefixed form; got "
                   .. "applied=" .. tostring(applied)
                   .. " reason=" .. tostring(hPrefixed:reason())))
    end

    -- (4) alias.
    do
        local applied = hAlias:applied()
        kcdx.test.report("CAP-33-alias", applied == true and aliasOk == true,
            (applied == true and aliasOk == true)
              and "kcdx.alias(\"lf\", \"...loadfile_by_pattern\") + kcdx.hook{ target=\"lf\" } resolved via the alias"
              or  ("expected aliasOk==true AND applied()==true; got aliasOk="
                   .. tostring(aliasOk) .. " applied=" .. tostring(applied)
                   .. " reason=" .. tostring(hAlias:reason())))
    end

    -- (5) kcdx.bytes target=<name>.
    do
        local applied = hBytes:applied()
        kcdx.test.report("CAP-33-bytes-by-name", applied == true,
            applied == true
              and "kcdx.bytes{ target=\"loadbuffer_safe_site\" } resolved the PATTERN author-target to a VA; idempotent no-op write applied"
              or  ("expected applied()==true (name resolved to a writable VA); "
                   .. "got applied=" .. tostring(applied)
                   .. " reason=" .. tostring(hBytes:reason())))
    end
end)

kcdx.log.info("CAP33",
    "registered author-target hooks (pattern-by-name, engine-tier, prefixed, "
    .. "alias) + kcdx.bytes target=loadbuffer_safe_site; applied() asserted at ready")
