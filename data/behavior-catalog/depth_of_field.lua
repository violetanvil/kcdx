-- kcdx.behavior.depth_of_field
--
-- A console-driven engine-catalog behavior. When set, it toggles the game's
-- depth-of-field console variable (`r_DepthOfField`) by running a console
-- command through the kcdx console surface — `kcdx.console.execute`, backed by
-- the engine's verified `IConsole::ExecuteString`. The author writes one line
-- (set the behavior true/false) and the engine does the work: it resolves the
-- console and applies the command, so no address, offset, or signature crosses
-- to this file.
--
-- `r_DepthOfField` is a known game console variable (the same name you would
-- type after `~`): a non-zero value (`2`) enables the depth-of-field effect,
-- `0` disables it. The default below is `true` (the effect on).
--
-- The implementation is a PURE toggle — it only runs the console command. It
-- reads back nothing and restores nothing; a behavior is a forward toggle, and
-- a consumer that wants the prior value back captures it itself.
--
-- Authored EXACTLY as a plugin would write a declare — a bare name + spec, zero
-- hex. Promotion of a plugin behavior into this catalog is a file move; the
-- declare code is unchanged, only the stamping root differs.

kcdx.behavior.declare("depth_of_field", {
    description = "toggle the depth-of-field effect (r_DepthOfField)",
    default     = true,
    implementation = function(value)
        kcdx.console.execute("r_DepthOfField " .. (value and "2" or "0"))
    end,
})
