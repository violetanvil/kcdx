-- PROBE helper.lua (THROWAWAY) — the SIBLING module require("helper")
-- must resolve and load from THIS plugin's own folder.
--
-- If require resolves and runs this file, plugin.lua gets this table back
-- and can read marker == "PROBE_HELPER_OK" to confirm it actually loaded
-- the sibling (not some other "helper" on the path, not a stale stub).
-- The marker string is the sentinel: greppable, unambiguous proof that
-- THIS file is what require returned.

return { loaded = true, marker = "PROBE_HELPER_OK" }
