-- CAP-24 plugin.lua — exercises the kcdx.on lifecycle-event bridge.
-- kcdx.on(name, fn) now accepts the 9 game-lifecycle
-- events that mirror the engine's kcdxMessage_* catalog; this plugin
-- subscribes to three of them and reports from inside each callback.
--
-- input_loaded is the AUTO-PASS: it fires on every boot (first update
-- tick, after the apply pass), so the row reports PASS with no player
-- input. save_game / post_load_game are [manual] — they only fire when
-- the dev saves / loads in-game, so their rows stay PENDING until the
-- gesture is performed at the verification checkpoint.
--
-- NO-ARG OBSERVABLE (input_loaded + post_load_game). Per src/lua_lifecycle
-- these two events carry NO argument by engine design — only save_game /
-- delete_game / load_game_selected pass a basename. A no-arg event has NO
-- payload to assert, so the ONLY observable is event ARRIVAL: the engine
-- dispatched the named no-arg event through the bridge to this callback.
-- The strongest falsifiable form a no-arg event admits is therefore an
-- ARRIVAL-COUNT condition reported from INSIDE the callback (reaching the
-- report line already requires the callback to have fired):
--   * input_loaded  → EXACTLY-ONCE per boot (== 1): a single InputLoaded
--                      dispatch per boot is the contract; a second fire in
--                      one boot is a bridge double-dispatch regression.
--   * post_load_game → AT-LEAST-ONCE (>= 1): a load gesture can legitimately
--                      repeat in one session (multiple loads), so each load
--                      re-fires it — exactly-once would be wrong here.
-- save_game keeps its payload assertion (#basename > 0) — it has an arg.

-- CAP-24-input-loaded (AUTO-PASS): the no-arg lifecycle event that fires
-- every boot. Falsifiable: FAILs if the bridge stops dispatching
-- kcdxMessage_InputLoaded to kcdx.on subscribers (callback never runs →
-- row stays PENDING, never PASSes) OR if it double-dispatches in one boot
-- (g_inputFires > 1 → FAIL). PASS iff the no-arg event arrived exactly once.
--
-- DOUBLE-FIRE GUARD via a sticky-PASS latch, mirroring CAP-03's PASS_LATCH
-- idiom (test-plugins/cap-03-hook-lua-callback/plugin.lua). The latch is a
-- persistent _G key, so it survives a plugin.lua re-eval across save-loads
-- in the single shared Lua state. Why a latch is needed: on a save-load this
-- chunk re-evaluates with a FRESH g_inputFires=0, then input_loaded fires
-- again for the new boot/load context → count==1 again → a genuine re-eval
-- PASS, NOT a regression. The latch makes the FIRST exactly-once PASS
-- TERMINAL so a benign later re-eval (last-verdict-wins in the aggregator)
-- cannot un-do it; only a SECOND fire WITHIN one chunk evaluation
-- (g_inputFires > 1 before any re-eval reset) is the real double-dispatch
-- FAIL signal, and that FAIL is reported before the latch is set.
local g_inputFires = 0
local INPUT_PASS_LATCH = "__kcdx_cap24_input_passed"
local function input_already_passed()
    return rawget(_G, INPUT_PASS_LATCH) == true
end

kcdx.on("input_loaded", function()
    g_inputFires = g_inputFires + 1
    if g_inputFires > 1 then
        -- A SECOND fire within this same chunk evaluation: the bridge
        -- dispatched the no-arg InputLoaded event twice for one boot →
        -- double-dispatch regression. Report FAIL even if a prior eval
        -- latched PASS — a real in-eval double-fire is a genuine regression.
        kcdx.test.report("CAP-24-input-loaded", false,
            "input_loaded fired " .. g_inputFires .. " times in one chunk "
            .. "evaluation — expected exactly once per boot; the bridge "
            .. "double-dispatched the no-arg InputLoaded event")
        return
    end
    if input_already_passed() then return end  -- prior-eval PASS is terminal
    rawset(_G, INPUT_PASS_LATCH, true)
    kcdx.test.report("CAP-24-input-loaded", g_inputFires == 1,
        "kcdx.on('input_loaded') arrived exactly once this boot (count="
        .. g_inputFires .. ") — no-arg lifecycle event dispatched through the "
        .. "bridge; no payload to assert, exactly-once arrival is the observable")
end)

-- CAP-24-save-game ([manual]): fires on every in-game save. Carries the
-- save basename (e.g. "save561.whs") as the single arg — assert it's a
-- non-empty string, proving the save/load arg path copies the engine's
-- const char* through to Lua. UNCHANGED — this event has a payload.
kcdx.on("save_game", function(basename)
    local ok = type(basename) == "string" and #basename > 0
    kcdx.test.report("CAP-24-save-game", ok,
        ok and ("save_game fired with basename='" .. basename .. "'")
            or ("save_game fired but basename was " .. tostring(basename)))
end)

-- CAP-24-post-load-game ([manual]): the no-arg lifecycle event that fires
-- after a load completes (PreLoadGame/PostLoadGame carry no basename — the
-- engine doc says `data` is NULL). The falsifiable signal is STRUCTURAL, not
-- in the boolean: reaching this report line requires the no-arg post-load
-- event to have been dispatched through the bridge, so the row stays PENDING
-- (never PASSes) if the bridge stops delivering kcdxMessage_PostLoadGame to
-- Lua — that is the feature-broken state. We report a literal `true` (the
-- arrival IS the pass) with the fire count in the message; a `>= 1` boolean
-- here would be a tautology (the in-callback increment makes it always true at
-- the check), so we do not dress arrival up as a comparison. AT-LEAST-ONCE,
-- not exactly-once: a session can load multiple times, each re-firing this
-- event, so a count > 1 across loads is correct (not a double-dispatch
-- regression — contrast input_loaded's once-per-boot contract above).
local g_postLoadFires = 0
kcdx.on("post_load_game", function()
    g_postLoadFires = g_postLoadFires + 1
    kcdx.test.report("CAP-24-post-load-game", true,
        "post_load_game arrived (count=" .. g_postLoadFires .. ") — no-arg "
        .. "lifecycle event dispatched through the bridge after a load; no "
        .. "payload to assert, arrival is the observable (this report line is "
        .. "only reached when the bridge delivers the event; absence = PENDING)")
end)

kcdx.log.info("CAP24", "registered kcdx.on lifecycle subscriptions "
    .. "(input_loaded auto-pass; save_game + post_load_game manual)")
