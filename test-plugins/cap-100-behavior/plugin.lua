-- CAP-100 plugin.lua — kcdx.behavior.* declare/get/list (the named-behavior
-- registry's read side).
--
-- Declares are a LOAD-TIME act, so every declare (clean + the deliberately-bad
-- fixtures, all pcall-guarded) runs HERE at the top level of plugin.lua — the
-- canonical load window. The assertions run immediately after (declare is
-- synchronous); the rows REPORT at ready, the suite's usual reporting point.
--
-- `set` does not exist yet (a later step), so every behavior here is
-- never-set: get must answer the spec's DEFAULT, and a list entry's `current`
-- must equal its `default`. The recorded-value machinery is asserted exactly
-- through that contract.
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

local ROWS = {
    "cap-100-declare-list",
    "cap-100-missing-field-errors",
    "cap-100-duplicate-second-errors",
    "cap-100-get-default",
    "cap-100-list-prefix-filter",
    "cap-100-alias-get",
}

-- The domain must be a TABLE before any member is read — a non-table
-- kcdx.behavior would make the member index itself the error. The type check
-- strictly precedes every member access (condition AND failure message),
-- mirroring the sibling cap-99 guard's order.
local domain_ok = type(kcdx.behavior) == "table"
local verbs_ok = domain_ok
    and type(kcdx.behavior.declare) == "function"
    and type(kcdx.behavior.get) == "function"
    and type(kcdx.behavior.list) == "function"
if not verbs_ok then
    -- The domain (or a verb) did not register — every row is a real FAIL,
    -- never a silent skip.
    local detail = "behavior=" .. type(kcdx.behavior)
    if domain_ok then
        detail = detail
            .. ", declare=" .. type(kcdx.behavior.declare)
            .. ", get=" .. type(kcdx.behavior.get)
            .. ", list=" .. type(kcdx.behavior.list)
    end
    for _, row in ipairs(ROWS) do
        rec(row, false,
            "the kcdx.behavior domain did not register its declare/get/list "
            .. "verbs (" .. detail
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
            if not verdict and #entries ~= 3 then
                verdict = "list('" .. PREFIX .. "') returned " .. #entries
                    .. " entries (expected exactly 3: speed_mult / greeting "
                    .. "/ dup_target — the rejected fixtures must not "
                    .. "register, and no foreign entry may match)"
            end
        end
        if verdict then
            rec(row, false, verdict)
        else
            rec(row, true, "list('" .. PREFIX .. "') returned exactly this "
                .. "plugin's 3 registered behaviors (speed_mult / greeting / "
                .. "dup_target), every name prefix-matched — the filter "
                .. "scopes to the stamped namespace and rejected declares "
                .. "left no entry")
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
end

-- Report at ready (the suite's reporting point); the observations above were
-- captured at load.
kcdx.on("ready", function()
    for _, r in ipairs(results) do
        kcdx.test.report(r.row, r.pass, r.reason)
    end
    kcdx.log.info("CAP100",
        "kcdx.behavior declare/get/list self-test reported "
        .. #results .. " rows (declare+stamp+list round-trip, missing-field "
        .. "teaching errors, duplicate-second-errors/first-stands, "
        .. "get-answers-default, list prefix filter, alias-handle get)")
end)
