-- CAP-103 plugin.lua — the engine behavior-catalog loader path.
--
-- The engine loads the behavior-catalog pack (<kcdx-engine>/behavior-catalog/
-- *.lua) as a builtin AHEAD of every user plugin: the call site in hooks.cpp
-- runs the catalog strictly BEFORE lua_plugin_loader::RunAll. So when THIS
-- plugin's plugin.lua runs, the catalog's proving entry
-- kcdx.behavior.log_texture_streaming is ALREADY declared — a present-at-load
-- assertion proves the pin-ahead order (a catalog that ran late, or not at all,
-- would not have the name yet).
--
-- The proving entry stamps the RESERVED kcdx.behavior.<bare> root via the
-- engine identity (declarer "kcdx"), distinct from a user plugin's
-- <author>.<plugin>.<bare>. The catalog entry is set from this plugin's MAIN
-- stop (plugin.lua); a catalog-tier name is settable from any stop (the §6
-- window law), and the apply boundary invokes its implementation once with the
-- recorded value — the boundary-dependent row reports at input_loaded.
--
-- This plugin's own stamp prefix: ts.cap_103_catalog_loader. (used only to
-- prove the catalog entry is NOT under this plugin's prefix — tier separation.)

local CATALOG_NAME = "kcdx.behavior.log_texture_streaming"
local OWN_PREFIX   = "ts.cap_103_catalog_loader."

