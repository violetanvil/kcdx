-- CAP-30 plugin.lua — kcdx.code Lua verb regression.
--
-- Proves three things about kcdx.code, all boot-only:
--   (1) ALLOCATION + LIVE WRITABLE REGION (CAP-30-alloc, synchronous at load):
--       kcdx.code allocates immediately and returns a live kcdx.memory.pointer
--       userdata. We write a known byte + dword in via the returned pointer
--       and read them back — the round-trip is unforgeable proof the region is
--       real, allocated, and writable (a nil/non-writable region fails the
--       read-back). Pointer methods used: :set_byte/:get_byte (uint8) and
--       :set_dword/:get_dword (uint32) take the VALUE only (no offset arg);
--       to operate at an offset use :add(N), which returns a NEW pointer N
--       bytes along — the real names from src/lua_bind_pointer.cpp (kMethods).
--   (2) NOP-PAD (folded into CAP-30-alloc): a region allocated with
--       size > #bytes NOP-fills the unused tail (0x90). We allocate
--       { bytes="C3", size=8 } (1 code byte + 7 NOP) and assert a tail byte
--       reads 0x90.
--   (3) EXPORT-SYMBOL INTERLOCK (CAP-30-export, asserted at kcdx.on("ready")):
--       kcdx.code{ export=... } publishes the address as a symbol; a deferred
--       kcdx.bytes{ target_symbol=... } resolves it at the apply pass and
--       :applied()==true proves the export resolved to the live region.
--
-- Pointer values cross the boundary as a userdata, never a raw number
-- (lua-precision.md) — so we hold the region pointer and call methods on it;
-- we never round-trip an address through a Lua number.

local EXPORT_SYMBOL = "kcdx.cap-30-lua-code.region"

-- ====================================================================
-- (1)+(2) ALLOCATION + WRITE/READ ROUND-TRIP + NOP-PAD — synchronous.
-- kcdx.code allocates IMMEDIATELY at the call and returns a live pointer,
-- so this whole block runs and reports at plugin load (no event wait).
-- ====================================================================
do
    -- A 64-byte region (size only, no initial bytes). The whole region is
    -- NOP-padded (0x90) by the engine, but we overwrite the head ourselves.
    local region = kcdx.code{ name = "cap30_region", size = 64 }

    if region == nil then
        kcdx.test.report("CAP-30-alloc", false,
            "kcdx.code{ name=\"cap30_region\", size=64 } returned nil — "
            .. "no region allocated (expected a live kcdx.memory.pointer)")
    else
        -- Write a known byte + dword via the returned pointer, then read
        -- them back. The accessors take the VALUE only (no offset arg); to
        -- write at an offset, navigate with :add(N) (a NEW pointer N bytes
        -- along). Distinct, non-overlapping slots so the writes don't clobber:
        --   byte  at base    (offset 0)
        --   dword at offset 4 (clear of the byte at 0)
        -- If the pointer is a fake / the region is not writable, the read-back
        -- mismatches and the row FAILs loudly.
        region:set_byte(0xAB)               -- value-only, at base (offset 0)
        region:add(4):set_dword(0xDEADBEEF) -- value-only, at offset 4

        local got_byte  = region:get_byte()         -- reads base (offset 0)
        local got_dword = region:add(4):get_dword() -- reads offset 4

        -- A bytes+size region to prove the NOP-pad: 1 code byte (C3 = RET)
        -- at the head, the remaining 7 bytes NOP-filled (0x90). Read the head
        -- byte (at base) and a tail byte (offset 4, well inside the padded
        -- tail, via :add(4)) and assert head==0xC3, tail==0x90.
        local nop_region = kcdx.code{ name = "cap30_nop", bytes = "C3", size = 8 }
        local head_byte  = nop_region and nop_region:get_byte()        -- C3 (code, base)
        local tail_byte  = nop_region and nop_region:add(4):get_byte() -- 90 (pad, offset 4)

        local ok = got_byte == 0xAB
               and got_dword == 0xDEADBEEF
               and nop_region ~= nil
               and head_byte == 0xC3
               and tail_byte == 0x90

        if ok then
            kcdx.test.report("CAP-30-alloc", true,
                "kcdx.code allocated a live writable region: "
                .. "set_byte(0xAB)/get_byte==0xAB AND "
                .. "add(4):set_dword(0xDEADBEEF)/add(4):get_dword==0xDEADBEEF "
                .. "round-tripped; NOP-pad ok (bytes=\"C3\" size=8: "
                .. "head=0xC3, tail[4]=0x90)")
        else
            kcdx.test.report("CAP-30-alloc", false,
                "kcdx.code region round-trip / NOP-pad mismatch: "
                .. "get_byte()=" .. tostring(got_byte) .. " (want 171/0xAB) "
                .. "add(4):get_dword()=" .. tostring(got_dword)
                .. " (want 3735928559/0xDEADBEEF) "
                .. "nop head=" .. tostring(head_byte) .. " (want 195/0xC3) "
                .. "nop tail[4]=" .. tostring(tail_byte) .. " (want 144/0x90)")
        end
    end
