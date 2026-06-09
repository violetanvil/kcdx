-- CAP-88 plugin.lua — kcdx.functions.* reference namespace + kcdx.dll.declare.
--
-- Proves the AUTHOR SURFACE built in src/lua_bind_functions.cpp: the
-- kcdx.functions.* function-reference value namespace + the
-- kcdx.dll.declare(plugin_namespace, function_map) verb. Each kcdx.functions.X.Y
-- access returns a function-reference VALUE (a userdata) whose :resolve()
-- accessor reports { found, is_game, stem, name, signature, has_address,
-- address }. The reference SELF-VERIFIES: no hook/statement verb consumes it
-- yet (those verbs are not yet built); :resolve is the seam the rows assert against.
--
-- Each row reads the ACTUAL :resolve output, never a value it set, and states
-- its red condition (FALSIFIABLE — a row that can never go red proves nothing).

-- The plugin namespace the declare row registers under. The AUTHOR-OWNED dotted
-- <author>.<plugin> stem (the design's example uses "redmoon.outfit_mod"); a
-- distinct test namespace here so the row is self-contained.
local PLUGIN_NS  = "a.test"
local DECL_FN    = "DeclaredFn"
local DECL_SIG   = "bool (ptr self)"

-- A game-DLL function CURRENT in the shipped reference DB (the curated set was
-- renumbered to a contiguous 1-157 scheme — resolve by NAME, never a retired id).
local GAME_FN    = "SaveGame"
-- A CURRENT curated stable id (CCryPak_FOpen = 131 in the 1-157 scheme) — the
-- by_id accessor target. NOT a retired id (1172/1124 etc are gone).
local GAME_ID    = 131

-- A game resolve that returns found=false because the reference DB is not open
-- (a pre-deploy state) is a DEGRADED observation, not a failure. refdb signals
-- that with db_not_loaded; name_unknown is DELIBERATELY NOT degraded — SaveGame
-- resolves BY NAME, so a name_unknown is a real rename/renumber regression that
-- must go RED, never a silent green.
local function is_degraded(res)
    return res ~= nil and res.found == false and res.reason == "db_not_loaded"
end

kcdx.on("ready", function()
    -- Both namespaces must exist for any row to run.
    if kcdx.functions == nil or kcdx.dll == nil then
        for _, row in ipairs({
            "cap-88-declare-populates-namespace",
            "cap-88-game-fn-resolves",
            "cap-88-by-id-resolves",
        }) do
            kcdx.test.report(row, false,
                "kcdx.functions / kcdx.dll namespace is not registered — the "
                .. "function-reference binder did not bind")
        end
        return
    end

    -- ROW 1 — kcdx.dll.declare populates kcdx.functions["a.test"].DeclaredFn
    -- with the AUTHOR-DECLARED signature. DB-INDEPENDENT (the declare store is
    -- in-memory; no reference-DB resolve). RED if declare did not register the
    -- function (found=false / not_declared), or the resolved reference does not
    -- carry the declared signature, or it is mis-classified as a game reference.
    do
        local row = "cap-88-declare-populates-namespace"
        local ok = kcdx.dll.declare(PLUGIN_NS, {
            [DECL_FN] = { signature = DECL_SIG },
        })
        if ok ~= true then
            kcdx.test.report(row, false,
                "kcdx.dll.declare(\"" .. PLUGIN_NS .. "\", {...}) returned "
                .. tostring(ok) .. " — expected true (the declaration was "
                .. "rejected)")
        else
            local ref = kcdx.functions[PLUGIN_NS][DECL_FN]
            if ref == nil then
                kcdx.test.report(row, false,
                    "kcdx.functions[\"" .. PLUGIN_NS .. "\"]." .. DECL_FN
                    .. " is nil — declare did not populate the namespace")
            else
                local r = ref:resolve()
                if r == nil then
                    kcdx.test.report(row, false,
                        ":resolve() returned nil — the accessor produced no "
                        .. "result table")
                elseif r.found ~= true then
                    kcdx.test.report(row, false,
                        ":resolve found=" .. tostring(r.found) .. " reason="
                        .. tostring(r.reason) .. " — expected found=true (the "
                        .. "declared function must resolve)")
                elseif r.is_game ~= false then
                    kcdx.test.report(row, false,
                        "the declared plugin reference reported is_game="
                        .. tostring(r.is_game) .. " — a kcdx.dll.declare "
                        .. "function is a PLUGIN reference (is_game=false), not "
                        .. "a game-DLL one")
                elseif r.signature ~= DECL_SIG then
                    kcdx.test.report(row, false,
                        "the declared reference carries signature="
                        .. tostring(r.signature) .. " — expected the author-"
                        .. "declared \"" .. DECL_SIG .. "\" (the reference lost "
                        .. "the declared signature)")
                else
                    kcdx.test.report(row, true,
                        "kcdx.dll.declare populated kcdx.functions[\""
                        .. PLUGIN_NS .. "\"]." .. DECL_FN .. "; :resolve -> "
                        .. "found=true is_game=false stem=" .. tostring(r.stem)
                        .. " signature=\"" .. tostring(r.signature) .. "\" "
                        .. "(the author-declared ABI, no disassembly)")
                end
            end
        end
    end

    -- ROW 2 — kcdx.functions.WHGame.SaveGame resolves against the reference DB
    -- to a NON-ZERO address + the verified signature. RED if SaveGame does not
    -- resolve (found=false reason name_unknown — a real renumber regression), or
    -- resolves with no address. DEGRADED PASS only if the DB is not open.
    do
        local row = "cap-88-game-fn-resolves"
        local ref = kcdx.functions.WHGame[GAME_FN]
        if ref == nil then
            kcdx.test.report(row, false,
                "kcdx.functions.WHGame." .. GAME_FN .. " is nil — the game-DLL "
                .. "stem proxy did not mint a reference")
        else
            local r = ref:resolve()
            if r == nil then
                kcdx.test.report(row, false,
                    ":resolve() returned nil for a game reference")
            elseif is_degraded(r) then
                kcdx.test.report(row, true,
                    "DEGRADED PASS: the reference DB is not open (reason="
                    .. tostring(r.reason) .. ") — kcdx.functions.WHGame + "
                    .. ":resolve are wired; the address check is skipped this "
                    .. "deploy state")
            elseif r.found ~= true then
                kcdx.test.report(row, false,
                    "kcdx.functions.WHGame." .. GAME_FN .. ":resolve found="
                    .. tostring(r.found) .. " reason=" .. tostring(r.reason)
                    .. " — expected found=true (resolved BY NAME; a name_unknown "
                    .. "here is a real rename/renumber regression)")
            elseif r.is_game ~= true then
                kcdx.test.report(row, false,
                    "the game reference reported is_game=" .. tostring(r.is_game)
                    .. " — a no-dot stem (WHGame) is a GAME reference")
            elseif r.has_address ~= true or r.address == nil then
                kcdx.test.report(row, false,
                    "kcdx.functions.WHGame." .. GAME_FN .. " resolved but "
                    .. "has_address=" .. tostring(r.has_address)
                    .. " address=" .. tostring(r.address)
                    .. " — expected a non-zero resolved VA")
            else
                kcdx.test.report(row, true,
                    "kcdx.functions.WHGame." .. GAME_FN .. ":resolve -> "
                    .. "found=true is_game=true address=" .. tostring(r.address)
                    .. " signature=\"" .. tostring(r.signature) .. "\" (resolved "
                    .. "by name against the reference DB)")
            end
        end
    end

    -- ROW 3 — kcdx.functions.by_id[131] (CCryPak_FOpen, a CURRENT curated id)
    -- resolves the same function by its stable-across-versions id. RED if id 131
    -- does not resolve, or it is mis-classified as a plugin reference (by_id is
    -- GAME-functions only). DEGRADED PASS only if the DB is not open.
    do
        local row = "cap-88-by-id-resolves"
        local ref = kcdx.functions.by_id[GAME_ID]
        if ref == nil then
            kcdx.test.report(row, false,
                "kcdx.functions.by_id[" .. GAME_ID .. "] is nil — the by_id "
                .. "proxy did not mint a reference")
        else
            local r = ref:resolve()
            if r == nil then
                kcdx.test.report(row, false,
                    ":resolve() returned nil for a by_id reference")
            elseif is_degraded(r) then
                kcdx.test.report(row, true,
                    "DEGRADED PASS: the reference DB is not open (reason="
                    .. tostring(r.reason) .. ") — kcdx.functions.by_id + "
                    .. ":resolve are wired; the address check is skipped this "
                    .. "deploy state")
            elseif r.found ~= true then
                kcdx.test.report(row, false,
                    "kcdx.functions.by_id[" .. GAME_ID .. "]:resolve found="
                    .. tostring(r.found) .. " reason=" .. tostring(r.reason)
                    .. " — expected found=true (id " .. GAME_ID .. " is a "
                    .. "CURRENT curated id in the 1-157 scheme)")
            elseif r.is_game ~= true then
                kcdx.test.report(row, false,
                    "the by_id reference reported is_game=" .. tostring(r.is_game)
                    .. " — by_id is GAME-functions only")
            elseif r.has_address ~= true or r.address == nil then
                kcdx.test.report(row, false,
                    "kcdx.functions.by_id[" .. GAME_ID .. "] resolved but "
                    .. "has_address=" .. tostring(r.has_address)
                    .. " — expected a non-zero resolved VA")
            else
                kcdx.test.report(row, true,
                    "kcdx.functions.by_id[" .. GAME_ID .. "]:resolve -> "
                    .. "found=true is_game=true address=" .. tostring(r.address)
                    .. " (resolved by stable id against the reference DB)")
            end
        end
    end

    kcdx.log.info("CAP88",
        "kcdx.functions.* + kcdx.dll.declare self-test reported all 3 rows: a "
        .. "declared plugin function (signature from source), a game-DLL "
        .. "function (by name), and a by-id game reference")
end)
