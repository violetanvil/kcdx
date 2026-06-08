-- CAP-21 plugin.lua — installs the kcdx.hook mode="mid" hooks the
-- companion DLL verifies on InputLoaded. This is the surface under test:
-- the new mid-function capture hook (capture read/write via :get()/:set(),
-- run-vs-skip via the callback's return value).
--
-- Each stub's CAPTURE-SITE address comes from the DLL via
-- kcdx.cap21.addr_*() (an exact lightuserdata pointing AT the `add rax`
-- instruction). mode=mid takes `captures` (not a `signature`): the
-- register/memory values to read/write at the site. We capture `rax`,
-- which holds `seed` just before the add executes.
--
-- Capture forms (both exercised below):
--   name map:        captures = { rax = "rax" }   -> c.rax handle
--   positional list: captures = { "rax" }         -> c[1] handle
-- The callback gets a table of capture HANDLES, each with :get()/:set().

local cap21 = kcdx.cap21   -- DLL-registered capture-site accessors

-- CAP-21-read: read the capture (assert rax==seed==10), let the add run.
-- Name-map form -> handle keyed by name (c.rax).
kcdx.hook{
    name     = "cap21_read",
    address  = cap21.addr_read(),
    captures = { rax = "rax" },
    mid      = function(c)
        -- rax holds seed (10) at the capture site, before `add rax,0x64`.
        assert(c.rax:get() == 10,
            "CAP-21-read: expected rax==10, got " .. tostring(c.rax:get()))
        -- return nothing -> the captured `add` runs (10 -> 110)
    end,
}

-- CAP-21-write: overwrite the captured rax (10 -> 1000); the add then runs
-- on 1000 -> 1100. Proves :set() lands in the real register downstream.
kcdx.hook{
    name     = "cap21_write",
    address  = cap21.addr_write(),
    captures = { rax = "rax" },
    mid      = function(c)
        c.rax:set(1000)
        -- return nothing -> the `add` runs on the mutated rax -> 1100
    end,
}

-- CAP-21-skip: return "skip" -> the captured `add` NEVER runs, so rax
-- stays seed (10) and the stub returns 10. Positional-list capture form
-- here (c[1]) to exercise the 1..N keying alongside the name-map form.
kcdx.hook{
    name     = "cap21_skip",
    address  = cap21.addr_skip(),
    captures = { "rax" },
    mid      = function(c)
        assert(c[1]:get() == 10,
            "CAP-21-skip: expected rax==10, got " .. tostring(c[1]:get()))
        return "skip"   -- the `add rax,0x64` is skipped
    end,
}

-- CAP-21-run: control — return nothing, the `add` runs (10 -> 110). Proves
-- the default (no return) executes the captured instruction.
kcdx.hook{
    name     = "cap21_run",
    address  = cap21.addr_run(),
    captures = { "rax" },
    mid      = function(c)
        -- no return -> run the captured instruction
    end,
}

-- CAP-21-mem: MEMORY capture writeback. Capture `[rcx]` (the int the stub's
-- `mov eax,[rcx]` is about to read; rcx = the int* arg). :set(1000) writes 1000
-- THROUGH the derefed address; the captured mov re-reads 1000 -> add -> 1100.
-- Exercises the new Context64 memory-deref path (F4-F9), beyond the GPR form.
kcdx.hook{
    name     = "cap21_mem",
    address  = cap21.addr_mem(),
    captures = { slot = "[rcx]:i32" },   -- i32: the stub's `mov eax,[rcx]` is 32-bit
    mid      = function(c)
        c.slot:set(1000)
        -- return nothing -> the `mov eax,[rcx]` re-reads 1000, add -> 1100
    end,
}

-- CAP-21-xmm: XMM lane capture writeback. Capture `xmm0` (the float seed) at the
-- `cvttss2si rax,xmm0` site. :set(50.0) writes the lane; the captured cvttss2si
-- then converts 50.0 -> rax=50. Exercises the new Context64 XMM-lane path (F3).
kcdx.hook{
    name     = "cap21_xmm",
    address  = cap21.addr_xmm(),
    captures = { seed = "xmm0:f32" },   -- "expr:type" string; f32 selects the XMM lane
    mid      = function(c)
        c.seed:set(50.0)
        -- return nothing -> cvttss2si converts the mutated 50.0 -> 50
    end,
}

kcdx.log.info("CAP21",
    "installed all cap-21 mid hooks (read/write/skip/run/mem/xmm)")
