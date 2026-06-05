-- PROBE A (KI-0006) — the ORIGINAL loose mod-init script.
--
-- This file is LOOSE (it is NOT inside a .pak), placed at the mod-init vpath
-- scripts/mods/ki6_loose_modinit.lua. The engine runs scripts/mods/<modid>.lua
-- for every installed mod right after scripts/main.lua (wiki KM-A-3); a LOOSE
-- .lua opens via CCryFile::Open -> ICryPak::FOpen slot 36 — HOOK 2's exact lane
-- (recon F2). So this is the vehicle whose open the kcdx overlay can win.
--
-- Its file scope writes KCDX_KI6_MODINIT_RAN to the game log (kcd.log). This is
-- the BASELINE marker: it proves the engine runs this mod's loose init at all.
-- The DISTINCT overlay marker (KCDX_KI6_OVERLAY_SERVED_AND_RAN) lives in the
-- kcdx plugin's overlay copy of this same vpath. Which marker reaches kcd.log
-- tells us whether HOOK 2's serve won the open (overlay marker) or the engine
-- ran this original file (this marker) — the theory-independent observation.
--
-- System.LogAlways is the engine's game-script logging API (writes to kcd.log);
-- guarded so the chunk still emits its marker through whatever logging global is
-- bound when a startup script runs, rather than raising a Lua error.

local MARKER = "KCDX_KI6_MODINIT_RAN ki6_loose_modinit"

if type(System) == "table" and type(System.LogAlways) == "function" then
    System.LogAlways(MARKER)
elseif type(Log) == "function" then
    Log(MARKER)
elseif type(print) == "function" then
    print(MARKER)
end
