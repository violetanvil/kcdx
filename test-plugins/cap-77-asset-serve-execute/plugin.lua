-- CAP-77 plugin.lua — the PASSIVE boot self-report for the served-.lua-EXECUTES
-- test. One falsifiable boot row; the EXECUTE proof is the agent's post-launch
-- game-log read of the marker (a [manual] README row, not a plugin test_name).
--
-- CAP-77-keyed asserts the overlay file the EXECUTE test depends on is PRESENT and
-- resolvable through this plugin's own assets/ — the prerequisite a serve needs.
-- It is the boot half; the in-game EXECUTE half (CAP-77-serve-execute) is the
-- agent reading kcd.log for "KCDX_SEAMA_LUA_LOADED cap-77" after a save load.
--
-- This row does NOT report the EXECUTE result — reporting PASS for execution the
-- plugin cannot observe (the marker lands in the game log, not in Lua) would be a
-- non-falsifiable PASS (a row that reports PASS before the behavior under test
-- could fire). The plugin reports only what it can falsifiably check at boot; the
-- EXECUTE confirmation stays the agent's log read.

local OVERLAY_ASSET = "scripts/startup/sl_saveload.lua"  -- the overlay under assets/

local function ends_with(s, suffix)
    return type(s) == "string" and #s >= #suffix
        and s:sub(-#suffix) == suffix
end

-- A loadable disk path uses native separators; compare against both the
-- forward-slash and backslash tail so the row is OS-separator-agnostic.
local function resolves_to(path, rel)
    return ends_with(path, rel) or ends_with(path, (rel:gsub("/", "\\")))
end

-- ====================================================================
-- CAP-77-keyed — the overlay .lua the EXECUTE test depends on is PRESENT
--   and resolvable through THIS plugin's own assets/ (a pure read of the
--   calling plugin's assets/ root — the file that HOOK 2 will serve when
--   the engine opens scripts/startup/sl_saveload.lua exists on disk and
--   resolves). The keying-into-the-map half is the agent's `overlay_entry`
--   log grep (named in the matrix row); this Lua row proves the overlay
--   asset is reachable, the prerequisite for the serve.
--
-- FALSIFIABLE: get_by_path returns nil/empty (the overlay file is absent
--   or did not resolve — the serve could never fire), OR a path that does
--   NOT end with the overlay's relative path (the wrong file resolved)
--   → FAIL. Only a real loadable path for the overlay asset is PASS.
-- ====================================================================
do
    local path, err = kcdx.assets.get_by_path(OVERLAY_ASSET)

    if type(path) ~= "string" or path == "" then
        kcdx.test.report("CAP-77-keyed", false,
            "kcdx.assets.get_by_path(\"" .. OVERLAY_ASSET .. "\") returned "
            .. tostring(path) .. " (err: " .. tostring(err) .. ") — the overlay "
            .. ".lua the EXECUTE test serves is not present / did not resolve "
            .. "under this plugin's assets/. Without the overlay file on disk, "
            .. "HOOK 2 has nothing to serve and the served-EXECUTE proof cannot "
            .. "run. The agent additionally confirms the sidecar KEYED the overlay "
            .. "by grepping `ASSET_OVERLAY overlay_entry "
            .. "vpath=scripts/startup/sl_saveload.lua winner=cap_77_serve_execute`")
    elseif not resolves_to(path, OVERLAY_ASSET) then
        kcdx.test.report("CAP-77-keyed", false,
            "kcdx.assets.get_by_path(\"" .. OVERLAY_ASSET .. "\") returned \""
            .. path .. "\" — a non-nil path, but it does NOT end with the overlay's "
            .. "relative path (" .. OVERLAY_ASSET .. "). The resolver joined the "
            .. "wrong file or root, so the served bytes would not be the overlay")
    else
        kcdx.test.report("CAP-77-keyed", true,
            "kcdx.assets.get_by_path(\"" .. OVERLAY_ASSET .. "\") resolved the "
            .. "overlay .lua to the loadable path \"" .. path .. "\" — the file "
            .. "HOOK 2 serves for scripts/startup/sl_saveload.lua is present and "
            .. "resolvable. The keying half is the agent's `overlay_entry` grep; "
            .. "the EXECUTE half is the agent's post-save-load grep of kcd.log for "
            .. "`KCDX_SEAMA_LUA_LOADED cap-77` (CAP-77-serve-execute)")
    end
end

kcdx.log.info("CAP77",
    "passive boot check ran (CAP-77-keyed: the served-.lua overlay is present + "
    .. "resolvable). The EXECUTE proof is the agent's post-launch read of the game "
    .. "log for the marker `KCDX_SEAMA_LUA_LOADED cap-77` after a save load "
    .. "(CAP-77-serve-execute, [manual]/in-game) — the marker's presence proves "
    .. "the served startup-script chunk EXECUTED; its absence is the falsifiable "
    .. "FAIL whose cause the matrix row's outcome→meaning map names.")
