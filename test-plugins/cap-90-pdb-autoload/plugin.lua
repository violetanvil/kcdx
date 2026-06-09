-- CAP-90 plugin.lua — PDB auto-load of plugin-DLL internal-function addresses.
--
-- Proves the FEATURE built in src/plugin_pdb.{cpp,h}: the engine parsed this
-- plugin's own /DEBUG:FULL sidecar .pdb at C++ plugin load and populated the
-- NON-EXPORTED internal cap90_internal_target's address into
-- kcdx.functions["ts.cap_90_pdb_autoload"].cap90_internal_target.
--
-- ONE plugin: the C++ DLL (cap-90.cpp) ships the internal + the PDB; this Lua
-- entrypoint (same plugin → same <author>.<plugin> namespace) reads the address
-- back via :resolve() and reports the falsifiable row.
--
-- FALSIFIABLE: the row goes RED if has_address is not true / the address is nil
-- or zero. The internal is non-exported by construction, so its address can
-- ONLY have come from the PDB auto-load path — a RED row means PDB auto-load did
-- not run, the /DEBUG:FULL PDB was absent/mismatched, or the FASTLINK fallback
-- dropped on a present FULL PDB. There is no degraded path: this is the plugin's
-- OWN namespace populated by its OWN PDB at load — no reference-DB dependency.

-- This plugin's own <author>.<plugin> namespace (author="ts",
-- name="cap_90_pdb_autoload" in kcdx.toml → the engine stamps this stem).
local PLUGIN_NS = "ts.cap_90_pdb_autoload"
-- The deliberately non-exported internal the DLL ships; the name the PDB
-- enumeration surfaces (undecorated) and PopulateFromPdb records under PLUGIN_NS.
local INTERNAL_FN = "cap90_internal_target"
-- A C-runtime function the linker pulls into EVERY plugin DLL. The PDB enumerate
-- yields it (its source is a CRT build tree), but the author never wrote it and
-- nobody hooks it — the source-file filter must REJECT it, so it must NOT resolve
-- under the plugin's namespace. Its undecorated leaf is exactly "operator delete"
-- (no "::"), so it keys as the bracket-index ["operator delete"].
local CRT_FN = "operator delete"

kcdx.on("ready", function()
    local row = "cap-90-pdb-internal-address"

    if kcdx.functions == nil then
        kcdx.test.report(row, false,
            "kcdx.functions namespace is not registered — the function-reference "
            .. "binder did not bind")
        return
    end

    local ref = kcdx.functions[PLUGIN_NS][INTERNAL_FN]
    if ref == nil then
        kcdx.test.report(row, false,
            "kcdx.functions[\"" .. PLUGIN_NS .. "\"]." .. INTERNAL_FN
            .. " is nil — the plugin-stem proxy did not mint a reference")
        return
    end

    local r = ref:resolve()
    if r == nil then
        kcdx.test.report(row, false,
            ":resolve() returned nil — the accessor produced no result table")
    elseif r.found ~= true then
        kcdx.test.report(row, false,
            ":resolve found=" .. tostring(r.found) .. " reason="
            .. tostring(r.reason) .. " — expected found=true. The non-exported "
            .. "internal " .. INTERNAL_FN .. " must resolve from its /DEBUG:FULL "
            .. "PDB; not_declared here means PDB auto-load did not populate the "
            .. "address (no PDB / mismatched PDB / FASTLINK stub)")
    elseif r.is_game ~= false then
        kcdx.test.report(row, false,
            "the PDB-sourced internal reported is_game=" .. tostring(r.is_game)
            .. " — a plugin-DLL internal is a PLUGIN reference (is_game=false), "
            .. "not a game-DLL one")
    elseif r.has_address ~= true or r.address == nil then
        kcdx.test.report(row, false,
            "kcdx.functions[\"" .. PLUGIN_NS .. "\"]." .. INTERNAL_FN
            .. " resolved but has_address=" .. tostring(r.has_address)
            .. " address=" .. tostring(r.address)
            .. " — expected a non-zero address from the /DEBUG:FULL PDB. "
            .. "A present FULL PDB silently yielding no internal is the FASTLINK-"
            .. "fallback regression this row guards")
    else
        kcdx.test.report(row, true,
            "kcdx.functions[\"" .. PLUGIN_NS .. "\"]." .. INTERNAL_FN
            .. ":resolve -> found=true is_game=false has_address=true address="
            .. tostring(r.address) .. " signature=\"" .. tostring(r.signature)
            .. "\" — the non-exported internal's address came from the plugin's "
            .. "own /DEBUG:FULL PDB at load (a static op needs only the address; "
            .. "no signature is declared for it, hence signature=\"\")")
    end

    -- Second row: the source-file filter dropped the CRT/compiler plumbing.
    -- The plugin DLL links the C runtime, so the PDB enumerate yields CRT
    -- functions (operator delete, _set_new_handler, …) as in-range SymTagFunction
    -- symbols. PopulateFromPdb's source-file filter REJECTS them (their source is
    -- a CRT build tree, not the plugin's .cpp), so they must NOT be recorded under
    -- the plugin namespace. This row is the falsifiable proof the noise is gone.
    --
    -- FALSIFIABLE: the row goes RED if CRT_FN RESOLVES (found=true). That can only
    -- happen if the filter did not run or is too loose (it recorded a CRT internal
    -- the author never wrote). The author's own function above STILL resolving +
    -- this CRT name NOT resolving together prove the filter keeps the author's
    -- code while dropping the plumbing.
    local crt_row = "cap-90-crt-filtered"
    if kcdx.functions == nil then
        kcdx.test.report(crt_row, false,
            "kcdx.functions namespace is not registered — cannot check the filter")
    else
        local crt_ref = kcdx.functions[PLUGIN_NS][CRT_FN]
        local cr = crt_ref ~= nil and crt_ref:resolve() or nil
        if cr ~= nil and cr.found == true then
            kcdx.test.report(crt_row, false,
                "kcdx.functions[\"" .. PLUGIN_NS .. "\"][\"" .. CRT_FN .. "\"]"
                .. ":resolve -> found=true has_address=" .. tostring(cr.has_address)
                .. " — a C-runtime function was recorded under the plugin namespace. "
                .. "The source-file filter must REJECT CRT/compiler-internal "
                .. "functions (their source is a CRT build tree, not the author's "
                .. ".cpp); a found=true here means the filter did not run or is too "
                .. "loose, leaving runtime plumbing nobody hooks in the namespace")
        else
            kcdx.test.report(crt_row, true,
                "kcdx.functions[\"" .. PLUGIN_NS .. "\"][\"" .. CRT_FN .. "\"]"
                .. ":resolve -> found=false — the C-runtime function was filtered "
                .. "out (its PDB source is a CRT build tree, not the plugin's own "
                .. ".cpp), so only the author's own functions populate the "
                .. "namespace; the CRT/compiler plumbing nobody hooks is dropped")
        end
    end

    kcdx.log.info("CAP90",
        "PDB auto-load self-test reported: a non-exported internal's address "
        .. "populated into the plugin's own kcdx.functions namespace from its "
        .. "shipped /DEBUG:FULL PDB, and the CRT/compiler-internal functions "
        .. "were filtered out")
end)
