-- CAP-75 plugin.lua — the kcdx.assets.* runtime surface regression.
--
-- kcdx.assets has FIVE verbs (design asset-replacement.md §5):
--   * get_by_path(path)      — pure read: own asset path -> loadable path.
--   * get_by_name(name)      — read: own published name -> loadable path.
--   * declare(name, file)    — publish <author>.<plugin>.<name> -> file's path.
--   * register(vpath, file)  — runtime overlay vpath -> file (takes effect
--                              for opens THEREAFTER).
--   * replace(target, with)  — runtime replacement keyed by target.
--
-- get_by_path shipped first (step 8); the four runtime verbs (get_by_name /
-- declare / register / replace) land here against the §5.1 runtime store. Each
-- write/read is verified by a FALSIFIABLE row.
--
-- The cross-plugin form kcdx.plugin.<author>.<plugin>.assets.get_by_path /
-- .get_by_name resolves through the cap-74 navigable namespace: the .assets leaf
-- on a resolved plugin handle binds the read verb to the NAVIGATED plugin. These
-- rows navigate THIS plugin's OWN identity (self > engine > other guarantees it
-- is loaded — the reliable known-present target, the cap-74 rationale).
--
-- This plugin ships assets/icons/marker.txt so the own + cross reads resolve a
-- real file; the missing rows ask for a path / name NOT present to prove the
-- teaching errors fire (never a silent nil, AP14).
--
-- replace(target, with) serves only the VANILLA-PATH target this step; a PACKED
-- cross-mod target ("<author>.<plugin>.<bare>") needs cross-mod resolution (a
-- later step, design §5.3) and returns a TEACHING ERROR — never a silent overlay
-- write the resolver could never hit (AP13/AP14). Row 9 below asserts that.
--
-- TEST MODE: boot-only for every STORE ROUND-TRIP row (declare/get_by_name and
-- register/replace's path-return + teaching errors are pure in-process store
-- writes+reads, fully checkable at load). The register/replace SERVE (the engine
-- opening a registered vpath -> kcdx's file) is an IN-GAME open, confirmed by the
-- agent reading the resolver's `overlay_opened ... source=runtime` log line —
-- CAP-75-register-serve is the `[manual]`/in-game row for that (see README).

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
local function ends_with(s, suffix)
    return type(s) == "string" and #s >= #suffix
        and s:sub(-#suffix) == suffix
end

-- A loadable disk path uses native separators; compare against both the
-- forward-slash and backslash tail so the rows are OS-separator-agnostic.
local function resolves_to(path, rel)
    return ends_with(path, rel) or ends_with(path, (rel:gsub("/", "\\")))
end

do
    local path, err = kcdx.assets.get_by_path(OWN_ASSET)

    if type(path) ~= "string" or path == "" then
        kcdx.test.report("CAP-75-own", false,
            "kcdx.assets.get_by_path(\"" .. OWN_ASSET .. "\") returned "
            .. tostring(path) .. " (err: " .. tostring(err) .. ") — a real "
            .. "asset under THIS plugin's assets/ did not resolve to a "
            .. "loadable path. get_by_path must return the on-disk path the "
            .. "seam serves, never nil for an asset that exists")
    elseif not resolves_to(path, OWN_ASSET) then
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
-- (3) CAP-75-cross — the cross-plugin get_by_path form
--     kcdx.plugin.<author>.<plugin>.assets.get_by_path(path) resolves
--     through the cap-74 navigable namespace to a non-nil loadable path.
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
-- (4) CAP-75-declare — declare(name, file) publishes the caller's
--     <author>.<plugin>.<name>, and the SAME plugin's get_by_name(name)
--     resolves it back to the file's loadable path. A store round-trip:
--     a write (declare) the read (get_by_name) reflects.
--
-- FALSIFIABLE: declare returns (nil, err) for a real file -> FAIL;
-- get_by_name("shirt") returns nil after declaring it, or a path that
-- does NOT end with the declared file's relative path -> FAIL. Only a
-- successful publish + a matching resolve is PASS (proves the published-
-- name store actually holds + serves the write).
-- ====================================================================
do
    local PUB_NAME = "shirt"
    local declared, derr = kcdx.assets.declare(PUB_NAME, OWN_ASSET)
    local resolved, rerr = kcdx.assets.get_by_name(PUB_NAME)

    if type(declared) ~= "string" or declared == "" then
        kcdx.test.report("CAP-75-declare", false,
            "kcdx.assets.declare(\"" .. PUB_NAME .. "\", \"" .. OWN_ASSET
            .. "\") returned " .. tostring(declared) .. " (err: "
            .. tostring(derr) .. ") — declaring a name for a REAL asset must "
            .. "publish it and return its loadable path, never nil")
    elseif type(resolved) ~= "string" or resolved == "" then
        kcdx.test.report("CAP-75-declare", false,
            "kcdx.assets.get_by_name(\"" .. PUB_NAME .. "\") returned "
            .. tostring(resolved) .. " (err: " .. tostring(rerr) .. ") AFTER "
            .. "declare published it — the published-name store did not hold "
            .. "the write (get_by_name must resolve a name declare just set)")
    elseif not resolves_to(resolved, OWN_ASSET) then
        kcdx.test.report("CAP-75-declare", false,
            "kcdx.assets.get_by_name(\"" .. PUB_NAME .. "\") resolved to \""
            .. resolved .. "\" — a path, but NOT the file declare named ("
            .. OWN_ASSET .. "). The published name points at the wrong file")
    else
        kcdx.test.report("CAP-75-declare", true,
            "kcdx.assets.declare(\"" .. PUB_NAME .. "\", \"" .. OWN_ASSET
            .. "\") published the name and get_by_name(\"" .. PUB_NAME
            .. "\") resolved it back to the same file's loadable path (\""
            .. resolved .. "\") — a store round-trip: the published-name store "
            .. "holds the write and serves it back, own form (no owner prefix)")
    end
end

-- ====================================================================
-- (5) CAP-75-getbyname-missing — get_by_name for a name the caller never
--     declared returns a TEACHING ERROR (nil, err) naming it, NOT a
--     silent nil (AP14).
--
-- FALSIFIABLE: a non-nil return (an undeclared name "resolved"), a bare
-- nil with no err (silent nil), or an err that does not name the bad
-- name -> FAIL.
-- ====================================================================
do
    local NEVER = "never_declared_name"
    local path, err = kcdx.assets.get_by_name(NEVER)

    if path ~= nil then
        kcdx.test.report("CAP-75-getbyname-missing", false,
            "kcdx.assets.get_by_name(\"" .. NEVER .. "\") returned a non-nil "
            .. "value (" .. tostring(path) .. ") for a name that was NEVER "
            .. "declared — an undeclared name must FAIL loud, never resolve")
    elseif type(err) ~= "string" then
        kcdx.test.report("CAP-75-getbyname-missing", false,
            "kcdx.assets.get_by_name(\"" .. NEVER .. "\") returned a bare nil "
            .. "with no error string (err = " .. tostring(err) .. ") — an "
            .. "undeclared name is a silent nil here, the AP14 defect. It "
            .. "must return (nil, teaching_err) naming the missing name")
    elseif not err:find(NEVER, 1, true) then
        kcdx.test.report("CAP-75-getbyname-missing", false,
            "kcdx.assets.get_by_name(\"" .. NEVER .. "\") returned (nil, err) "
            .. "but the error (\"" .. err .. "\") does NOT name the missing "
            .. "name — a teaching error must name what was rejected")
    else
        kcdx.test.report("CAP-75-getbyname-missing", true,
            "kcdx.assets.get_by_name(\"" .. NEVER .. "\") returned (nil, err) "
            .. "naming the undeclared name — an unpublished name fails LOUD "
            .. "with a teaching error, never a silent nil (AP14)")
    end
end

-- ====================================================================
-- (6) CAP-75-getbyname-cross — the §6 cross-plugin get_by_name form
--     kcdx.plugin.<author>.<plugin>.assets.get_by_name(name) resolves
--     ANOTHER plugin's published name through the cap-74 namespace.
--     Navigates THIS plugin's own identity and resolves the "shirt" name
--     row (4) declared on it — proving the cross-plugin leaf binds
--     get_by_name to the NAVIGATED plugin's published-name store.
--
-- FALSIFIABLE: the __index chain raises, OR the navigated get_by_name
-- returns nil for a name THIS plugin declared (row 4), OR a path not
-- ending with the declared file -> FAIL.
-- ====================================================================
do
    local PUB_NAME = "shirt"  -- declared in row (4) above on THIS plugin
    local ok, pathOrErr = pcall(function()
        return kcdx.plugin[MY_AUTHOR][MY_PLUGIN].assets.get_by_name(PUB_NAME)
    end)

    if not ok then
        kcdx.test.report("CAP-75-getbyname-cross", false,
            "kcdx.plugin." .. MY_AUTHOR .. "." .. MY_PLUGIN
            .. ".assets.get_by_name(\"" .. PUB_NAME .. "\") RAISED: "
            .. tostring(pathOrErr) .. " — a segment or the .assets.get_by_name "
            .. "leaf failed to resolve through the navigable namespace")
    elseif type(pathOrErr) ~= "string" or pathOrErr == "" then
        kcdx.test.report("CAP-75-getbyname-cross", false,
            "kcdx.plugin." .. MY_AUTHOR .. "." .. MY_PLUGIN
            .. ".assets.get_by_name(\"" .. PUB_NAME .. "\") returned "
            .. tostring(pathOrErr) .. " — the chain resolved but the leaf did "
            .. "not resolve a name THIS plugin published (row 4 declared it)")
    elseif not resolves_to(pathOrErr, OWN_ASSET) then
        kcdx.test.report("CAP-75-getbyname-cross", false,
            "kcdx.plugin." .. MY_AUTHOR .. "." .. MY_PLUGIN
            .. ".assets.get_by_name(\"" .. PUB_NAME .. "\") resolved to \""
            .. pathOrErr .. "\" — a path, but NOT the file declared for that "
            .. "name (" .. OWN_ASSET .. ")")
    else
        kcdx.test.report("CAP-75-getbyname-cross", true,
            "kcdx.plugin." .. MY_AUTHOR .. "." .. MY_PLUGIN
            .. ".assets.get_by_name(\"" .. PUB_NAME .. "\") resolved through "
            .. "the navigable namespace to \"" .. pathOrErr .. "\" — the "
            .. ".assets leaf bound get_by_name to the navigated plugin's "
            .. "published-name store (§6 cross-plugin published-name read)")
    end
end

-- ====================================================================
-- (7) CAP-75-register — register(vpath, file) returns a non-nil loadable
--     path for a REAL file (the runtime overlay was written), AND
--     register(vpath, missing file) returns a teaching error (AP14).
--     The path-return + reject are boot-checkable; the SERVE (engine
--     opens the vpath -> kcdx's file) is the in-game CAP-75-register-serve
--     row below.
--
-- FALSIFIABLE: register for a real file returns nil -> FAIL; register
-- for a missing file returns a non-nil path or a bare nil -> FAIL.
-- ====================================================================
do
    local GOOD_VPATH = "Data/cap75_runtime_gen.txt"
    local good, gerr = kcdx.assets.register(GOOD_VPATH, OWN_ASSET)
    local bad, berr  = kcdx.assets.register("Data/cap75_runtime_bad.txt",
                                            MISSING_ASSET)

    if type(good) ~= "string" or good == "" then
        kcdx.test.report("CAP-75-register", false,
            "kcdx.assets.register(\"" .. GOOD_VPATH .. "\", \"" .. OWN_ASSET
            .. "\") returned " .. tostring(good) .. " (err: " .. tostring(gerr)
            .. ") — registering a REAL file must write the overlay and return "
            .. "its loadable path, never nil")
    elseif bad ~= nil then
        kcdx.test.report("CAP-75-register", false,
            "kcdx.assets.register(..., \"" .. MISSING_ASSET .. "\") returned a "
            .. "non-nil value (" .. tostring(bad) .. ") for a file NOT in "
            .. "assets/ — a missing source file must FAIL loud, never register "
            .. "a broken overlay")
    elseif type(berr) ~= "string" or not berr:find(MISSING_ASSET, 1, true) then
        kcdx.test.report("CAP-75-register", false,
            "kcdx.assets.register(..., \"" .. MISSING_ASSET .. "\") returned "
            .. "(nil, " .. tostring(berr) .. ") — a missing source file must "
            .. "return a teaching error NAMING the bad path, not a bare nil")
    else
        kcdx.test.report("CAP-75-register", true,
            "kcdx.assets.register(\"" .. GOOD_VPATH .. "\", \"" .. OWN_ASSET
            .. "\") wrote the runtime overlay and returned its loadable path; "
            .. "register with a file NOT in assets/ failed LOUD with a "
            .. "teaching error naming the path (AP14). The runtime-overlay "
            .. "store holds the write (the in-game open serves it — "
            .. "CAP-75-register-serve)")
    end
end

-- ====================================================================
-- (8) CAP-75-replace — replace(target, with) returns a non-nil loadable
--     path for a REAL replacement file (the runtime overlay keyed by the
--     target was written), AND replace(target, missing file) teaches
--     loud. Path-return + reject are boot-checkable; the thereafter-serve
--     is the same in-game mechanism as register (CAP-75-register-serve).
--
-- FALSIFIABLE: replace for a real file returns nil -> FAIL; replace for
-- a missing file returns a non-nil path or a bare nil -> FAIL.
-- ====================================================================
do
    local TARGET = "Data/cap75_replace_target.dds"
    local good, gerr = kcdx.assets.replace(TARGET, OWN_ASSET)
    local bad, berr  = kcdx.assets.replace("Data/cap75_replace_bad.dds",
                                           MISSING_ASSET)

    if type(good) ~= "string" or good == "" then
        kcdx.test.report("CAP-75-replace", false,
            "kcdx.assets.replace(\"" .. TARGET .. "\", \"" .. OWN_ASSET
            .. "\") returned " .. tostring(good) .. " (err: " .. tostring(gerr)
            .. ") — replacing a target with a REAL file must write the runtime "
            .. "overlay keyed by the target and return its loadable path")
    elseif bad ~= nil then
        kcdx.test.report("CAP-75-replace", false,
            "kcdx.assets.replace(..., \"" .. MISSING_ASSET .. "\") returned a "
            .. "non-nil value (" .. tostring(bad) .. ") for a file NOT in "
            .. "assets/ — a missing replacement file must FAIL loud")
    elseif type(berr) ~= "string" or not berr:find(MISSING_ASSET, 1, true) then
        kcdx.test.report("CAP-75-replace", false,
            "kcdx.assets.replace(..., \"" .. MISSING_ASSET .. "\") returned "
            .. "(nil, " .. tostring(berr) .. ") — a missing replacement file "
            .. "must return a teaching error NAMING the bad path")
    else
        kcdx.test.report("CAP-75-replace", true,
            "kcdx.assets.replace(\"" .. TARGET .. "\", \"" .. OWN_ASSET
            .. "\") wrote the runtime overlay keyed by the target and returned "
            .. "its loadable path; replace with a file NOT in assets/ failed "
            .. "LOUD with a teaching error (AP14). Take-effect is thereafter — "
            .. "the in-game open serves it (same mechanism as register)")
    end
end

-- ====================================================================
-- (8b) CAP-75-replace-real-vpath — replace a REAL vanilla vpath the engine
--      actually opens, so the in-game serve fires the resolver's runtime-store
--      consult (source=runtime). Targets Libs/UI/Textures/KCDLogo.dds — a
--      memory-mapped .dds the engine opens at the menu (crash-safe: a .dds
--      overwrite renders altered, never faults). The replacement is cap-75's
--      own valid placeholder .dds (textures/cap75_runtime_logo.dds, a zeroed
--      payload = a gray rectangle). Boot: the path-return self-reports here;
--      the SERVE is the in-game CAP-75-register-serve row (overlay_resolved/
--      opened source=runtime). This is the REAL-vpath serve vehicle (the prior
--      synthetic Data/cap75_runtime_gen.txt is never opened by the engine).
--
-- FALSIFIABLE: replace of the real vpath returns nil/empty -> FAIL (the runtime
-- overlay was not keyed at a vpath the engine opens).
-- ====================================================================
do
    local LOGO_VPATH = "Libs/UI/Textures/KCDLogo.dds"  -- a real, engine-opened vanilla vpath
    local LOGO_WITH  = "textures/cap75_runtime_logo.dds"  -- cap-75's own placeholder .dds
    local ret, err = kcdx.assets.replace(LOGO_VPATH, LOGO_WITH)

    if type(ret) ~= "string" or ret == "" then
        kcdx.test.report("CAP-75-replace-real-vpath", false,
            "kcdx.assets.replace(\"" .. LOGO_VPATH .. "\", \"" .. LOGO_WITH
            .. "\") returned " .. tostring(ret) .. " (err: " .. tostring(err)
            .. ") — replacing a REAL engine-opened vanilla vpath must key the "
            .. "runtime-overlay store and return the loadable path; nil means the "
            .. "runtime store was not keyed at a vpath the engine opens")
    else
        kcdx.test.report("CAP-75-replace-real-vpath", true,
            "kcdx.assets.replace(\"" .. LOGO_VPATH .. "\", \"" .. LOGO_WITH
            .. "\") keyed the runtime-overlay store at a REAL engine-opened "
            .. "vanilla vpath and returned the loadable path (\"" .. ret .. "\"). "
            .. "The engine's in-game open of " .. LOGO_VPATH .. " now consults "
            .. "the runtime store and serves cap-75's .dds — confirmed by the "
            .. "[manual] CAP-75-register-serve row (overlay_resolved/opened "
            .. "source=runtime), and visibly by the logo rendering cap-75's "
            .. "placeholder")
    end
end

-- ====================================================================
-- (9) CAP-75-replace-crossmod-unresolvable-teach — replace(target, with)
--     where TARGET is a PACKED CROSS-MOD published name
--     ("<author>.<plugin>.<bare>") that resolves to NO published asset
--     (the owner plugin is not loaded / the name was never published)
--     returns a TEACHING ERROR (nil, err), NOT a path / silent nil.
--
--     Cross-mod RESOLUTION (a packed name -> the publisher's serve-vpath,
--     design §5.3) is LIVE (step 8c). A packed name that RESOLVES serves
--     (the comp-16 cross-mod fixtures prove the resolving path). This row
--     proves the OTHER half: an UNRESOLVABLE packed name (here
--     "redmoon.outfit.belt" — redmoon.outfit is not a loaded plugin in
--     this suite, so nothing published `belt`) fails LOUD (AP14), never a
--     silent nil and never a path keyed at a non-existent serve-vpath.
--
--     `with` is a REAL file (icons/marker.txt) so the only reason for a
--     (nil, err) here is the unresolvable packed target — isolating the
--     cross-mod-resolution-miss path from the missing-`with`-file path (row 8).
--
-- FALSIFIABLE: a non-nil return (the unresolvable packed name "resolved"
-- to some serve-vpath / a path was returned — wrong), OR a bare nil with
-- no err (silent nil), OR an err that does not name the packed target
-- -> FAIL. Only (nil, err) naming the packed target is PASS.
-- ====================================================================
do
    local PACKED = "redmoon.outfit.belt"  -- a packed <author>.<plugin>.<bare>
                                          -- whose owner is NOT loaded here
    local ret, err = kcdx.assets.replace(PACKED, OWN_ASSET)

    if ret ~= nil then
        kcdx.test.report("CAP-75-replace-crossmod-unresolvable-teach", false,
            "kcdx.assets.replace(\"" .. PACKED .. "\", \"" .. OWN_ASSET
            .. "\") returned a non-nil value (" .. tostring(ret) .. ") for a "
            .. "PACKED cross-mod target that resolves to NO published asset "
            .. "(redmoon.outfit is not loaded). An unresolvable cross-mod name "
            .. "must NOT return a path (there is no serve-vpath to key); it must "
            .. "teach loud (AP14)")
    elseif type(err) ~= "string" then
        kcdx.test.report("CAP-75-replace-crossmod-unresolvable-teach", false,
            "kcdx.assets.replace(\"" .. PACKED .. "\", ...) returned a bare nil "
            .. "with no error string (err = " .. tostring(err) .. ") — an "
            .. "unresolvable packed cross-mod target is a silent nil here. It "
            .. "must return (nil, teaching_err) naming the unresolved name")
    elseif not err:find(PACKED, 1, true) then
        kcdx.test.report("CAP-75-replace-crossmod-unresolvable-teach", false,
            "kcdx.assets.replace(\"" .. PACKED .. "\", ...) returned (nil, err) "
            .. "but the error (\"" .. err .. "\") does NOT name the packed "
            .. "target — a teaching error must name what was rejected so the "
            .. "author knows which call to change")
    else
        kcdx.test.report("CAP-75-replace-crossmod-unresolvable-teach", true,
            "kcdx.assets.replace(\"" .. PACKED .. "\", \"" .. OWN_ASSET
            .. "\") returned (nil, err) naming the packed target — an "
            .. "UNRESOLVABLE cross-mod name (owner not loaded / never published) "
            .. "fails LOUD (AP14), never a path keyed at a non-existent "
            .. "serve-vpath. A RESOLVABLE cross-mod name serves (the comp-16 "
            .. "fixtures); the vanilla-path form (row 8) serves now")
    end
end

-- ====================================================================
-- (8d) CAP-75-replace-boot-asset-warn — replacing a vpath the engine
--      opened at BOOT (before plugin.lua ran) emits a ONE-TIME teaching
--      warn (KI-0005): the runtime overlay can never serve a boot-cached
--      asset (take-effect="thereafter", the engine opens + caches it once
--      at boot, before this call), so kcdx teaches the author to use the
--      declarative replaces.toml sidecar instead — never a SILENT
--      non-serve (AP14). Targets Libs/UI/Textures/KCDLogo.dds — PROBE B
--      (KI-0005) proved the engine opens it EXACTLY ONCE at boot, before
--      plugin.lua. (Row 8b above already replaces this vpath, so the warn
--      fires for that call too; this row documents + agent-verifies it.)
--
--      TEST MODE — [manual]/boot-only, agent reads the log. The warn is a
--      LOG line (LOG_WARN_KV, category ASSET_RUNTIME, action
--      `runtime_overlay_boot_asset`), NOT a Lua return value — the Lua side
--      cannot observe it. So the Lua self-report here asserts only the
--      ADDITIVE-no-break half (the warn does NOT break replace's path
--      return — replace still returns a non-nil loadable path for the boot
--      target). The WARN itself is the AGENT's falsifiable log-read
--      observable: the agent greps kcdx-dev.log for
--      `ASSET_RUNTIME runtime_overlay_boot_asset vpath="libs/ui/textures/kcdlogo.dds"`
--      after the launch-to-menu run.
--
-- FALSIFIABLE (Lua half): replace of the boot vpath returns nil/empty (the
--      warn broke the additive path return) -> FAIL.
-- FALSIFIABLE (agent log-read half, the KI-0005 fix proper): the replace of
--      a boot-opened target produces NO `runtime_overlay_boot_asset` warn
--      line for libs/ui/textures/kcdlogo.dds (the silent no-op KI-0005 is
--      closing) -> FAIL.
-- ====================================================================
do
    local BOOT_VPATH = "Libs/UI/Textures/KCDLogo.dds"  -- PROBE B: opened once at boot
    local BOOT_WITH  = "textures/cap75_runtime_logo.dds"  -- cap-75's placeholder .dds
    -- A distinct register of the boot vpath (separate from row 8b's replace)
    -- so the warn-bearing register/replace path is exercised regardless of 8b's
    -- ordering; the per-vpath warn dedup means the warn line still appears once.
    local ret, err = kcdx.assets.register(BOOT_VPATH, BOOT_WITH)

    if type(ret) ~= "string" or ret == "" then
        kcdx.test.report("CAP-75-replace-boot-asset-warn", false,
            "kcdx.assets.register(\"" .. BOOT_VPATH .. "\", \"" .. BOOT_WITH
            .. "\") returned " .. tostring(ret) .. " (err: " .. tostring(err)
            .. ") — registering a boot-opened vpath must STILL return the loadable "
            .. "path (the KI-0005 teaching warn is ADDITIVE, it must not break the "
            .. "store write / return). The agent confirms the warn fired by reading "
            .. "the `ASSET_RUNTIME runtime_overlay_boot_asset` line in kcdx-dev.log")
    else
        kcdx.test.report("CAP-75-replace-boot-asset-warn", true,
            "kcdx.assets.register(\"" .. BOOT_VPATH .. "\", \"" .. BOOT_WITH
            .. "\") returned the loadable path (\"" .. ret .. "\") — the KI-0005 "
            .. "teaching warn for a boot-opened vpath is ADDITIVE (the store write "
            .. "+ path return are unchanged). The WARN itself is the agent's "
            .. "falsifiable observable: grep kcdx-dev.log for "
            .. "`ASSET_RUNTIME runtime_overlay_boot_asset "
            .. "vpath=\"libs/ui/textures/kcdlogo.dds\"` — its ABSENCE for a "
            .. "boot-opened target is the silent no-op KI-0005 closes (FAIL)")
    end
end

kcdx.log.info("CAP75",
    "ran the kcdx.assets.* runtime self-test (get_by_path own/missing/cross; "
    .. "declare+get_by_name round-trip own + cross; get_by_name teaching error; "
    .. "register + replace path-return + teaching errors; replace cross-mod "
    .. "packed-target teaching error; boot-asset teaching warn KI-0005) — all "
    .. "reported synchronously at load. The register/replace IN-GAME serve is the "
    .. "[manual] CAP-75-register-serve row; the KI-0005 boot-asset warn is the "
    .. "[manual] CAP-75-replace-boot-asset-warn row (agent reads "
    .. "runtime_overlay_boot_asset in kcdx-dev.log).")
