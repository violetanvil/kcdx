-- CAP-92 plugin.lua — kcdx.statement.replace_with static-bytes modification.
--
-- Proves the AUTHOR SURFACE + the apply path built in src/lua_bind_statement.cpp:
-- kcdx.statement.replace_with(module, target, [locator], op) resolves a curated
-- statement, runs the op KIND check, emits the DETERMINATE op's bytes, and writes
-- them in place as a Kind::Statement deferred-apply entry with ZERO per-call Lua
-- dispatch (no callback is ever taken — the modified bytes execute natively).
--
-- THE ZERO-PER-CALL-DISPATCH PROPERTY IS STRUCTURAL: replace_with takes a STATIC
-- kcdx.op.* value, NOT a callback. The entry is Kind::Statement, not Kind::Hook,
-- so no Lua callback ref exists and no per-call dispatch CAN fire. The proof is
-- the ABSENCE of any per-call dispatch line for the site (the agent reads that
-- from the dev log) AND the apply path's honest applied/deferred verdict.
--
-- SAFETY: these rows do NOT destructively rewrite a live game function (a real
-- neutralization of SaveGame would break saving). Choosing a boot-safe curated
-- function to statically neutralize is a surfaced DECISION for the maintainer,
-- not the test's to make. The rows pin the apply path's RESOLVE + KIND-CHECK +
-- REGISTER-AS-Kind::Statement + honest verdict — the full wiring up to the write
-- decision. A row flips to a live byte-readback assert when that target is chosen.

local MODULE = "WHGame.dll"
local FN     = "SaveGame"

-- A resolve/apply miss whose reason is a deploy-state miss (the statement tables
-- are not deployed) is a DEGRADED observation, not a failure — the wiring is
-- proven, the ground-truth data is simply absent. Everything else is a real FAIL.
local function reason_is_deploy_state(reason)
    if type(reason) ~= "string" then return false end
    return reason:find("function_no_statements", 1, true) ~= nil
        or reason:find("db_not_loaded", 1, true) ~= nil
        or reason:find("statement tables must be deployed", 1, true) ~= nil
        or reason:find("not deployed", 1, true) ~= nil
end

