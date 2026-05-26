-- CAP-01 — byte rewrite via kcdx.bytes (Phase 4a pilot migration).
--
-- This is the FIRST plugin migrated off the legacy [[patch]] TOML path
-- onto the pure-Lua kcdx.bytes surface. Same site, same observable, new
-- mechanism:
--   * was: a [[patch]] block in kcdx.toml + a companion verifier DLL
--     (cap-01.cpp) that re-scanned WHGame.dll on kInputLoaded.
--   * now: this plugin.lua registers the rewrite with kcdx.bytes{...} and
--     self-verifies entirely in Lua.
--
-- The rewrite is identical to the [[patch]] one it replaces: at the
-- outfit-swap site, `mov r14b, al` (44 8A F0) -> `xor r14d, r14d`
-- (45 31 F6). original= gives the apply pass its pre-write byte check;
-- idempotent= lets a second boot skip cleanly (the replacement already
-- there). Same 23-byte context disambiguates the locator.
--
-- TWO falsifiable signals, both asserted at kcdx.on("ready") (after the
-- engine's deferred apply pass has run):
--   1. h:applied()==true. The kcdx.bytes apply pass routes through
--      patch::ApplyPatch, which resolves the locator, VERIFIES the
--      original bytes (44 8A F0) match before writing, handles the
--      idempotent-skip, and writes. A wrong locator / original-mismatch
--      leaves applied()==false with a :reason(). So applied()==true alone
--      already proves resolve+match+write.
--   2. Independent read-back (belt-and-suspenders, matches the old DLL's
--      independent check): scan WHGame.dll for the POST-rewrite 23-byte
--      context and read the 3 rewritten bytes back via the returned
--      pointer userdata. The rewrite site is the last 3 bytes of the
--      context (offsets 20/21/22), so the post-patch context will only
--      match if the bytes really are 45 31 F6 — and we read them to be
--      sure.

local POST_CONTEXT =
    "48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 45 31 F6"

local h = kcdx.bytes{
    name        = "cap_01_outfit_swap_rewrite",
    module      = "WHGame.dll",
    pattern     = "48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0",
    offset      = 13,
    original    = "44 8A F0",
    replacement = "45 31 F6",
    idempotent  = true,
    context     = "48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0",
}

if not h then
    kcdx.log.error("CAP01",
        "kcdx.bytes returned nil at registration — the rewrite never "
        .. "registered (parse/locator failure)")
    kcdx.test.report("CAP-01", false,
        "kcdx.bytes returned nil at registration (parse/locator failure) — "
        .. "the rewrite never queued for the apply pass")
    return
end

kcdx.on("ready", function()
    local applied = h:applied()
    local reason  = h:reason()

    if applied ~= true then
        kcdx.test.report("CAP-01", false,
            string.format(
                "kcdx.bytes did NOT apply: applied()=%s, reason=%q. The "
                .. "apply pass rejected the locator or the original-byte "
                .. "check (44 8A F0) failed.",
                tostring(applied), tostring(reason)))
        return
    end

    -- Belt-and-suspenders independent read-back. Scan for the POST-rewrite
    -- context; r.addr points at its first byte. The 3 rewritten bytes are
    -- the last 3 of the 23-byte context -> offsets 20/21/22.
    local r = kcdx.scan{ name = "cap_01_readback", pattern = POST_CONTEXT }
    if not r or r.count ~= 1 or not r.addr then
        kcdx.test.report("CAP-01", false,
            string.format(
                "applied()==true but post-rewrite context did not resolve "
                .. "uniquely (count=%s). Cannot confirm the bytes at the "
                .. "site independently.",
                tostring(r and r.count)))
        return
    end

    local b0 = r.addr:add(20):get_byte()
    local b1 = r.addr:add(21):get_byte()
    local b2 = r.addr:add(22):get_byte()
    local match = (b0 == 0x45 and b1 == 0x31 and b2 == 0xF6)

    kcdx.test.report("CAP-01", match,
        string.format(
            "kcdx.bytes pilot: applied()=%s reason=%q; read-back at "
            .. "post-rewrite site = %02X %02X %02X (expected 45 31 F6). "
            .. "Phase 4a migration off legacy [[patch]] — same site, same "
            .. "observable, kcdx.bytes mechanism.",
            tostring(applied), tostring(reason), b0, b1, b2))
end)
