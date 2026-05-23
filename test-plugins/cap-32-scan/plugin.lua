-- CAP-32 plugin.lua — kcdx.scan diagnostic-AOB-scan regression.
--
-- kcdx.scan{ name=, pattern=, module?, offset?, ... } is a TOP-LEVEL Lua verb
-- that runs the diagnostic scan and ALWAYS RETURNS a table on a resolved scan:
--   { count   = <int, pattern match count>,
--     matches = { { addr=<kcdx.memory.pointer>, module=<string>,
--                   offset=<int, module-relative> }, ... },
--     addr    = <first match's addr pointer, or nil when count==0> }
-- It returns (nil, err) ONLY on bad INPUT (non-table / missing name|pattern /
-- parse failure). A no-match and a not-loaded module are count==0 RESULTS,
-- never a nil return.
--
-- All three rows resolve + report synchronously at plugin load (kcdx.scan
-- returns immediately) — boot-only, no event wait, no player gesture.

-- ====================================================================
-- (1) CAP-32-resolve — a KNOWN-UNIQUE pattern resolves to count==1.
-- ====================================================================
-- Reuses scan-demo/kcdx.toml's LIVE-PROVEN [[scan]] pattern + module: the
-- outfit-swap site, documented there as "pattern matches: 1". Resting the
-- resolve assertion on an already-verified site (not a fresh RE guess) is the
-- point. We omit `offset` (default 0): matches[1].offset is then the hit's
-- own module-relative offset. We do NOT assert a specific absolute address —
-- that shifts per game build; count/module/non-nil-addr are the falsifiable
-- resolve proof.
do
    local r = kcdx.scan{
        name    = "cap32_outfit_swap",
        pattern = "48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0",
        module  = "WHGame.dll",
    }

    if type(r) ~= "table" then
        kcdx.test.report("CAP-32-resolve", false,
            "kcdx.scan returned " .. type(r) .. " (expected a table) for the "
            .. "known-unique outfit-swap pattern — a resolved scan is ALWAYS "
            .. "a table, never nil")
    else
        local m1 = r.matches and r.matches[1]
        local ok = r.count == 1
               and r.addr ~= nil
               and m1 ~= nil
               and m1.module == "WHGame.dll"
               and m1.addr ~= nil
               and type(m1.offset) == "number"

        if ok then
            kcdx.test.report("CAP-32-resolve", true,
                "known-unique pattern resolved: count==1, addr~=nil, "
                .. "matches[1].module==\"WHGame.dll\", matches[1].addr~=nil, "
                .. "matches[1].offset==" .. tostring(m1.offset)
                .. " (a number, module-relative — not asserting a per-build "
                .. "absolute address)")
        else
            kcdx.test.report("CAP-32-resolve", false,
                "known-unique resolve mismatch: count=" .. tostring(r.count)
                .. " (want 1) addr=" .. tostring(r.addr) .. " (want non-nil) "
                .. "matches[1]=" .. tostring(m1)
                .. " module=" .. tostring(m1 and m1.module)
                .. " (want \"WHGame.dll\") matches[1].addr="
                .. tostring(m1 and m1.addr) .. " (want non-nil) offset type="
                .. tostring(m1 and type(m1.offset)) .. " (want number)")
        end
    end
end

-- ====================================================================
-- (2) CAP-32-nomatch — a bogus pattern resolves to the always-a-table
--     count==0 contract (NOT a nil return / NOT an error).
-- ====================================================================
do
    local r = kcdx.scan{
        name    = "cap32_nomatch",
        pattern = "DE AD BE EF DE AD BE EF DE AD BE EF DE AD BE EF DE AD BE EF",
        module  = "WHGame.dll",
    }

    local ok = type(r) == "table"
           and r.count == 0
           and r.addr == nil
           and r.matches ~= nil
           and #r.matches == 0

    if ok then
        kcdx.test.report("CAP-32-nomatch", true,
            "bogus pattern returned the always-a-table contract: "
            .. "type(r)==table, count==0, addr==nil, #matches==0 "
            .. "(no-match is a count==0 RESULT, not a nil return / not an error)")
    else
        kcdx.test.report("CAP-32-nomatch", false,
            "no-match contract mismatch: type(r)=" .. type(r)
            .. " (want table) count=" .. tostring(r and r.count) .. " (want 0) "
            .. "addr=" .. tostring(r and r.addr) .. " (want nil) #matches="
            .. tostring(r and r.matches and #r.matches) .. " (want 0)")
    end
end

-- ====================================================================
-- (3) CAP-32-badinput — bad input returns (nil, err), NOT a table.
-- ====================================================================
-- kcdx.scan{} is a table but missing the required `name`+`pattern`. The binder
-- checks `name` first (src/lua_bind_scan.cpp Lua_Scan, the `name` required
-- branch) and returns (nil, err) with err naming the `name` field:
--   "kcdx.scan{...}: `name` (string) is required ..."
-- Assert r==nil, err is a string, and err contains the literal substring
-- "name" (string.find with plain=true so the backtick-quoted `name` matches).
do
    local r, err = kcdx.scan({})

    local ok = r == nil
           and type(err) == "string"
           and string.find(err, "name", 1, true) ~= nil

    if ok then
        kcdx.test.report("CAP-32-badinput", true,
            "missing-field call returned (nil, err): r==nil, err is a string, "
            .. "and err names the missing `name` field "
            .. "(string.find(err, \"name\") matched) — bad input is (nil, err), "
            .. "NOT a table")
    else
        kcdx.test.report("CAP-32-badinput", false,
            "bad-input contract mismatch: r=" .. tostring(r) .. " (want nil) "
            .. "err type=" .. type(err) .. " (want string) err="
            .. tostring(err) .. " (want a string naming the missing `name` field)")
    end
end

kcdx.log.info("CAP32",
    "kcdx.scan self-test ran synchronously at load: CAP-32-resolve "
    .. "(known-unique outfit-swap pattern -> count==1), CAP-32-nomatch "
    .. "(bogus pattern -> always-a-table count==0), CAP-32-badinput "
    .. "(kcdx.scan{} -> (nil, err) naming `name`)")
