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
    "cap-100-toggle-revert-order",
    "cap-100-toggle-never-applied",
    "cap-100-toggle-impl-raises",
    "cap-100-toggle-revert-raises",
    "cap-100-post-load-declare-error",
}

-- State the boundary-dependent rows read at input_loaded. Declared at file
-- scope (NOT inside the verbs_ok branch) so the input_loaded closure below
-- captures these as upvalues, not globals.
local counted_fires = 0
local counted_value = nil
local ok_counted, err_counted
local ok_set, err_set
local ok_get_after_set, get_after_set

-- Post-load TOGGLE fixtures (design §5.4, main-thread inline). All declared at
-- load (the load-time act); the togglers WITH a load-time set are applied at
-- the boundary, the never-applied toggler is not. The post-load sets +
-- assertions run in the input_loaded handler (post-boundary). A shared call
-- log proves the revert→implementation ORDER (each fn appends a tag).
local toggle_log = {}            -- ordered tags: "revert:<old>", "impl:<new>"
local ok_tog, err_tog            -- declare of the order-tracking toggler
local ok_tog_set, err_tog_set    -- the load-time set that applies it
local ok_never, err_never        -- declare of the never-applied toggler
local never_revert_fired = false -- set true iff the never-applied revert ran
local never_impl_value = nil     -- the value the never-applied impl received
local ok_ir, err_ir              -- declare: revert ok, impl raises
local ok_ir_set, err_ir_set      -- the load-time set that applies it
local ok_rr, err_rr              -- declare: revert itself raises
local ok_rr_set, err_rr_set      -- the load-time set that applies it

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

    -- ---------------------------------------------------------------------
    -- Post-load TOGGLE fixtures — declared at load (the load-time act).
    -- ---------------------------------------------------------------------

    -- US-5 order toggler: a `revert` declarer, SET at load (so the boundary
    -- applies it). The post-load toggle (in input_loaded) must call
    -- revert(old) THEN implementation(new) IN THAT ORDER, then record. Each fn
    -- appends a tag to toggle_log so the order is observed, not inferred.
    ok_tog, err_tog = pcall(kcdx.behavior.declare, "toggler", {
        description    = "cap-100 order toggler (revert+impl call-order)",
        default        = "tog-default",
        implementation = function(value)
            toggle_log[#toggle_log + 1] = "impl:" .. tostring(value)
        end,
        revert = function(old_value)
            toggle_log[#toggle_log + 1] = "revert:" .. tostring(old_value)
        end,
    })
    -- The load-time set: applies "tog-loaded" at the boundary. (The boundary
    -- invokes implementation only — never revert — at load; toggle_log carries
    -- one "impl:tog-loaded" tag after the boundary, BEFORE the post-load set.)
    ok_tog_set, err_tog_set = pcall(kcdx.behavior.set, "toggler", "tog-loaded")

    -- Never-applied toggler: a `revert` declarer NEVER set at load, so the
    -- boundary SKIPS it (applied == false). A post-load set must call
    -- implementation ONLY (revert skipped — never handed an uncreated state).
    ok_never, err_never = pcall(kcdx.behavior.declare, "never_applied", {
        description    = "cap-100 never-applied toggler (revert skipped)",
        default        = "never-default",
        implementation = function(value) never_impl_value = value end,
        revert         = function(old_value) never_revert_fired = true end,
    })

    -- impl-raises toggler: revert SUCCEEDS, implementation RAISES on the
    -- post-load toggle. Disposition: record + applied cleared to unset (get()
    -- reads the default), error attributed to the declarer. SET at load (so
    -- applied; revert runs at toggle time and succeeds).
    ok_ir, err_ir = pcall(kcdx.behavior.declare, "impl_raiser", {
        description    = "cap-100 toggle: revert ok, impl raises",
        default        = "ir-default",
        implementation = function(value)
            -- Raise ONLY on the post-load toggle value, not the load-time
            -- apply (so the boundary applies cleanly and the behavior IS
            -- applied when the toggle runs).
            if value == "ir-toggle" then error("impl_raiser: deliberate") end
        end,
        revert = function(old_value) end,  -- succeeds
    })
    ok_ir_set, err_ir_set = pcall(kcdx.behavior.set, "impl_raiser", "ir-loaded")

    -- revert-raises toggler: revert ITSELF raises on the post-load toggle.
    -- Disposition: record + applied stay AS THEY WERE (get() still reads the
    -- loaded value), error attributed to the declarer. SET at load (applied).
    ok_rr, err_rr = pcall(kcdx.behavior.declare, "revert_raiser", {
        description    = "cap-100 toggle: revert itself raises",
        default        = "rr-default",
        implementation = function(value) end,
        revert         = function(old_value)
            error("revert_raiser: deliberate")
        end,
    })
    ok_rr_set, err_rr_set = pcall(kcdx.behavior.set, "revert_raiser", "rr-loaded")

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
            [PREFIX .. "speed_mult"]    = false,
            [PREFIX .. "greeting"]      = false,
            [PREFIX .. "dup_target"]    = false,
            [PREFIX .. "counted"]       = false,
            [PREFIX .. "toggler"]       = false,
            [PREFIX .. "never_applied"] = false,
            [PREFIX .. "impl_raiser"]   = false,
            [PREFIX .. "revert_raiser"] = false,
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
            if not verdict and #entries ~= 8 then
                verdict = "list('" .. PREFIX .. "') returned " .. #entries
                    .. " entries (expected exactly 8: speed_mult / greeting "
                    .. "/ dup_target / counted / toggler / never_applied / "
                    .. "impl_raiser / revert_raiser — the rejected fixtures "
                    .. "must not register, and no foreign entry may match)"
            end
        end
        if verdict then
            rec(row, false, verdict)
        else
            rec(row, true, "list('" .. PREFIX .. "') returned exactly this "
                .. "plugin's 8 registered behaviors (speed_mult / greeting / "
                .. "dup_target / counted / toggler / never_applied / "
                .. "impl_raiser / revert_raiser), every name prefix-matched — "
                .. "the filter scopes to the stamped namespace and rejected "
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
    -- cap-100-post-load-set-error — a post-load set on a REVERT-LESS behavior
    -- ('counted' ships no revert) RAISES the revert-less teaching error
    -- ("applies at load; it cannot change mid-session") and the applied
    -- record is untouched (get still 99). This is the §5.4 "without revert"
    -- disposition — distinct from a `revert` declarer's successful toggle
    -- (the rows below). The error is the SETTING plugin's (consumer-misuse),
    -- raised at the set site.
    -- FALSIFIABLE: the post-load set on a revert-less behavior succeeds (a
    -- recorded-but-never-applied value — get would lie), the error does not
    -- teach the applies-at-load / revert rule, or the record changed -> FAIL.
    -- =====================================================================
    do
        local row = "cap-100-post-load-set-error"
        local okSet, errSet = pcall(kcdx.behavior.set, "counted", 123)
        local okGet, v = pcall(kcdx.behavior.get, "counted")
        if okSet then
            rec_late(row, false, "a post-load set('counted', 123) "
                .. "SUCCEEDED — 'counted' ships no `revert`, so a post-load "
                .. "set must RAISE the revert-less teaching error; a "
                .. "recorded-but-never-applied value would make get() lie")
        elseif type(errSet) ~= "string"
            or not string.find(errSet, "applies at load", 1, true)
            or not string.find(errSet, "revert", 1, true) then
            rec_late(row, false, "the post-load set raised but the error "
                .. "does not teach the applies-at-load / `revert` rule "
                .. "(expected 'applies at load ... cannot change mid-session' "
                .. "naming the `revert` fix; got: " .. tostring(errSet) .. ")")
        elseif not okGet or v ~= 99 then
            rec_late(row, false, "after the rejected post-load set, "
                .. "get('counted') reads " .. tostring(v)
                .. " (expected the applied 99) — the rejected set mutated "
                .. "the record")
        else
            rec_late(row, true, "a post-load set on the revert-less "
                .. "'counted' raised the teaching error ('applies at load; "
                .. "it cannot change mid-session' naming the `revert` fix) "
                .. "and the applied record is untouched (get still 99) — the "
                .. "§5.4 without-revert disposition")
        end
    end

    -- =====================================================================
    -- cap-100-toggle-revert-order — US-5: a post-load set on an APPLIED
    -- `revert` declarer calls revert(old) THEN implementation(new) IN THAT
    -- ORDER, then records the new value (get tracks). The order is OBSERVED
    -- via toggle_log: the load-time set applied "tog-loaded" at the boundary
    -- (one "impl:tog-loaded" tag already present), then the post-load toggle
    -- appends "revert:tog-loaded" then "impl:tog-toggled".
    -- FALSIFIABLE: the declare/load-set raised, the toggle raised, the tags
    -- are out of order or missing, revert did not receive the OLD value, impl
    -- did not receive the NEW value, or get does not track the new value -> FAIL.
    -- =====================================================================
    do
        local row = "cap-100-toggle-revert-order"
        if not ok_tog then
            rec_late(row, false, "the 'toggler' declare RAISED: "
                .. tostring(err_tog))
        elseif not ok_tog_set then
            rec_late(row, false, "the load-time set('toggler', 'tog-loaded') "
                .. "RAISED: " .. tostring(err_tog_set)
                .. " — the boundary must apply it so the toggle has an OLD "
                .. "value to revert")
        else
            -- Pre-toggle: the boundary applied the load-time value, so
            -- toggle_log holds exactly { "impl:tog-loaded" } and get == that.
            local okPre, vPre = pcall(kcdx.behavior.get, "toggler")
            local okTog, errTog =
                pcall(kcdx.behavior.set, "toggler", "tog-toggled")
            local okPost, vPost = pcall(kcdx.behavior.get, "toggler")
            if #toggle_log < 1 or toggle_log[1] ~= "impl:tog-loaded" then
                rec_late(row, false, "before the toggle, toggle_log[1] is "
                    .. tostring(toggle_log[1]) .. " (expected "
                    .. "'impl:tog-loaded' — the boundary must have applied the "
                    .. "load-time set exactly once, impl only, no revert)")
            elseif not okPre or vPre ~= "tog-loaded" then
                rec_late(row, false, "pre-toggle get('toggler') reads "
                    .. tostring(vPre) .. " (expected the applied 'tog-loaded')")
            elseif not okTog then
                rec_late(row, false, "the post-load set('toggler', "
                    .. "'tog-toggled') on an APPLIED revert declarer RAISED: "
                    .. tostring(errTog) .. " — a `revert` declarer must toggle "
                    .. "post-load, not error")
            elseif #toggle_log ~= 3 then
                rec_late(row, false, "after the toggle, toggle_log has "
                    .. #toggle_log .. " tags { "
                    .. table.concat(toggle_log, ", ")
                    .. " } (expected exactly 3: impl:tog-loaded, "
                    .. "revert:tog-loaded, impl:tog-toggled)")
            elseif toggle_log[2] ~= "revert:tog-loaded" then
                rec_late(row, false, "toggle_log[2] is "
                    .. tostring(toggle_log[2]) .. " (expected "
                    .. "'revert:tog-loaded' — revert must run FIRST and "
                    .. "receive the OLD value 'tog-loaded')")
            elseif toggle_log[3] ~= "impl:tog-toggled" then
                rec_late(row, false, "toggle_log[3] is "
                    .. tostring(toggle_log[3]) .. " (expected "
                    .. "'impl:tog-toggled' — implementation must run AFTER "
                    .. "revert and receive the NEW value 'tog-toggled')")
            elseif not okPost or vPost ~= "tog-toggled" then
                rec_late(row, false, "post-toggle get('toggler') reads "
                    .. tostring(vPost) .. " (expected the new 'tog-toggled' — "
                    .. "the toggle must record the new value)")
            else
                rec_late(row, true, "a post-load set on an applied `revert` "
                    .. "declarer called revert('tog-loaded') THEN "
                    .. "implementation('tog-toggled') in that order (observed "
                    .. "via the shared call log) and get now tracks "
                    .. "'tog-toggled' — US-5 toggle, revert-before-impl order, "
                    .. "old/new values correct")
            end
        end
    end

    -- =====================================================================
    -- cap-100-toggle-never-applied — the never-applied gate (§5.4): a post-load
    -- set on a `revert` declarer that was NEVER set at load (boundary skipped
    -- it, applied == false) calls implementation(new) ONLY — revert is SKIPPED
    -- (never handed a state the implementation did not create) — then records.
    -- FALSIFIABLE: the toggle raised, revert FIRED (it must be skipped), impl
    -- did not receive the new value, or get does not track the new value -> FAIL.
    -- =====================================================================
    do
        local row = "cap-100-toggle-never-applied"
        if not ok_never then
            rec_late(row, false, "the 'never_applied' declare RAISED: "
                .. tostring(err_never))
        else
            -- Pre: never set at load, so get answers the default and applied
            -- is false (the boundary skipped it).
            local okPre, vPre = pcall(kcdx.behavior.get, "never_applied")
            local okTog, errTog =
                pcall(kcdx.behavior.set, "never_applied", "na-toggled")
            local okPost, vPost = pcall(kcdx.behavior.get, "never_applied")
            if not okPre or vPre ~= "never-default" then
                rec_late(row, false, "pre-toggle get('never_applied') reads "
                    .. tostring(vPre) .. " (expected the spec default "
                    .. "'never-default' — it was never set, so the boundary "
                    .. "skipped it)")
            elseif not okTog then
                rec_late(row, false, "the post-load set('never_applied', "
                    .. "'na-toggled') RAISED: " .. tostring(errTog)
                    .. " — a never-applied `revert` declarer must apply "
                    .. "implementation only (revert skipped), not error")
            elseif never_revert_fired then
                rec_late(row, false, "the never-applied toggle FIRED revert — "
                    .. "revert must be SKIPPED on a behavior the "
                    .. "implementation never applied (it would be handed a "
                    .. "state the implementation did not create)")
            elseif never_impl_value ~= "na-toggled" then
                rec_late(row, false, "the never-applied implementation "
                    .. "received " .. tostring(never_impl_value)
                    .. " (expected the new value 'na-toggled')")
            elseif not okPost or vPost ~= "na-toggled" then
                rec_late(row, false, "post-toggle get('never_applied') reads "
                    .. tostring(vPost) .. " (expected the new 'na-toggled' — "
                    .. "the toggle must record the new value)")
            else
                rec_late(row, true, "a post-load set on a NEVER-applied "
                    .. "`revert` declarer called implementation('na-toggled') "
                    .. "ONLY — revert was skipped (never handed an uncreated "
                    .. "state) — and get now tracks 'na-toggled': the §5.4 "
                    .. "never-applied gate")
            end
        end
    end

    -- =====================================================================
    -- cap-100-toggle-impl-raises — §5.4 failure disposition: revert(old)
    -- SUCCEEDS but implementation(new) RAISES on a post-load toggle. The
    -- engine clears the recorded value AND the applied flag to unset (the
    -- world is in the reverted state, get returns the default), logs the error
    -- attributed to the DECLARER, and the SET does NOT re-raise at the consumer
    -- call site (a declarer-code failure). 'impl_raiser' was set at load with
    -- 'ir-loaded' (applied cleanly); the post-load toggle to 'ir-toggle' makes
    -- the implementation raise.
    -- FALSIFIABLE: the toggle set RAISED at the call site (declarer-code
    -- failures don't re-raise on the consumer), OR get does not return the
    -- default after the cleared record (a lying surface) -> FAIL.
    -- =====================================================================
    do
        local row = "cap-100-toggle-impl-raises"
        if not ok_ir then
            rec_late(row, false, "the 'impl_raiser' declare RAISED: "
                .. tostring(err_ir))
        elseif not ok_ir_set then
            rec_late(row, false, "the load-time set('impl_raiser', "
                .. "'ir-loaded') RAISED: " .. tostring(err_ir_set)
                .. " — it must apply cleanly so the behavior is APPLIED when "
                .. "the toggle runs (so revert runs and succeeds)")
        else
            -- Pre: the load-time apply succeeded, so get == 'ir-loaded'.
            local okPre, vPre = pcall(kcdx.behavior.get, "impl_raiser")
            local okTog, errTog =
                pcall(kcdx.behavior.set, "impl_raiser", "ir-toggle")
            local okPost, vPost = pcall(kcdx.behavior.get, "impl_raiser")
            if not okPre or vPre ~= "ir-loaded" then
                rec_late(row, false, "pre-toggle get('impl_raiser') reads "
                    .. tostring(vPre) .. " (expected the applied 'ir-loaded')")
            elseif not okTog then
                rec_late(row, false, "the toggle set('impl_raiser', "
                    .. "'ir-toggle') RAISED at the consumer call site ("
                    .. tostring(errTog) .. ") — a declarer-code raise "
                    .. "(implementation) is logged against the DECLARER and "
                    .. "does NOT re-raise at the setting consumer's site (the "
                    .. "boundary-raise attribution rule)")
            elseif not okPost or vPost ~= "ir-default" then
                rec_late(row, false, "after revert-succeeds / impl-raises, "
                    .. "get('impl_raiser') reads " .. tostring(vPost)
                    .. " (expected the spec default 'ir-default' — the record "
                    .. "AND applied flag must clear to unset so get is "
                    .. "truthful: the implementation did NOT create the new "
                    .. "state, the world is reverted)")
            else
                rec_late(row, true, "revert succeeded but implementation "
                    .. "raised on the toggle: the set did NOT re-raise at the "
                    .. "consumer (declarer-code failure, logged against the "
                    .. "declarer), and get returns the default 'ir-default' — "
                    .. "the record + applied flag cleared to unset (truthful: "
                    .. "the new state was not created, the world is reverted)")
            end
        end
    end

    -- =====================================================================
    -- cap-100-toggle-revert-raises — §5.4 failure disposition: revert(old)
    -- ITSELF RAISES on a post-load toggle. The engine cannot know how far the
    -- failed revert got, so the recorded value AND applied state stay AS THEY
    -- WERE (get still reads the loaded value — the least-lying record), logs
    -- the error attributed to the DECLARER, and the SET does NOT re-raise at
    -- the consumer call site. 'revert_raiser' was set at load with 'rr-loaded'
    -- (applied); the post-load toggle's revert raises.
    -- FALSIFIABLE: the toggle set RAISED at the call site, OR get does not
    -- still read the loaded value (the record was wrongly cleared/changed) -> FAIL.
    -- =====================================================================
    do
        local row = "cap-100-toggle-revert-raises"
        if not ok_rr then
            rec_late(row, false, "the 'revert_raiser' declare RAISED: "
                .. tostring(err_rr))
        elseif not ok_rr_set then
            rec_late(row, false, "the load-time set('revert_raiser', "
                .. "'rr-loaded') RAISED: " .. tostring(err_rr_set)
                .. " — it must apply cleanly so the behavior is APPLIED when "
                .. "the toggle runs (so revert is invoked and then raises)")
        else
            local okPre, vPre = pcall(kcdx.behavior.get, "revert_raiser")
            local okTog, errTog =
                pcall(kcdx.behavior.set, "revert_raiser", "rr-toggle")
            local okPost, vPost = pcall(kcdx.behavior.get, "revert_raiser")
            if not okPre or vPre ~= "rr-loaded" then
                rec_late(row, false, "pre-toggle get('revert_raiser') reads "
                    .. tostring(vPre) .. " (expected the applied 'rr-loaded')")
            elseif not okTog then
                rec_late(row, false, "the toggle set('revert_raiser', "
                    .. "'rr-toggle') RAISED at the consumer call site ("
                    .. tostring(errTog) .. ") — a declarer-code raise "
                    .. "(revert) is logged against the DECLARER and does NOT "
                    .. "re-raise at the setting consumer's site")
            elseif not okPost or vPost ~= "rr-loaded" then
                rec_late(row, false, "after revert ITSELF raised, "
                    .. "get('revert_raiser') reads " .. tostring(vPost)
                    .. " (expected the UNCHANGED 'rr-loaded' — the engine "
                    .. "cannot know how far the failed revert got, so the "
                    .. "recorded value and applied state stay as they were, "
                    .. "the least-lying record)")
            else
                rec_late(row, true, "revert itself raised on the toggle: the "
                    .. "set did NOT re-raise at the consumer (declarer-code "
                    .. "failure, logged against the declarer), and get still "
                    .. "reads the unchanged 'rr-loaded' — the record + applied "
                    .. "state are kept as they were (the engine cannot know "
                    .. "how far the failed revert got)")
            end
        end
    end

    -- =====================================================================
    -- cap-100-post-load-declare-error — the post-load DECLARE wall (§5.4:
    -- declares are a load-time act). This input_loaded handler runs AFTER the
    -- apply boundary, so behavior_registry::BoundaryCompleted() is true — the
    -- binder's declare-window gate (re-grounded on BoundaryCompleted,
    -- symmetric with the post-load SET gate) trips here. A declare of a NEW
    -- bare name no other fixture uses must RAISE the load-time-act teaching
    -- error AND register nothing (the rejected name is absent from list()).
    -- FALSIFIABLE: the post-load declare SUCCEEDED (the wall did not trip),
    -- the error does not teach the load-time-act rule ("load-time act" /
    -- "declares are a load-time act"), OR the attempted name appears in
    -- kcdx.behavior.list() afterward (it registered despite the error) -> FAIL.
    -- =====================================================================
    do
        local row = "cap-100-post-load-declare-error"
        local NEW_BARE = "post_load_declared"
        local okDecl, errDecl = pcall(kcdx.behavior.declare, NEW_BARE, {
            description    = "cap-100 post-load declare (must trip the wall)",
            default        = "pld-default",
            implementation = function(value) end,
        })
        if okDecl then
            rec_late(row, false, "a post-load declare('" .. NEW_BARE .. "') "
                .. "SUCCEEDED — declares are a load-time act; a declare after "
                .. "the apply boundary (BoundaryCompleted) must RAISE the "
                .. "teaching error, never register")
        elseif type(errDecl) ~= "string"
            or not string.find(errDecl, "load-time act", 1, true) then
            rec_late(row, false, "the post-load declare raised but the error "
                .. "does not teach the load-time-act rule (expected 'declares "
                .. "are a load-time act'; got: " .. tostring(errDecl) .. ")")
        else
            -- The rejected declare must have registered nothing.
            local found = nil
            local entries = kcdx.behavior.list(PREFIX)
            if type(entries) == "table" then
                for _, e in ipairs(entries) do
                    if e.name == PREFIX .. NEW_BARE then found = e break end
                end
            end
            if found then
                rec_late(row, false, "after the rejected post-load declare, '"
                    .. PREFIX .. NEW_BARE .. "' appears in list() — a "
                    .. "post-load declare must register NOTHING")
            else
                rec_late(row, true, "a declare from the input_loaded handler "
                    .. "(post-boundary, BoundaryCompleted()==true) RAISED the "
                    .. "load-time-act teaching error ('declares are a "
                    .. "load-time act') and registered nothing ('" .. PREFIX
                    .. NEW_BARE .. "' absent from list()) — the post-load "
                    .. "declare wall trips, symmetric with the post-load set")
            end
        end
    end

    for _, r in ipairs(late_results) do
        kcdx.test.report(r.row, r.pass, r.reason)
    end
    kcdx.log.info("CAP100",
        "kcdx.behavior set/boundary/toggle self-test reported "
        .. #late_results .. " post-boundary rows (set-records-and-applies "
        .. "once-with-final-value; the §5.4 post-load toggle contract: "
        .. "revert-less set teaching error, revert-before-impl toggle order, "
        .. "never-applied revert-skip, impl-raises + revert-raises failure "
        .. "dispositions; and the post-load DECLARE wall — a declare after "
        .. "the apply boundary raises the load-time-act teaching error and "
        .. "registers nothing).")
end)
