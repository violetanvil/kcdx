-- CAP-99 plugin.lua — the kcdx.find Lua BINDER surface + the kcdx_find /
-- kcdx_dev_inspect console paths (Phase 9.4 step 3).
--
-- DISTINCT FROM cap-98: cap-98 self-tests the ENGINE search layer
-- (refdb::FindFunctions / EnumerateStatements) from engine code. THIS plugin
-- calls kcdx.find from Lua and asserts on the returned table — the LUA BINDER
-- surface (src/lua_bind_find.cpp), which the engine self-test cannot observe.
-- The console rows drive kcdx_find / kcdx_dev_inspect (engine `~`-console
-- commands) and report their DISPATCH at input_loaded; the full overlay /
-- dev-log observable is confirmed by the agent from the log after the user's
-- ~-console gesture (`console` mode).
--
-- Ground truth (the as-built dev DB corpus, verified against
-- data/reference-dev.sqlite 2026-06-10 — the SAME fixture cap-98 uses):
--   KNOWN_STRING has exactly 1 owner: FUN_18043ee28 / WHGame.dll.
--   OVERCAP_CALLEE ("_Init_thread_footer") is called by 30,393 functions
--     (> the 500 cap) — the deterministic loud-truncation target AND the
--     KI-0015 boot-hang regression target: the original eager find built
--     every one of the 500 capped records' full statement lists (~400K nested
--     Lua tables) on the boot worker thread and HUNG. find now returns LEAN
--     headers (statement_count, no statement bodies), so this row must no
--     longer hang and asserts every record is lean.
--
-- THE DEV-GATE GRACEFUL CONTRACT IS THE LOAD-BEARING SAFETY ROW. kcdx.find is
-- dev-mode-only over a separate ~1.3 GB dev DB the shipped product does NOT
-- carry. For a VALID criterion the binder ALWAYS returns a table (never nil,
-- never a raise) — {} when the dev gate fails (dev mode off OR dev DB absent),
-- the SAME empty contract as a genuine no-match, so a shipped mod's
-- `if #r == 0` path runs harmlessly. cap-99-graceful-contract pins exactly
-- that.
--
-- GRACEFUL on absent dev DB (the same posture as cap-98 / cap-86): a
-- known-string find that returns 0 records is a DEGRADED observation (the dev
-- DB is not deployed, or dev mode is off — the empty result is deliberately
-- indistinguishable from a no-match; find.md "the empty-table result is the
-- same"). The field checks (and a real FAIL) fire only when a record actually
-- comes back — i.e. when the dev DB IS present. So a no-dev-DB install never
-- false-FAILs, while a present-but-WRONG result does.

local KNOWN_STRING =
    "   You have to set r_TexturesStreaming = 1 to see texture information!"
local OVERCAP_CALLEE = "_Init_thread_footer"
local MODULE         = "WHGame.dll"

-- ============================================================================
-- Boot-time Lua-surface rows — call kcdx.find at ready, assert the table.
-- ============================================================================
kcdx.on("ready", function()
    -- The kcdx.find binder must exist for any Lua-surface row to run. Its
    -- absence is a real FAIL (the binder did not register) — never a silent
    -- skip.
    if type(kcdx.find) ~= "function" then
        for _, row in ipairs({
            "cap-99-known-string-find", "cap-99-empty-criteria-rejects",
            "cap-99-graceful-contract", "cap-99-truncates-loud",
        }) do
            kcdx.test.report(row, false,
                "kcdx.find is not a function (type=" .. type(kcdx.find)
                .. ") — the kcdx.find binder did not register; the Lua "
                .. "discovery surface is missing")
        end
        return
    end

    -- =====================================================================
    -- cap-99-known-string-find — kcdx.find{string=<known>} returns a table
    -- with >=1 LEAN record: .function/.module non-empty, .rva a
    -- kcdx.memory.pointer (non-nil userdata), .statement_count a number, and
    -- NO .statements field (find returns headers only — the boot-hang fix,
    -- KI-0015).
    -- FALSIFIABLE: a record comes back but function/module is empty, rva is
    -- nil / not a pointer userdata, .statement_count is not a number, OR the
    -- record carries a .statements field (a non-lean record, the hang shape) ->
    -- FAIL. DEGRADED PASS when 0 records (dev DB absent / dev mode off — the
    -- known string CANNOT resolve without the dev DB, and the empty result is
    -- indistinguishable from a no-match).
    do
        local row = "cap-99-known-string-find"
        local r = kcdx.find({ string = KNOWN_STRING })
        if type(r) ~= "table" then
            kcdx.test.report(row, false,
                "kcdx.find{string=<known>} returned " .. type(r)
                .. " (expected a table on a valid criterion) — the binder must "
                .. "always return a table for a valid criterion")
        elseif #r == 0 then
            -- The dev DB is not deployed (or dev mode is off): the known string
            -- cannot resolve. Indistinguishable from a no-match by design — a
            -- DEGRADED observation, never a hard FAIL on a no-dev-DB install.
            kcdx.test.report(row, true,
                "DEGRADED PASS: kcdx.find{string=<known>} returned an EMPTY "
                .. "table — the dev reference DB is not deployed (or dev mode "
                .. "is off), so the known string cannot resolve; the empty "
                .. "result is the dev-gate's by-design no-match-equivalent. The "
                .. "field checks run when a record is present")
        else
            local fn = r[1]
            local name = fn["function"]
            local mod  = fn.module
            local rva  = fn.rva
            local sc   = fn.statement_count
            if type(name) ~= "string" or name == "" then
                kcdx.test.report(row, false,
                    "the first record's .function is empty/non-string ("
                    .. tostring(name) .. ") — a broken record header; expected "
                    .. "a non-empty function name (ground truth FUN_18043ee28)")
            elseif type(mod) ~= "string" or mod == "" then
                kcdx.test.report(row, false,
                    "the first record's .module is empty/non-string ("
                    .. tostring(mod) .. ") — a broken record header; expected "
                    .. "a non-empty module (ground truth " .. MODULE .. ")")
            elseif rva == nil then
                kcdx.test.report(row, false,
                    "the first record's .rva is nil — expected a "
                    .. "kcdx.memory.pointer (a real address is never nil); the "
                    .. "PushPointer wiring or the rva column is broken")
            elseif type(rva) ~= "userdata" then
                -- rva must be a kcdx.memory.pointer userdata, NOT a lossy number
                -- (lua-precision.md: a VA pushed as a number rounds). A number
                -- here means the binder regressed to lua_pushinteger.
                kcdx.test.report(row, false,
                    "the first record's .rva is a " .. type(rva)
                    .. ", expected a kcdx.memory.pointer userdata — a VA pushed "
                    .. "as a number is lossy (lua-precision.md); the binder "
                    .. "must use PushPointer")
            elseif type(sc) ~= "number" then
                kcdx.test.report(row, false,
                    "the first record's .statement_count is a " .. type(sc)
                    .. " (expected a number) — a lean find record carries the "
                    .. "SQL-computed statement_count; a missing/non-number count "
                    .. "means the lean-header wiring is broken")
            elseif fn.statements ~= nil then
                -- A .statements field means the record is NOT lean — the
                -- boot-hang shape (KI-0015: 500 records x full statement lists =
                -- ~400K nested tables on the boot thread -> HANG). Find must
                -- return headers only; the detail is kcdx_dev_inspect's.
                kcdx.test.report(row, false,
                    "the first record carries a .statements field (type="
                    .. type(fn.statements) .. ") — a find record must be LEAN "
                    .. "(headers only, no statement bodies); a non-lean record "
                    .. "is the KI-0015 boot-hang shape. Use kcdx_dev_inspect for "
                    .. "a function's statements")
            else
                kcdx.test.report(row, true,
                    "kcdx.find{string=<known>} returned " .. #r .. " record(s); "
                    .. "the first is function=\"" .. name .. "\" module=\""
                    .. mod .. "\" rva=" .. tostring(rva)
                    .. " statement_count=" .. tostring(sc)
                    .. " (rva a kcdx.memory.pointer userdata, statement_count a "
                    .. "number, NO .statements field) — the binder builds a "
                    .. "complete LEAN record (ground truth FUN_18043ee28 / "
                    .. MODULE .. ")")
            end
        end
    end

    -- =====================================================================
    -- cap-99-empty-criteria-rejects — kcdx.find{} (no criterion) is rejected
    -- LOUD: the binder returns (nil, teaching-error), never a table. The
    -- at-least-one-of-N parse-time check (lua_bind_find.cpp setCount==0).
    -- FALSIFIABLE: an empty-criteria call returns a TABLE (the required-
    -- criterion check silently passed) -> FAIL; nil with no teaching error
    -- string -> FAIL. pcall guards the call so a (defensive) raise is still
    -- caught as a loud rejection (not a crash).
    do
        local row = "cap-99-empty-criteria-rejects"
        -- The binder returns (nil, err) for no-criterion input; pcall also
        -- catches the (not-expected) raise form. Either loud rejection passes;
        -- a returned table FAILs.
        local ok, ret, err = pcall(kcdx.find, {})
        if ok == false then
            -- kcdx.find raised (defensive form). A raise IS a loud rejection of
            -- the empty table — it did NOT silently return a result. PASS, and
            -- record that the binder raised rather than returned (nil, err).
            kcdx.test.report(row, true,
                "kcdx.find({}) RAISED a Lua error (caught by pcall) — the "
                .. "empty-criteria table was rejected loud, never a silent "
                .. "no-match table. Error: " .. tostring(ret))
        elseif ret == nil then
            -- The expected as-built form: returns (nil, teaching-error).
            if type(err) == "string" and err ~= "" then
                kcdx.test.report(row, true,
                    "kcdx.find({}) returned (nil, teaching-error) — the "
                    .. "at-least-one-of-N parse check rejected the empty "
                    .. "criteria table loud, never a silent {}. Error: \""
                    .. err .. "\"")
            else
                kcdx.test.report(row, false,
                    "kcdx.find({}) returned nil but WITHOUT a teaching error "
                    .. "string (err=" .. tostring(err) .. ") — a rejection must "
                    .. "teach why (the at-least-one-of-N requirement), never a "
                    .. "bare nil")
            end
        else
            -- ret is non-nil: the binder returned a value for no criterion.
            kcdx.test.report(row, false,
                "kcdx.find({}) returned a " .. type(ret) .. " (non-nil) for an "
                .. "EMPTY criteria table — the required at-least-one-of-N check "
                .. "silently passed; an empty-criteria call must be rejected "
                .. "loud (nil + teaching error), never return a result table")
        end
    end

    -- =====================================================================
    -- cap-99-graceful-contract — THE LOAD-BEARING SAFETY ROW. For a VALID
    -- criterion, kcdx.find ALWAYS returns a table (type=="table"), NEVER nil
    -- and NEVER a raise — in BOTH the dev-DB-present and absent cases. This is
    -- the shipped-mod-in-a-player-install contract: a mod calling find() in a
    -- non-dev install must not break.
    -- FALSIFIABLE: kcdx.find raises (pcall catches an error) OR returns nil on
    -- a valid-criteria call -> FAIL. (Holds whether the dev DB is present or
    -- absent — a valid criterion always yields a table; {} when gated off.)
    do
        local row = "cap-99-graceful-contract"
        local ok, ret = pcall(kcdx.find, { string = "kcdx_cap99_no_such_string" })
        if ok == false then
            kcdx.test.report(row, false,
                "kcdx.find{string=\"...\"} RAISED a Lua error (caught by pcall) "
                .. "on a VALID criterion — a shipped mod calling find() in a "
                .. "non-dev install would break. find() must NEVER raise on a "
                .. "valid criterion (it returns {} when the dev gate fails). "
                .. "Error: " .. tostring(ret))
        elseif type(ret) ~= "table" then
            kcdx.test.report(row, false,
                "kcdx.find{string=\"...\"} returned " .. type(ret)
                .. " on a VALID criterion (expected a table, always) — a valid "
                .. "criterion must yield a table (a record array, or {} when "
                .. "gated off / no match), NEVER nil. The safety contract is "
                .. "broken")
        else
            kcdx.test.report(row, true,
                "kcdx.find{string=\"...\"} returned a table (#=" .. #ret
                .. ") on a VALID criterion and did NOT raise — the shipped-mod "
                .. "safety contract holds: a valid criterion ALWAYS returns a "
                .. "table ({} when the dev gate fails), so `if #r == 0` runs "
                .. "harmlessly in a player's non-dev install")
        end
    end

    -- =====================================================================
    -- cap-99-truncates-loud — kcdx.find{callee=OVERCAP_CALLEE} (30,393 owners,
    -- > the 500 cap) carries the loud-truncation markers passed transparently
    -- from FindFunctions: #r==500, _truncated==true, _total_matches>500 — AND
    -- each returned record is LEAN (a numeric .statement_count, NO .statements
    -- field). THIS ROW IS THE KI-0015 REGRESSION GUARD: the original eager
    -- design built every one of the 500 records' full statement lists (~400K
    -- nested Lua tables) on the boot worker thread → memory/GC stall → HANG.
    -- With the lean fix it builds 500 small header tables and returns; THIS ROW
    -- MUST NO LONGER HANG.
    -- FALSIFIABLE: a silent partial (_truncated absent/false), a wrong/absent
    -- _total_matches, a record count != 500, a non-number .statement_count, OR
    -- a record carrying a .statements field (a non-lean record — the very shape
    -- that hangs) -> FAIL (the AP14 silent-failure / KI-0015 shapes). DEGRADED
    -- PASS when 0 records (dev DB absent).
    do
        local row = "cap-99-truncates-loud"
        local r = kcdx.find({ callee = OVERCAP_CALLEE })
        if type(r) ~= "table" then
            kcdx.test.report(row, false,
                "kcdx.find{callee=\"" .. OVERCAP_CALLEE .. "\"} returned "
                .. type(r) .. " (expected a table on a valid criterion)")
        elseif #r == 0 then
            kcdx.test.report(row, true,
                "DEGRADED PASS: kcdx.find{callee=\"" .. OVERCAP_CALLEE
                .. "\"} returned an EMPTY table — the dev reference DB is not "
                .. "deployed (or dev mode is off); the over-cap truncation "
                .. "markers need the deployed corpus")
        else
            local trunc = r._truncated
            local total = r._total_matches
            -- Lean-record check across the whole capped set: every record must
            -- be a header (numeric .statement_count, no .statements). A single
            -- non-lean record is the KI-0015 boot-hang shape.
            local firstNonLean = nil  -- index of the first non-lean record, if any
            local badReason    = nil
            for i = 1, #r do
                local rec = r[i]
                if type(rec.statement_count) ~= "number" then
                    firstNonLean = i
                    badReason = ".statement_count is a "
                        .. type(rec.statement_count) .. " (expected a number)"
                    break
                elseif rec.statements ~= nil then
                    firstNonLean = i
                    badReason = "carries a .statements field (type="
                        .. type(rec.statements)
                        .. ") — a non-lean record, the KI-0015 hang shape"
                    break
                end
            end
            if #r ~= 500 then
                kcdx.test.report(row, false,
                    "kcdx.find{callee=\"" .. OVERCAP_CALLEE .. "\"} returned "
                    .. #r .. " records, expected exactly 500 (the cap) — a "
                    .. "capped result must carry exactly the first 500; "
                    .. (#r < 500 and "fewer than 500 means the corpus shrank or "
                        .. "the over-cap fixture changed" or "more than 500 "
                        .. "means the cap is not enforced"))
            elseif trunc ~= true then
                kcdx.test.report(row, false,
                    "kcdx.find returned 500 records but _truncated=="
                    .. tostring(trunc) .. " (expected true) — an over-cap "
                    .. "result that does NOT flag _truncated is a SILENT "
                    .. "partial (the AP14 silent-failure shape); the author "
                    .. "cannot tell the result was capped")
            elseif type(total) ~= "number" or total <= 500 then
                kcdx.test.report(row, false,
                    "kcdx.find flagged _truncated but _total_matches=="
                    .. tostring(total) .. " (expected a number > 500, the full "
                    .. "uncapped count, ground truth 30393) — a truncated "
                    .. "result must report the true total so the author knows "
                    .. "how much to narrow")
            elseif firstNonLean ~= nil then
                kcdx.test.report(row, false,
                    "kcdx.find returned 500 truncated records but record #"
                    .. firstNonLean .. " " .. badReason .. " — every find "
                    .. "record must be a LEAN header (headers only, no statement "
                    .. "bodies); a non-lean record over the 500-record cap is "
                    .. "exactly the KI-0015 boot-hang shape this row guards")
            else
                kcdx.test.report(row, true,
                    "kcdx.find{callee=\"" .. OVERCAP_CALLEE .. "\"} truncated "
                    .. "LOUDLY and LEANLY: #records==500, _truncated==true, "
                    .. "_total_matches==" .. tostring(total) .. " (>500, ground "
                    .. "truth 30393), and all 500 records are lean headers "
                    .. "(numeric .statement_count, no .statements) — the cap is "
                    .. "enforced, the truncation is never silent, and the "
                    .. "KI-0015 boot-hang (500 records x full statement lists) "
                    .. "is gone")
            end
        end
    end

    kcdx.log.info("CAP99",
        "kcdx.find Lua-surface self-test reported 4 boot rows (lean known-string "
        .. "record, empty-criteria loud reject, the dev-gate graceful safety "
        .. "contract, lean+loud truncation / KI-0015 boot-hang guard); the "
        .. "console rows report at input_loaded")
end)

