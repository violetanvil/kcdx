-- kcdx.behavior.log_texture_streaming
--
-- A benign engine-catalog behavior. It reads the game's texture-streaming
-- console variable (`r_TexturesStreaming`) through the kcdx CVar surface and
-- writes what it observed to the log when set. It changes no game state: it is
-- a read-only catalog entry that proves the catalog loader registers a behavior
-- under the reserved `kcdx.behavior.*` root and that its implementation reaches
-- a verified engine value.
--
-- The CVar read goes through `kcdx.cvar.get_int(name)` — the engine resolves
-- the console and the integer accessor from the name alone; no address, offset,
-- or signature crosses to this file. The accessors are verified engine surfaces
-- (KCD2 build release_1_5_1164953_841): `kcdx.cvar.get_int` resolves the console
-- `GetCVar` lookup and the integer-value accessor, both confirmed against the
-- game binary.
--
-- Authored EXACTLY as a plugin would write a declare — a bare name + spec, zero
-- hex. Promotion of a plugin behavior into this catalog is a file move; the
-- declare code is unchanged, only the stamping root differs.

kcdx.behavior.declare("log_texture_streaming", {
    description = "log the game's texture-streaming CVar when set (read-only)",
    default     = false,
    implementation = function(value)
        -- Reading the CVar is the benign effect — a verified engine read,
        -- never a write. nil = the CVar/console is not ready yet (an honest
        -- miss, never a fabricated 0); coalesce so the log line is clean.
        local streaming = kcdx.cvar.get_int("r_TexturesStreaming") or -1
        kcdx.log.info("BEHAVIOR_CATALOG",
            "log_texture_streaming set=" .. tostring(value)
            .. " r_TexturesStreaming=" .. tostring(streaming))
    end,
})
