-- PROBE (KI-0002) plugin.lua — scan-returns-0-at-input_loaded timing isolator.
--
-- The 2×2 the existing fixtures left half-empty (pattern × timing):
--
--                       | plugin-load           | input_loaded
--   luaL_openlibs AOB   | (CELL A — this probe) | (CELL B — this probe)
--   outfit-swap AOB     | cap-32 = 1 (known)    | (CELL C — this probe)
--
-- cap-70-result observed CELL B = 0. cap-32 observed outfit-swap@load = 1. No
-- one has observed CELL A (the SAME failing pattern at the WORKING timing) or
-- CELL C (the working pattern at the failing timing). This probe fills A, B, C
-- with the SAME kcdx.scan{} seam cap-32/cap-70-result use, so one launch tells
-- which variable owns the 0.
--
-- THEORY-INDEPENDENT: each cell logs the RAW count flat (no "expected"). The
-- outcome map (pre-committed in the KI doc) has an outcome that FALSIFIES the
-- "timing" theory: if CELL A == 0, the same pattern fails at the working
-- timing too → it is NOT timing, it is the target-RVA/site.
--
-- Reporting convention: report PASS with the observed count in the reason
-- string. PASS here means "the cell ran and produced an observation" — this is
-- a PROBE, not an assertion; the VALUE in the reason is the finding the agent
-- reads, not the pass/fail bit. (A genuine resolve error — kcdx.scan returning
-- non-table — reports FAIL so a broken seam is visible.)

local OPENLIBS = "48 89 5C 24 08 57 48 83 EC 20 48 8B F9 48 8D 1D"  -- luaL_openlibs entry AOB (cap-70 site)
local OUTFIT   = "48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0"  -- outfit-swap AOB (cap-32 site)

-- Run one scan cell, log + report the raw count. Returns the count (or -1 on a
-- non-table resolve error).
local function cell(report_name, scan_name, pattern, when_label)
    local r = kcdx.scan{ name = scan_name, pattern = pattern, module = "WHGame.dll" }
    if type(r) ~= "table" then
        kcdx.log.error("KI2PROBE",
            scan_name .. " @" .. when_label .. ": kcdx.scan returned "
            .. type(r) .. " (expected a table) — resolve seam broken")
        kcdx.test.report(report_name, false,
            "RESOLVE ERROR: kcdx.scan returned " .. type(r)
            .. " (not a table) for " .. scan_name .. " @" .. when_label)
        return -1
    end
    local count = r.count
    local off = r.matches and r.matches[1] and r.matches[1].offset
    kcdx.log.info("KI2PROBE",
        scan_name .. " @" .. when_label .. ": count=" .. tostring(count)
        .. " offset=" .. tostring(off))
    kcdx.test.report(report_name, true,
        "OBSERVATION " .. scan_name .. " @" .. when_label .. ": count="
        .. tostring(count) .. " offset=" .. tostring(off)
        .. " (raw observation, not an assertion)")
    return count
end

-- CELL A — luaL_openlibs AOB at PLUGIN LOAD (the failing pattern at the working
-- timing). This is the falsifying cell: if it returns 0, the bug is NOT timing.
cell("PRB-KI2-openlibs-load", "ki2_openlibs_load", OPENLIBS, "plugin-load")

-- CELL C — outfit-swap AOB at INPUT_LOADED (the working pattern at the failing
-- timing). NOTE: cap-39 rewrites this site's tail (44 8A F0 -> 45 31 F6) at the
-- apply pass, which runs BEFORE input_loaded. So a 0 here is EXPECTED-by-rewrite
-- and does NOT indict timing — it is logged to keep the 2×2 complete and to
-- confirm the rewrite explanation (a 0 here with CELL B also 0 but CELL A == 1
-- would mean timing; a 0 here alongside CELL A == 1 and CELL B == 1 would mean
-- this cell's 0 is purely the rewrite). Kept flat; the KI map interprets it.
-- CELL B — luaL_openlibs AOB at INPUT_LOADED (reproduces cap-70-result's 0
-- through THIS probe's seam, confirming the bug reproduces in isolation).
kcdx.on("input_loaded", function()
    cell("PRB-KI2-openlibs-input", "ki2_openlibs_input", OPENLIBS, "input_loaded")
    cell("PRB-KI2-outfit-input",   "ki2_outfit_input",   OUTFIT,   "input_loaded")
end)

kcdx.log.info("KI2PROBE",
    "registered KI-0002 timing isolator: luaL_openlibs AOB scanned at "
    .. "plugin-load (CELL A) + input_loaded (CELL B), outfit-swap AOB at "
    .. "input_loaded (CELL C). Reads the KI-0002 probe-plan outcome map.")