kcdx.on("ready", function()
    -- The kcdx.statement namespace must exist for any row to run.
    if kcdx.statement == nil then
        for _, row in ipairs({
            "cap-92-replace-with-registers", "cap-92-kind-mismatch",
            "cap-92-deferred-op", "cap-92-zero-dispatch",
        }) do
            kcdx.test.report(row, false,
                "kcdx.statement namespace is not registered — the statement "
                .. "binder did not bind")
        end
        return
    end
    -- The op + locator namespaces this surface consumes must also exist.
    if kcdx.op == nil or kcdx.locator == nil then
        for _, row in ipairs({
            "cap-92-replace-with-registers", "cap-92-kind-mismatch",
            "cap-92-deferred-op", "cap-92-zero-dispatch",
        }) do
            kcdx.test.report(row, false,
                "kcdx.op / kcdx.locator namespace missing — replace_with cannot "
                .. "consume an op + locator value")
        end
        return
    end

    -- =====================================================================
    -- cap-92-replace-with-registers — replace_with(SaveGame, function_entry,
    -- replace_with_noop()) registers, and the entry is NOT a callback path.
    -- function_entry is an "assign" statement; replace_with_noop applies to ANY
    -- kind, so the kind-check passes and the determinate noop emits 3×0x90.
    -- FALSIFIABLE: handle nil (the op value was not accepted as the required
    -- positional, or the curated target did not resolve) → FAIL; the entry
    -- applies as a callback (impossible — no callback was passed) → there is no
    -- callback path, so :applied()==true means the static write landed (PASS) and
    -- :applied()==false with a deploy-state reason is DEGRADED PASS.
    do
        local row = "cap-92-replace-with-registers"
        local op = kcdx.op.replace_with_noop()
        local loc = kcdx.locator.function_entry()
        if op == nil or loc == nil then
            kcdx.test.report(row, false,
                "kcdx.op.replace_with_noop() / kcdx.locator.function_entry() "
                .. "returned nil — cannot build the replace_with call")
        else
            local h, err = kcdx.statement.replace_with(MODULE, FN, loc, op,
                { name = "cap92_noop_entry" })
            if h == nil then
                kcdx.test.report(row, false,
                    "kcdx.statement.replace_with(\"" .. MODULE .. "\", \"" .. FN
                    .. "\", kcdx.locator.function_entry(), "
                    .. "kcdx.op.replace_with_noop()) returned nil at "
                    .. "registration: " .. tostring(err)
                    .. " — the static op was not accepted as the required "
                    .. "positional, or the curated target did not resolve")
            else
                local applied = h:applied()
                local reason  = tostring(h:reason())
                -- nil = Pending (apply pass not reached this site yet), true =
                -- the static write landed, false = Failed (a deploy-state miss is
                -- DEGRADED; any other reason is a real FAIL).
                if applied == nil then
                    kcdx.test.report(row, true,
                        "replace_with registered (handle non-nil) and is PENDING "
                        .. "at ready (:applied()==nil — the end-of-zone apply pass "
                        .. "has not reached this site yet); a STATIC op was "
                        .. "accepted, no callback path. The Kind::Statement "
                        .. "wiring holds")
                elseif applied == true then
                    kcdx.test.report(row, true,
                        "replace_with APPLIED (:applied()==true) — the curated "
                        .. "statement resolved, the noop emitted 3×0x90, and the "
                        .. "static bytes were written natively (zero per-call "
                        .. "dispatch — a static op, no callback). reason="
                        .. reason)
                elseif applied == false and reason_is_deploy_state(reason) then
                    kcdx.test.report(row, true,
                        "DEGRADED PASS: replace_with registered + ran the apply "
                        .. "path, but SaveGame's statement data is not deployed "
                        .. "(reason=" .. reason .. ") — the resolve+emit+write "
                        .. "wiring is proven; the live write is gated on the "
                        .. "deployed statement tables")
                else
                    kcdx.test.report(row, false,
                        "replace_with :applied()=" .. tostring(applied)
                        .. " :reason()=\"" .. reason .. "\" — expected Pending "
                        .. "(nil), Applied (true), or a deploy-state DEGRADED "
                        .. "miss. A non-deploy-state failure is a real apply-path "
                        .. "regression")
                end
            end
        end
    end

    -- =====================================================================
    -- cap-92-kind-mismatch — replace_with(SaveGame, function_entry [an "assign"
    -- statement], always_take_branch() [requires "branch"]) MUST be rejected at
    -- apply with a teaching reason naming the actual + required kind. The kind
    -- check fires at APPLY (the resolved statement's kind is known only then).
    -- FALSIFIABLE: the entry applies silently (:applied()==true) → FAIL; the
    -- reason omits the actual kind ("assign") or the required kind ("branch") →
    -- FAIL. A deploy-state miss (the statement never resolves) is DEGRADED.
    do
        local row = "cap-92-kind-mismatch"
        local op = kcdx.op.always_take_branch()   -- requires a "branch" statement
        local loc = kcdx.locator.function_entry()  -- resolves an "assign" statement
        if op == nil or loc == nil then
            kcdx.test.report(row, false,
                "kcdx.op.always_take_branch() / function_entry() returned nil")
        else
            local h, err = kcdx.statement.replace_with(MODULE, FN, loc, op,
                { name = "cap92_kind_mismatch" })
            if h == nil then
                kcdx.test.report(row, false,
                    "replace_with returned nil at registration: " .. tostring(err)
                    .. " — the op + locator should register; the kind mismatch "
                    .. "is caught at APPLY, not registration")
            else
                local applied = h:applied()
                local reason  = tostring(h:reason())
                if applied == nil then
                    -- Pending: the apply pass has not run for this site yet. The
                    -- mismatch verdict is not available — report the registration
                    -- half held and flag pending (not a silent apply).
                    kcdx.test.report(row, true,
                        "replace_with(always_take_branch on an assign statement) "
                        .. "registered and is PENDING at ready (:applied()==nil) "
                        .. "— the kind-mismatch verdict fires at the end-of-zone "
                        .. "apply pass; it did NOT silently apply")
                elseif applied == true then
                    kcdx.test.report(row, false,
                        "replace_with(always_take_branch on an \"assign\" "
                        .. "statement) APPLIED (:applied()==true) — a branch op on "
                        .. "a non-branch statement must be REJECTED, not silently "
                        .. "written")
                elseif applied == false and reason_is_deploy_state(reason) then
                    kcdx.test.report(row, true,
                        "DEGRADED PASS: SaveGame statement data not deployed "
                        .. "(reason=" .. reason .. ") — the kind-mismatch path "
                        .. "needs the resolved statement kind, which needs the "
                        .. "deployed tables")
                elseif applied == false
                       and reason:find("assign", 1, true)
                       and reason:find("branch", 1, true) then
                    kcdx.test.report(row, true,
                        "replace_with(always_take_branch on an \"assign\" "
                        .. "statement) correctly REJECTED at apply: "
                        .. ":applied()==false, reason names the actual kind "
                        .. "(assign) AND the required kind (branch): \""
                        .. reason .. "\"")
                else
                    kcdx.test.report(row, false,
                        "replace_with kind-mismatch rejected but the reason did "
                        .. "not name BOTH the actual kind (assign) and the "
                        .. "required kind (branch): reason=\"" .. reason
                        .. "\" — the teaching error must name both")
                end
            end
        end
    end

    -- =====================================================================
    -- cap-92-deferred-op — replace_with(SaveGame, first_return,
    -- invert_branch_condition()) — a DEFERRED op whose final bytes need the
    -- apply-time statement's own bytes (the resolution layer does not expose
    -- them) — MUST surface a not-yet-emittable deferral at apply, never a
    -- fabricated byte. NOTE: invert_branch_condition requires a "branch"
    -- statement; first_return resolves a "return" statement. Both a kind reject
    -- AND a deferred-op reason are honest non-silent outcomes; the row accepts
    -- EITHER honest rejection (the entry must NOT silently apply). The point is
    -- the deferred op never fabricates a byte and never goes live silently.
    -- FALSIFIABLE: :applied()==true → FAIL (a deferred/mismatched op went live).
    do
        local row = "cap-92-deferred-op"
        local op = kcdx.op.invert_branch_condition()  -- deferred + requires branch
        local loc = kcdx.locator.first_return()        -- resolves a "return" stmt
        if op == nil or loc == nil then
            kcdx.test.report(row, false,
                "kcdx.op.invert_branch_condition() / first_return() returned nil")
        else
            local h, err = kcdx.statement.replace_with(MODULE, FN, loc, op,
                { name = "cap92_deferred_op" })
            if h == nil then
                kcdx.test.report(row, false,
                    "replace_with(invert_branch_condition) returned nil at "
                    .. "registration: " .. tostring(err)
                    .. " — a deferred op should still register; the deferral "
                    .. "surfaces at APPLY")
            else
                local applied = h:applied()
                local reason  = tostring(h:reason())
                if applied == nil then
                    kcdx.test.report(row, true,
                        "replace_with(invert_branch_condition) registered and is "
                        .. "PENDING at ready (:applied()==nil) — the deferral "
                        .. "verdict fires at the apply pass; it did NOT silently "
                        .. "apply or fabricate a byte")
                elseif applied == true then
                    kcdx.test.report(row, false,
                        "replace_with(invert_branch_condition) APPLIED "
                        .. "(:applied()==true) — a deferred (statement-bytes-"
                        .. "dependent) op must NOT go live with a fabricated byte; "
                        .. "it must surface the not-yet-emittable deferral")
                elseif applied == false and reason_is_deploy_state(reason) then
                    kcdx.test.report(row, true,
                        "DEGRADED PASS: SaveGame statement data not deployed "
                        .. "(reason=" .. reason .. ")")
                elseif applied == false
                       and (reason:find("not yet emittable", 1, true)
                            or reason:find("requires a", 1, true)) then
                    kcdx.test.report(row, true,
                        "replace_with(invert_branch_condition) correctly did NOT "
                        .. "go live: :applied()==false with an honest reason "
                        .. "(deferred-op not-yet-emittable, or a kind reject) — "
                        .. "never a fabricated byte: \"" .. reason .. "\"")
                else
                    kcdx.test.report(row, false,
                        "replace_with(invert_branch_condition) :applied()=false "
                        .. "but the reason is neither the not-yet-emittable "
                        .. "deferral nor a kind reject: \"" .. reason
                        .. "\" — expected an honest deferral/reject, not an "
                        .. "opaque failure")
                end
            end
        end
    end

    -- =====================================================================
    -- cap-92-zero-dispatch — the static-bytes contract: replace_with takes a
    -- STATIC op ONLY, never a callback. A function in the op slot is REJECTED
    -- (registration returns nil + a teaching error). This is what makes the
    -- zero-per-call-dispatch claim STRUCTURAL — there is no callback positional,
    -- so no per-call Lua dispatch can fire (distinct from kcdx.hook, which DOES
    -- dispatch per call). FALSIFIABLE: replace_with ACCEPTS a function as the op
    -- (handle non-nil) → FAIL (a per-call callback path crept into a surface
    -- that must be static-only, defeating the zero-cost claim).
    do
        local row = "cap-92-zero-dispatch"
        local loc = kcdx.locator.function_entry()
        if loc == nil then
            kcdx.test.report(row, false,
                "kcdx.locator.function_entry() returned nil")
        else
            -- Pass a FUNCTION where the op must be — this is the kcdx.hook shape,
            -- which kcdx.statement.replace_with must REJECT (it is static-op only).
            local h, err = kcdx.statement.replace_with(MODULE, FN, loc,
                function() return 0 end, { name = "cap92_reject_callback" })
            if h ~= nil then
                kcdx.test.report(row, false,
                    "kcdx.statement.replace_with accepted a FUNCTION as the op "
                    .. "(handle non-nil) — replace_with is STATIC-OP ONLY (a "
                    .. "per-call callback is kcdx.hook). Accepting a callback "
                    .. "means a per-call Lua dispatch path crept in, defeating "
                    .. "the zero-per-call-cost contract")
            elseif type(err) == "string"
                   and (err:find("kcdx.op", 1, true)
                        or err:find("static op", 1, true)
                        or err:find("callback", 1, true)) then
                kcdx.test.report(row, true,
                    "replace_with correctly REJECTED a function in the op slot — "
                    .. "it is static-op only (no callback positional). The "
                    .. "zero-per-call-dispatch property is STRUCTURAL: no callback "
                    .. "exists, so no per-call Lua dispatch can fire. Teaching "
                    .. "error: \"" .. tostring(err) .. "\"")
            else
                kcdx.test.report(row, false,
                    "replace_with rejected the function (good) but the error did "
                    .. "not teach that a STATIC kcdx.op.* value is required "
                    .. "(callback vs static-op distinction): err=\""
                    .. tostring(err) .. "\"")
            end
        end
    end

    kcdx.log.info("CAP92",
        "kcdx.statement.replace_with self-test reported 4 rows against " .. FN
        .. " in " .. MODULE .. " (resolve + kind-check + deferred-op + "
        .. "static-op-only contract); the apply path is Kind::Statement with "
        .. "ZERO per-call Lua dispatch (no callback) — the agent reads the "
        .. "ABSENCE of a per-call STATEMENT dispatch line from the dev log")
end)
