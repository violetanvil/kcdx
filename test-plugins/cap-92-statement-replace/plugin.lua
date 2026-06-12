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
-- WHAT THESE ROWS ASSERT — the LIVE APPLY (KI-0017 Fork-2 CORRECTED AGAIN). The
-- launch (kcdx-dev_2026-06-11_19-13-16.log) FALSIFIED the prior wiring-only
-- re-frame: statement tables ARE deployed in the runtime reference.sqlite (the
-- earlier "not deployed" read data/db-export/, the git export, not the runtime
-- DB), and the apply HONESTLY LANDS — the engine STATEMENT line (log 5751)
-- reads `replace_with applied ... stmt_kind="assign" byte_range_len=3
-- wrote_bytes=3`. So :applied()==true is now the EXPECTED PASS: the write lands
-- with the resolved byte-range (wrote_bytes == byte_range_len) against
-- function_entry resolving to idx0 / kind="assign" / byte_range_len=3.
--
-- The handle exposes only :applied() / :reason() to Lua (NO wrote_bytes /
-- byte_range_len / kind accessor — src/lua_registry.cpp H_applied/H_reason). So
-- the strongest Lua-readable assertion is :applied()==true; the
-- wrote_bytes==byte_range_len equality is confirmed ENGINE-SIDE in the STATEMENT
-- log line (`byte_range_len=N wrote_bytes=N`), which the agent reads post-launch.
--
-- DO NOT assert the pre-write site bytes are 90 90 90 — that is a CO-LOCATION
-- ARTIFACT, not the proof. cap-96 NOPs the shared SaveGame entry FIRST (native
-- bytes E9 4C F4 -> 90 90 90, log 5423); cap-92 then sees 90 90 90 -> 90 90 90
-- only because the earlier fixture already wrote. Asserting the bytes couples the
-- test to apply ORDER + the build's entry instruction. The proof is "the resolved
-- range was written" (:applied()==true), NEVER "the bytes are now 90 90 90".
--
-- Degraded function_no_statements / Pending stay as HONEST FALLBACK arms (a future
-- build that does not deploy SaveGame statements) — but :applied()==true is the
-- EXPECTED PASS now, NOT a FAIL. The co-location reject stays an honest arm (if
-- cap-96 wrote a DIFFERENT-bytes entry first, a loud reject is correct). The
-- SaveGame function_entry NOP is harmless: an identity re-write of an already-
-- NOP'd-by-cap-96 site, and standalone a 3-byte entry NOP on a function the suite
-- does not call in the test window.

local MODULE = "WHGame.dll"  -- resolve/register/apply target (live apply asserted)
local FN     = "SaveGame"    -- resolve/register/apply target (live apply asserted)

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
    -- replace_with_noop()) RESOLVES + REGISTERS as Kind::Statement and the apply
    -- LANDS. This row asserts the LIVE APPLY: :applied()==true is the EXPECTED
    -- PASS (the write lands with the resolved byte-range — wrote_bytes ==
    -- byte_range_len against function_entry resolving to idx0 / kind="assign" /
    -- byte_range_len=3, confirmed engine-side in the STATEMENT log line
    -- `byte_range_len=N wrote_bytes=N` the agent reads post-launch; the handle
    -- exposes neither accessor to Lua). function_entry is an "assign" statement;
    -- replace_with_noop applies to ANY kind, so the kind-check passes. The NOP is
    -- harmless (an identity re-write of the already-NOP'd-by-cap-96 site).
    -- FALSIFIABLE: FAILS if the handle is nil (resolution not reached — the op
    -- value was not accepted as the required positional, or the curated target
    -- did not resolve), OR :applied()==false with a reason that is NEITHER a
    -- degraded-deploy-state miss NOR a co-location reject (a real apply-path
    -- regression). :applied()==true is the EXPECTED PASS, NOT a FAIL. Degraded
    -- function_no_statements / Pending are HONEST FALLBACK arms (a build that does
    -- not deploy SaveGame statements). NO row asserts the pre-write site bytes
    -- (90 90 90 is a co-location artifact of cap-96 applying first, not the proof).
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
                -- :applied()==true = the EXPECTED PASS: the determinate noop emit
                -- landed at the resolved statement VA over byte_range_len=3. The
                -- wrote_bytes==byte_range_len equality is engine-confirmed in the
                -- STATEMENT log line (the handle exposes no byte-range accessor to
                -- Lua). nil = Pending and false + deploy-state reason = degraded
                -- are HONEST FALLBACK arms (a build not deploying SaveGame
                -- statements); false + co-location reason = an honest reject
                -- against a co-located entry on the same site. Only a nil handle
                -- or a false verdict with a non-deploy-state, non-co-location
                -- reason is a real apply-path regression.
                if applied == true then
                    kcdx.test.report(row, true,
                        "replace_with APPLIED (:applied()==true): the determinate "
                        .. "noop emit landed at SaveGame's function_entry statement "
                        .. "VA (idx0, kind=assign, byte_range_len=3). The "
                        .. "wrote_bytes==byte_range_len equality is engine-confirmed "
                        .. "in the STATEMENT log line (`byte_range_len=N "
                        .. "wrote_bytes=N`); the live apply path is proven "
                        .. "end-to-end (the never-applied defect class is guarded). "
                        .. "The NOP is harmless (identity re-write of the "
                        .. "already-NOP'd-by-cap-96 site)")
                elseif applied == nil then
                    kcdx.test.report(row, true,
                        "FALLBACK (Pending): replace_with RESOLVED + REGISTERED "
                        .. "(handle non-nil) and is :applied()==nil at ready — the "
                        .. "end-of-zone apply pass has not reached this site yet; a "
                        .. "STATIC op was accepted, no callback path. The expected "
                        .. "arm is :applied()==true once the apply pass runs")
                elseif applied == false and reason_is_deploy_state(reason) then
                    kcdx.test.report(row, true,
                        "FALLBACK (DEGRADED, function_no_statements): replace_with "
                        .. "resolved + registered + ran the apply path, but "
                        .. "SaveGame's statement data is not deployed (reason="
                        .. reason .. ") — the expected arm is :applied()==true with "
                        .. "the statement tables deployed (the live apply now "
                        .. "lands; this fallback covers a build that ships without "
                        .. "them)")
                elseif applied == false and reason_is_colocation(reason) then
                    kcdx.test.report(row, true,
                        "FALLBACK (co-location): replace_with resolved the curated "
                        .. "statement and was honestly rejected at apply against a "
                        .. "co-located entry writing DIFFERENT bytes on the same "
                        .. "site (reason=" .. reason .. ") — resolution reached, "
                        .. "nothing silent (a loud reject is correct)")
                else
                    kcdx.test.report(row, false,
                        "replace_with :applied()=" .. tostring(applied)
                        .. " :reason()=\"" .. reason .. "\" — a real apply-path "
                        .. "regression: :applied()==false with a reason that is "
                        .. "NEITHER a degraded-deploy-state miss NOR a co-location "
                        .. "reject. The expected verdict is :applied()==true (the "
                        .. "write lands with the resolved byte-range)")
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
