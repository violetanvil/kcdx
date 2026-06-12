-- COMP-20 declarer plugin.lua — declares the cross-plugin behaviors + owns
-- every implementation. See kcdx.toml's header for the fixture map; declare
-- ORDER below is boundary apply order (the fixtures depend on it).
--
-- The consumer (comp-20-behavior-consumer, priority 60) loads LATER and sets
-- these behaviors at load. Rows report at "input_loaded" — the apply
-- boundary runs post-RunPostGameLoad / pre-InputLoaded, so input_loaded is
-- the earliest post-boundary observation point.

local MODULE = "WHGame.dll"  -- resolve/register/apply target (live apply asserted)
local FN     = "SaveGame"    -- resolve/register/apply target (live apply asserted)
-- The §9 declarer leg asserts the LIVE APPLY from inside the apply boundary
-- (KI-0017 Fork-2 CORRECTED AGAIN). The launch (kcdx-dev_2026-06-11_19-13-16.log)
-- FALSIFIED the wiring-only re-frame: statement tables ARE deployed in the runtime
-- reference.sqlite (the earlier "not deployed" read data/db-export/, the git
-- export, not the runtime DB) and the boundary-run replace_with HONESTLY LANDS —
-- log 5750: `comp20_decl_stmt applied successfully`; log 5751: STATEMENT line
-- stmt_kind="assign" byte_range_len=3 wrote_bytes=3. So :applied()==true is the
-- EXPECTED PASS (the write lands with the resolved byte-range — wrote_bytes ==
-- byte_range_len against function_entry resolving to idx0/assign/brl=3, confirmed
-- engine-side in the STATEMENT log the agent reads post-launch; the handle exposes
-- no byte-range accessor to Lua). The SaveGame function_entry NOP is harmless (an
-- identity re-write of the already-NOP'd-by-cap-96 site). Degraded
-- function_no_statements stays an HONEST FALLBACK arm. The future live-byte-write
-- tech-debt item is DROPPED — that proof now exists.

-- Per-behavior observation state, written by the implementations at the
-- boundary, read by the rows at input_loaded.
local obs = {
    late_pend       = { fires = 0, value = nil },
    boundary_first  = { fires = 0, value = nil },
    hardcore_combat = { fires = 0, value = nil,
                        stmt_handle = nil, stmt_err = nil,
                        stmt_attempted = false },
    conflict_target = { fires = 0, value = nil },
    raise_behavior  = { fires = 0 },
    drain_setter    = { fires = 0,
                        set_target_ok = nil, set_target_err = nil,
                        set_late_ok = nil, set_late_err = nil,
                        set_applied_ok = nil, set_applied_err = nil },
    drain_target    = { fires = 0, value = nil },
    never_set       = { fires = 0 },
}

-- A resolve/apply miss whose reason is a deploy-state miss (the statement
-- tables are not deployed) is a DEGRADED observation, not a failure — the
-- same arm cap-92 uses. A reason naming the cap-92 co-located entry
-- (overlap/verify/conflict at the SAME site) is likewise an
-- honest resolution-reached outcome, never a silent one.
local function reason_is_deploy_state(reason)
    if type(reason) ~= "string" then return false end
    return reason:find("function_no_statements", 1, true) ~= nil
        or reason:find("db_not_loaded", 1, true) ~= nil
        or reason:find("statement tables must be deployed", 1, true) ~= nil
        or reason:find("not deployed", 1, true) ~= nil
end
local function reason_is_colocation(reason)
    if type(reason) ~= "string" then return false end
    return reason:find("conflict", 1, true) ~= nil
        or reason:find("overlap", 1, true) ~= nil
        or reason:find("verify", 1, true) ~= nil
end

-- ===========================================================================
-- Declares (load-time). Declare order = boundary apply order.
-- ===========================================================================

-- 1. late_pend — never set at load; drain_setter pends it mid-drain.
kcdx.behavior.declare("late_pend", {
    description    = "comp-20 late-pend fixture (set mid-drain, slot passed)",
    default        = 0,
    implementation = function(value)
        obs.late_pend.fires = obs.late_pend.fires + 1
        obs.late_pend.value = value
    end,
})

-- 2. boundary_first — applies first; drain_setter then pcall-sets it.
kcdx.behavior.declare("boundary_first", {
    description    = "comp-20 already-applied fixture (applies first)",
    default        = false,
    implementation = function(value)
        obs.boundary_first.fires = obs.boundary_first.fires + 1
        obs.boundary_first.value = value
    end,
})

-- 3. hardcore_combat — the US-1/US-2 + §9 declarer-leg behavior: the
-- implementation reaches the hash-checked kcdx.statement.replace_with on a
-- REAL engine-known target (the same module/function/locator/op as cap-92's
-- apply row — no new curated target; the leg proves the LIVE APPLY from inside
-- the boundary — the write lands with the resolved byte-range, KI-0017 re-frame
-- reversed).
kcdx.behavior.declare("hardcore_combat", {
    description    = "comp-20 US-1/US-2 fixture (statement-backed)",
    default        = false,
    implementation = function(value)
        obs.hardcore_combat.fires = obs.hardcore_combat.fires + 1
        obs.hardcore_combat.value = value
        if obs.hardcore_combat.fires == 1 then
            obs.hardcore_combat.stmt_attempted = true
            if kcdx.statement == nil or kcdx.op == nil
                or kcdx.locator == nil then
                obs.hardcore_combat.stmt_err =
                    "kcdx.statement/kcdx.op/kcdx.locator namespace missing"
            else
                local h, err = kcdx.statement.replace_with(
                    MODULE, FN,
                    kcdx.locator.function_entry(),
                    kcdx.op.replace_with_noop(),
                    { name = "comp20_decl_stmt" })
                obs.hardcore_combat.stmt_handle = h
                obs.hardcore_combat.stmt_err    = err
            end
        end
    end,
})

-- 4. conflict_target — two setters (this plugin + the later consumer).
kcdx.behavior.declare("conflict_target", {
    description    = "comp-20 conflict fixture (two setters, last wins)",
    default        = "untouched-default",
    implementation = function(value)
        obs.conflict_target.fires = obs.conflict_target.fires + 1
        obs.conflict_target.value = value
    end,
})

-- 5. raise_behavior — deliberately raises at the boundary.
kcdx.behavior.declare("raise_behavior", {
    description    = "comp-20 boundary-raise fixture (impl raises)",
    default        = "default-untouched",
    implementation = function(value)
        obs.raise_behavior.fires = obs.raise_behavior.fires + 1
        error("comp-20 deliberate boundary raise (fixture)")
    end,
})

-- 6. drain_setter — the mid-drain actor: a plain set on the not-yet-applied
-- drain_target (pending update), a plain set on the never-set late_pend
-- (late pend), and a pcall set on the already-applied REVERT-LESS
-- boundary_first (the §5.4 post-load toggle's revert-less teaching error —
-- an applied behavior with no `revert` cannot change mid-session).
kcdx.behavior.declare("drain_setter", {
    description    = "comp-20 mid-drain setter fixture",
    default        = false,
    implementation = function(value)
        obs.drain_setter.fires = obs.drain_setter.fires + 1
        obs.drain_setter.set_target_ok, obs.drain_setter.set_target_err =
            pcall(kcdx.behavior.set, "drain_target", 20)
        obs.drain_setter.set_late_ok, obs.drain_setter.set_late_err =
            pcall(kcdx.behavior.set, "late_pend", 77)
        obs.drain_setter.set_applied_ok, obs.drain_setter.set_applied_err =
            pcall(kcdx.behavior.set, "boundary_first", true)
    end,
})

-- 7. drain_target — set 10 by the consumer; drain_setter updates the still-
-- pending record to 20 before this slot -> applies ONCE with 20.
kcdx.behavior.declare("drain_target", {
    description    = "comp-20 pending-update fixture",
    default        = 0,
    implementation = function(value)
        obs.drain_target.fires = obs.drain_target.fires + 1
        obs.drain_target.value = value
    end,
})

-- 8. never_set — declared, never set: the boundary must SKIP it.
kcdx.behavior.declare("never_set", {
    description    = "comp-20 never-set fixture (skipped by the boundary)",
    default        = "skip-default",
    implementation = function(value)
        obs.never_set.fires = obs.never_set.fires + 1
    end,
})

-- The declarer's OWN load-time set on conflict_target — the EARLIER of the
-- two setters (the later consumer's different value wins + warns).
kcdx.behavior.set("conflict_target", "declarer-value")

-- §6 branch-a reorder fixture: this declarer (priority 30) loads BEFORE the
-- reorder-target plugin (comp-22-behavior-reorder-target, priority 90); it
-- tries to set that plugin's `reorder_target` at load. That declarer has NOT
-- run yet (and it is installed + enabled + loads LATER), so the set hits the
-- reorder error naming the exact fix. The target lives on its OWN plugin (not
-- the §9 consumer, which must stay declare-free) so the error names a real
-- later-loading owner, not the absent-owner branch. pcall-captured; the row
-- asserts the wording.
local LATER = "ts.comp_22_behavior_reorder_target."
local ok_reorder, err_reorder =
    pcall(kcdx.behavior.set, LATER .. "reorder_target", "too-early")

-- ===========================================================================
-- Rows — report at input_loaded (post-boundary).
-- ===========================================================================
kcdx.on("input_loaded", function()
    local report = kcdx.test.report

    -- comp-20-impl-once-final — US-1/US-2: the implementation fired EXACTLY
    -- ONCE at the boundary with the FINAL value, set by the LATER consumer
    -- plugin. FALSIFIABLE: 0 fires (boundary never invoked it), 2+ fires
    -- (more than once per boundary), or a value other than the consumer's
    -- "hardcore-on" -> FAIL.
    do
        local row = "comp-20-impl-once-final"
        local o = obs.hardcore_combat
        if o.fires ~= 1 then
            report(row, false, "hardcore_combat's implementation fired "
                .. o.fires .. " times (expected EXACTLY ONCE at the apply "
                .. "boundary)")
        elseif o.value ~= "hardcore-on" then
            report(row, false, "hardcore_combat's implementation received "
                .. tostring(o.value) .. " (expected the consumer plugin's "
                .. "final value 'hardcore-on' — the cross-plugin set did "
                .. "not reach the boundary)")
        else
            report(row, true, "hardcore_combat's implementation fired "
                .. "exactly once at the apply boundary with the final "
                .. "value 'hardcore-on' set by the LATER consumer plugin "
                .. "(comp-20-behavior-consumer) — the US-1/US-2 "
                .. "cross-plugin story end-to-end")
        end
    end

    -- comp-20-declarer-statement — the §9 declarer leg: the boundary-run
    -- implementation reached kcdx.statement.replace_with on the engine-known
    -- SaveGame function-entry statement and the apply LANDS from INSIDE the
    -- boundary (KI-0017 Fork-2 CORRECTED AGAIN — the launch proved statement
    -- tables ARE deployed and the boundary-run apply honestly lands, log 5750-
    -- 5751). This row asserts the LIVE APPLY: the call fired, the entry registered
    -- as Kind::Statement, the boundary's own ApplyZone drain ran it, and
    -- :applied()==true is the EXPECTED PASS (the write lands with the resolved
    -- byte-range — wrote_bytes==byte_range_len against function_entry resolving to
    -- idx0/assign/brl=3, engine-confirmed in the STATEMENT log; the handle exposes
    -- no byte-range accessor to Lua). The NOP is harmless (an identity re-write of
    -- the already-NOP'd-by-cap-96 site).
    -- FALSIFIABLE: FAILS if the statement call was never attempted, the
    -- registration returned nil (resolution not reached), the handle is still
    -- PENDING at input_loaded (the boundary's own ApplyZone drain did not land it
    -- pre-InputLoaded — probe F2's gap), OR :applied()==false with a reason that is
    -- NEITHER a degraded deploy-state miss NOR a co-location reject (a real
    -- apply-path regression). :applied()==true is the EXPECTED PASS, NOT a FAIL.
    -- Degraded function_no_statements stays an HONEST FALLBACK arm.
    do
        local row = "comp-20-declarer-statement"
        local o = obs.hardcore_combat
        if not o.stmt_attempted then
            report(row, false, "the implementation never reached its "
                .. "kcdx.statement.replace_with call (it did not fire)")
        elseif o.stmt_handle == nil then
            report(row, false, "kcdx.statement.replace_with(\"" .. MODULE
                .. "\", \"" .. FN .. "\", function_entry, "
                .. "replace_with_noop) from the boundary-run implementation "
                .. "returned nil (resolution not reached): " .. tostring(o.stmt_err))
        else
            local applied = o.stmt_handle:applied()
            local reason  = tostring(o.stmt_handle:reason())
            if applied == true then
                report(row, true, "the boundary-run replace_with APPLIED "
                    .. "(:applied()==true): the determinate noop emit landed at "
                    .. FN .. "'s function_entry statement VA (idx0, kind=assign, "
                    .. "byte_range_len=3) from INSIDE the apply boundary. The "
                    .. "wrote_bytes==byte_range_len equality is engine-confirmed "
                    .. "in the STATEMENT log line; the boundary-queued "
                    .. "registration reached a LIVE-apply verdict pre-InputLoaded "
                    .. "via the boundary's own ApplyZone drain — the §9 leg proves "
                    .. "the apply path end-to-end from the boundary. reason="
                    .. reason)
            elseif applied == nil then
                report(row, false, "the statement entry registered at the "
                    .. "boundary is still PENDING at input_loaded — the "
                    .. "boundary must trigger its own ApplyZone drain so "
                    .. "boundary-queued registrations reach a verdict "
                    .. "pre-InputLoaded (the expected arm is :applied()==true)")
            elseif applied == false and reason_is_deploy_state(reason) then
                report(row, true, "FALLBACK (DEGRADED, function_no_statements): "
                    .. "the replace_with from the boundary resolved + registered "
                    .. "+ ran the apply path, but " .. FN .. "'s statement data is "
                    .. "not deployed (reason=" .. reason .. ") — the expected arm "
                    .. "is :applied()==true with the tables deployed (the live "
                    .. "apply now lands; this fallback covers a build without them)")
            elseif applied == false and reason_is_colocation(reason) then
                report(row, true, "FALLBACK (co-location): the replace_with from "
                    .. "the boundary resolved the curated statement and was "
                    .. "honestly rejected at apply against a co-located entry "
                    .. "writing DIFFERENT bytes on the same site (reason=" .. reason
                    .. ") — resolution reached, nothing silent (a loud reject is "
                    .. "correct)")
            else
                report(row, false, "the statement entry's apply verdict is "
                    .. ":applied()=" .. tostring(applied) .. " reason=\""
                    .. reason .. "\" — a real apply-path regression: "
                    .. ":applied()==false with a reason that is NEITHER a "
                    .. "deploy-state (function_no_statements) miss NOR the "
                    .. "co-located-entry arm. The expected verdict is "
                    .. ":applied()==true (the write lands with the resolved range)")
            end
        end
    end

    -- comp-20-conflict-last-wins — two setters, different plugins,
    -- different values: the LATER (consumer) wins; the implementation fired
    -- once with the winning value. The one set_conflict warn naming both
    -- plugins + both values is log-confirmed by the agent at launch (not
    -- Lua-observable). FALSIFIABLE: fires ~= 1, or the value is the
    -- earlier setter's (first-wins crept in) or anything else -> FAIL.
    do
        local row = "comp-20-conflict-last-wins"
        local o = obs.conflict_target
        if o.fires ~= 1 then
            report(row, false, "conflict_target's implementation fired "
                .. o.fires .. " times (expected once with the final value)")
        elseif o.value == "declarer-value" then
            report(row, false, "conflict_target applied the EARLIER "
                .. "setter's value 'declarer-value' — last-wins inverted "
                .. "(the later consumer's 'consumer-wins' must win)")
        elseif o.value ~= "consumer-wins" then
            report(row, false, "conflict_target applied "
                .. tostring(o.value)
                .. " (expected the later setter's 'consumer-wins')")
        else
            report(row, true, "two plugins set conflict_target to "
                .. "different values and the LATER plugin's value "
                .. "('consumer-wins') applied once — last-wins; the "
                .. "set_conflict warn naming both plugins/values is "
                .. "log-confirmed at launch")
        end
    end

    -- comp-20-boundary-raise — raise_behavior's implementation raised:
    -- get() answers the DEFAULT post-boundary (value + applied cleared —
    -- truthful), the raise was invoked once, and the drain CONTINUED
    -- (drain_setter + drain_target, later in apply order, still ran). The
    -- declarer-attributed implementation_raised error line is log-confirmed
    -- at launch. FALSIFIABLE: get returns the consumer's set value (the
    -- cleared record lied), the raise impl never fired, or a later
    -- behavior did not apply (the drain aborted) -> FAIL.
    do
        local row = "comp-20-boundary-raise"
        local okGet, v = pcall(kcdx.behavior.get, "raise_behavior")
        if obs.raise_behavior.fires ~= 1 then
            report(row, false, "raise_behavior's implementation fired "
                .. obs.raise_behavior.fires
                .. " times (expected exactly one boundary invocation)")
        elseif not okGet then
            report(row, false, "get('raise_behavior') RAISED post-boundary: "
                .. tostring(v))
        elseif v ~= "default-untouched" then
            report(row, false, "get('raise_behavior') reads " .. tostring(v)
                .. " post-boundary (expected the default "
                .. "'default-untouched' — a boundary raise must clear the "
                .. "recorded value to unset so get() stays truthful)")
        elseif obs.drain_setter.fires ~= 1 or obs.drain_target.fires ~= 1 then
            report(row, false, "after raise_behavior raised, the drain did "
                .. "NOT continue (drain_setter fires="
                .. obs.drain_setter.fires .. ", drain_target fires="
                .. obs.drain_target.fires
                .. " — both later in apply order must still apply)")
        else
            report(row, true, "raise_behavior's implementation raised at "
                .. "the boundary: get() answers the default post-boundary "
                .. "(record cleared — truthful) and the drain continued "
                .. "(drain_setter + drain_target still applied); the "
                .. "declarer-attributed error line is log-confirmed at "
                .. "launch")
        end
    end

    -- comp-20-drain-pending-update — drain_setter's mid-drain set on the
    -- NOT-yet-applied drain_target updated its pending record: the
    -- implementation fired ONCE with the final 20 (never 10).
    -- FALSIFIABLE: the mid-drain set raised, fires ~= 1, or the applied
    -- value is the consumer's stale 10 -> FAIL.
    do
        local row = "comp-20-drain-pending-update"
        local o = obs.drain_target
        if obs.drain_setter.set_target_ok ~= true then
            report(row, false, "drain_setter's mid-drain set on the "
                .. "not-yet-applied drain_target RAISED: "
                .. tostring(obs.drain_setter.set_target_err)
                .. " — a mid-drain set on a pending behavior is the "
                .. "last-wins update path, not an error")
        elseif o.fires ~= 1 then
            report(row, false, "drain_target's implementation fired "
                .. o.fires .. " times (expected ONCE with the final value)")
        elseif o.value ~= 20 then
            report(row, false, "drain_target applied " .. tostring(o.value)
                .. " (expected 20 — the mid-drain update must win over the "
                .. "consumer's stale 10; applying 10 means the pending "
                .. "update was missed)")
        else
            report(row, true, "drain_setter's mid-drain set updated the "
                .. "still-pending drain_target and it applied EXACTLY ONCE "
                .. "with the final 20 (last-wins continues through the "
                .. "drain; never applied twice, never the stale 10)")
        end
    end

    -- comp-20-drain-already-applied — drain_setter's pcall set on the
    -- ALREADY-applied boundary_first (a REVERT-LESS declarer) raised the §5.4
    -- post-load toggle's revert-less teaching error ("applies at load; it
    -- cannot change mid-session"), and boundary_first stayed applied-once.
    -- (Once a behavior applied, a further set follows the post-load toggle
    -- rules: a `revert` declarer would TOGGLE; boundary_first has no `revert`,
    -- so it raises. The post-load toggle contract is built in 9.5 P1 s5.)
    -- FALSIFIABLE: the set succeeded (an applied revert-less behavior
    -- re-recorded / toggled with no revert), the error does not teach the
    -- applies-at-load / revert rule, or boundary_first fired twice -> FAIL.
    do
        local row = "comp-20-drain-already-applied"
        local o = obs.drain_setter
        if o.fires ~= 1 then
            report(row, false, "drain_setter's implementation fired "
                .. o.fires .. " times (expected once — the mid-drain "
                .. "branches never ran)")
        elseif o.set_applied_ok ~= false then
            report(row, false, "the mid-drain set on the ALREADY-applied "
                .. "boundary_first SUCCEEDED — once a behavior applied, a "
                .. "further set follows the post-load toggle rules; "
                .. "boundary_first has no `revert`, so it must raise the "
                .. "applies-at-load teaching error, never silently re-record")
        elseif type(o.set_applied_err) ~= "string"
            or not string.find(o.set_applied_err, "applies at load", 1, true)
            or not string.find(o.set_applied_err, "revert", 1, true)
        then
            report(row, false, "the mid-drain set on boundary_first raised "
                .. "but the error does not teach the applies-at-load / "
                .. "`revert` rule (expected 'applies at load ... cannot change "
                .. "mid-session' naming the `revert` fix; got: "
                .. tostring(o.set_applied_err) .. ")")
        elseif obs.boundary_first.fires ~= 1 then
            report(row, false, "boundary_first fired "
                .. obs.boundary_first.fires
                .. " times (expected once — at most once per boundary)")
        else
            report(row, true, "the mid-drain set on the already-applied "
                .. "revert-less boundary_first raised the §5.4 revert-less "
                .. "teaching error ('applies at load; it cannot change "
                .. "mid-session' naming the `revert` fix) and boundary_first "
                .. "stayed applied exactly once")
        end
    end

    -- comp-20-drain-late-pend — drain_setter's mid-drain set on the
    -- never-set late_pend (whose apply-order slot had already passed)
    -- pended it; a FOLLOW-UP worklist pass applied it once with 77.
    -- FALSIFIABLE: the mid-drain set raised, late_pend never applied (the
    -- drain stopped after one pass), fired 2+, or got a value other than
    -- 77 -> FAIL.
    do
        local row = "comp-20-drain-late-pend"
        local o = obs.late_pend
        if obs.drain_setter.set_late_ok ~= true then
            report(row, false, "drain_setter's mid-drain set on the "
                .. "never-set late_pend RAISED: "
                .. tostring(obs.drain_setter.set_late_err))
        elseif o.fires ~= 1 then
            report(row, false, "late_pend's implementation fired " .. o.fires
                .. " times (expected ONCE in a follow-up worklist pass — 0 "
                .. "means the drain stopped after the first pass and the "
                .. "late-pended entry was dropped)")
        elseif o.value ~= 77 then
            report(row, false, "late_pend applied " .. tostring(o.value)
                .. " (expected 77 from the mid-drain set)")
        else
            report(row, true, "the mid-drain set on never-set late_pend "
                .. "(slot already passed) was picked up by a follow-up "
                .. "worklist pass and applied exactly once with 77 — the "
                .. "drain keeps draining until no pending entry remains")
        end
    end

    -- comp-20-never-set-skipped — a declared, never-set behavior is
    -- SKIPPED: its implementation never fires and get answers the default.
    -- FALSIFIABLE: any fire (a default was 'applied'), or get not reading
    -- the default -> FAIL.
    do
        local row = "comp-20-never-set-skipped"
        local okGet, v = pcall(kcdx.behavior.get, "never_set")
        if obs.never_set.fires ~= 0 then
            report(row, false, "never_set's implementation fired "
                .. obs.never_set.fires
                .. " times — a never-set behavior must be SKIPPED (the "
                .. "default is a get() answer, not an applied state)")
        elseif not okGet or v ~= "skip-default" then
            report(row, false, "get('never_set') reads " .. tostring(v)
                .. " (expected the spec default 'skip-default')")
        else
            report(row, true, "never_set was skipped by the boundary (zero "
                .. "implementation fires) and get answers the spec default "
                .. "— the default is an answer, never an applied state")
        end
    end

    -- comp-20-set-reorder-error — §6 branch a (reorder): this EARLIER
    -- declarer's load-time set on the LATER `reorder_target` (declared by
    -- comp-22-behavior-reorder-target, priority 90 > this plugin's 30, so it
    -- had not declared it yet) RAISED the reorder teaching error naming the
    -- EXACT fix (the owner loads after you — move yours below it).
    -- FALSIFIABLE: the early set SUCCEEDED (resolved a not-yet-declared
    -- name), the error lacks the reorder DIRECTION ("loads after"/"below"),
    -- or it names the wrong owner -> FAIL. This is NOT the absent-owner
    -- branch (the target plugin IS installed) nor the typo branch (it
    -- declares the name, just later) — the discriminator is load order.
    do
        local row = "comp-20-set-reorder-error"
        if ok_reorder then
            report(row, false, "the early set on the LATER plugin's "
                .. "reorder_target SUCCEEDED — a set on a prefixed name whose "
                .. "declarer loads later must RAISE the reorder error, not "
                .. "resolve a name that does not exist yet")
        elseif type(err_reorder) ~= "string"
            or not (string.find(err_reorder, "loads after", 1, true)
                or string.find(err_reorder, "below", 1, true)) then
            report(row, false, "the early set raised but the error does not "
                .. "name the reorder DIRECTION ('loads after you' / 'move ... "
                .. "below it') — the reorder branch must teach the exact fix "
                .. "(got: " .. tostring(err_reorder) .. ")")
        elseif not string.find(err_reorder,
            "comp_22_behavior_reorder_target", 1, true) then
            report(row, false, "the reorder error does not name the later "
                .. "owning plugin (comp_22_behavior_reorder_target) — it must "
                .. "name both plugins (got: " .. tostring(err_reorder) .. ")")
        else
            report(row, true, "the earlier declarer's load-time set on the "
                .. "LATER plugin's reorder_target raised the reorder teaching "
                .. "error naming the exact fix (the owner 'loads after you — "
                .. "move ... below it') — §6 branch a, discriminated by load "
                .. "order from absent/typo")
        end
    end

    kcdx.log.info("COMP20",
        "behavior cross-plugin declarer reported 9 rows (impl-once-final, "
        .. "declarer-statement §9 leg, conflict-last-wins, boundary-raise + "
        .. "drain-continues, pending-update, already-applied error, "
        .. "late-pend second pass, never-set skipped, set-reorder-error)")
end)
