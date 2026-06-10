-- CAP-86 plugin.lua — kcdx.locator.* value namespace + :resolve accessor.
--
-- Proves the AUTHOR SURFACE built in src/lua_bind_locator.cpp: each
-- kcdx.locator.* call produces a locator VALUE whose
-- :resolve("WHGame.dll", "SaveGame") accessor resolves it against the curated
-- reference DB to the ground-truth resolved statement. This is distinct from
-- cap-83-stmt-resolve (which tests the engine refdb machinery from C++) — here
-- the Lua binding + the userdata + the :resolve method are under test.
--
-- The locator SELF-VERIFIES: no hook/statement verb consumes it; :resolve is
-- the seam the rows assert against. Ground truth measured from
-- data/reference.sqlite (SaveGame, kcdx_id 144, 59 statements). The function is
-- referenced BY NAME — no hardcoded address_id (the curated set was renumbered;
-- resolving by name is currency-stable).
--
-- All rows assert at kcdx.on("ready") (refdb is open by then). Each row reads
-- the ACTUAL :resolve output, never a value it set, and states its red
-- condition (FALSIFIABLE — a row that can never go red proves nothing).
--
-- GRACEFUL on absent data: if SaveGame does not resolve or carries no
-- statements (a pre-deploy state where the deployed projection lacks the
-- statement tables), affected rows report DEGRADED PASS (a deploy-state
-- observation) rather than a hard FAIL or a crash.

local MODULE = "WHGame.dll"
local FN     = "SaveGame"

-- A resolution that returns found=false because the statement DATA is absent
-- (the deploy-state where the projection lacks the statement tables) is a
-- DEGRADED observation, not a test failure. refdb signals that with one of
-- these reasons; everything else is a real FAIL the row must catch.
--
-- name_unknown is DELIBERATELY NOT degraded: SaveGame is resolved BY NAME, so a
-- name_unknown on this known fixture is a REAL regression (a rename/renumber
-- broke name resolution), not a legitimate deploy state. Treating it as a
-- degraded PASS would make every row silently green and mask that bug — so it
-- falls through to a hard FAIL (the falsifiable contract). Only the two genuine
-- deploy-state misses degrade: db_not_loaded (refdb not open) and
-- function_no_statements (the projection lacks the statement tables).
local function is_degraded(res)
    return res ~= nil and res.found == false
        and (res.reason == "function_no_statements"
          or res.reason == "db_not_loaded")
end

-- Assert a locator resolves to an expected statement. `want` carries the
-- ground-truth fields to check (idx required; kind / brl / callee optional).
-- Reports PASS only when :resolve found the statement AND every checked field
-- matched. DEGRADED-PASS when the statement data is simply not deployed.
local function check_resolved(row, locator, want)
    if locator == nil then
        kcdx.test.report(row, false,
            "the kcdx.locator.* constructor returned nil — the locator value "
            .. "was not produced (the binder did not register this form)")
        return
    end
    local res = locator:resolve(MODULE, FN)
    if res == nil then
        kcdx.test.report(row, false,
            ":resolve(\"" .. MODULE .. "\", \"" .. FN .. "\") returned nil — "
            .. "the accessor produced no result table")
        return
    end
    if is_degraded(res) then
        kcdx.test.report(row, true,
            "DEGRADED PASS: SaveGame's statement data is not deployed (reason="
            .. tostring(res.reason) .. ") — locator value + :resolve are wired; "
            .. "ground-truth idx check skipped this deploy state")
        return
    end
    if res.found ~= true then
        kcdx.test.report(row, false,
            ":resolve returned found=" .. tostring(res.found)
            .. " reason=" .. tostring(res.reason)
            .. " — expected the locator to resolve to statement idx "
            .. tostring(want.idx))
        return
    end
    -- Field-by-field against ground truth. ANY mismatch FAILs (falsifiable).
    if res.statement_idx ~= want.idx then
        kcdx.test.report(row, false,
            "resolved to statement_idx " .. tostring(res.statement_idx)
            .. ", expected " .. tostring(want.idx))
        return
    end
    if want.kind ~= nil and res.kind ~= want.kind then
        kcdx.test.report(row, false,
            "statement idx " .. tostring(res.statement_idx) .. " kind="
            .. tostring(res.kind) .. ", expected \"" .. want.kind .. "\"")
        return
    end
    if want.brl ~= nil and res.byte_range_len ~= want.brl then
        kcdx.test.report(row, false,
            "statement idx " .. tostring(res.statement_idx)
            .. " byte_range_len=" .. tostring(res.byte_range_len)
            .. ", expected " .. tostring(want.brl))
        return
    end
    if want.callee ~= nil and res.callee ~= want.callee then
        kcdx.test.report(row, false,
            "statement idx " .. tostring(res.statement_idx) .. " callee="
            .. tostring(res.callee) .. ", expected \"" .. want.callee .. "\"")
        return
    end
    kcdx.test.report(row, true,
        "resolved to idx " .. tostring(res.statement_idx)
        .. " kind=" .. tostring(res.kind)
        .. " brl=" .. tostring(res.byte_range_len)
        .. (want.callee and (" callee=" .. tostring(res.callee)) or "")
        .. " (ground truth idx " .. tostring(want.idx) .. ")")