-- ============================================================================
-- Console-path rows (`console` mode) — drive the kcdx_find / kcdx_dev_inspect
-- engine `~`-console commands. The boot check asserts DISPATCH (the command is
-- registered and runs without crashing — kcdx.console.execute returns true),
-- the FALSIFIABLE machine half. The full parse/print/teaching-error OBSERVABLE
-- is confirmed by the agent from the dev log after the user's ~-console
-- gesture (the same console-row pattern as cap-70). Run at input_loaded so the
-- dev DB has lazy-opened by the time the search dispatches.
-- ============================================================================
kcdx.on("input_loaded", function()
    -- kcdx.console.execute must exist to drive the console verbs from Lua.
    if not (kcdx.console and type(kcdx.console.execute) == "function") then
        for _, row in ipairs({
            "cap-99-console-find-parses", "cap-99-console-inspect-not-found",
        }) do
            kcdx.test.report(row, false,
                "kcdx.console.execute is unavailable — cannot drive the "
                .. "kcdx_find / kcdx_dev_inspect console verbs from Lua")
        end
        return
    end

    -- =====================================================================
    -- cap-99-console-find-parses — `kcdx_find WHGame.dll --string "<known>"`
    -- parses module + a criterion and prints the matched function row.
    -- Boot machine half: kcdx.console.execute dispatches the command (returns
    -- true => kcdx_find is registered and its parse + search path ran without
    -- crashing). The agent confirms the OVERLAY / dev-log observable after the
    -- user's ~-console gesture.
    -- FALSIFIABLE (machine): execute returns false => kcdx_find not registered
    -- / surface not armed -> FAIL. FALSIFIABLE (agent, post-run): the command
    -- errors on parse, prints no match for the known-owned string with the dev
    -- DB present, OR prints the dev-tool-unavailable teaching message when the
    -- dev DB IS present -> FAIL.
    do
        local row = "cap-99-console-find-parses"
        -- The line is a single Lua string; the known string is double-quoted so
        -- it reaches kcdx_find as one --string value token.
        local line = 'kcdx_find ' .. MODULE .. ' --string "' .. KNOWN_STRING
            .. '"'
        local ok = kcdx.console.execute(line)
        if ok == true then
            kcdx.test.report(row, true,
                "kcdx.console.execute(" .. line .. ") returned true — kcdx_find "
                .. "is registered and dispatched its module+criterion parse + "
                .. "dev-DB search without crashing. CONSOLE OBSERVABLE (agent "
                .. "confirms from the dev log after the user opens ~ and types "
                .. "the command): with the dev DB present, a '[find] N matches:' "
                .. "header + a 'FUN_18043ee28  " .. MODULE .. "+0x...' row; "
                .. "never the dev-tool-unavailable teaching message and never a "
                .. "parse/usage error")
        else
            kcdx.test.report(row, false,
                "kcdx.console.execute(" .. line .. ") returned " .. tostring(ok)
                .. " (expected true) — kcdx_find did not dispatch (not "
                .. "registered, or the console surface is not armed at "
                .. "input_loaded)")
        end
    end

    -- =====================================================================
    -- cap-99-console-inspect-not-found — `kcdx_dev_inspect WHGame.dll
    -- IsInCombatt` (a deliberate one-char typo) parses module + function and,
    -- on the not-found path, prints the documented teaching error with the
    -- nearest curated name suggestion + the kcdx_find --name_contains fallback.
    -- Boot machine half: execute dispatches the command (returns true =>
    -- kcdx_dev_inspect is registered and its not-found teaching path ran
    -- without crashing).
    -- FALSIFIABLE (machine): execute returns false => not registered -> FAIL.
    -- FALSIFIABLE (agent, post-run, dev DB present): a silent no-output, a
    -- generic error with NO nearest-name "Did you mean:" suggestion, or a crash
    -- -> FAIL.
    do
        local row = "cap-99-console-inspect-not-found"
        -- IsInCombatt is a deliberate typo of the curated IsInCombat — exercises
        -- the not-found nearest-name teaching path, not a real function.
        local line = 'kcdx_dev_inspect ' .. MODULE .. ' IsInCombatt'
        local ok = kcdx.console.execute(line)
        if ok == true then
            kcdx.test.report(row, true,
                "kcdx.console.execute(" .. line .. ") returned true — "
                .. "kcdx_dev_inspect is registered and dispatched its "
                .. "module+function parse + not-found teaching path without "
                .. "crashing. CONSOLE OBSERVABLE (agent confirms from the dev "
                .. "log after the user opens ~ and types the command): with the "
                .. "dev DB present, the teaching error \"[dev_inspect] no "
                .. "function 'IsInCombatt' in " .. MODULE .. ".\" + a \"Did you "
                .. "mean: <nearest curated name>?\" Levenshtein suggestion line "
                .. "+ the 'Or search by content:' + 'kcdx_find " .. MODULE
                .. " --name_contains IsInCombatt' fallback line; never a silent "
                .. "no-output and never a bare/generic error")
        else
            kcdx.test.report(row, false,
                "kcdx.console.execute(" .. line .. ") returned " .. tostring(ok)
                .. " (expected true) — kcdx_dev_inspect did not dispatch (not "
                .. "registered, or the console surface is not armed at "
                .. "input_loaded)")
        end
    end
end)
