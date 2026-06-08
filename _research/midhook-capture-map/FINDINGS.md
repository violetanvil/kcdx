# Mid-hook capture-form -> safetyhook Context64 map (resolves U4)

hook-backend-marriage Phase 3 step 6a. Static enumeration of every named-capture
form `runtime_func_t::make_jit_midfunc` supports, mapped to its
`safetyhook::Context64` equivalent, so step 6b builds the Context64 capture wire
against THIS map (no surprise mid-rewrite). Read-only analysis + this durable
finding; NO live mid-hook code changed this step.

**Verdict: EVERY supported capture form maps to a `Context64` field. No U4 gap.**
The mapping is mechanical for register captures and a value-arithmetic compute for
memory captures; both are expressible from the Context64 field set. One subtlety
6b must honor (sub-register width) and one writeback hazard (memory writeback) are
noted below -- neither is an unmappable form, both are build details.

This is a STATIC analysis (its proof is 6b landing on it without a surprise, not a
runtime observation). Trust level: agent-authored hypothesis grounded in two
primary-source reads this session (the producer code + the vendored header), NOT a
live probe.

## SOURCE -- read this session (dependencies.md)

- **Context64 field set** -- `vendor/safetyhook/include/safetyhook/context.hpp:30-33`.
  `struct Context64 { Xmm xmm0..xmm15; uintptr_t rflags, r15, r14, r13, r12, r11,
  r10, r9, r8, rdi, rsi, rdx, rcx, rbx, rax, rbp, rsp, trampoline_rsp, rip; };`
  `Xmm` is a 16-byte union (`context.hpp:15-22`: u8[16]/u16[8]/u32[4]/u64[2]/
  f32[4]/f64[2]). By-value with writeback (`context.hpp:25-26` doc: "full access
  to the 64-bit registers"; the spike proved register READ; the design's named-
  captures section states writeback lands when the original resumes).
- **`rsp` is READ-ONLY; `trampoline_rsp` is the writable stack pointer** --
  `context.hpp:28-29` verbatim: *"rsp is read-only. Modifying it will have no
  effect. Use trampoline_rsp to modify rsp if needed but make sure the top of the
  stack is the rip you want to resume at."*
- **The capture grammar acceptor** -- `src/rom_borrowed/asmjit_helper.cpp`
  (`get_gp_from_name` :115, `get_xmm_from_name` :147, `get_addr_from_name` :194,
  `parse_address_component` :162, `parse_number_from_string` :174,
  `get_useable_gp_id_from_name` :270).
- **The capture read/write codegen** -- `src/rom_borrowed/runtime_func_t.cpp`
  `make_jit_midfunc` capture-to-stack loop :467-538, apply-change (writeback) loop
  :604-637.
- **The capture-declaration parser (Lua surface)** -- `src/lua_bind_hook.cpp`
  `ReadCaptures` :381, `SplitCapture` :358, `IsKnownCaptureType` :348; payload
  fields `captureExprs`/`captureTypes`/`captureNames` `src/hook_payload.h:174-176`.

## The capture-form grammar make_jit_midfunc accepts

A capture is an EXPRESSION (`captureExprs[i]`) + a TYPE (`captureTypes[i]`, default
`i64`). The expression -- NOT the type -- is the form that must map; the type only
selects GP-vs-XMM dispatch (`is_general_register` / `is_XMM_register`,
`asmjit_helper.cpp:13-35`) and the read width.

The codegen branches first on `argCapture.at(0) == '['` (memory vs register;
`runtime_func_t.cpp:470`). Two top-level families, with the memory family carrying
a full SIB sub-grammar (`get_addr_from_name`):