end

-- ====================================================================
-- (3) EXPORT-SYMBOL INTERLOCK — the cross-feature proof.
-- ====================================================================
-- kcdx.code{ export=... } registers the symbol IMMEDIATELY at the call
-- (symbols::Register). A later kcdx.bytes{ target_symbol=... } is DEFERRED to
-- the apply pass, where it resolves the export (symbols::Lookup) to the live
-- region and writes into it. We assert the kcdx.bytes handle :applied()==true
-- at kcdx.on("ready") — proving target_symbol RESOLVED the export.
--
-- The write is a NOP-over-NOP, same-length, idempotent: the whole region is
-- already NOP-padded (0x90), so writing "90" over offset 0 is a no-op-valued
-- but genuine write that applies because the region is real writable memory.
-- That is what makes :applied()==true a REAL proof (not "symbol not found",
-- not a bad-target failure) — the only way it applies is if target_symbol
-- resolved the export to the allocated address.
local exported = kcdx.code{
    name   = "cap30_exported",
    size   = 16,
    export = EXPORT_SYMBOL,
}

local consumer = nil
if exported == nil then
    -- Allocation/export failed up front (e.g. an export collision). Surface
    -- it now; the ready handler will report FAIL since `consumer` stays nil.
    kcdx.log.error("CAP30",
        "kcdx.code{ export=\"" .. EXPORT_SYMBOL .. "\" } returned nil — "
        .. "the exported region was not allocated; export interlock cannot "
        .. "be proven this boot")
else
    consumer = kcdx.bytes{
        name          = "cap30_export_consumer",
        target_symbol = EXPORT_SYMBOL,   -- resolves the kcdx.code export
        original      = "90",            -- the region is NOP-padded
        replacement   = "90",            -- NOP over NOP: same length, idempotent
    }
    if consumer == nil then
        kcdx.log.error("CAP30",
            "kcdx.bytes{ target_symbol=\"" .. EXPORT_SYMBOL .. "\" } returned "
            .. "nil at registration — bad call (not a resolution result); "
            .. "export interlock cannot be proven this boot")
    end
end

-- kcdx.bytes is DEFERRED; :applied() is nil in straight-line code. Read the
-- final status from kcdx.on("ready") (fires after the apply pass).
kcdx.on("ready", function()
    if consumer == nil then
        kcdx.test.report("CAP-30-export", false,
            "no kcdx.bytes consumer registered against the export — the "
            .. "exported region or the consumer registration failed (see the "
            .. "CAP30 error log above); export interlock not proven")
        return
    end

    local applied = consumer:applied()
    if applied == true then
        kcdx.test.report("CAP-30-export", true,
            "export interlock ok: kcdx.bytes{ target_symbol=\""
            .. EXPORT_SYMBOL .. "\" } :applied()==true — target_symbol "
            .. "resolved the kcdx.code export to the live region")
    else
        kcdx.test.report("CAP-30-export", false,
            "export interlock FAILED: kcdx.bytes{ target_symbol=\""
            .. EXPORT_SYMBOL .. "\" } :applied()=" .. tostring(applied)
            .. " reason=" .. tostring(consumer:reason())
            .. " (expected true — the export should resolve to the live "
            .. "allocated region)")
    end
end)

kcdx.log.info("CAP30",
    "allocated cap30_region (write/read round-trip + NOP-pad asserted "
    .. "synchronously) and cap30_exported (export=" .. EXPORT_SYMBOL
    .. "); export interlock asserted via kcdx.bytes{ target_symbol } at ready")
