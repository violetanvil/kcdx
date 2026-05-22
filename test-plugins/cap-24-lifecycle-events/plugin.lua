-- CAP-24 plugin.lua — exercises the kcdx.on lifecycle-event bridge
-- (Phase 2b sub-8). kcdx.on(name, fn) now accepts the 9 game-lifecycle
-- events that mirror the engine's kcdxMessage_* catalog; this plugin
-- subscribes to three of them and reports from inside each callback.
--
-- input_loaded is the AUTO-PASS: it fires on every boot (first update
-- tick, after the apply pass), so the row reports PASS with no player
-- input. save_game / post_load_game are [manual] — they only fire when
-- the dev saves / loads in-game, so their rows stay PENDING until the
-- gesture is performed at the verification checkpoint.

-- CAP-24-input-loaded (AUTO-PASS): the no-arg lifecycle event that fires
-- every boot. Reaching this callback proves the bridge wired kcdx.on
-- through to the engine kcdxMessage_InputLoaded dispatch.
kcdx.on("input_loaded", function()
    kcdx.test.report("CAP-24-input-loaded", true,
        "kcdx.on('input_loaded') fired on boot — lifecycle bridge live")
end)

-- CAP-24-save-game ([manual]): fires on every in-game save. Carries the
-- save basename (e.g. "save561.whs") as the single arg — assert it's a
-- non-empty string, proving the save/load arg path copies the engine's
-- const char* through to Lua.
kcdx.on("save_game", function(basename)
    local ok = type(basename) == "string" and #basename > 0
    kcdx.test.report("CAP-24-save-game", ok,
        ok and ("save_game fired with basename='" .. basename .. "'")
            or ("save_game fired but basename was " .. tostring(basename)))
end)

-- CAP-24-post-load-game ([manual]): fires after a load completes. NO arg
-- (PreLoadGame/PostLoadGame carry no basename — the engine doc says
-- `data` is NULL). Reaching this callback confirms a no-arg lifecycle
-- event fires through the bridge.
kcdx.on("post_load_game", function()
    kcdx.test.report("CAP-24-post-load-game", true,
        "post_load_game fired (no-arg lifecycle event) after load")
end)

kcdx.log.info("CAP24", "registered kcdx.on lifecycle subscriptions "
    .. "(input_loaded auto-pass; save_game + post_load_game manual)")