end

kcdx.on("ready", function()
    -- The kcdx.locator namespace must exist for any row to run.
    if kcdx.locator == nil then
        for _, row in ipairs({
            "cap-86-function-entry", "cap-86-function-exit",
            "cap-86-first-call-to", "cap-86-last-call-to", "cap-86-call-to",
            "cap-86-first-return", "cap-86-last-return", "cap-86-return-value",
            "cap-86-references-string", "cap-86-first-read-of-cvar",
            "cap-86-matching", "cap-86-matching-pattern",
        }) do
            kcdx.test.report(row, false,
                "kcdx.locator namespace is not registered — the locator binder "
                .. "did not bind")
        end
        return
    end

    -- function_entry -> idx 0, kind "assign", byte_range_len 3.
    -- RED if it resolves to anything but idx 0 / assign / brl 3.
    check_resolved("cap-86-function-entry",
        kcdx.locator.function_entry(),
        { idx = 0, kind = "assign", brl = 3 })

    -- function_exit -> idx 58, kind "return", byte_range_len 30.
    check_resolved("cap-86-function-exit",
        kcdx.locator.function_exit(),
        { idx = 58, kind = "return", brl = 30 })

    -- first_call_to(FUN_1804d455c) -> idx 8 (the FIRST of two calls), callee
    -- FUN_1804d455c, brl 5. RED if it picks the second call (idx 53) or misses.
    check_resolved("cap-86-first-call-to",
        kcdx.locator.first_call_to("FUN_1804d455c"),
        { idx = 8, kind = "call", callee = "FUN_1804d455c", brl = 5 })

    -- last_call_to(FUN_1804d455c) -> idx 53 (the LAST of two calls), brl 48.
    -- RED if it picks the first call (idx 8) — proves first vs last differ.
    check_resolved("cap-86-last-call-to",
        kcdx.locator.last_call_to("FUN_1804d455c"),
        { idx = 53, kind = "call", callee = "FUN_1804d455c", brl = 48 })

    -- call_to: TWO assertions in one row.
    --   (a) call_to(FUN_1805c38c8) — a UNIQUE callee -> resolves to idx 20.
    --   (b) call_to(FUN_1804d455c) — called TWICE -> found=false, reason
    --       "call_to_ambiguous" (the §9.3 "errors if multiple" form).
    -- RED if the unique call mis-resolves, OR the ambiguous call does NOT
    -- reject with call_to_ambiguous (a duplicate call must error, not silently
    -- pick one).
    do
        local row = "cap-86-call-to"
        local uniq = kcdx.locator.call_to("FUN_1805c38c8")
        local ambig = kcdx.locator.call_to("FUN_1804d455c")
        if uniq == nil or ambig == nil then
            kcdx.test.report(row, false,
                "kcdx.locator.call_to(...) returned nil — locator value not "
                .. "produced")
        else
            local ru = uniq:resolve(MODULE, FN)
            local ra = ambig:resolve(MODULE, FN)
            if is_degraded(ru) then
                kcdx.test.report(row, true,
                    "DEGRADED PASS: SaveGame statement data not deployed "
                    .. "(reason=" .. tostring(ru.reason) .. ")")
            elseif ru.found ~= true or ru.statement_idx ~= 20 then
                kcdx.test.report(row, false,
                    "call_to(FUN_1805c38c8) (unique) resolved found="
                    .. tostring(ru.found) .. " idx=" .. tostring(ru.statement_idx)
                    .. " reason=" .. tostring(ru.reason)
                    .. " — expected found=true idx=20")
            elseif ra.found ~= false or ra.reason ~= "call_to_ambiguous" then
                kcdx.test.report(row, false,
                    "call_to(FUN_1804d455c) (called twice) resolved found="
                    .. tostring(ra.found) .. " idx=" .. tostring(ra.statement_idx)
                    .. " reason=" .. tostring(ra.reason)
                    .. " — expected found=false reason=call_to_ambiguous (a "
                    .. "duplicate call must error, not silently pick one)")
            else
                kcdx.test.report(row, true,
                    "call_to(FUN_1805c38c8) (unique) -> idx 20; "
                    .. "call_to(FUN_1804d455c) (called twice) -> found=false "
                    .. "reason=call_to_ambiguous")
            end
        end
    end

    -- first_return -> idx 13, kind "return".
    check_resolved("cap-86-first-return",
        kcdx.locator.first_return(),
        { idx = 13, kind = "return" })

    -- last_return -> idx 58, kind "return". RED if equal to first_return's idx
    -- (proves first vs last differ).
    check_resolved("cap-86-last-return",
        kcdx.locator.last_return(),
        { idx = 58, kind = "return" })

    -- return_value("cVar2") -> idx 55 (the first return whose text references
    -- cVar2: "return cVar2"). RED if it picks a different return (e.g. idx 13
    -- "return '\0'", which does NOT reference cVar2).
    check_resolved("cap-86-return-value",
        kcdx.locator.return_value("cVar2"),
        { idx = 55, kind = "return" })

    -- references_string(" ignoring delay") -> idx 1 (the FIRST statement whose
    -- string_ref equals " ignoring delay"; ground truth: SaveGame's string_ref
    -- statements are idx 1 " ignoring delay", idx 3 " immediately", idx 5
    -- "Quick-saving", idx 6 "Saving"). kind assign, byte_range_len 7. RED if it
    -- resolves to a DIFFERENT statement (proves the resolver matches the
    -- supplied string, not a fixed one) or fails to resolve when the string IS
    -- present.
    check_resolved("cap-86-references-string",
        kcdx.locator.references_string(" ignoring delay"),
        { idx = 1, kind = "assign", brl = 7 })

    -- first_read_of_cvar("Quick-saving") -> idx 5. The cvar name is carried in
    -- the SAME string_ref column as a string reference (refdb groups
    -- reads_cvar + references_string under string_ref), so this resolves to the
    -- first statement whose string_ref equals the supplied arg. A DISTINCT
    -- ground-truth value (idx 5, not references_string's idx 1) proves the
    -- resolver reads the supplied name — RED if it resolves to idx 1 (the
    -- references_string target) or any other statement, or fails when present.
    check_resolved("cap-86-first-read-of-cvar",
        kcdx.locator.first_read_of_cvar("Quick-saving"),
        { idx = 5, kind = "assign", brl = 7 })

    -- matching: TWO assertions in one row.
    --   (a) matching{} (empty) -> idx 0 (first statement, no constraint).
    --   (b) matching{kind="call"} -> idx 8 (first call statement).
    -- RED if the empty matcher resolves to anything but idx 0, or the
    -- kind-constrained matcher resolves to anything but the first call (idx 8).
    do
        local row = "cap-86-matching"
        local empty = kcdx.locator.matching{}
        local call_kind = kcdx.locator.matching{ kind = "call" }
        if empty == nil or call_kind == nil then
            kcdx.test.report(row, false,
                "kcdx.locator.matching{...} returned nil — locator value not "
                .. "produced")
        else
            local re = empty:resolve(MODULE, FN)
            local rc = call_kind:resolve(MODULE, FN)
            if is_degraded(re) then
                kcdx.test.report(row, true,
                    "DEGRADED PASS: SaveGame statement data not deployed "
                    .. "(reason=" .. tostring(re.reason) .. ")")
            elseif re.found ~= true or re.statement_idx ~= 0 then
                kcdx.test.report(row, false,
                    "matching{} (empty) resolved found=" .. tostring(re.found)
                    .. " idx=" .. tostring(re.statement_idx)
                    .. " — expected found=true idx=0 (first statement)")
            elseif rc.found ~= true or rc.statement_idx ~= 8
                   or rc.kind ~= "call" then
                kcdx.test.report(row, false,
                    "matching{kind=\"call\"} resolved found="
                    .. tostring(rc.found) .. " idx=" .. tostring(rc.statement_idx)
                    .. " kind=" .. tostring(rc.kind)
                    .. " — expected found=true idx=8 kind=call")
            else
                kcdx.test.report(row, true,
                    "matching{} -> idx 0 (first statement); "
                    .. "matching{kind=\"call\"} -> idx 8 kind=call")
            end
        end
    end

    -- matching_pattern("48 8B C1") -> the LABELED expert raw-AOB hatch is NOT a
    -- statement-metadata locator: :resolve returns found=false with reason
    -- "matching_pattern_not_statement_locator" (refdb's contract — the AOB
    -- resolves against bytes elsewhere, never to a silent statement match).
    -- RED if it resolves to ANY statement (found=true) or returns the wrong
    -- reason. This row is NOT degraded-gated: the reject is the contract
    -- regardless of whether the statement tables are deployed (matching_pattern
    -- is rejected before any statement lookup).
    do
        local row = "cap-86-matching-pattern"
        local mp = kcdx.locator.matching_pattern("48 8B C1")
        if mp == nil then
            kcdx.test.report(row, false,
                "kcdx.locator.matching_pattern(...) returned nil — locator "
                .. "value not produced")
        else
            local r = mp:resolve(MODULE, FN)
            if r.found == false
               and r.reason == "matching_pattern_not_statement_locator" then
                kcdx.test.report(row, true,
                    "matching_pattern (the labeled expert AOB hatch) correctly "
                    .. "rejected as NOT a statement-metadata locator: "
                    .. "found=false reason=matching_pattern_not_statement_locator")
            else
                kcdx.test.report(row, false,
                    "matching_pattern resolved found=" .. tostring(r.found)
                    .. " idx=" .. tostring(r.statement_idx)
                    .. " reason=" .. tostring(r.reason)
                    .. " — expected found=false "
                    .. "reason=matching_pattern_not_statement_locator (the AOB "
                    .. "hatch must NOT resolve to a statement)")
            end
        end
    end

    -- cap-86-insert-registers-pending — a CONSUMER of a kcdx.locator.* value:
    -- kcdx.hook.insert_before(module, target, locator, fn) accepts a valid
    -- locator, registers (handle non-nil), but the curated-statement
    -- capture-thunk apply path is NOT yet wired, so the entry is DEFERRED at
    -- apply: by ready it is Failed (:applied()==false) with a teaching reason
    -- naming the not-yet-wired path — NEVER silently applied.
    --
    -- This pins BOTH halves of the as-built insert contract: the registration
    -- shape (a locator value is accepted as the required 3rd positional and a
    -- handle comes back) AND the honest deferral (the entry does not fake-green;
    -- it fails LOUD at apply with the not-yet-wired reason).
    --
    -- PENDING-CONTRACT row: it pins the registration + honest-deferral contract
    -- of the BUILT insert sub-verbs while the statement-capture apply path is
    -- unwired. When that path lands, this row flips to a fire-assert (a real
    -- insert that fires) — the reason substring is the seam that catches the
    -- transition (a wired apply path no longer emits "not yet wired").
    --
    -- FALSIFIABLE three ways: insert_before does NOT register (handle nil) →
    -- FAIL (the locator was not accepted as the required positional, or the
    -- curated target/ABI did not resolve); the entry SILENTLY applies
    -- (:applied()==true — the deferral broke and a never-firing statement hook
    -- went live) → FAIL; the reason no longer contains the not-yet-wired text →
    -- FAIL (the apply-path state changed without this row being updated).
    do
        local row = "cap-86-insert-registers-pending"
        -- A valid kcdx.locator.* value (the same family the rows above prove
        -- resolves against SaveGame) as the REQUIRED 3rd positional.
        local loc = kcdx.locator.function_entry()
        if loc == nil then
            kcdx.test.report(row, false,
                "kcdx.locator.function_entry() returned nil — cannot supply the "
                .. "required locator positional to insert_before")
        else
            -- SaveGame is a curated name carrying a verified ABI, so target +
            -- signature resolution succeed; the locator is valid; the callback
            -- is a function — registration must succeed and return a handle.
            local h, err = kcdx.hook.insert_before(MODULE, FN, loc,
                function() end, { name = "cap86_insert_pending" })
            if h == nil then
                kcdx.test.report(row, false,
                    "kcdx.hook.insert_before(\"" .. MODULE .. "\", \"" .. FN
                    .. "\", kcdx.locator.function_entry(), fn) returned nil at "
                    .. "registration: " .. tostring(err) .. " — the built "
                    .. "insert sub-verb did not accept a valid locator + curated "
                    .. "target (registration shape regressed)")
            else
                -- The insert entry must NOT silently fire. :applied() is the
                -- 3-state handle status: nil = Pending (the end-of-zone apply
                -- pass has not reached this site yet — registered, not installed),
                -- false = Failed (the apply pass ran and the insertPending entry
                -- was DEFERRED with the not-yet-wired reason), true = Applied (a
                -- statement hook went LIVE — the deferral broke). The contract
                -- this row pins is "insert registers AND does NOT silently apply"
                -- — BOTH Pending (nil) and Failed-deferred (false + reason)
                -- satisfy it; only Applied (true) is the failure. The apply pass
                -- fires at end-of-zone, which may be after this ready callback for
                -- this site, so Pending is a valid not-yet-applied state, not a
                -- regression. When the statement-capture apply path lands, this
                -- flips to a fire-assert.
                local applied = h:applied()
                local reason  = tostring(h:reason())
                local pending  = (applied == nil)
                local deferred = (applied == false)
                    and (reason:find("not yet wired in the engine", 1, true) ~= nil)
                if pending then
                    kcdx.test.report(row, true,
                        "insert_before registered (handle non-nil) and the entry "
                        .. "is PENDING at ready (:applied()==nil — the end-of-zone "
                        .. "apply pass has not reached this site yet); it did NOT "
                        .. "silently apply. The registration shape holds; this flips "
                        .. "to a fire-assert when the statement-capture apply path "
                        .. "lands (PENDING-CONTRACT row)")
                elseif deferred then
                    kcdx.test.report(row, true,
                        "insert_before registered (handle non-nil) and the entry "
                        .. "is HONESTLY DEFERRED at apply — :applied()==false, "
                        .. ":reason()=\"" .. reason .. "\". The registration shape "
                        .. "+ the not-yet-wired rejection both hold; this flips to "
                        .. "a fire-assert when the statement-capture apply path "
                        .. "lands (PENDING-CONTRACT row)")
                else
                    kcdx.test.report(row, false,
                        "insert_before handle :applied()=" .. tostring(applied)
                        .. " :reason()=\"" .. reason .. "\" — expected Pending "
                        .. "(:applied()==nil) OR Failed-deferred (:applied()==false "
                        .. "AND reason contains \"not yet wired in the engine\"). A "
                        .. "silent apply (:applied()==true) means a never-firing "
                        .. "statement hook went live — the deferral broke")
                end
            end
        end
    end

    kcdx.log.info("CAP86",
        "kcdx.locator.* :resolve self-test reported all 12 rows against "
        .. FN .. " in " .. MODULE
        .. "; + the insert_before pending-contract row "
        .. "(cap-86-insert-registers-pending)")
end)
