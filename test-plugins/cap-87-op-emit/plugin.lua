-- CAP-87 plugin.lua — kcdx.op.* static-bytes op value namespace + :emit_for.
--
-- Proves the AUTHOR SURFACE built in src/lua_bind_op.cpp: each kcdx.op.* call
-- produces an OP VALUE (a userdata carrying an op descriptor) whose
-- :emit_for(kind, byte_range_len) accessor returns the emitted bytes (for a
-- statement-bytes-INDEPENDENT op) or a deferral verdict (for a statement-bytes-
-- DEPENDENT op), plus a kind-mismatch teaching error. The op SELF-VERIFIES: no
-- statement verb (step 5) consumes it; :emit_for is the seam the rows assert
-- against, fed SYNTHETIC kind+range inputs.
--
-- DB-INDEPENDENT: the op-emit produces bytes for a given kind + byte_range, so
-- the rows need no reference-DB resolve, no address_id, no fixture. Each row
-- reads the ACTUAL :emit_for output, never a value it set, and states its red
-- condition (FALSIFIABLE — a row that can never go red proves nothing).
--
-- The same-size-vs-trampoline FIT pick is the STATEMENT VERB's job at apply
-- time (step 5); the op value only carries the descriptor + the byte-emit. The
-- DEFERRED ops (always_take_branch / invert_branch_condition /
-- replace_call_target / replace_assignment_value / replace_compare_constant)
-- cannot emit final bytes from kind+range alone — they need the apply-time
-- statement bytes — so :emit_for reports deferred=true, bytes=nil for them. A
-- deferred row that returned FABRICATED bytes would be an AP14-class silent
-- defect; the row asserts the deferral, never a guessed byte.

-- Compare an emitted byte array (the :emit_for result.bytes) against an
-- expected {int,...}. Returns true + "" when equal, false + a diff message.
local function bytes_eq(got, want)
    if type(got) ~= "table" then
        return false, "bytes is " .. type(got) .. " (expected a table)"
    end
    if #got ~= #want then
        return false, "got " .. #got .. " bytes, expected " .. #want
    end
    for i = 1, #want do
        if got[i] ~= want[i] then
            return false, "byte " .. i .. " = " .. tostring(got[i])
                .. ", expected " .. tostring(want[i])
        end
    end
    return true, ""
end

-- Render a byte array as "0xNN 0xNN ..." for report text.
local function hex(bytes)
    if type(bytes) ~= "table" then return tostring(bytes) end
    local parts = {}
    for i = 1, #bytes do
        parts[i] = string.format("0x%02X", bytes[i] % 256)
    end
    return table.concat(parts, " ")
end

-- Assert a DETERMINATE op emits exactly `want` bytes for (kind, range), and the
-- accessor reports kind_ok=true, deferred=false. RED on a kind reject, an
-- unexpected deferral, or any byte mismatch.
local function check_determinate(row, op, kind, range, want)
    if op == nil then
        kcdx.test.report(row, false,
            "the kcdx.op.* constructor returned nil — the op value was not "
            .. "produced (the binder did not register this form)")
        return
    end
    local r = op:emit_for(kind, range)
    if r == nil then
        kcdx.test.report(row, false,
            ":emit_for(\"" .. kind .. "\", " .. range .. ") returned nil — the "
            .. "accessor produced no result table")
        return
    end
    if r.kind_ok ~= true then
        kcdx.test.report(row, false,
            ":emit_for reported kind_ok=" .. tostring(r.kind_ok)
            .. " reason=" .. tostring(r.reason)
            .. " — expected this op to apply to a \"" .. kind .. "\" statement")
        return
    end
    if r.deferred ~= false then
        kcdx.test.report(row, false,
            ":emit_for reported deferred=" .. tostring(r.deferred)
            .. " — this op's emit is determinate from kind+range and must "
            .. "return bytes, not defer")
        return
    end
    local ok, why = bytes_eq(r.bytes, want)
    if not ok then
        kcdx.test.report(row, false,
            "emitted bytes [" .. hex(r.bytes) .. "] do not match expected ["
            .. hex(want) .. "]: " .. why)
        return
    end
    kcdx.test.report(row, true,
        "emit_for(\"" .. kind .. "\", " .. range .. ") -> [" .. hex(r.bytes)
        .. "] fit=" .. tostring(r.fit) .. " (matches expected)")
end