-- Load-observable rows reported at ready.
local results = {}
local function rec(row, pass, reason)
    results[#results + 1] = { row = row, pass = pass, reason = reason }
end

-- Boundary-dependent rows reported at input_loaded (post-boundary).
local late_results = {}
local function rec_late(row, pass, reason)
    late_results[#late_results + 1] = { row = row, pass = pass, reason = reason }
end

local ROWS = {
    "cap-103-catalog-loaded-ahead",
    "cap-103-catalog-stamp-reserved-root",
    "cap-103-catalog-list-filter",
}
local LATE_ROWS = {
    "cap-103-catalog-set-main-stop",
}

-- State the main-stop set row reads at input_loaded (declared at file scope so
-- the input_loaded closure captures these as upvalues).
local ok_set, err_set
local ok_get_after_set, get_after_set

-- The domain must be a TABLE before any member is read (mirrors cap-100/cap-99).
local domain_ok = type(kcdx.behavior) == "table"
local verbs_ok = domain_ok
    and type(kcdx.behavior.get) == "function"
    and type(kcdx.behavior.set) == "function"
    and type(kcdx.behavior.list) == "function"
if not verbs_ok then
    local detail = "behavior=" .. type(kcdx.behavior)
    if domain_ok then
        detail = detail
            .. ", get=" .. type(kcdx.behavior.get)
            .. ", set=" .. type(kcdx.behavior.set)
            .. ", list=" .. type(kcdx.behavior.list)
    end
    for _, row in ipairs(ROWS) do
        rec(row, false,
            "the kcdx.behavior domain did not register its get/set/list verbs ("
            .. detail .. ") — the binder is missing; no catalog row can run")
    end
    for _, row in ipairs(LATE_ROWS) do
        rec(row, false,
            "the kcdx.behavior domain did not register its get/set/list verbs ("
            .. detail .. ") — the binder is missing; no catalog row can run")
    end
else
    -- The catalog set at the main stop runs HERE (the load-time act). The
    -- boundary applies it; the post-boundary assertions run at input_loaded.
    ok_set, err_set = pcall(kcdx.behavior.set, CATALOG_NAME, true)
    ok_get_after_set, get_after_set = pcall(kcdx.behavior.get, CATALOG_NAME)

    -- =====================================================================
    -- cap-103-catalog-loaded-ahead — the catalog name already RESOLVES at this
    -- plugin's load window (get answers its default without raising). The
    -- catalog pack ran BEFORE this user plugin (the pin-ahead call site), so
    -- the name exists by the time this code runs.
    -- FALSIFIABLE: get(CATALOG_NAME) RAISES "no declared behavior" at load (the
    -- catalog did not run ahead, or did not register the entry) -> FAIL.
    -- (Read via the explicit full form so resolution is the reserved-root exact
    -- lookup, never this plugin's self-tier.)
    -- =====================================================================
    do
        local row = "cap-103-catalog-loaded-ahead"
        local okGet, v = pcall(kcdx.behavior.get, CATALOG_NAME)
        if not okGet then
            rec(row, false, "get('" .. CATALOG_NAME .. "') RAISED at this "
                .. "plugin's load window (" .. tostring(v) .. ") — the engine "
                .. "behavior-catalog did not load ahead of this user plugin, or "
                .. "the proving entry did not register under the reserved root")
        else
            -- The proving entry's declared default is `false` (a read-only
            -- benign behavior). get must answer it (never set yet at this point
            -- for the read — the set above recorded true, but get reads the
            -- recorded value once set; so accept either the default OR the
            -- just-recorded true: the load-ahead claim is "it resolves", not a
            -- specific value). Resolving without a raise IS the proof.
            rec(row, true, "get('" .. CATALOG_NAME .. "') resolved at this "
                .. "plugin's load window (value " .. tostring(v) .. ") — the "
                .. "catalog pack ran BEFORE this user plugin (pin-ahead) and "
                .. "registered the entry")
        end
    end

    -- =====================================================================
    -- cap-103-catalog-stamp-reserved-root — the entry is in list("kcdx.behavior.")
    -- with name == CATALOG_NAME and declarer == "kcdx" (the engine-identity
    -- stamp), NOT an <author>.<plugin> prefix.
    -- FALSIFIABLE: the entry is absent from the kcdx.behavior. filter, its name
    -- lacks the reserved root, or its declarer is not "kcdx" -> FAIL.
    -- =====================================================================
    do
        local row = "cap-103-catalog-stamp-reserved-root"
        local entries = kcdx.behavior.list("kcdx.behavior.")
        if type(entries) ~= "table" then
            rec(row, false, "list('kcdx.behavior.') returned "
                .. type(entries) .. " (expected a table)")
        else
            local found = nil
            for _, e in ipairs(entries) do
                if e.name == CATALOG_NAME then found = e break end
            end
            if not found then
                rec(row, false, "list('kcdx.behavior.') does not carry '"
                    .. CATALOG_NAME .. "' — the catalog entry did not register "
                    .. "under the reserved kcdx.behavior.<bare> root (the "
                    .. "engine-identity stamping is broken)")
            elseif found.declarer ~= "kcdx" then
                rec(row, false, "the catalog entry's .declarer is "
                    .. tostring(found.declarer) .. " (expected 'kcdx' — the "
                    .. "engine-identity stamp; a plugin <author>.<plugin> stamp "
                    .. "means the catalog ran through the plugin path, not "
                    .. "DeclareEngine)")
            else
                rec(row, true, "the catalog entry '" .. CATALOG_NAME
                    .. "' is in list('kcdx.behavior.') with declarer 'kcdx' — "
                    .. "stamped under the reserved root via the engine identity, "
                    .. "not a plugin <author>.<plugin>.<bare> prefix")
            end
        end
    end

    -- =====================================================================
    -- cap-103-catalog-list-filter — list("kcdx.behavior.") returns the catalog
    -- tier (>=1 entry, every returned name under the reserved root), and the
    -- catalog entry is NOT present in this plugin's own prefix (tier separation).
    -- FALSIFIABLE: the kcdx.behavior. filter is empty / leaks a non-reserved
    -- name, or the catalog entry appears under this plugin's prefix -> FAIL.
    -- =====================================================================
    do
        local row = "cap-103-catalog-list-filter"
        local catEntries = kcdx.behavior.list("kcdx.behavior.")
        local ownEntries = kcdx.behavior.list(OWN_PREFIX)
        local verdict = nil
        if type(catEntries) ~= "table" then
            verdict = "list('kcdx.behavior.') returned " .. type(catEntries)
                .. " (expected a table)"
        elseif #catEntries < 1 then
            verdict = "list('kcdx.behavior.') is EMPTY — the catalog tier "
                .. "carries no entries (the pack did not load, or registered "
                .. "nothing under the reserved root)"
        else
            for _, e in ipairs(catEntries) do
                if string.sub(e.name, 1, #"kcdx.behavior.") ~= "kcdx.behavior." then
                    verdict = "list('kcdx.behavior.') leaked the non-catalog "
                        .. "entry '" .. tostring(e.name) .. "' — the reserved-"
                        .. "root filter must return only kcdx.behavior.* names"
                    break
                end
            end
            if not verdict and type(ownEntries) == "table" then
                for _, e in ipairs(ownEntries) do
                    if e.name == CATALOG_NAME then
                        verdict = "the catalog entry '" .. CATALOG_NAME
                            .. "' appears under this plugin's own prefix '"
                            .. OWN_PREFIX .. "' — the tier stamp is wrong"
                        break
                    end
                end
            end
        end
        if verdict then
            rec(row, false, verdict)
        else
            rec(row, true, "list('kcdx.behavior.') returned the catalog tier ("
                .. #catEntries .. " entr" .. (#catEntries == 1 and "y" or "ies")
                .. ", every name under the reserved root) and the catalog entry "
                .. "is absent from this plugin's own prefix — tier separation "
                .. "holds")
        end
    end
end

-- Report the load-observable rows at ready.
kcdx.on("ready", function()
    for _, r in ipairs(results) do
        kcdx.test.report(r.row, r.pass, r.reason)
    end
    kcdx.log.info("CAP103",
        "engine behavior-catalog loader self-test reported " .. #results
        .. " load-observable rows (catalog loaded ahead of this plugin, the "
        .. "proving entry stamped under the reserved kcdx.behavior.<bare> root "
        .. "with declarer 'kcdx', the kcdx.behavior. list filter returns the "
        .. "catalog tier with tier separation)")
end)

-- Boundary-dependent row reports at input_loaded (post-boundary — the apply
-- boundary runs pre-InputLoaded; "ready" fires BEFORE the boundary).
kcdx.on("input_loaded", function()
    if not verbs_ok then return end  -- already reported as FAILs at ready

    -- =====================================================================
    -- cap-103-catalog-set-main-stop — a main-stop set on the catalog entry
    -- (settable from any stop, §6 window law) RECORDED (no raise — a catalog
    -- name resolves and is settable from the main stop), and the apply boundary
    -- accepted it: get tracks the recorded value (true) post-boundary.
    -- FALSIFIABLE: the set RAISED (a catalog name must resolve + be settable
    -- from the main stop), or get does not read the recorded value
    -- post-boundary -> FAIL.
    -- =====================================================================
    do
        local row = "cap-103-catalog-set-main-stop"
        local okNow, vNow = pcall(kcdx.behavior.get, CATALOG_NAME)
        if not ok_set then
            rec_late(row, false, "set('" .. CATALOG_NAME .. "', true) at the "
                .. "main stop RAISED: " .. tostring(err_set) .. " — a "
                .. "catalog-tier behavior must resolve and be settable from any "
                .. "stop (the §6 window law); the catalog name did not resolve")
        elseif not ok_get_after_set or get_after_set ~= true then
            rec_late(row, false, "get('" .. CATALOG_NAME .. "') immediately "
                .. "after the main-stop set read " .. tostring(get_after_set)
                .. " (expected true) — the set did not record visibly")
        elseif not okNow or vNow ~= true then
            rec_late(row, false, "post-boundary get('" .. CATALOG_NAME
                .. "') reads " .. tostring(vNow) .. " (expected the recorded "
                .. "true — the boundary must have applied/kept the recorded "
                .. "value, never cleared it on a clean implementation)")
        else
            rec_late(row, true, "a main-stop set on the catalog entry recorded "
                .. "(get answered true immediately, no raise — settable from "
                .. "any stop) and post-boundary get still reads the recorded "
                .. "true: the catalog tier is settable + applies from the main "
                .. "stop")
        end
    end

    for _, r in ipairs(late_results) do
        kcdx.test.report(r.row, r.pass, r.reason)
    end
    kcdx.log.info("CAP103",
        "engine behavior-catalog set self-test reported " .. #late_results
        .. " post-boundary row (a main-stop set on the catalog entry records "
        .. "and applies — the catalog tier is settable from any stop)")
end)
