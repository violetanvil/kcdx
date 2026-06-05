-- COMP-16 consumer B (CODE form) — runtime cross-mod replace of a published name.
--
-- The CODE half of cross-mod resolution (design §5.3). Step 8b left this verb
-- returning a teaching error for a packed cross-mod target ("cross-mod resolution
-- lands next step"); step 8c makes it RESOLVE: the packed name resolves to
-- publisher A's serve-vpath and the runtime-overlay store is keyed by it.
--
-- B replaces A's published name `belt` (sidecar-published by A at BUILD time, so
-- it is in the runtime published-name store before any plugin.lua runs — B's
-- resolution is independent of A's plugin.lua ordering). The author writes A's
-- NAME, never A's vpath (the disassembler test, cornerstones.md).

local PACKED = "ts.comp_16_publisher_a.belt"  -- A's sidecar-published name
local WITH   = "data/comp16_b.xml"            -- B's own replacement file

-- ====================================================================
-- COMP-16-replace-code — the runtime code-form cross-mod replace RESOLVES
--   the packed published name to A's serve-vpath and returns the loadable
--   path of B's `with` file (the runtime-overlay store was keyed at A's
--   serve-vpath). This is the step-8c change: step 8b returned a teaching
--   error for a packed cross-mod target; this step resolves it.
--
-- FALSIFIABLE:
--   * a (nil, err) return for a RESOLVABLE published name -> FAIL (the
--     step-8b cross-mod stub was NOT replaced — the resolution did not
--     land, or A's publish was not visible to B's resolution);
--   * a non-string / empty return -> FAIL.
-- Only a non-nil loadable path (B's `with` resolved + the store keyed by A's
-- serve-vpath) is PASS. The actual SERVE at A's vpath is the in-game
-- COMP-16-serve-code row.
-- ====================================================================
do
    local ret, err = kcdx.assets.replace(PACKED, WITH)

    if type(ret) ~= "string" or ret == "" then
        kcdx.test.report("COMP-16-replace-code", false,
            "kcdx.assets.replace(\"" .. PACKED .. "\", \"" .. WITH .. "\") "
            .. "returned " .. tostring(ret) .. " (err: " .. tostring(err)
            .. ") — a PACKED cross-mod target that A published (`belt`) must now "
            .. "RESOLVE: hop 1 resolves the name to A's serve-vpath, hop 2 keys "
            .. "the runtime-overlay store by it, replace returns B's `with` "
            .. "loadable path. A (nil, err) here means the step-8b cross-mod "
            .. "teaching-error stub was NOT replaced (resolution did not land), "
            .. "or A's publish was not visible to B's resolution")
    else
        kcdx.test.report("COMP-16-replace-code", true,
            "kcdx.assets.replace(\"" .. PACKED .. "\", \"" .. WITH .. "\") "
            .. "RESOLVED the packed cross-mod published name to A's serve-vpath "
            .. "and returned B's `with` loadable path (\"" .. ret .. "\") — the "
            .. "two-hop §5.3 resolution: the name resolved to the vpath A's "
            .. "`belt` serves at, the runtime-overlay store was keyed by it. The "
            .. "engine opening A's serve-vpath now serves B's file (the in-game "
            .. "COMP-16-serve-code row confirms the SERVE via overlay_resolved "
            .. "source=runtime). Step 8b returned a teaching error here; step 8c "
            .. "resolves")
    end
end
