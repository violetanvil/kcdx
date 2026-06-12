-- kcdx.behavior.show_compass
--
-- A console-driven engine-catalog behavior. When set, it toggles the game's
-- HUD compass console variable (`wh_ui_showCompass`) by running a console
-- command through the kcdx console surface — `kcdx.console.execute`, backed by
-- the engine's verified `IConsole::ExecuteString`. The author writes one line
-- (set the behavior true/false) and the engine does the work: it resolves the
-- console and applies the command, so no address, offset, or signature crosses
-- to this file.
--
-- `wh_ui_showCompass` is a known game console variable (the same name you would
-- type after `~`): `1` shows the HUD compass, `0` hides it. The default below
-- is `true` (the compass shown).
--
-- The implementation is a PURE toggle — it only runs the console command. It
-- reads back nothing and restores nothing; a behavior is a forward toggle, and
-- a consumer that wants the prior value back captures it itself.
--
-- Authored EXACTLY as a plugin would write a declare — a bare name + spec, zero
-- hex. Promotion of a plugin behavior into this catalog is a file move; the
-- declare code is unchanged, only the stamping root differs.

kcdx.behavior.declare("show_compass", {
    description = "toggle the HUD compass (wh_ui_showCompass)",
    default     = true,
    implementation = function(value)
        kcdx.console.execute("wh_ui_showCompass " .. (value and "1" or "0"))
    end,
})
