-- CAP-04 plugin.lua — mid-hook (kcdx.hook mode=mid) on kcdx.code memory.
--
-- The novel composition under test: allocate an executable stub with the
-- author-facing kcdx.code verb, then mid-hook INTO that allocation with
-- kcdx.hook mode=mid. cap-21 hooks a stub from the C++ raw branch-pool floor;
-- cap-30/cap-40 allocate via kcdx.code but never hook the result. Here both
-- author surfaces compose: kcdx.code allocate -> kcdx.hook mid on the
-- allocation -> (the C++ companion) call it to observe the mid took effect.
--
-- The 9-byte stub (verbatim from legacy cap-04, verified-safe self-contained):
--   +0:  48 89 C8        mov rax, rcx     ; rax = seed (arg in rcx, win64)
--   +3:  48 83 C0 64     add rax, 0x64    ; rax += 100   <-- MID HOOK at +3
--   +7:  90              nop              ; padding for MinHook's 5-byte patch
--   +8:  C3              ret
--
-- For each sub-test we allocate a FRESH stub (distinct region -> distinct VA,
-- independent detours) and publish its base via export= so the C++ companion
-- can resolve the call address. size=16 (> the 9 code bytes) leaves NOP-padded
-- tail room well past the +3..+7 5-byte patch site. pool="branch" is REQUIRED:
-- the mid detour writes a rel32 jmp into the region, which must sit within
-- ±2 GB of WHGame.dll's .text for the branch to reach.
--
-- The hook target is `region:add(3)` — a kcdx.memory.pointer userdata pointing
-- AT the `add rax,0x64` instruction. kcdx.hook{ address = <pointer> } accepts a
-- pointer userdata as the locator (the exact NOVEL surface: a mode=mid hook
-- whose address comes from a kcdx.code pointer, not a C++-handed lightuserdata).

local STUB_BYTES = "48 89 C8 48 83 C0 64 90 C3"  -- 9 bytes; mov/add/nop/ret
local STUB_SIZE  = 16   -- > 9, NOP-pads the tail past the +3..+7 patch site
local MID_OFFSET = 3    -- the `add rax,0x64` capture site

-- Allocate a fresh stub, publish its base as `export`, return the base pointer.
-- Returns nil on a bad kcdx.code call (the row then reports via the companion's
-- ResolveSymbolAs miss / InputLoaded backstop).
local function alloc_stub(name, export)
    local region, err = kcdx.code{
        name   = name,
        bytes  = STUB_BYTES,
        size   = STUB_SIZE,
        pool   = "branch",   -- REQUIRED: rel32 reach for the mid jmp
        export = export,     -- bare; engine stamps ts.cap_04_midhook prefix
    }
    if region == nil then
        kcdx.log.error("CAP04",
            "kcdx.code{ name=\"" .. name .. "\" } returned nil: "
            .. tostring(err) .. " — stub not allocated; the companion's "
            .. "ResolveSymbolAs miss reports FAIL for this row")
    end
    return region
end

-- CAP-04-mid-on-code-run: allocate stub via kcdx.code, mid-hook +3 capturing
-- rax, callback returns NOTHING -> the captured `add` runs. The companion calls
-- the stub with seed=10 and asserts 110 (the add took effect on allocated code).
do
    local region = alloc_stub("cap04_stub_run", "stub_run")
    if region ~= nil then
        kcdx.hook{
            name     = "cap04_mid_run",
            address  = region:add(MID_OFFSET),   -- kcdx.code pointer -> the `add`
            captures = { "rax" },
            mid      = function(c)
                -- rax holds seed (10) at the capture site, before `add rax,0x64`.
                assert(c[1]:get() == 10,
                    "CAP-04-mid-on-code-run: expected rax==10, got "
                    .. tostring(c[1]:get()))
                -- return nothing -> the captured `add` runs (10 -> 110)
            end,
        }
    end
end

-- CAP-04-mid-on-code-skip: same allocation, callback returns "skip" -> the
-- captured `add` NEVER runs. The companion calls the stub with seed=10 and
-- asserts 10 (the add was skipped on allocated code). This proves the
-- skip-original codegen reaches a kcdx.code-allocated target, not just the
-- C++-floor stub cap-21 uses.
do
    local region = alloc_stub("cap04_stub_skip", "stub_skip")
    if region ~= nil then
        kcdx.hook{
            name     = "cap04_mid_skip",
            address  = region:add(MID_OFFSET),
            captures = { "rax" },
            mid      = function(c)
                assert(c[1]:get() == 10,
                    "CAP-04-mid-on-code-skip: expected rax==10, got "
                    .. tostring(c[1]:get()))
                return "skip"   -- the `add rax,0x64` is skipped (10 stays 10)
            end,
        }
    end
end

kcdx.log.info("CAP04",
    "allocated 2 kcdx.code stubs (export=stub_run/stub_skip) and installed "
    .. "mode=mid hooks at +3; the C++ companion calls each on InputLoaded")
