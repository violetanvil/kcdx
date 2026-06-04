-- CAP-75 plugin.lua — kcdx.assets.get_by_path regression.
--
-- kcdx.assets.get_by_path(path) resolves the CALLING plugin's OWN asset (no
-- owner prefix — the engine knows who you are) to a loadable on-disk path. It
-- is a PURE READ: it joins the calling plugin's assets/ root with `path` and
-- confirms the file exists, returning the absolute disk path the asset-
-- resolution seam opens to serve the file. A path NOT in the plugin's assets/
-- returns a TEACHING ERROR naming the missing path (never a silent nil).
--
-- The cross-plugin form kcdx.plugin.<author>.<plugin>.assets.get_by_path(path)
-- resolves through the step-6 navigable namespace: the .assets leaf on a
-- resolved plugin handle binds get_by_path to the NAVIGATED plugin. This test
-- navigates THIS plugin's OWN identity (self > engine > other guarantees it is
-- loaded — the reliable known-present target, the cap-74 rationale).
--
-- This plugin ships assets/icons/marker.txt so the own + cross reads resolve a
-- real file; the missing row asks for a path NOT in assets/ to prove the
-- teaching error fires.

local MY_AUTHOR = "ts"
local MY_PLUGIN = "cap_75_assets_get_by_path"

local OWN_ASSET     = "icons/marker.txt"          -- a real file under assets/
local MISSING_ASSET = "icons/does_not_exist.dds"  -- NOT a file under assets/