kcdx.on("ready", function()
    -- The kcdx.op namespace must exist for any row to run.
    if kcdx.op == nil then
        for _, row in ipairs({
            "cap-87-noop-emit", "cap-87-skip-call-void-emit",
            "cap-87-never-take-branch-emit",
            "cap-87-replace-with-return-zero",
            "cap-87-replace-with-return-nonzero",
            "cap-87-replace-return-value-emit",
            "cap-87-skip-call-return-value-emit",
            "cap-87-deferred-ops", "cap-87-kind-mismatch-reject",
        }) do
            kcdx.test.report(row, false,
                "kcdx.op namespace is not registered — the op binder did not "
                .. "bind")
        end
        return
    end

    -- replace_with_noop over a 5-byte statement -> 5 × 0x90. The op applies to
    -- ANY statement kind (whole-statement neutralize); tested against "call".
    -- RED if it emits anything but five 0x90 bytes.
    check_determinate("cap-87-noop-emit",
        kcdx.op.replace_with_noop(), "call", 5,
        { 0x90, 0x90, 0x90, 0x90, 0x90 })

    -- skip_call_void over a 5-byte call (E8 rel32 = 5 bytes) -> 5 × 0x90.
    -- RED if it does not NOP the whole call range.
    check_determinate("cap-87-skip-call-void-emit",
        kcdx.op.skip_call_void(), "call", 5,
        { 0x90, 0x90, 0x90, 0x90, 0x90 })

    -- never_take_branch over a 2-byte short Jcc -> 2 × 0x90 (fall through =
    -- neutralize the jump). RED if it does not NOP the branch range.
    check_determinate("cap-87-never-take-branch-emit",
        kcdx.op.never_take_branch(), "branch", 2,
        { 0x90, 0x90 })

    -- replace_with_return(0) over a 5-byte return statement -> xor eax,eax
    -- (31 C0) ; ret (C3) ; then NOP-pad to 5 (90 90). RED if it uses mov eax,0
    -- (B8 00 00 00 00) instead of the canonical xor, omits the ret, or mis-pads.
    check_determinate("cap-87-replace-with-return-zero",
        kcdx.op.replace_with_return(0), "return", 5,
        { 0x31, 0xC0, 0xC3, 0x90, 0x90 })

    -- replace_with_return(110) over an 8-byte return -> mov eax, 0x6E
    -- (B8 6E 00 00 00, little-endian imm32) ; ret (C3) ; NOP-pad to 8 (90 90).
    -- RED if the imm32 is wrong-endian, the wrong value, or the ret/pad is off.
    -- 110 == 0x6E.
    check_determinate("cap-87-replace-with-return-nonzero",
        kcdx.op.replace_with_return(110), "return", 8,
        { 0xB8, 0x6E, 0x00, 0x00, 0x00, 0xC3, 0x90, 0x90 })

    -- replace_return_value(1) over a 4-byte return -> mov eax, 1
    -- (B8 01 00 00 00) is 5 bytes > the 4-byte range, so it returns the natural
    -- 5-byte instruction and fit="trampoline" (the apply-time engine
    -- trampolines; the author never sees a doesn't-fit failure). NO ret (the
    -- statement already returns; this op overwrites only the value). RED if it
    -- emits a ret, truncates the imm32, or reports fit="same_size" for bytes
    -- that exceed the range.
    do
        local row = "cap-87-replace-return-value-emit"
        local op = kcdx.op.replace_return_value(1)
        if op == nil then
            kcdx.test.report(row, false,
                "kcdx.op.replace_return_value returned nil")
        else
            local r = op:emit_for("return", 4)
            local want = { 0xB8, 0x01, 0x00, 0x00, 0x00 }  -- mov eax, 1; no ret
            local ok, why = bytes_eq(r.bytes, want)
            if r.kind_ok ~= true or r.deferred ~= false then
                kcdx.test.report(row, false,
                    "kind_ok=" .. tostring(r.kind_ok) .. " deferred="
                    .. tostring(r.deferred) .. " — expected kind_ok=true "
                    .. "deferred=false")
            elseif not ok then
                kcdx.test.report(row, false,
                    "emitted [" .. hex(r.bytes) .. "] != expected ["
                    .. hex(want) .. "]: " .. why)
            elseif r.fit ~= "trampoline" then
                kcdx.test.report(row, false,
                    "5-byte emit over a 4-byte range reported fit="
                    .. tostring(r.fit) .. " — expected \"trampoline\" (the "
                    .. "op bytes exceed the statement range)")
            else
                kcdx.test.report(row, true,
                    "replace_return_value(1) -> [" .. hex(r.bytes)
                    .. "] (mov eax,1, no ret) fit=trampoline over a 4-byte range")
            end
        end
    end

    -- skip_call_return_value(0) over a 5-byte call -> xor eax,eax (31 C0) ;
    -- NOP-pad to 5 (90 90 90). The call is skipped but its result register is
    -- set to 0. RED if it emits a ret, uses mov eax,0, or mis-pads.
    check_determinate("cap-87-skip-call-return-value-emit",
        kcdx.op.skip_call_return_value(0), "call", 5,
        { 0x31, 0xC0, 0x90, 0x90, 0x90 })

    -- The DEFERRED ops: each cannot emit final bytes from kind+range alone (it
    -- needs the apply-time statement bytes / a resolved target address), so
    -- :emit_for must report kind_ok=true, deferred=true, bytes=nil — the
    -- descriptor is carried; the byte-emit runs at apply time (step 5). RED if
    -- ANY of them returns fabricated bytes (deferred=false / bytes present) —
    -- guessing a branch displacement / call target / operand encoding is an
    -- AP14-class silent defect.
    do
        local row = "cap-87-deferred-ops"
        local cases = {
            { name = "always_take_branch",
              op = kcdx.op.always_take_branch(),       kind = "branch" },
            { name = "invert_branch_condition",
              op = kcdx.op.invert_branch_condition(),  kind = "branch" },
            { name = "replace_call_target",
              op = kcdx.op.replace_call_target("SomeFn"), kind = "call" },
            { name = "replace_assignment_value",
              op = kcdx.op.replace_assignment_value(7), kind = "assign" },
            { name = "replace_compare_constant",
              op = kcdx.op.replace_compare_constant(3), kind = "compare" },
        }
        local failed = nil
        for _, c in ipairs(cases) do
            if c.op == nil then
                failed = c.name .. " constructor returned nil"
                break
            end
            local r = c.op:emit_for(c.kind, 6)
            if r.kind_ok ~= true then
                failed = c.name .. " reported kind_ok=" .. tostring(r.kind_ok)
                    .. " for its own kind \"" .. c.kind .. "\" (expected true)"
                break
            end
            if r.deferred ~= true then
                failed = c.name .. " reported deferred=" .. tostring(r.deferred)
                    .. " — a statement-bytes-dependent op must DEFER, not emit"
                break
            end
            if r.bytes ~= nil then
                failed = c.name .. " returned bytes [" .. hex(r.bytes)
                    .. "] — a deferred op must NOT fabricate bytes (AP14)"
                break
            end
        end
        if failed then
            kcdx.test.report(row, false, failed)
        else
            kcdx.test.report(row, true,
                "all 5 statement-bytes-dependent ops (always_take_branch, "
                .. "invert_branch_condition, replace_call_target, "
                .. "replace_assignment_value, replace_compare_constant) "
                .. "correctly report deferred=true, bytes=nil")
        end
    end

    -- The KIND MISMATCH (the §9.3 teaching error). always_take_branch requires
    -- a conditional-jump (branch) statement. Handed a "call" statement,
    -- :emit_for returns kind_ok=false with a reason that NAMES the actual kind
    -- ("call") AND the required kind ("branch"). The row reads the ACTUAL reason
    -- text and asserts it contains the actual kind — NOT a tautology (AP15):
    -- RED if it does NOT reject (kind_ok ~= false), or the reason does not name
    -- "call" (the actual statement kind it was handed).
    do
        local row = "cap-87-kind-mismatch-reject"
        local op = kcdx.op.always_take_branch()
        if op == nil then
            kcdx.test.report(row, false,
                "kcdx.op.always_take_branch returned nil")
        else
            local r = op:emit_for("call", 5)
            if r.kind_ok ~= false then
                kcdx.test.report(row, false,
                    "always_take_branch on a \"call\" statement reported "
                    .. "kind_ok=" .. tostring(r.kind_ok)
                    .. " — expected false (a branch op must reject a call "
                    .. "statement, not silently accept it)")
            elseif type(r.reason) ~= "string"
                   or not string.find(r.reason, "call", 1, true) then
                kcdx.test.report(row, false,
                    "rejected (kind_ok=false) but the reason did not name the "
                    .. "actual kind \"call\": reason=" .. tostring(r.reason)
                    .. " — the teaching error must name what the statement "
                    .. "actually IS (AP15: not a tautology)")
            elseif not string.find(r.reason, "branch", 1, true) then
                kcdx.test.report(row, false,
                    "the reject reason names the actual kind but not the "
                    .. "REQUIRED kind \"branch\": reason=" .. tostring(r.reason)
                    .. " — the teaching error must name both")
            else
                kcdx.test.report(row, true,
                    "always_take_branch on a \"call\" statement correctly "
                    .. "rejected: kind_ok=false, reason names the actual kind "
                    .. "(call) AND the required kind (branch): \""
                    .. r.reason .. "\"")
            end
        end
    end

    kcdx.log.info("CAP87",
        "kcdx.op.* :emit_for self-test reported all 9 rows against synthetic "
        .. "kind+range inputs (DB-independent)")
end)
