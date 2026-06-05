-- CAP-78 plugin.lua — the PASSIVE boot self-report for the loose-mod-init
-- serve-AND-EXECUTE test. One falsifiable boot row; the EXECUTE
-- proof is the agent's post-launch read of kcd.log for the two markers (a [manual]
-- README row, not a plugin test_name).
--
-- CAP-78-keyed asserts the overlay .lua the serve-AND-EXECUTE test depends on is
-- PRESENT and resolvable through this plugin's own assets/ — the prerequisite a
-- serve needs. It is the boot half; the in-game EXECUTE half (CAP-78-serve-execute)
-- is the agent reading kcd.log for "KCDX_KI6_OVERLAY_SERVED_AND_RAN" (kcdx's
-- overlay ran) vs "KCDX_KI6_MODINIT_RAN" (the original loose file ran) after a
-- boot-into-world.
--
-- This row does NOT report the EXECUTE result — reporting PASS for execution the
-- plugin cannot observe (the marker lands in the game log, not in Lua) would be a
-- non-falsifiable PASS. The plugin reports only what it can falsifiably check at
-- boot; the EXECUTE confirmation stays the agent's log read.

local OVERLAY_ASSET = "scripts/mods/ki6_loose_modinit.lua"  -- the overlay under assets/

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
-- CAP-78-keyed — the overlay .lua the serve-AND-EXECUTE test depends on is
--   PRESENT and resolvable through THIS plugin's own assets/ (a pure read of the
--   calling plugin's assets/ root — the file HOOK 2 will serve when the engine
--   opens scripts/mods/ki6_loose_modinit.lua exists on disk and resolves). The
--   keying-into-the-map half is the agent's `overlay_entry` log grep (named in
--   the matrix row); this Lua row proves the overlay asset is reachable.
--
-- FALSIFIABLE: get_by_path returns nil/empty (the overlay file is absent or did
--   not resolve — the serve could never fire), OR a path that does NOT end with
--   the overlay's relative path (the wrong file resolved) → FAIL. Only a real
--   loadable path for the overlay asset is PASS.
-- ====================================================================
do
    local path, err = kcdx.assets.get_by_path(OVERLAY_ASSET)

    if type(path) ~= "string" or path == "" then
        kcdx.test.report("CAP-78-keyed", false,
            "kcdx.assets.get_by_path(\"" .. OVERLAY_ASSET .. "\") returned "
            .. tostring(path) .. " (err: " .. tostring(err) .. ") — the overlay "
            .. ".lua the serve-AND-EXECUTE test serves is not present / did not "
            .. "resolve under this plugin's assets/. Without the overlay file on "
            .. "disk, HOOK 2 has nothing to serve and the proof cannot run. The "
            .. "agent additionally confirms the sidecar KEYED the overlay by "
            .. "grepping `ASSET_OVERLAY overlay_entry "
            .. "vpath=scripts/mods/ki6_loose_modinit.lua winner=cap_78_loose_modinit`")
    elseif not resolves_to(path, OVERLAY_ASSET) then
        kcdx.test.report("CAP-78-keyed", false,
            "kcdx.assets.get_by_path(\"" .. OVERLAY_ASSET .. "\") returned \""
            .. path .. "\" — a non-nil path, but it does NOT end with the overlay's "
            .. "relative path (" .. OVERLAY_ASSET .. "). The resolver joined the "
            .. "wrong file or root, so the served bytes would not be the overlay")
    else
        kcdx.test.report("CAP-78-keyed", true,
            "kcdx.assets.get_by_path(\"" .. OVERLAY_ASSET .. "\") resolved the "
            .. "overlay .lua to the loadable path \"" .. path .. "\" — the file "
            .. "HOOK 2 serves for scripts/mods/ki6_loose_modinit.lua is present and "
            .. "resolvable. The keying half is the agent's `overlay_entry` grep; the "
            .. "EXECUTE half is the agent's post-launch read of kcd.log for the two "
            .. "markers — KCDX_KI6_OVERLAY_SERVED_AND_RAN (kcdx's overlay ran, serve "
            .. "won) vs KCDX_KI6_MODINIT_RAN (the original loose file ran, HOOK 2 "
            .. "lost the open) — plus kcdx-dev.log for the probe_ki6_lua_open line "
            .. "(CAP-78-serve-execute)")
    end
end

kcdx.log.info("CAP78",
    "passive boot check ran (CAP-78-keyed: the loose-mod-init overlay is present + "
    .. "resolvable). The serve-AND-EXECUTE proof is the agent's post-launch read of "
    .. "kcd.log for the two distinct markers after a boot-into-world "
    .. "(CAP-78-serve-execute, [manual]/in-game) — KCDX_KI6_OVERLAY_SERVED_AND_RAN "
    .. "proves the served overlay chunk EXECUTED (HOOK 2 won the mod-init open); "
    .. "only KCDX_KI6_MODINIT_RAN means the engine ran the original (HOOK 2 lost the "
    .. "open); neither means the mod's init never ran. The probe_ki6_lua_open log in "
    .. "kcdx-dev.log reveals the actual opened vpath + whether the map HIT + served.")