-- ====================================================================
-- (1) CAP-75-own — get_by_path resolves the calling plugin's OWN asset
--     to a non-nil, non-empty loadable path that ends with the asset's
--     relative path.
--
-- FALSIFIABLE: a nil / empty return (the resolver failed or returned a
-- silent nil), or a path that does NOT end with the asset's relative
-- path (the wrong file was resolved) -> FAIL. Only a real loadable path
-- for the requested asset is PASS.
-- ====================================================================
do
    local path, err = kcdx.assets.get_by_path(OWN_ASSET)

    -- A loadable disk path uses native separators; compare against both the
    -- forward-slash and backslash tail so the row is OS-separator-agnostic.
    local tail_fwd = OWN_ASSET
    local tail_bwd = OWN_ASSET:gsub("/", "\\")
    local function ends_with(s, suffix)
        return type(s) == "string" and #s >= #suffix
            and s:sub(-#suffix) == suffix
    end

    if type(path) ~= "string" or path == "" then
        kcdx.test.report("CAP-75-own", false,
            "kcdx.assets.get_by_path(\"" .. OWN_ASSET .. "\") returned "
            .. tostring(path) .. " (err: " .. tostring(err) .. ") — a real "
            .. "asset under THIS plugin's assets/ did not resolve to a "
            .. "loadable path. get_by_path must return the on-disk path the "
            .. "seam serves, never nil for an asset that exists")
    elseif not (ends_with(path, tail_fwd) or ends_with(path, tail_bwd)) then
        kcdx.test.report("CAP-75-own", false,
            "kcdx.assets.get_by_path(\"" .. OWN_ASSET .. "\") returned \""
            .. path .. "\" — a non-nil path, but it does NOT end with the "
            .. "requested relative path (" .. OWN_ASSET .. "). The resolver "
            .. "joined the wrong file or root")
    else
        kcdx.test.report("CAP-75-own", true,
            "kcdx.assets.get_by_path(\"" .. OWN_ASSET .. "\") resolved THIS "
            .. "plugin's own asset to the loadable path \"" .. path .. "\" "
            .. "(no owner prefix — the engine resolved the calling plugin), "
            .. "ending with the requested relative path. A pure read of the "
            .. "calling plugin's assets/ root, the disk path the seam serves")
    end
end

-- ====================================================================
-- (2) CAP-75-missing — a path to a file NOT in the plugin's assets/
--     returns a TEACHING ERROR (nil, err) naming the missing path, NOT
--     a silent nil (AP14: a typo fails loud, never a silent orphan).
--
-- FALSIFIABLE: a non-nil return (the missing asset somehow "resolved"),
-- a bare nil with no err (a silent nil — the exact AP14 defect), or an
-- err that does not name the missing path -> FAIL.
-- ====================================================================
do
    local path, err = kcdx.assets.get_by_path(MISSING_ASSET)

    if path ~= nil then
        kcdx.test.report("CAP-75-missing", false,
            "kcdx.assets.get_by_path(\"" .. MISSING_ASSET .. "\") returned a "
            .. "non-nil value (" .. tostring(path) .. ") for a path that is "
            .. "NOT in the plugin's assets/ — a missing asset must FAIL loud, "
            .. "never resolve to a path")
    elseif type(err) ~= "string" then
        kcdx.test.report("CAP-75-missing", false,
            "kcdx.assets.get_by_path(\"" .. MISSING_ASSET .. "\") returned a "
            .. "bare nil with no error string (err = " .. tostring(err)
            .. ") — a missing asset is a silent nil here, the exact AP14 "
            .. "defect. It must return (nil, teaching_err) naming the path")
    elseif not err:find(MISSING_ASSET, 1, true) then
        kcdx.test.report("CAP-75-missing", false,
            "kcdx.assets.get_by_path(\"" .. MISSING_ASSET .. "\") returned "
            .. "(nil, err) but the error (\"" .. err .. "\") does NOT name the "
            .. "missing path — a teaching error must name what was rejected so "
            .. "the author can fix the typo without consulting docs")
    else
        kcdx.test.report("CAP-75-missing", true,
            "kcdx.assets.get_by_path(\"" .. MISSING_ASSET .. "\") returned "
            .. "(nil, err) and the error names the missing path — a path NOT "
            .. "in the plugin's assets/ fails LOUD with a teaching error, "
            .. "never a silent nil (AP14)")
    end
end

-- ====================================================================
-- (3) CAP-75-cross — the cross-plugin form
--     kcdx.plugin.<author>.<plugin>.assets.get_by_path(path) resolves
--     through the step-6 navigable namespace to a non-nil loadable path.
--     Navigates THIS plugin's own identity (guaranteed loaded).
--
-- FALSIFIABLE: the __index chain raises (a segment or the .assets leaf
-- failed to resolve), or get_by_path returns nil (the leaf did not bind
-- to the navigated plugin) -> FAIL. Only a non-nil loadable path from
-- the navigated read is PASS.
-- ====================================================================
do
    local ok, pathOrErr = pcall(function()
        return kcdx.plugin[MY_AUTHOR][MY_PLUGIN].assets.get_by_path(OWN_ASSET)
    end)

    if not ok then
        kcdx.test.report("CAP-75-cross", false,
            "kcdx.plugin." .. MY_AUTHOR .. "." .. MY_PLUGIN
            .. ".assets.get_by_path(\"" .. OWN_ASSET .. "\") RAISED: "
            .. tostring(pathOrErr) .. " — a segment, the .assets leaf, or the "
            .. "bound get_by_path failed to resolve through the navigable "
            .. "namespace for THIS plugin's own (guaranteed-loaded) identity")
    elseif type(pathOrErr) ~= "string" or pathOrErr == "" then
        kcdx.test.report("CAP-75-cross", false,
            "kcdx.plugin." .. MY_AUTHOR .. "." .. MY_PLUGIN
            .. ".assets.get_by_path(\"" .. OWN_ASSET .. "\") returned "
            .. tostring(pathOrErr) .. " — the chain resolved but the .assets "
            .. "leaf did not bind get_by_path to the navigated plugin (a real "
            .. "asset under it must resolve to a loadable path)")
    else
        kcdx.test.report("CAP-75-cross", true,
            "kcdx.plugin." .. MY_AUTHOR .. "." .. MY_PLUGIN
            .. ".assets.get_by_path(\"" .. OWN_ASSET .. "\") resolved through "
            .. "the navigable namespace to the loadable path \"" .. pathOrErr
            .. "\" — the .assets leaf on the resolved plugin handle bound "
            .. "get_by_path to the navigated (author, plugin), serving its "
            .. "asset by path")
    end
end

-- ====================================================================
-- (4) CAP-75-nyi-declare — DELIBERATELY-FAILING contract pin for the
--     runtime verb kcdx.assets.declare. It is NOT bound yet (it needs
--     the runtime store, a later step). This row PASSES only when
--     declare becomes a callable function; it FAILS until then.
--
-- FALSIFIABLE (and INVERTED from a normal row — this one is EXPECTED to
-- fail today): type(kcdx.assets.declare) ~= "function" -> FAIL. When the
-- runtime-store step binds declare, this flips to PASS automatically.
-- The contract is pinned and visible, never lost (AP13: not a vague
-- someday; a real row that fails until the verb lands).
-- ====================================================================
do
    local is_fn = (type(kcdx.assets.declare) == "function")
    kcdx.test.report("CAP-75-nyi-declare", is_fn,
        is_fn
        and ("kcdx.assets.declare is now a callable function — the runtime-"
             .. "store step landed it; this contract-pin row flips to PASS")
        or ("kcdx.assets.declare is " .. type(kcdx.assets.declare) .. ", not a "
            .. "function — NOT YET IMPLEMENTED (needs the runtime store, a "
            .. "later step). This row is a deliberate contract pin: it FAILS "
            .. "until declare is bound, then flips to PASS — the contract stays "
            .. "visible, never lost"))
end

-- ====================================================================
-- (5) CAP-75-nyi-runtime — DELIBERATELY-FAILING contract pin for the
--     three runtime-store verbs get_by_name / register / replace. None
--     are bound yet. PASSES only when ALL THREE are callable functions;
--     FAILS until the runtime-store step lands them.
--
-- FALSIFIABLE (inverted — EXPECTED to fail today): any of get_by_name /
-- register / replace not a function -> FAIL. Flips to PASS when all three
-- bind. Pins the full runtime-verb contract (§5 — five verbs at parity).
-- ====================================================================
do
    local missing = {}
    for _, verb in ipairs({"get_by_name", "register", "replace"}) do
        if type(kcdx.assets[verb]) ~= "function" then
            missing[#missing + 1] = verb
        end
    end
    local all_bound = (#missing == 0)
    kcdx.test.report("CAP-75-nyi-runtime", all_bound,
        all_bound
        and ("kcdx.assets.get_by_name / .register / .replace are all callable "
             .. "functions now — the runtime-store step landed them; this "
             .. "contract-pin row flips to PASS")
        or ("the runtime-store verbs are NOT YET IMPLEMENTED — not bound: "
            .. table.concat(missing, ", ") .. " (each needs the runtime store, "
            .. "a later step). This row is a deliberate contract pin: it FAILS "
            .. "until all three bind, then flips to PASS — pinning the full "
            .. "five-verb surface (§5), never losing the contract"))
end

kcdx.log.info("CAP75",
    "ran the kcdx.assets.get_by_path self-test (CAP-75-own: own-asset read "
    .. "resolves to a loadable path; CAP-75-missing: a path not in assets/ "
    .. "teaches loud, never a silent nil; CAP-75-cross: the cross-plugin form "
    .. "resolves through the step-6 namespace; CAP-75-nyi-declare / "
    .. "CAP-75-nyi-runtime: deliberately-failing pins for the four deferred "
    .. "runtime verbs) — all five reported synchronously at load")
