-- COMP-16 publisher (A) — the RUNTIME code-form publish half.
--
-- A's sidecar (assets/data/replaces.toml) already published `belt` at build
-- time (the build-time published-name store). This plugin.lua ADDITIONALLY does
-- a RUNTIME code-form publish so consumer B's RUNTIME cross-mod replace has a
-- name to resolve:
--
--   kcdx.assets.declare("belt_code", "data/comp16_belt.xml")
--
-- A runtime declare is a PURE ADD-NEW publish (design §5.3) — its serve-vpath is
-- the asset's OWN add-new vpath = the `file` arg ("data/comp16_belt.xml",
-- normalized). So a runtime cross-mod replace("ts.comp_16_publisher_a.belt_code",
-- with) resolves to "data/comp16_belt.xml" and keys the runtime-overlay store by
-- it — B's file serves where A's belt_code add-new asset serves.
--
-- A asserts NOTHING (the consumers own the COMP-16 rows). The declare's return is
-- logged so the publish is observable; a failed declare would surface here loud.

local PUB_NAME  = "belt_code"
local PUB_FILE  = "data/comp16_belt.xml"  -- A's own asset, under assets/

local declared, derr = kcdx.assets.declare(PUB_NAME, PUB_FILE)

if type(declared) ~= "string" or declared == "" then
    -- A real asset under assets/ failed to publish — surface loud (the consumers
    -- depend on this name resolving; a silent failure would make B's cross-mod
    -- replace fail to resolve for the wrong reason).
    kcdx.log.error("COMP16",
        "publisher A: kcdx.assets.declare(\"" .. PUB_NAME .. "\", \"" .. PUB_FILE
        .. "\") returned " .. tostring(declared) .. " (err: " .. tostring(derr)
        .. ") — the runtime code-form publish FAILED; consumer B's runtime "
        .. "cross-mod replace of this name will not resolve")
else
    kcdx.log.info("COMP16",
        "publisher A: published ts.comp_16_publisher_a." .. PUB_NAME
        .. " (runtime add-new) -> serve-vpath \"" .. PUB_FILE .. "\" (disk \""
        .. declared .. "\"); the sidecar additionally published `belt` "
        .. "(publish-and-replace, serve-vpath = its vanilla target). Consumers "
        .. "resolve these cross-mod names to A's serve-vpath (design §5.3)")
end
