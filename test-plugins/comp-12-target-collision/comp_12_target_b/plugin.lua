-- COMP-12 plugin B — the collider.
--
-- B's targets.toml already declared the colliding bare target `combat_check`
-- (loaded by the engine before this script runs). B asserts NOTHING — plugin A
-- owns the COMP-12 row. This script exists only so B is unambiguously a loaded
-- plugin whose target registration is live when A hooks the bare name.
kcdx.log.info("COMP12",
    "plugin B loaded; its bare target `combat_check` (bogus locator) is "
    .. "registered to collide with plugin A's same-named target")
