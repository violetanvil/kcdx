-- CAP-78 overlay .lua — the SERVE-AND-EXECUTE marker.
--
-- This file is served by HOOK 2 (kcdx's own CRT FILE*) wherever the engine opens
-- the mod-init vpath scripts/mods/ki6_loose_modinit.lua. The paired fixture mod
-- ships the SAME vpath LOOSE; the
-- engine runs scripts/mods/<modid>.lua for that mod right after scripts/main.lua.
-- The CHECKABLE UNKNOWN: does the engine's mod-init open of that loose file route
-- through HOOK 2's FOpen seam (so kcdx serves THIS overlay instead), AND do the
-- served bytes EXECUTE?
--
-- The DISTINCT marker is the whole point: this overlay emits
-- KCDX_KI6_OVERLAY_SERVED_AND_RAN; the original loose file emits
-- KCDX_KI6_MODINIT_RAN. Which marker reaches kcd.log tells us, theory-independently,
-- whether HOOK 2's serve won the open (this marker) or the engine ran the original
-- (the other marker). Its PRESENCE proves kcdx's served bytes EXECUTED.
--
-- The marker is emitted at FILE SCOPE so it runs the instant the chunk executes.
-- System.LogAlways is the engine's game-script logging API (writes to kcd.log);
-- guarded so the chunk still emits the marker through whatever logging global is
-- bound when a mod-init script runs, rather than raising a Lua error.

local MARKER = "KCDX_KI6_OVERLAY_SERVED_AND_RAN"

if type(System) == "table" and type(System.LogAlways) == "function" then
    System.LogAlways(MARKER)
elseif type(Log) == "function" then
    Log(MARKER)
elseif type(print) == "function" then
    print(MARKER)
end