| # | Capture form (the captureExprs[i] expression) | Grammar source | Read mechanism (JIT slot) | Writeback mechanism |
|---|---|---|---|---|
| F1 | **64-bit GPR** -- rax rbx rcx rdx rsi rdi rbp rsp r8..r15 | get_gp_from_name reg_map (asmjit_helper.cpp:119-122); non-[ branch (runtime_func_t.cpp:501) | mov [rsp+16*i], reg (:522); rsp special-cased to lea the saved value (:515-520) | mov reg, [rsp+16*i] (:630) -- only when get_gp_from_name resolves (a named reg, not a computed addr) |
| F2 | **sub-width GPR** -- eax..r15d (32), ax..r15w (16), al/ah..r15b (8) | same reg_map, 32/16/8 sub-tables (asmjit_helper.cpp:124-138); non-[ branch | same mov [rsp+16*i], reg but reg is the sub-width asmjit Gp -> a 32/16/8-bit store | same mov reg, [rsp+16*i] sub-width store-back |
| F3 | **XMM** -- xmm0..xmm15 (declared with :f32 / :f64) | get_xmm_from_name (asmjit_helper.cpp:147); non-[ XMM branch (runtime_func_t.cpp:526) | movq [rsp+16*i], xmm (:532) | movq xmm, [rsp+16*i] (:634) |
| F4 | **memory [base]** -- base GPR deref | get_addr_from_name, base-only (asmjit_helper.cpp:252-266) | mov rbp,[addr]; mov [rsp+16*i+8],rbp (:479-482) | temp-reg store-back mov [addr],temp (:609-615) |
| F5 | **memory [base+disp] / [base-disp]** -- base + signed displacement (hex 0x.. / h, dec, oct o, bin b) | get_addr_from_name +/- cases (:211-232); parse_number_from_string bases (:174-192) | same as F4 (asmjit::x86::ptr(base, offset)) | same as F4 |
| F6 | **memory [base+index]** -- base + index GPR | get_addr_from_name index branch (:263-265 -> ptr(base, index, shift, offset)) | same as F4 | same as F4 |
| F7 | **memory [base+index*scale]** -- full SIB; scale is a power-of-2 number converted to a shift (:233-247) | get_addr_from_name * case | same as F4 (ptr(base, index, shift, offset)) | same as F4 |
| F8 | **memory [absolute_number]** -- a literal absolute VA, no base reg | get_addr_from_name [-with-number early return -> ptr(*num) (:204-205) | same F4 shape (absolute Mem) | same as F4 |
| F9 | **rsp-relative memory [rsp+N]** -- base = rsp; the parser adds the live rsp_offset (the JIT's saved-frame delta) so the author's [rsp+N] resolves against the ORIGINAL rsp | get_addr_from_name rsp special-case (asmjit_helper.cpp:259-261) + the caller passing stack_size + 8*10 + 8 as rsp_offset (runtime_func_t.cpp:473) | F4 shape, base=rsp with the frame-delta folded into the offset | F4 store-back |

Note on F2 (type-vs-expr split): the capture path chooses GP-vs-XMM from the
declared :type (get_type_id -> is_general_register / is_XMM_register), NOT from the
register-name width. The register-NAME width (eax vs rax) selects the asmjit Gp
width at codegen. A form like `eax:i64` is accepted by the parser
(get_gp_from_name("eax") resolves) but is an unusual author mix; F2 is the form
where the author writes a sub-width register name.

## The Context64 mapping -- each form, with the context.hpp field

Context64 carries register VALUES by reference-with-writeback. A REGISTER capture
maps to its field directly (read = field read, writeback = field write). A MEMORY
capture has no direct field -- but every base/index it needs IS a Context64 field,
so the adapter computes the effective address from field values and derefs it (the
captured memory lives at a real VA the adapter dereferences in C++, exactly as the
JIT did via mov [rsp+...]).

| # | Form | Context64 field(s) | Maps? | How 6b reads / writes it |
|---|---|---|---|---|
| F1 | 64-bit GPR | ctx.<reg> -- rax/rbx/rcx/rdx/rsi/rdi/rbp/r8..r15 (context.hpp:32); rsp is **read-only** (see hazard H1) | **Y** | read ctx.<reg>; write ctx.<reg> = v (writeback per the design) |
| F2 | sub-width GPR | same ctx.<reg> field, masked to width (32/16/8-bit slice of the uintptr_t) | **Y** | read low N bits of ctx.<reg>; write masks the low N bits, preserves the high bits (see hazard H2) |
| F3 | XMM | ctx.xmm<N> -- Xmm union (context.hpp:31); .f32[0] / .f64[0] / .u64[0] per declared type | **Y** | read/write ctx.xmm<N>.<lane> (the 16-byte union covers f32/f64/u64) |
| F4 | [base] | base value from ctx.<base>; deref *(T*)(ctx.<base>) | **Y** | addr = ctx.<base>; read *(T*)addr; write *(T*)addr = v |
| F5 | [base+/-disp] | ctx.<base> + the literal disp | **Y** | addr = ctx.<base> + disp |
| F6 | [base+index] | ctx.<base> + ctx.<index> | **Y** | addr = ctx.<base> + ctx.<index> |
| F7 | [base+index*scale] | ctx.<base> + ctx.<index>*scale | **Y** | addr = ctx.<base> + ctx.<index>*scale + disp |
| F8 | [absolute_VA] | none needed (the VA is a literal) | **Y** | addr = literal; deref directly |
| F9 | [rsp+N] | **ctx.rsp** carries the original rsp value (READ-only is fine -- we only READ it to form an address; see hazard H1) | **Y** | addr = ctx.rsp + N; deref. Context64 already presents the ORIGINAL rsp, so the JIT's manual rsp_offset frame-delta correction (asmjit_helper.cpp:259-261) is NO LONGER NEEDED -- safetyhook hands back the real rsp directly (see finding for 6b below) |

**Conclusion: F1-F9 all map. No capture form make_jit_midfunc supports is
inexpressible in Context64. There is NO U4 design gap to surface.** This is the
clean result the step's outcome map allowed for ("If EVERY form maps cleanly, say
so explicitly").

## Cross-check against the tests (which forms are live-tested in 6b)

Grepped test-plugins/ for every mid-hook plugin's captures=. Three plugins exercise
mid captures; ALL three use ONLY **F1 (64-bit GPR rax)**:

| Test | Surface | Capture form used | What it exercises | tested form |
|---|---|---|---|---|
| cap-04 (cap-04-midhook) | Lua | captures = {"rax"} (positional) | read + run/skip; CAP-04a/b/c/d matrix | F1 read |
| cap-21 (cap-21-mid-hook) | Lua | captures = {rax="rax"} (name-map) AND {"rax"} (positional) | READ (c.rax:get()==10), **WRITE** (c.rax:set(1000)->1100), skip, run | F1 read **and F1 writeback** |
| cap-42 (cap-42-cpp-mid-skip) | C++ | { "rax", "i64", nullptr } (positional) | C++ Mid run/skip parity | F1 read |

- **F1 (64-bit GPR) MUST map** -- it is live-tested in 6b across read (cap-04/21/42),
  **writeback** (cap-21 c.rax:set()), and skip. It maps (mechanical ctx.rax).
  cap-21's :set() is the live writeback proof 6b lands (the spike proved READ only;
  E9/context.md names cap-21 as the writeback proof).
- **F2-F9 are SUPPORTED but UNTESTED by the current suite.** No test exercises a
  sub-width register, an XMM capture, or any [mem] form. They map on paper (above);
  6b preserves them via the same value-compute wire. Per the step doc, a
  supported-but-untested form is NOTED here (6b preserves it), not surfaced -- it is
  not a gap, just uncovered by a regression row. **Recommendation for 6b/6c:** add a
  regression row exercising at least one [base+disp] memory capture and one XMM
  capture so F3-F9 gain live coverage in the new Context64 path (test-suite.md -- the
  new mechanism's own machinery should be tested; currently only F1 is). This is a
  test-coverage recommendation, surfaced for the user/6b, not a step-6a change.

## Findings 6b needs to know (the subtleties -- mappable, but build-load-bearing)

These are NOT U4 gaps (every form maps). They are the build details 6b must honor
so the Context64 rewire is correct:

- **H1 -- rsp is READ-ONLY in Context64; never WRITE through an rsp-based capture
  target.** A capture form whose *target* is rsp itself (F1 rsp) can be READ
  (ctx.rsp) but a writeback to ctx.rsp is a no-op (context.hpp:28-29). The current
  JIT special-cases rsp on the READ side only (runtime_func_t.cpp:515) and its
  writeback loop only writes back a named GPR via get_gp_from_name (:627-631) --
  writing rsp was never a supported writeback in the JIT either (the original
  instruction owns rsp). So this is parity-preserving: 6b reads ctx.rsp, never
  writes it. A WRITE to a [rsp+N] *memory* location (F9) is fine -- that writes
  through the dereferenced address, not the rsp register; it does NOT touch ctx.rsp.
  Only stack-POINTER edits go through trampoline_rsp, which the capture path never
  does (captures read/write registers and pointed-at memory, not the stack pointer
  itself). trampoline_rsp is therefore NOT needed for any capture form -- it is the
  resume-machinery's concern (6b's ctx.rip resume), not the capture wire's.
- **H2 -- sub-width GPR writeback (F2) must mask, not clobber the high bits.** The
  JIT stores/loads the sub-width register natively (a 32-bit store zero-extends per
  x86; 16/8-bit preserve the high bits). When 6b writes a sub-width capture back to
  ctx.<reg> (a full uintptr_t field), it must replicate that width semantics
  (32-bit write zero-extends the upper 32; 16/8-bit write preserves the upper bits)
  rather than writing the full 64-bit field. Mappable -- just compute the masked
  value before assigning the field. (Untested today; the recommendation above covers
  adding coverage.)
- **H3 -- memory writeback (F4-F9) derefs a real VA, with the SAME caveat the JIT
  had.** The JIT's apply-change loop only writes back a memory capture when it has a
  free temp register (get_useable_gp_id_from_name, :609-615); the value is written
  to the captured memory address. 6b's C++ adapter writes *(T*)addr = v directly (no
  temp-register juggling needed in C++) -- strictly simpler, same semantics. The
  address is computed from Context64 field values, so there is no
  RIP-relative-truncation hazard (the original asmjit mov [absolute],reg truncation
  that bled cap-04 history) -- the C++ deref has none of that.
- **H4 -- resume is 6b's, not the capture wire's, but interacts at F9.** Per the
  spike (U3, _research/safetyhook-midhook-spike/FINDINGS.md), resume = captured-VA +
  instruction LENGTH (ctx.rip = resume_addr for skip), computed by a kept
  hde64/Zydis length helper -- NOT the patch width. No capture form depends on the
  resume address; F9 ([rsp+N]) reads ctx.rsp which safetyhook presents as the
  original rsp regardless of the resume mechanism. So the capture wire and the
  resume wire are independent -- 6b can build them separately.
- **Finding for 6b -- the rsp_offset frame-delta correction disappears.** The
  current JIT threads an rsp_offset (stack_size + 8*10 + 8, runtime_func_t.cpp:473)
  into get_addr_from_name so an author's [rsp+N] resolves against the ORIGINAL rsp
  despite the JIT's own pushes having moved rsp. In the Context64 model safetyhook
  hands back the original ctx.rsp directly -- there is no JIT prologue shifting rsp
  underfoot -- so 6b drops the rsp_offset machinery entirely and uses ctx.rsp + N
  raw. (This is a simplification the rewrite gains, recorded so 6b does not port the
  dead frame-math.)

## The map's completeness (the falsifiable claim)

Every capture form make_jit_midfunc supports (F1-F9, enumerated from the actual
grammar acceptors in asmjit_helper.cpp, not a guess) AND every form the tests
exercise (cap-04/21/42 -- all F1) is in the table above, each mapped to a Context64
field. No form is omitted; no form is unmappable. **There is no U4 gap.** The map's
proof is 6b's Context64 rewire landing on it without encountering a form the map did
not cover.