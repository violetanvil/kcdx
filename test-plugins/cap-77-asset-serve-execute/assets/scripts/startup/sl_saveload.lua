-- CAP-77 overlay .lua — the EXECUTE marker for the handle-consumed serve test.
--
-- This file is served by HOOK 2 (kcdx's own CRT FILE*) wherever the engine opens
-- the vpath scripts/startup/sl_saveload.lua. The test is whether the engine
-- actually RE-RUNS this startup chunk on a save load: if it does, this file scope
-- executes and writes the marker to the game log (kcd.log) via the engine's own
-- script-runtime logging API. The marker's PRESENCE proves the served bytes ran.
--
-- The marker string is the agent's falsifiable observable: it greps the game log
-- for "KCDX_SEAMA_LUA_LOADED cap-77" after the save-load gesture. Its ABSENCE is
-- a FAIL whose cause the CAP-77-serve-execute row's outcome→meaning map names
-- (the script not re-run on this load → re-vehicle to a peer; or the overlay did
-- not serve → check the resolver HIT line).
--
-- The marker is emitted at FILE SCOPE (so it runs the instant the chunk is
-- executed, mirroring the recon's proven vehicle). System.LogAlways is the
-- engine's game-script logging API that writes to the game log; it is guarded so
-- that, on the rare load where the game-script System global is not yet bound when
-- this chunk runs, the chunk still writes the marker through whatever logging
-- global IS available rather than raising a Lua error inside a startup script.

local MARKER = "KCDX_SEAMA_LUA_LOADED cap-77"

if type(System) == "table" and type(System.LogAlways) == "function" then
    System.LogAlways(MARKER)
elseif type(Log) == "function" then
    Log(MARKER)
elseif type(print) == "function" then
    print(MARKER)
end
