-- COMP-13 subject plugin.lua — INERT BY DESIGN.
--
-- This file's body is never executed at runtime. The reason: this plugin's
-- kcdx.toml declares [load_order].zone = "before_game", which trips the
-- synthetic After-required capability entry kcdx.zone_gate_test_after_only
-- in zone_gate's capability table (src/zone_gate.cpp kCapabilities[]).
-- zone_gate's EvaluateAllPlugins runs in LoadAllConfigs at config-load
-- time — WELL BEFORE any plugin.lua. It detects the mismatch
-- (declared zone='before_game' vs the synthetic API's
-- requireZone='after_game'), flips this plugin's engineAccepted=false,
-- and records the reason under zone_gate::g_rejected.
--
-- By the time the engine's RunAll loop reaches this plugin in
-- src/lua_plugin_loader.cpp, IsPluginEnabled() returns false (because
-- engineAccepted is false), and the enriched skip-log line
-- "rejected by zone_gate: ..." fires INSTEAD of loading + executing
-- this file. Nothing below this comment runs.
--
-- The kcdx.log.info call below is therefore unreachable. It exists as
-- a CANARY: if it ever shows up in the log, zone_gate's rejection path
-- has regressed. (The observer reports COMP-13 PASS by asserting
-- kcdx.plugin.is_rejected returns true for this subject; THIS file
-- running at all would mean the rejection failed, which is a separate
-- bug that the observer's assertion would also catch via
-- is_rejected==false.)
--
-- Note: this file deliberately does NOT call kcdx.zone_gate_test_after_only
-- (the synthetic API). That call would now succeed at runtime — the engine
-- registered a no-op stub for the name — but the call is irrelevant
-- because the gate already rejected this plugin based on the static
-- capability table at config-load. The whole rejection model is
-- declarative (zone declared in manifest vs API's requireZone in the
-- capability table); no runtime call participates.

kcdx.log.info("COMP13",
    "subject plugin.lua should NEVER run — if you see this, zone_gate "
    .. "failed to reject the before_game subject (regression in the "
    .. "gate; observer's COMP-13-zone-reject row will also FAIL since "
    .. "kcdx.plugin.is_rejected would return false)")
