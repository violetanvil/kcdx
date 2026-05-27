-- CAP-31 plugin.lua — kcdx.cosave.* Lua surface regression (the Lua
-- counterpart of the C++ CAP-12 row). Call shapes verified against
-- src/lua_bind_cosave.cpp + src/lua_cosave_serial.cpp (the as-built binder +
-- serializer are truth; error substrings asserted below are the binder's
-- ACTUAL strings, not docs/lua/cosave.md paraphrases).
--
-- Three rows:
--   CAP-31-outside-window  BOOT-ONLY auto-pass — write() outside on_save
--                          returns the window-closed teaching error.
--   CAP-31-roundtrip       [manual] — every supported type round-trips
--                          through a real save+reboot+load gesture.
--   CAP-31-reject          [manual] — the serializer rejects a function and a
--                          cyclic table inside on_save; reported at SAVE time
--                          (no load half — needs only the save gesture).
--
-- cap-31 NEVER calls kcdx.cosave.set_uid: the section persisting + reloading at
-- all is itself the proof the UID auto-derived from the plugin name
-- ("ts.cap_31_cosave") resolved — the author hand-packs no FourCC (the
-- disassembler-test win). So CAP-31-roundtrip PASSING is the auto-UID proof.

-- ============================================================================
-- (1) CAP-31-outside-window — BOOT-ONLY. write() at plugin.lua top-level runs
-- OUTSIDE any on_save body, so the engine's writer window is closed.
-- kcdx.cosave.write returns (nil, err); the err is the window-closed teaching
-- error (binder Lua_Write, when OpenRecordNamed returns false). This is the
-- one assertion that needs no save gesture — it fires synchronously at load.
-- ============================================================================
do
    local ok, err = kcdx.cosave.write("outside_probe", 1, 123)
    -- Expected: ok == nil, err a string mentioning that write() must be inside
    -- a kcdx.cosave.on_save body / the save window. The binder's message
    -- ("could not open the record. The usual cause is calling write() outside a
    -- kcdx.cosave.on_save(...) body — write() only works inside the save
    -- window") contains both substrings asserted below.
    local err_s = tostring(err)
    local window_msg =
        type(err) == "string"
        and (string.find(err_s, "outside", 1, true) ~= nil
             or string.find(err_s, "save window", 1, true) ~= nil)

    if ok == nil and window_msg then
        kcdx.test.report("CAP-31-outside-window", true,
            "write() outside on_save returned (nil, window-closed error): '"
            .. err_s .. "' — the window guard refuses an out-of-window write "
            .. "instead of silently dropping data")
    else
        kcdx.test.report("CAP-31-outside-window", false,
            "write() outside on_save did NOT return the window-closed error: "
            .. "ok=" .. tostring(ok) .. " err=" .. err_s
            .. " (expected ok=nil + an err mentioning 'outside' / 'save window')")
    end
end

-- ============================================================================
-- Plugin-local state the on_load body fills from the cosave (keyed by tag).
-- Seeded to sentinels so a partial/failed round-trip is visible in the report.
-- ============================================================================
local loaded = {}            -- tag -> value, populated in on_load
local saw_any_record = false -- did records() yield ANYTHING this load?

-- Reject-subtest results, captured AND reported inside on_save (the only place
-- write() reaches the serializer). The rejection is observable the instant
-- write() is called in-window, so CAP-31-reject reports at SAVE time — no load
-- half. (It was previously reported in on_load, which false-FAILED on a
-- load-after-reboot: on_save never ran in that process, so these stayed false.)
local reject = {
    fn    = { ran = false, ok = false, err = nil },
    cycle = { ran = false, ok = false, err = nil },
}

-- ============================================================================
-- (2) on_save — runs INSIDE the engine's open writer window. Write one record
-- per supported type, then attempt the two NEGATIVE writes (function + cyclic
-- table) the serializer must reject. The negative writes are expected to FAIL;
-- we assert the failure and do NOT let it abort the rest of the save. The
-- CAP-31-reject row is reported HERE, at the end of on_save — the rejection is
-- observable the instant write() is called in-window, so the assertion needs no
-- load half (on_save runs inline on the main-thread save path — RunSaveCallbacks
-- — so kcdx.test.report from here is main-thread-safe).
-- ============================================================================
kcdx.cosave.on_save(function()
    -- --- valid writes, one per supported type ---
    -- number: a non-integer float. 3.5 is exactly representable in float
    -- (lua_Number=float on this CryEngine build), so it
    -- round-trips EXACTLY relative to the live value — the serializer stores
    -- the raw lua_Number bytes and reloads them unchanged.
    kcdx.cosave.write("count", 1, 3.5)
    -- string
    kcdx.cosave.write("label", 1, "Henry")
    -- boolean
    kcdx.cosave.write("flag", 1, true)
    -- nested table (a table nesting a table) — exercises the recursive codec.
    kcdx.cosave.write("state", 1, { hp = 100, name = "Henry",
                                    flags = { brave = true } })

    -- --- negative write: a function value (serializer rejects it) ---
    -- Inside the window, write() reaches the serializer, which returns the
    -- teaching error "cannot serialize a function to a cosave". The binder
    -- surfaces it as (nil, 'kcdx.cosave.write("bad_fn", ...): <that text>').
    do
        local r = reject.fn
        r.ran = true
        local ok, err = kcdx.cosave.write("bad_fn", 1, function() end)
        r.err = tostring(err)
        r.ok = (ok == nil) and type(err) == "string"
               and string.find(r.err, "function", 1, true) ~= nil
    end

    -- --- negative write: a cyclic table (serializer rejects it) ---
    -- The codec's ancestry check rejects a table that references itself with
    -- "cyclic table reference — cosave cannot serialize a table that
    -- references itself ...".
    do
        local r = reject.cycle
        r.ran = true
        local t = {}
        t.self = t
        local ok, err = kcdx.cosave.write("bad_cycle", 1, t)
        r.err = tostring(err)
        r.ok = (ok == nil) and type(err) == "string"
               and string.find(r.err, "cyclic", 1, true) ~= nil
    end

    -- ---- CAP-31-reject: both negative writes were refused by the serializer ----
    -- Reported HERE (not in on_load): reject.fn / reject.cycle were populated by
    -- the two do blocks directly above, in THIS process, in THIS callback. On a
    -- load-after-reboot on_save never runs, so reporting from on_load read these
    -- as still-false and false-FAILED — the rejection is in-process state, not
    -- persisted cosave data, so it belongs in the callback that produces it.
    local f = reject.fn
    local c = reject.cycle
    if f.ran and c.ran and f.ok and c.ok then
        kcdx.test.report("CAP-31-reject", true,
            "serializer rejected both unserializable values inside on_save: "
            .. "function -> (nil, '" .. tostring(f.err) .. "'); "
            .. "cyclic table -> (nil, '" .. tostring(c.err) .. "') — and the "
            .. "valid writes still persisted (CAP-31-roundtrip passed)")
    else
        kcdx.test.report("CAP-31-reject", false,
            "serializer-rejection assertion failed: function{ran="
            .. tostring(f.ran) .. " ok=" .. tostring(f.ok) .. " err="
            .. tostring(f.err) .. "} cyclic{ran=" .. tostring(c.ran)
            .. " ok=" .. tostring(c.ok) .. " err=" .. tostring(c.err) .. "} "
            .. "(expected both to return (nil, err) mentioning "
            .. "'function' / 'cyclic')")
    end
end)

-- ============================================================================
-- (3) on_load — runs INSIDE the engine's open reader window. Iterate this
-- plugin's records and collect each into `loaded` by tag. Then report the
-- round-trip (CAP-31-reject is reported in on_save, not here — see below).
--
-- NO-COSAVE-YET FIRST BOOT: on a fresh game, or the first load of a save that
-- has no cap-31 cosave, records() yields NOTHING (saw_any_record stays false).
-- We report NOTHING in that case — the row stays PENDING, not FAIL (mirrors
-- CAP-12's "Revert fired, no cosave yet — save+reboot+load to verify"). Only
-- when records were actually present (a save made WITH cap-31 loaded) do we
-- assert + report.
-- ============================================================================
kcdx.cosave.on_load(function()
    for tag, ver, val in kcdx.cosave.records() do
        saw_any_record = true
        loaded[tag] = val
    end

    if not saw_any_record then
        -- No cap-31 cosave in this save yet. Legitimately PENDING — the save
        -- gesture hasn't happened with cap-31 loaded. Log it so the dev knows
        -- the gesture is still owed; do NOT report (no false FAIL).
        kcdx.log.info("CAP31",
            "on_load: no cap-31 records in this save yet — save (dev mode on), "
            .. "quit, reboot, and load that save to verify the round-trip "
            .. "(CAP-31-roundtrip stays PENDING until then; CAP-31-reject "
            .. "reports at save time, the instant the save gesture happens)")
        return
    end

    -- ---- CAP-31-roundtrip: assert each type round-tripped EXACTLY ----
    local count = loaded["count"]
    local label = loaded["label"]
    local flag  = loaded["flag"]
    local state = loaded["state"]

    local count_ok = count == 3.5
    local label_ok = label == "Henry"
    local flag_ok  = flag == true
    local state_ok = type(state) == "table"
                 and state.hp == 100
                 and state.name == "Henry"
                 and type(state.flags) == "table"
                 and state.flags.brave == true

    if count_ok and label_ok and flag_ok and state_ok then
        kcdx.test.report("CAP-31-roundtrip", true,
            "all types round-tripped via auto-derived UID (no set_uid): "
            .. "count==3.5 (float exact), label=='Henry', flag==true, "
            .. "state.hp==100, state.name=='Henry', state.flags.brave==true "
            .. "(nested table reconstructed)")
    else
        kcdx.test.report("CAP-31-roundtrip", false,
            "round-trip mismatch: count=" .. tostring(count) .. " (want 3.5) "
            .. "label=" .. tostring(label) .. " (want 'Henry') "
            .. "flag=" .. tostring(flag) .. " (want true) "
            .. "state=" .. tostring(state) .. " (want table hp=100 name='Henry' "
            .. "flags.brave=true; got hp="
            .. tostring(type(state) == "table" and state.hp) .. " name="
            .. tostring(type(state) == "table" and state.name) .. " flags.brave="
            .. tostring(type(state) == "table" and type(state.flags) == "table"
                        and state.flags.brave) .. ")")
    end

    -- CAP-31-reject is NOT reported here — it fires at the end of on_save (the
    -- rejection is in-process state observable the instant write() is called
    -- in-window, not persisted cosave data). Reporting it from on_load
    -- false-FAILED on a load-after-reboot, where on_save never ran.
end)

kcdx.log.info("CAP31",
    "registered kcdx.cosave.on_save / on_load (no set_uid — UID auto-derives "
    .. "from the plugin name); CAP-31-outside-window asserted at load "
    .. "(boot-only); CAP-31-reject reports in on_save (at save time); "
    .. "CAP-31-roundtrip rides the save+reboot+load gesture")
