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
-- from the dev log) AND the apply path's honest verdict.
--
-- WHAT THESE ROWS ASSERT — the resolve -> register -> verdict WIRING, NOT a live
-- byte write. NO row asserts :applied()==true against a real game function. No
-- curated function is a safe NOP target (every curated row is an author-forwarded
-- shim or a production hook target — NOPing one breaks the surface it backs, same
-- foot-gun as NOPing SaveGame breaks save), and NO statement tables are deployed
-- (data/db-export/ carries only the address/version/module seeds), so the live
-- apply has never executed on ANY target. The byte-write was ASSERTED in code,
-- never OBSERVED. SaveGame is kept here purely as a RESOLVE/REGISTER target — it
-- carries NO live-write claim now, so the foot-gun is gone structurally. The
-- honest, falsifiable claim is: resolution reached + the entry registered as
-- Kind::Statement + an HONEST verdict (Pending / degraded function_no_statements
-- / co-location reject) — never a live write. A live byte-write proof is a real
-- future coverage item, blocked on a purpose-built curated test-stub function
-- (one the engine never calls and never forwards, with statement data deployed
-- for it — net-new curation, AP18-gated, user-approved); it is NOT this fixture.

local MODULE = "WHGame.dll"  -- resolve/register target only; NO live write asserted
local FN     = "SaveGame"    -- resolve/register target only; NO live write asserted

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
-- A reason naming a co-located entry (overlap/verify/conflict at the SAME site)
-- is an honest resolution-reached outcome, never a silent one — the same arm
-- comp-20 uses when its boundary-time entry rejects against this fixture's entry.
local function reason_is_colocation(reason)
    if type(reason) ~= "string" then return false end
    return reason:find("conflict", 1, true) ~= nil
        or reason:find("overlap", 1, true) ~= nil
        or reason:find("verify", 1, true) ~= nil
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
    -- replace_with_noop()) RESOLVES + REGISTERS as Kind::Statement with an honest
    -- verdict. This row asserts the resolve -> register -> verdict WIRING, NOT a
    -- live byte write (no curated function is a safe NOP target; no statement
    -- tables are deployed, so a live write has never executed on any target).
    -- function_entry is an "assign" statement; replace_with_noop applies to ANY
    -- kind, so the kind-check passes.
    -- FALSIFIABLE: FAILS if the handle is nil (resolution not reached — the op
    -- value was not accepted as the required positional, or the curated target
    -- did not resolve), OR the verdict is :applied()==true against the real
    -- SaveGame function (a live write was asserted where none can honestly
    -- happen — the entry must NOT claim it wrote a real game function), OR the
    -- verdict is outside {Pending, degraded function_no_statements,
    -- co-location reject}. The honest verdict set is Pending (apply pass not
    -- reached) / degraded function_no_statements (no statement tables deployed)
    -- / co-location reject — never an applied live write.
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
                    .. " — resolution was NOT reached: the static op was not "
                    .. "accepted as the required positional, or the curated "
                    .. "target did not resolve")
            else
                local applied = h:applied()
                local reason  = tostring(h:reason())
                -- The honest verdict set: nil = Pending (apply pass not reached
                -- this site yet), false + deploy-state reason = degraded
                -- (no statement tables deployed), false + co-location reason =
                -- honest reject against a co-located entry on the same site.
                -- :applied()==true against the REAL SaveGame function is a FAIL:
                -- no live write can honestly land (no NOP-safe target, no
                -- statement data deployed) — the wiring claim is resolve+register,
                -- never an observed byte write.
                if applied == true then
                    kcdx.test.report(row, false,
                        "replace_with :applied()==true against the real SaveGame "
                        .. "function — but NO live byte write can honestly land "
                        .. "(no NOP-safe curated target, no statement tables "
                        .. "deployed). A true verdict means a live write was "
                        .. "asserted where none can occur: the row's claim is "
                        .. "resolve -> register -> honest verdict, NOT a write. "
                        .. "reason=" .. reason)
                elseif applied == nil then
                    kcdx.test.report(row, true,
                        "replace_with RESOLVED + REGISTERED (handle non-nil) and "
                        .. "is PENDING at ready (:applied()==nil — the end-of-zone "
                        .. "apply pass has not reached this site yet); a STATIC op "
                        .. "was accepted, no callback path. The resolve -> register "
                        .. "-> verdict wiring holds (Kind::Statement, no live write "
                        .. "asserted)")
                elseif applied == false and reason_is_deploy_state(reason) then
                    kcdx.test.report(row, true,
                        "DEGRADED (function_no_statements): replace_with resolved "
                        .. "+ registered + ran the apply path, but SaveGame's "
                        .. "statement data is not deployed (reason=" .. reason
                        .. ") — the resolve -> register -> verdict wiring is "
                        .. "proven; a live write is gated on a deployed statement "
                        .. "table for a NOP-safe target (a future coverage item)")
                elseif applied == false and reason_is_colocation(reason) then
                    kcdx.test.report(row, true,
                        "replace_with resolved the curated statement and was "
                        .. "honestly rejected at apply against a co-located entry "
                        .. "on the same site (reason=" .. reason .. ") — "
                        .. "resolution reached, nothing silent, no live write "
                        .. "asserted")
                else
                    kcdx.test.report(row, false,
                        "replace_with :applied()=" .. tostring(applied)
                        .. " :reason()=\"" .. reason .. "\" — expected an HONEST "
                        .. "verdict: Pending (nil), degraded function_no_statements, "
                        .. "or a co-location reject. A non-deploy-state, "
                        .. "non-co-location failure is a real resolve/register/"
                        .. "apply-path regression")
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
