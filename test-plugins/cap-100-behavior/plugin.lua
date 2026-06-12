-- CAP-100 plugin.lua — kcdx.behavior.* declare/set/get/list (the
-- named-behavior registry's single-plugin surface).
--
-- Declares + sets are a LOAD-TIME act, so every declare/set (clean + the
-- deliberately-bad fixtures, all pcall-guarded) runs HERE at the top level of
-- plugin.lua — the canonical load window. Read-side assertions run
-- immediately after (declare/set are synchronous records); those rows REPORT
-- at ready. The BOUNDARY-dependent rows (did the implementation fire once
-- with the recorded value; does a post-load set error) report at
-- "input_loaded" — the apply boundary runs pre-InputLoaded, so input_loaded
-- is the earliest post-boundary observation point ("ready" fires at the
-- first ApplyZone pass, BEFORE the boundary).
--
-- Behaviors this plugin never sets must still answer the spec's DEFAULT from
-- get, and a list entry's `current` must equal its `default` — the never-set
-- contract is asserted alongside the set round-trip.
--
-- This plugin's engine-derived stamp prefix: ts.cap_100_behavior.
-- (author "ts" + name "cap_100_behavior" from the manifest — the plugin never
-- types its own prefix in a declare; only the assertions spell it out, to
-- verify the STAMPING.)

local PREFIX = "ts.cap_100_behavior."

-- Row results captured at load, reported at ready.
local results = {}  -- array of { row=, pass=, reason= }
local function rec(row, pass, reason)
    results[#results + 1] = { row = row, pass = pass, reason = reason }
end

-- Boundary-dependent results, reported at input_loaded (post-boundary).
local late_results = {}
local function rec_late(row, pass, reason)
    late_results[#late_results + 1] = { row = row, pass = pass, reason = reason }
end

local ROWS = {
    "cap-100-declare-list",
    "cap-100-missing-field-errors",
    "cap-100-duplicate-second-errors",
    "cap-100-get-default",
    "cap-100-list-prefix-filter",
    "cap-100-alias-get",
    "cap-100-set-nil-error",
    "cap-100-set-typo-error",
    "cap-100-set-bare-no-declarer",
    "cap-100-set-owner-absent",
}
local LATE_ROWS = {
    "cap-100-set-records-and-applies",
    "cap-100-post-load-set-error",
}

-- State the boundary-dependent rows read at input_loaded. Declared at file
-- scope (NOT inside the verbs_ok branch) so the input_loaded closure below
-- captures these as upvalues, not globals.
local counted_fires = 0
local counted_value = nil
local ok_counted, err_counted
local ok_set, err_set
local ok_get_after_set, get_after_set

-- The domain must be a TABLE before any member is read — a non-table
-- kcdx.behavior would make the member index itself the error. The type check
-- strictly precedes every member access (condition AND failure message),
-- mirroring the sibling cap-99 guard's order.
local domain_ok = type(kcdx.behavior) == "table"
local verbs_ok = domain_ok
    and type(kcdx.behavior.declare) == "function"
    and type(kcdx.behavior.set) == "function"
    and type(kcdx.behavior.get) == "function"
    and type(kcdx.behavior.list) == "function"
if not verbs_ok then
    -- The domain (or a verb) did not register — every row is a real FAIL,
    -- never a silent skip.
    local detail = "behavior=" .. type(kcdx.behavior)
    if domain_ok then
        detail = detail
            .. ", declare=" .. type(kcdx.behavior.declare)
            .. ", set=" .. type(kcdx.behavior.set)
            .. ", get=" .. type(kcdx.behavior.get)
            .. ", list=" .. type(kcdx.behavior.list)
    end
    for _, row in ipairs(ROWS) do
        rec(row, false,
            "the kcdx.behavior domain did not register its "
            .. "declare/set/get/list verbs (" .. detail
            .. ") — the binder is missing; no behavior row can run")
    end
    for _, row in ipairs(LATE_ROWS) do
        rec(row, false,
            "the kcdx.behavior domain did not register its "
            .. "declare/set/get/list verbs (" .. detail
            .. ") — the binder is missing; no behavior row can run")
    end
else
    -- A shared no-op implementation for the fixtures (the apply boundary is a
    -- later step; nothing invokes it this step).
    local function impl(value) end

    -- =====================================================================
    -- Load-time declares + fixtures (everything pcall-guarded so a raise is
    -- an observation, never a plugin-load abort).
    -- =====================================================================

    -- Clean declares (the registry fixtures every later row reads).
    local ok_num, err_num = pcall(kcdx.behavior.declare, "speed_mult", {
        description    = "cap-100 numeric fixture (never set; get = default)",
        default        = 42,
        implementation = impl,
    })
    local ok_str, err_str = pcall(kcdx.behavior.declare, "greeting", {
        description    = "cap-100 string fixture (carries optional revert)",
        default        = "hello-from-default",
        implementation = impl,
        revert         = function(old_value) end,
    })

    -- Missing-field fixtures: each must RAISE with the field named.
    local bad = {
        { row_part = "description",
          name = "mf_no_desc",
          spec = { default = 1, implementation = impl } },
        { row_part = "default",
          name = "mf_nil_default",
          spec = { description = "x", implementation = impl } },
        { row_part = "implementation",
          name = "mf_no_impl",
          spec = { description = "x", default = 1 } },
    }
    for _, f in ipairs(bad) do
        local ok, err = pcall(kcdx.behavior.declare, f.name, f.spec)
        f.ok  = ok
        f.err = err
    end

    -- Duplicate fixture: the FIRST declare stands; the SECOND must raise.
    local ok_dup1, err_dup1 = pcall(kcdx.behavior.declare, "dup_target", {
        description    = "cap-100 duplicate fixture — the FIRST declaration",
        default        = "first-default",
        implementation = impl,
    })
    local ok_dup2, err_dup2 = pcall(kcdx.behavior.declare, "dup_target", {
        description    = "cap-100 duplicate fixture — the SECOND declaration",
        default        = "second-default",
        implementation = impl,
    })

    -- Set fixture: a counting implementation + a load-time set. The
    -- implementation must fire EXACTLY ONCE at the apply boundary with the
    -- final recorded value (asserted at input_loaded — post-boundary).
    -- (Assigned to the file-scope locals above, read at input_loaded.)
    ok_counted, err_counted = pcall(kcdx.behavior.declare, "counted", {
        description    = "cap-100 set fixture (counting implementation)",
        default        = 1,
        implementation = function(value)
            counted_fires = counted_fires + 1
            counted_value = value
        end,
    })
    ok_set, err_set = pcall(kcdx.behavior.set, "counted", 99)
    ok_get_after_set, get_after_set =
        pcall(kcdx.behavior.get, "counted")

    -- set(name, nil) fixture: nil is the unset sentinel — must RAISE, and
    -- the standing record must be untouched.
    local ok_setnil, err_setnil =
        pcall(kcdx.behavior.set, "speed_mult", nil)
    local ok_get_after_nil, get_after_nil =
        pcall(kcdx.behavior.get, "speed_mult")

    -- §6 discriminating set-resolution branches (single-plugin half).
    -- Each pcall-captures the raise; the rows assert the branch wording.
    --   b (typo): set the OWN full name with a non-existent bare — the
    --     declarer (this plugin) is loaded + declares others -> the
    --     "declares no behavior" error pointing at list("<owner>.").
    local ok_typo, err_typo =
        pcall(kcdx.behavior.set, PREFIX .. "no_such_behavior_xyz", 1)
    --   d (bare, no declarer): set a bare name no plugin declares -> the
    --     "no plugin loaded so far declares <bare>; use the full name"
    --     error (no <author>.<plugin> prefix to discriminate with).
    local ok_bare, err_bare =
        pcall(kcdx.behavior.set, "cap100_undeclared_bare_name", true)
    --   c (owner absent): set a 3-segment full name whose
    --     <author>.<plugin> is NOT an installed plugin -> the "belongs to
    --     <owner>, which is not installed" error.
    local ok_absent, err_absent =
        pcall(kcdx.behavior.set, "noauthor.noplugin.some_behavior", 1)

    -- =====================================================================
    -- cap-100-declare-list — the clean declares registered and list() shows
    -- the STAMPED full names with the right fields.
    -- FALSIFIABLE: a clean declare raised, the stamped name is absent from
    -- list(), or the entry's description/default/declarer is wrong -> FAIL.
    -- =====================================================================
    do
        local row = "cap-100-declare-list"
        if not ok_num then
            rec(row, false, "the clean declare of 'speed_mult' RAISED: "
                .. tostring(err_num) .. " — a valid spec must register")
        elseif not ok_str then
            rec(row, false, "the clean declare of 'greeting' RAISED: "
                .. tostring(err_str) .. " — a valid spec (with optional "
                .. "revert) must register")
        else
            local entries = kcdx.behavior.list()
            local found = nil
            for _, e in ipairs(entries) do
                if e.name == PREFIX .. "speed_mult" then found = e break end
            end
            if type(entries) ~= "table" then
                rec(row, false, "kcdx.behavior.list() returned "
                    .. type(entries) .. " (expected a table)")
            elseif not found then
                rec(row, false, "list() does not carry '" .. PREFIX
                    .. "speed_mult' — the declare did not register under the "
                    .. "engine-stamped <author>.<plugin>.<bare> full name "
                    .. "(the stamping or the registry walk is broken)")
            elseif found.default ~= 42 then
                rec(row, false, "the '" .. PREFIX .. "speed_mult' entry's "
                    .. ".default is " .. tostring(found.default)
                    .. " (expected 42) — the default ref round-trip is broken")
            elseif found.current ~= 42 then
                rec(row, false, "the '" .. PREFIX .. "speed_mult' entry's "
                    .. ".current is " .. tostring(found.current)
                    .. " (expected 42 — never set, so current must read the "
                    .. "default)")
            elseif found.declarer ~= "ts.cap_100_behavior" then
                rec(row, false, "the entry's .declarer is "
                    .. tostring(found.declarer)
                    .. " (expected ts.cap_100_behavior) — declarer "
                    .. "attribution is broken")
            elseif type(found.description) ~= "string"
                or found.description == "" then
                rec(row, false, "the entry's .description is "
                    .. tostring(found.description)
                    .. " — list() must surface the declare's one human line")
            else
                rec(row, true, "declare registered under the stamped name '"
                    .. PREFIX .. "speed_mult' and list() carries it with "
                    .. "description/default(42)/current(42)/declarer("
                    .. found.declarer .. ") — stamping + registry + list "
                    .. "round-trip verified")
            end
        end
    end

    -- =====================================================================
    -- cap-100-missing-field-errors — each bad spec RAISED with the missing
    -- field named; none of the rejected names registered.
    -- FALSIFIABLE: a bad declare succeeds, the error lacks the field name,
    -- or a rejected name appears in list() -> FAIL.
    -- =====================================================================
    do
        local row = "cap-100-missing-field-errors"
        local verdict = nil
        for _, f in ipairs(bad) do
            if f.ok then
                verdict = "declare('" .. f.name .. "') with no " .. f.row_part
                    .. " SUCCEEDED — a required-field gap must raise the "
                    .. "teaching error at the declare site, never register"
                break
            elseif type(f.err) ~= "string"
                or not string.find(f.err, f.row_part, 1, true) then
                verdict = "declare('" .. f.name .. "') raised but the error "
                    .. "does not name the missing field '" .. f.row_part
                    .. "' — errors must teach (got: " .. tostring(f.err) .. ")"
                break
            end
        end
        if not verdict then
            -- None of the rejected names may have registered.
            local entries = kcdx.behavior.list(PREFIX)
            for _, e in ipairs(entries) do
                for _, f in ipairs(bad) do
                    if e.name == PREFIX .. f.name then
                        verdict = "the REJECTED declare '" .. f.name
                            .. "' appears in list() — a rejected spec must "
                            .. "leave the registry untouched"
                        break
                    end
                end
                if verdict then break end
            end
        end
        if verdict then
            rec(row, false, verdict)
        else
            rec(row, true, "all three bad specs (no description / nil "
                .. "default / no implementation) RAISED a teaching error "
                .. "naming the missing field, and none of them registered — "
                .. "required-field validation is immediate and loud at the "
                .. "declare site")
        end
    end

    -- =====================================================================
    -- cap-100-duplicate-second-errors — the SECOND same-full-name declare
    -- raised ("already declared"); the FIRST stands and get answers ITS
    -- default.
    -- FALSIFIABLE: the second declare succeeds (silent clobber), the error
    -- lacks "already declared", get raises, or get answers "second-default"
    -- (the implementation swapped under the standing record) -> FAIL.
    -- =====================================================================
    do
        local row = "cap-100-duplicate-second-errors"
        if not ok_dup1 then
            rec(row, false, "the FIRST declare of 'dup_target' raised: "
                .. tostring(err_dup1) .. " — the duplicate fixture needs a "
                .. "clean first declaration")
        elseif ok_dup2 then
            rec(row, false, "the SECOND declare of 'dup_target' SUCCEEDED — "
                .. "a duplicate stamped full name must error against the "
                .. "second declare (the first stands; a silent re-declare "
                .. "swaps the implementation under the surface)")
        elseif type(err_dup2) ~= "string"
            or not string.find(err_dup2, "already declared", 1, true) then
            rec(row, false, "the second declare raised but the error does "
                .. "not say 'already declared' — the duplicate error must "
                .. "teach the rule (got: " .. tostring(err_dup2) .. ")")
        else
            local okGet, v = pcall(kcdx.behavior.get, "dup_target")
            if not okGet then
                rec(row, false, "after the rejected duplicate, "
                    .. "get('dup_target') RAISED (" .. tostring(v)
                    .. ") — the FIRST declaration must still resolve")
            elseif v ~= "first-default" then
                rec(row, false, "get('dup_target') returned " .. tostring(v)
                    .. " (expected 'first-default') — the rejected SECOND "
                    .. "declare leaked its spec over the standing first one")
            else
                rec(row, true, "the second 'dup_target' declare errored "
                    .. "('already declared' taught) and the FIRST "
                    .. "declaration still resolves with its own default — "
                    .. "first stands, second rejected, load continued")
            end
        end
    end

    -- =====================================================================
    -- cap-100-get-default — a never-set behavior's get answers the spec's
    -- default, via the bare self-resolved name AND the explicit full form;
    -- an undeclared name raises a teaching error (never a silent nil).
    -- FALSIFIABLE: a wrong/nil value on either form, or a non-raise on the
    -- unknown name -> FAIL.
    -- =====================================================================
    do
        local row = "cap-100-get-default"
        local okBare, vBare = pcall(kcdx.behavior.get, "speed_mult")
        local okFull, vFull = pcall(kcdx.behavior.get, PREFIX .. "greeting")
        local okUnknown, errUnknown =
            pcall(kcdx.behavior.get, "cap100_no_such_behavior")
        if not okBare then
            rec(row, false, "get('speed_mult') (bare, self-resolved) RAISED: "
                .. tostring(vBare))
        elseif vBare ~= 42 then
            rec(row, false, "get('speed_mult') returned " .. tostring(vBare)
                .. " (expected the spec default 42 — never set, so get must "
                .. "answer the default)")
        elseif not okFull then
            rec(row, false, "get('" .. PREFIX .. "greeting') (explicit full "
                .. "form) RAISED: " .. tostring(vFull))
        elseif vFull ~= "hello-from-default" then
            rec(row, false, "get('" .. PREFIX .. "greeting') returned "
                .. tostring(vFull) .. " (expected 'hello-from-default')")
        elseif okUnknown then
            rec(row, false, "get('cap100_no_such_behavior') did NOT raise "
                .. "(returned " .. tostring(errUnknown) .. ") — an "
                .. "unresolvable name must get a teaching error, never a "
                .. "silent nil/value")
        else
            rec(row, true, "get answers the spec default on a never-set "
                .. "behavior via both the bare name (42) and the full "
                .. "<author>.<plugin>.<bare> form ('hello-from-default'), "
                .. "and an undeclared name raises a teaching error")
        end
    end

    -- =====================================================================
    -- cap-100-list-prefix-filter — list(PREFIX) returns EXACTLY this
    -- plugin's own successfully-declared entries: speed_mult, greeting,
    -- dup_target (3 — the rejected fixtures never registered), every name
    -- carrying the prefix.
    -- FALSIFIABLE: a foreign entry passes the filter, an own entry is
    -- missing, or the count is wrong -> FAIL.
    -- =====================================================================
    do
        local row = "cap-100-list-prefix-filter"
        local entries = kcdx.behavior.list(PREFIX)
        local expected = {
            [PREFIX .. "speed_mult"] = false,
            [PREFIX .. "greeting"]   = false,
            [PREFIX .. "dup_target"] = false,
            [PREFIX .. "counted"]    = false,
        }
        local verdict = nil
        if type(entries) ~= "table" then
            verdict = "list('" .. PREFIX .. "') returned " .. type(entries)
                .. " (expected a table)"
        else
            for _, e in ipairs(entries) do
                if string.sub(e.name, 1, #PREFIX) ~= PREFIX then
                    verdict = "list('" .. PREFIX .. "') returned the "
                        .. "non-matching entry '" .. tostring(e.name)
                        .. "' — the prefix filter leaked a foreign name"
                    break
                end
                if expected[e.name] ~= nil then expected[e.name] = true end
            end
            if not verdict then
                for name, seen in pairs(expected) do
                    if not seen then
                        verdict = "list('" .. PREFIX .. "') is missing the "
                            .. "own entry '" .. name .. "'"
                        break
                    end
                end
            end
            if not verdict and #entries ~= 4 then
                verdict = "list('" .. PREFIX .. "') returned " .. #entries
                    .. " entries (expected exactly 4: speed_mult / greeting "
                    .. "/ dup_target / counted — the rejected fixtures must "
                    .. "not register, and no foreign entry may match)"
            end
        end
        if verdict then
            rec(row, false, verdict)
        else
            rec(row, true, "list('" .. PREFIX .. "') returned exactly this "
                .. "plugin's 4 registered behaviors (speed_mult / greeting / "
                .. "dup_target / counted), every name prefix-matched — the "
                .. "filter scopes to the stamped namespace and rejected "
                .. "declares left no entry")
        end
    end

    -- =====================================================================
    -- cap-100-alias-get — a kcdx.alias local handle to a declared behavior's
    -- full name resolves through kcdx.behavior.get to the same value as the
    -- direct full-name get (alias substitution runs before the precedence
    -- walk).
    -- FALSIFIABLE: the alias declaration fails, get via the alias raises,
    -- or it returns a different value than the direct get -> FAIL.
    -- =====================================================================
    do
        local row = "cap-100-alias-get"
        local okCall, aliasOk, aliasErr =
            pcall(kcdx.alias, "cap100_sm", PREFIX .. "speed_mult")
        if not okCall then
            rec(row, false, "kcdx.alias('cap100_sm', '" .. PREFIX
                .. "speed_mult') RAISED: " .. tostring(aliasOk)
                .. " — the alias declaration must register the local handle")
        elseif aliasOk ~= true then
            rec(row, false, "kcdx.alias('cap100_sm', '" .. PREFIX
                .. "speed_mult') was rejected: " .. tostring(aliasErr)
                .. " — the alias declaration must register the local handle")
        else
            local okAliasGet, vAlias = pcall(kcdx.behavior.get, "cap100_sm")
            local okDirect, vDirect =
                pcall(kcdx.behavior.get, PREFIX .. "speed_mult")
            if not okAliasGet then
                rec(row, false, "get('cap100_sm') (the aliased handle) "
                    .. "RAISED: " .. tostring(vAlias) .. " — the alias must "
                    .. "substitute its full target before the name resolves")
            elseif not okDirect then
                rec(row, false, "get('" .. PREFIX .. "speed_mult') (the "
                    .. "direct full name) RAISED: " .. tostring(vDirect))
            elseif vAlias ~= vDirect or vAlias ~= 42 then
                rec(row, false, "get('cap100_sm') returned "
                    .. tostring(vAlias) .. " but the direct get('" .. PREFIX
                    .. "speed_mult') returned " .. tostring(vDirect)
                    .. " (spec default 42) — the alias path resolved to a "
                    .. "different value")
            else
                rec(row, true, "kcdx.alias handle 'cap100_sm' -> '" .. PREFIX
                    .. "speed_mult' resolves through kcdx.behavior.get to "
                    .. "the same value as the direct full name (42) — alias "
                    .. "substitution feeds the behavior name resolution")
            end
        end
    end

    -- =====================================================================
    -- cap-100-set-nil-error — set(name, nil) RAISES the nil-sentinel
    -- teaching error and the standing record is untouched (get still 42).
    -- FALSIFIABLE: the nil set succeeds (nil stored as a value / treated as
    -- an unset call), the error does not teach the unset-sentinel rule, or
    -- get no longer answers 42 -> FAIL.
    -- =====================================================================
    do
        local row = "cap-100-set-nil-error"
        if ok_setnil then
            rec(row, false, "set('speed_mult', nil) SUCCEEDED — nil is the "
                .. "engine's unset sentinel, never a value; the nil set "
                .. "must raise the teaching error")
        elseif type(err_setnil) ~= "string"
            or not string.find(err_setnil, "unset", 1, true) then
            rec(row, false, "set('speed_mult', nil) raised but the error "
                .. "does not teach the unset-sentinel rule (got: "
                .. tostring(err_setnil) .. ")")
        elseif not ok_get_after_nil or get_after_nil ~= 42 then
            rec(row, false, "after the rejected nil set, get('speed_mult') "
                .. "reads " .. tostring(get_after_nil)
                .. " (expected the untouched default 42) — the rejected "
                .. "set mutated the record")
        else
            rec(row, true, "set('speed_mult', nil) raised the nil-sentinel "
                .. "teaching error ('to leave a behavior unset, don't set "
                .. "it') and the record is untouched (get still answers 42)")
        end
    end

    -- =====================================================================
    -- cap-100-set-typo-error — §6 branch b (loaded, no such bare): a set on
    -- the OWN full name with a non-existent bare RAISES the "declares no
    -- behavior" error pointing at list("<owner>."). This plugin IS the
    -- declarer, loaded, with other behaviors registered — so it is the
    -- loaded-no-such-name case, not absent/disabled/reorder.
    -- FALSIFIABLE: the typo set succeeds, the error lacks "declares no
    -- behavior", or it lacks the list() pointer -> FAIL.
    -- =====================================================================
    do
        local row = "cap-100-set-typo-error"
        if ok_typo then
            rec(row, false, "set('" .. PREFIX .. "no_such_behavior_xyz') "
                .. "SUCCEEDED — a prefixed name whose loaded declarer has no "
                .. "such bare behavior must RAISE the discriminating error")
        elseif type(err_typo) ~= "string"
            or not string.find(err_typo, "declares no behavior", 1, true) then
            rec(row, false, "the typo set raised but the error does not say "
                .. "'declares no behavior' (the loaded-no-such-name branch); "
                .. "got: " .. tostring(err_typo))
        elseif not string.find(err_typo, "kcdx.behavior.list", 1, true) then
            rec(row, false, "the typo error does not point at "
                .. "kcdx.behavior.list(\"<owner>.\") — discovery must stay "
                .. "name-based (got: " .. tostring(err_typo) .. ")")
        else
            rec(row, true, "a set on the own full name with a non-existent "
                .. "bare raised the loaded-no-such-name teaching error "
                .. "('declares no behavior') pointing at "
                .. "kcdx.behavior.list(\"<owner>.\") — the typo branch, "
                .. "name-based discovery, no hex burden")
        end
    end

    -- =====================================================================
    -- cap-100-set-bare-no-declarer — §6 branch d (bare, no declarer): a set
    -- on a bare name no plugin declares RAISES the "no plugin loaded so far
    -- declares <bare>" error pointing at the full <author>.<plugin>.<bare>
    -- form (a bare name carries no prefix to discriminate with).
    -- FALSIFIABLE: the bare set succeeds, the error lacks the no-declarer
    -- wording, or it lacks the full-name pointer -> FAIL.
    -- =====================================================================
    do
        local row = "cap-100-set-bare-no-declarer"
        if ok_bare then
            rec(row, false, "set('cap100_undeclared_bare_name') SUCCEEDED — "
                .. "a bare name no plugin declares must RAISE the no-declarer "
                .. "error")
        elseif type(err_bare) ~= "string"
            or not string.find(err_bare, "no plugin loaded", 1, true) then
            rec(row, false, "the bare-name set raised but the error does not "
                .. "say 'no plugin loaded so far declares' (the bare-name "
                .. "branch); got: " .. tostring(err_bare))
        elseif not string.find(err_bare,
            "<author>.<plugin>.<bare>", 1, true) then
            rec(row, false, "the bare-name error does not point at the full "
                .. "<author>.<plugin>.<bare> form (got: "
                .. tostring(err_bare) .. ")")
        else
            rec(row, true, "a set on an undeclared bare name raised the "
                .. "no-declarer teaching error ('no plugin loaded so far "
                .. "declares ...') pointing at the full "
                .. "<author>.<plugin>.<bare> form — the bare-name branch")
        end
    end

    -- =====================================================================
    -- cap-100-set-owner-absent — §6 branch c (owner absent): a set on a
    -- 3-segment full name whose <author>.<plugin> is NOT an installed plugin
    -- RAISES the "belongs to <owner>, which is not installed" error (no
    -- reorder suggestion — none fixes an uninstalled declarer).
    -- FALSIFIABLE: the absent set succeeds, the error lacks "not installed",
    -- or it suggests a reorder (wrong fix for an absent owner) -> FAIL.
    -- =====================================================================
    do
        local row = "cap-100-set-owner-absent"
        if ok_absent then
            rec(row, false, "set('noauthor.noplugin.some_behavior') "
                .. "SUCCEEDED — a prefixed name whose owner is not installed "
                .. "must RAISE the absent-owner error")
        elseif type(err_absent) ~= "string"
            or not string.find(err_absent, "not installed", 1, true) then
            rec(row, false, "the absent-owner set raised but the error does "
                .. "not say 'not installed' (got: " .. tostring(err_absent)
                .. ")")
        elseif string.find(err_absent, "move", 1, true)
            or string.find(err_absent, "below it", 1, true) then
            rec(row, false, "the absent-owner error suggests a REORDER "
                .. "('move'/'below it') — no reorder fixes an uninstalled "
                .. "declarer; the fix is to install it (got: "
                .. tostring(err_absent) .. ")")
        else
            rec(row, true, "a set on a full name whose <author>.<plugin> is "
                .. "not installed raised the absent-owner teaching error "
                .. "('belongs to <owner>, which is not installed') with NO "
                .. "reorder suggestion — branch c, the right fix named")
        end
    end
end

-- Report the load-observable rows at ready; the observations above were
-- captured at load. (When the binder is missing, the LATE_ROWS were folded
-- into `results` and report here too.)
kcdx.on("ready", function()
    for _, r in ipairs(results) do
        kcdx.test.report(r.row, r.pass, r.reason)
    end
    kcdx.log.info("CAP100",
        "kcdx.behavior declare/set/get/list self-test reported "
        .. #results .. " load-observable rows (declare+stamp+list "
        .. "round-trip, missing-field teaching errors, "
        .. "duplicate-second-errors/first-stands, get-answers-default, "
        .. "list prefix filter, alias-handle get, set-nil teaching error)")
end)

-- Boundary-dependent rows report at input_loaded — the apply boundary runs
-- post-RunPostGameLoad / pre-InputLoaded, so this is the earliest
-- post-boundary observation point ("ready" fires BEFORE the boundary).
kcdx.on("input_loaded", function()
    if not verbs_ok then return end  -- already reported as FAILs at ready

    -- =====================================================================
    -- cap-100-set-records-and-applies — the single-plugin set round-trip:
    -- the load-time set RECORDED immediately (get answered 99 right after
    -- the set, pre-boundary), and the boundary invoked the implementation
    -- EXACTLY ONCE with that final value (counter==1, captured==99), and
    -- get still answers 99 post-boundary.
    -- FALSIFIABLE: the set raised, get did not answer the recorded value
    -- immediately, the implementation fired 0 or 2+ times, it received a
    -- different value, or the post-boundary get drifted -> FAIL.
    -- =====================================================================
    do
        local row = "cap-100-set-records-and-applies"
        local okNow, vNow = pcall(kcdx.behavior.get, "counted")
        if not ok_counted then
            rec_late(row, false, "the 'counted' declare RAISED: "
                .. tostring(err_counted))
        elseif not ok_set then
            rec_late(row, false, "set('counted', 99) at load RAISED: "
                .. tostring(err_set) .. " — a load-window set on an own "
                .. "behavior must record")
        elseif not ok_get_after_set or get_after_set ~= 99 then
            rec_late(row, false, "get('counted') immediately after the "
                .. "load-time set read " .. tostring(get_after_set)
                .. " (expected 99) — the set did not record visibly")
        elseif counted_fires ~= 1 then
            rec_late(row, false, "the 'counted' implementation fired "
                .. counted_fires .. " times (expected EXACTLY ONCE at the "
                .. "apply boundary)")
        elseif counted_value ~= 99 then
            rec_late(row, false, "the 'counted' implementation received "
                .. tostring(counted_value)
                .. " (expected the final recorded value 99)")
        elseif not okNow or vNow ~= 99 then
            rec_late(row, false, "post-boundary get('counted') reads "
                .. tostring(vNow) .. " (expected 99 — the applied record)")
        else
            rec_late(row, true, "set recorded at load (get answered 99 "
                .. "immediately), the apply boundary invoked the "
                .. "implementation exactly once with the final value 99, "
                .. "and get still answers 99 post-boundary")
        end
    end

    -- =====================================================================
    -- cap-100-post-load-set-error — a set AFTER the boundary (from this
    -- input_loaded handler) RAISES the post-load placeholder teaching error
    -- (runtime toggling arrives with the revert contract) and the applied
    -- record is untouched.
    -- FALSIFIABLE: the post-load set succeeds (a recorded-but-never-applied
    -- value — get would lie), the error does not name the apply-boundary
    -- rule, or the record changed -> FAIL.
    -- =====================================================================
    do
        local row = "cap-100-post-load-set-error"
        local okSet, errSet = pcall(kcdx.behavior.set, "counted", 123)
        local okGet, v = pcall(kcdx.behavior.get, "counted")
        if okSet then
            rec_late(row, false, "a post-load set('counted', 123) "
                .. "SUCCEEDED — after the apply boundary a set must raise "
                .. "the teaching error (runtime toggling is not built yet); "
                .. "a recorded-but-never-applied value would make get() lie")
        elseif type(errSet) ~= "string"
            or not string.find(errSet, "apply boundary", 1, true) then
            rec_late(row, false, "the post-load set raised but the error "
                .. "does not name the apply-boundary rule (got: "
                .. tostring(errSet) .. ")")
        elseif not okGet or v ~= 99 then
            rec_late(row, false, "after the rejected post-load set, "
                .. "get('counted') reads " .. tostring(v)
                .. " (expected the applied 99) — the rejected set mutated "
                .. "the record")
        else
            rec_late(row, true, "the post-load set raised the placeholder "
                .. "teaching error (names the apply-boundary rule + that "
                .. "runtime toggling arrives with the revert contract) and "
                .. "the applied record is untouched (get still 99)")
        end
    end

    for _, r in ipairs(late_results) do
        kcdx.test.report(r.row, r.pass, r.reason)
    end
    kcdx.log.info("CAP100",
        "kcdx.behavior set/boundary self-test reported "
        .. #late_results .. " post-boundary rows (set-records-and-applies "
        .. "once-with-final-value, post-load set teaching error)")
end)
