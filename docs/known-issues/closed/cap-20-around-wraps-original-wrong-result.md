# CAP-20-around: around hook wraps original but produces wrong result

## Symptom

`kcdx.hook` mode=`around` on a native `int(int)` target. Callback:
`around = function(orig, seed) return 2 * orig(seed) end`. Target
`Cap20_Add_Around(seed)` returns `seed + 100`. Calling with seed=10,
expected `2 * (10+100) = 220`. Observed: FAIL (value != 220; the DLL's
verify logs pass/fail only, not the actual value — see PROBE A).

The other 6 CAP-20 sub-tests (before/after/replace/chain/wstr/conflict)
PASS, so install, marshaling, the chain, conflict resolution, and the
load-order composition all work. Only the around / call_original path
is wrong.

## Facts

- around installs cleanly: `hook_chain: installed around 'cap20_around'
  at target 0x...1600` (kcdx_2026-05-21_22-34-03.log:346). Distinct
  address from the other targets (COMDAT-fold fix worked).
- around is the FIRST (only) hook on its target → goes through Add's
  first-touch branch (hook_chain.cpp:782+), which builds the
  callOriginalThunk over `install.pOriginal` (hook_chain.cpp:811-818).
- The around callback ran WITHOUT throwing: no "around ... threw" error,
  no "call_original: unavailable" closure error in either log.
- DispatchExclusive stack handling is shared with `replace`, which
  PASSES — so the bug is NOT the pcall/stack/WriteReturn handling; it's
  specific to what `orig(seed)` (the callOriginalThunk) does.

## Open questions

- **The callOriginalThunk returns the wrong value (or orig() isn't
  really calling the original).** Probe A: log (a) whether
  chain.callOriginalThunk is non-null at dispatch, (b) the value the
  around callback returns, surfaced from the engine side. Cheapest
  first: have the cap-20 DLL log the ACTUAL observed return of
  Cap20_Add_Around(10) (not just pass/fail) so we know if it's 110
  (around returned orig unchanged — *2 didn't happen), 10 (orig
  returned seed not seed+100), 0/garbage (thunk returned nothing), or
  something else. Each value points at a different cause.

## Trail

| Action | Result |
|---|---|
| PROBE A: cap-20 DLL logs the actual Cap20_Add_Around(10) return value | **Observed = 0** (not 110, not 10). Return slot ended zero — points at orig() returning 0 OR the around return not being written back, NOT at "*2 lost" or "orig returned seed". |
| PROBE B: around callback logs orig(seed) via kcdx.log inside plugin.lua | **orig(10) = 0**. The callOriginalThunk returns 0, not 110. Callback/multiply/writeback are FINE (2*0=0 written correctly). Bug is entirely in the call_original thunk. |
| PROBE C: call install.pOriginal(10) DIRECTLY from C in Add, log result | **pOriginal(10) = 110** from C. MinHook trampoline is PERFECT. Bug is 100% in the Lua call-thunk (BuildLuaCallThunk/JitTrampoline) — H1 eliminated, H3 confirmed. Same trampoline via Lua orig() = 0. |
| PROBE D: log rt/pts strings + resolved type_info at thunk build | **rt='i64' (m_val=3 integer_), pts='i64', argcount=1.** Strings/types exactly as expected. Config is right; bug is the asmjit codegen calling an int32 fn as int64. |
| FIX: SigTypeToJitString preserves integer width (i32 stays i32, not i64) | **DISCONFIRMED as root cause.** PROBE D now rt='i32' (fix applied), but orig(10) STILL = 0. Width was a real latent bug (kept the fix) but NOT the cause of the 0. |

## PROBE E plan (outcome→meaning written BEFORE running, per results-driven.md)

In Add, right after building the thunk over pOriginal, invoke the thunk
itself (it IS a lua_CFunction) directly from C against the engine
lua_State with arg 10 on the stack, and log what it returns. This tests
the THUNK in isolation, removing the around-dispatch + Lua-callback
layers. Outcome map:
  - thunk(push 10) → 110: the thunk is correct in isolation; the bug is
    in how DispatchExclusive sets up the stack when calling orig (extra
    value at index 1? arg not where the thunk reads it?).
  - thunk(push 10) → 0: the thunk itself is broken (JitTrampoline
    int-return latent bug) regardless of caller — fix JitTrampoline; also
    means shipped kcdx.memory.dynamic_call has the same latent bug.

| Action | Result |
|---|---|
| PROBE E: call the built thunk directly from C (push L, push 10) in Add, log return | **n=1, val=0.0.** Thunk pushed exactly ONE value (return path ran) but it's 0, not 110. Isolated from dispatch/callback. The shared JitTrampoline is broken for this case → shipped kcdx.memory.dynamic_call shares the bug. NOT a dispatch/stack issue. |
| PROBE F: read emitted asm (dev log StringLogger dump) | cvtsi2sd→xmm1; lua_pushnumber called with NO movss/movsd xmm0,xmm1. Missing move = observed cause of the 0. (lua_Number=float verified vendor/lua/luaconf.h:504.) |
| FIX (Option A): return-push produces lua_Number-width FLOAT vreg (cvtsi2ss for int/ptr, cvtsd2ss to narrow double, f32 left as-is) | **RETURN PATH FIXED.** asm now `cvtsi2ss xmm1` and the value flows (was 0). But NEW observed fact: PROBE E thunk(10)=**100**, CAP-20-around=**200**. orig() ran (return marshaled correctly now) but received seed=**0**, not 10 (0+100=100). Return bug fixed; ARG bug exposed. |

## Reframe 2026-05-21 (#6) — return fixed; ARG arrives as 0 (separate bug)

The float-width fix worked: the converted return now flows through xmm0
(value 100 came out, not 0). But orig(10) returns 100 = 0+100, and
around=200=2*100 — so the ORIGINAL is being called with seed=0, not 10.
The arg isn't reaching pOriginal. Two distinct paths BOTH show seed=0:
PROBE E (push 10, call thunk directly) and the real orig(seed) from Lua.
asm: `mov edx,1` (index for lua_tonumber), `call lua_tonumber`,
`cvttsd2si rax,xmm0`, `mov rcx,rax`, `call pOriginal`. So the thunk reads
arg via lua_tonumber(L, index=1). The value at index 1 must not be 10.
This is the lua_remove/stack-base question from reframe #4, now an
OBSERVED discrepancy not a theory: dynamic_call's Handle_Call does
lua_remove(L,1) before calling the thunk (drops self), so the thunk's
"index 1" convention assumes a specific stack base. Need to OBSERVE what
is actually at index 1 when the thunk runs in (a) PROBE E and (b) the
around orig() path — PROBE G.

| Action | Result |
|---|---|
| PROBE G: invoke thunk via lua_pcall (proper frame) + log base | **status=0 val=100 base=1.** Even via a faithful lua_pcall(arg=10), orig got seed=0 (0+100=100). NOT a PROBE-E artifact, NOT a DispatchExclusive issue. The thunk's ARG read returns 0. |
| FIX (arg side, symmetric to return fix): lua_tonumber result is FLOAT (lua_Number), read it with cvttss2si not cvttsd2si | **VERIFIED FIXED.** PROBE G val=110, orig(10)=110, CAP-20-around=220 PASS. All 7 CAP-20 sub-tests green. |

## Resolution

**Cause (two manifestations of one root):** the shared JIT call-thunk
(`JitTrampoline`, `src/lua_bind_dynamic_call.cpp`) marshaled numbers as
`double` on both the argument-read and return-push paths, but this build
defines `LUA_NUMBER=float` (`vendor/lua/luaconf.h:504`). `lua_tonumber`
returns a 32-bit float in xmm0; `lua_pushnumber` takes a 32-bit float.
Treating those as doubles (`cvttsd2si` on the arg, `cvtsi2sd` +
double-typed `set_arg` on the return) reinterpreted the float bit-pattern
→ garbage → 0. Both directions were latent-broken since Phase 5c.7c and
never caught: the only `dynamic_call` tests (lua-memory-verify) used
`void` returns or bogus/error targets — no test ever round-tripped a real
arg+return.

**Fix (src/lua_bind_dynamic_call.cpp):**
- Return push: produce a lua_Number-width FLOAT vreg — `cvtsi2ss` (int),
  leave f32 as-is, `cvtsd2ss` to narrow f64; pass to the float-typed
  `lua_pushnumber` arg. (asmjit then wires xmm0 correctly.)
- Arg read: `lua_tonumber`'s result is a FLOAT — `cvttss2si` (float→int),
  use float as-is for a float target, `cvtss2sd` to widen for a double
  target.
- Also fixed (separate, kept): `SigTypeToJitString` (hook_chain.cpp) now
  preserves integer width (i32 stays i32) so the call_original thunk
  calls the original with its true ABI signature.

**Pinpointed by:** PROBE C (pOriginal good from C) → E/G (thunk broken in
isolation, even via lua_pcall) → F (asm dump showed the missing xmm0
move) → static check of `luaconf.h` (LUA_NUMBER=float) → return fix
(value flowed, exposed the symmetric arg bug) → arg fix (cvttss2si) →
7/7 green.

**Regression coverage added:** `CAP-20-dyncall` in test-plugins/cap-20-
hook-modes/ — a non-void int-return `kcdx.memory.dynamic_call` round-trip
(10→110), the exact path that was untested and hid this bug.

**Known limitation (documented, not a bug):** `LUA_NUMBER=float` means
int/ptr returns above 2^24 lose precision crossing the Lua boundary
(`.claude/rules/lua-precision.md`). For pointer-magnitude returns the
`PushPointer` userdata path is correct, not `lua_pushnumber`. sub-4's
i32 case is exact and correct; large-int/ptr returns through dynamic_call
remain lossy by the same documented boundary.

**Follow-on (same fix session):** the CAP-20-dyncall regression first
FAILed with "dynamic_call returned nil" — because `kcdx.memory.dynamic_call`'s
`target` reader accepted pointer-userdata + integer but NOT lightuserdata,
and cap20.addr_dyncall() hands the address as a lightuserdata (exact, per
lua-precision.md). Extended the target reader to accept lightuserdata too
(consistent with the kcdx.hook `address` locator). (dynamic_hook's
GetTargetField has the same pattern; not touched here — out of sub-4
scope, no failing test — note for a future consistency pass.)

**Landed:** sub-4 commit (Phase 2b).

## Reframe 2026-05-21 (#7) — SAME float-width bug, arg side (we'd fixed only the return side)

The arg-read in JitTrampoline (lua_bind_dynamic_call.cpp:135-158) had the
mirror of the return bug: lua_tonumber returns lua_Number = FLOAT (32-bit)
in xmm0, but the code did `cvttsd2si` (convert as DOUBLE) — reinterpreting
the float bit-pattern as a double yields garbage → truncates to 0. That's
why orig received seed=0. The earlier return-side fix (cvtsi2ss) addressed
the SAME LUA_NUMBER=float root cause but only on the output; the input had
the symmetric defect. FIX: read tmp as float — integer arg `cvttss2si`,
float arg use tmp as-is, double arg `cvtss2sd` to widen. Both directions
now respect lua_Number=float. (Both arg AND return paths of JitTrampoline
were latent-unverified: lua-memory-verify only tested void/bogus targets,
never a real arg+return round-trip.)

## ROOT CAUSE FOUND (PROBE F — read the emitted asm, dev log line 850)

The JIT stub's actual machine code (asmjit StringLogger dump) for the
i32(i32) call-thunk:
```
call lua_tonumber          ; arg -> xmm0
cvttsd2si rax, xmm0        ; rax = 10
mov rcx, rax               ; arg to original in rcx
call pOriginal             ; rax = 110  (correct! matches PROBE C)
movdqa xmm1, [rsp+0x30]    ; (preload dest)
cvtsi2sd xmm1, rax         ; xmm1 = 110.0
call lua_pushnumber        ; reads its double arg from XMM0 — but the
                           ; value is in XMM1, and nothing moved it!
mov rax, 1; ret
```
OBSERVED FACT (asm, not inference): the converted return is in **xmm1**;
`lua_pushnumber` reads its FP arg from **xmm0**; **no `movss/movsd xmm0,
xmm1` is emitted between** the conversion and the call. So lua_pushnumber
reads xmm0 = stale (the lua_tonumber result / whatever) → pushes 0.0, not
110.0. That missing register move is the observed cause of the 0.

WHY the move is missing is not yet directly observed — candidate
(NOT banked as fact): the return-push path produces a *double* vreg
(`cvtsi2sd`) but `lua_Number` is **float** on this build
(`vendor/lua/luaconf.h:504` LUA_NUMBER=float, verified), so the vreg
type may disagree with the float-typed `set_arg`, and asmjit fails to
wire xmm0. The FIX targets the OBSERVED fact (make the move appear) by
producing a lua_Number-width (float) vreg; VERIFICATION is re-dumping
the asm to confirm a `movss xmm0,...` now precedes the call — not a
declaration that the theory was right.
Latent since Phase 5c.7c — never caught because no shipped test ever
checked a non-void dynamic_call RETURN VALUE (only void + bogus-target
error paths exist in lua-memory-verify). PROBE C/E/asm together prove
it: original returns 110 (rax), the bug is purely the FP-return →
lua_pushnumber arg-register wiring.

## Reframe 2026-05-21 (#5) — bug is in JitTrampoline itself (n=1, val=0)

PROBE E calls the thunk in pure isolation (C pushes 10, invokes): it
pushes 1 value = 0.0. So: arg read ran, original called (or not), return
pushed — but value is 0. Bug is inside JitTrampoline's codegen, shared
with shipped kcdx.memory.dynamic_call. exactly-0 (not 100=0+100, not
garbage) suggests the original's return register was never captured /
the call didn't execute / the return reg read the wrong place. Next
(read-only, static): re-read JitTrampoline's call+return emission for how
target_func_ptr (a runtime uintptr_t param, NOT compile-time constant)
is passed to cc.invoke, and whether set_ret wiring captures the value
before the lua_push InvokeNode clobbers the return register. Candidate:
the int return is captured into a virtual reg but the subsequent
cvtsi2sd / lua_pushnumber InvokeNode reorders or the value reg isn't
pinned across the call.

## Reframe 2026-05-21 (#4) — width was NOT it; thunk still returns 0

Even with the correct i32 signature, the JIT thunk over pOriginal returns
0 while PROBE C calls the SAME pOriginal directly from C and gets 110.
The width-collapse fix is correct and KEPT (calling an int fn as int64 is
genuinely wrong), but it is not the 0. Remaining difference between the
working PROBE C and the broken thunk:
  - C calls pOriginal directly; thunk calls it via asmjit cc.invoke.
  - Thunk reads its arg via lua_tonumber(L, 1); maybe the arg isn't 10.
  - Maybe cc.invoke(<runtime VA in a variable>, sig) mis-emits vs the
    dynamic_call path which bakes a compile-time-constant target.
KEY TEST (PROBE E): the SAME JitTrampoline backs the shipped
kcdx.memory.dynamic_call. Does dynamic_call with an int-return target
work? If it ALSO returns 0, JitTrampoline has a latent int-return bug
(pre-existing, never exercised). If it works, the difference is how the
around path invokes the thunk (bare pushed lua_CFunction vs handle __call
which does lua_remove(L,1)). The lua_remove! dynamic_call's Handle_Call
removes the self userdata at index 1 BEFORE calling fn — so the thunk
reads args at 1..N with self gone. When I push the raw thunk as `orig`
and Lua calls orig(seed), there is NO self... but is there? Need to
verify what's actually at stack index 1 when the bare thunk runs.

## Reframe 2026-05-21 (#3) — ROOT CAUSE: ABI width mismatch in call_original

PROBE C: pOriginal(10)=110 from C (which calls it as `int(int)` — arg in
ECX, return read from EAX). Thunk: orig(10)=0, calling the SAME pOriginal
but with asmjit signature `int64(int64)` because `SigTypeToJitString`
collapsed ALL integer widths to "i64". Calling an `int`(=i32)-returning
function as i64 reads the return from the full 64-bit RAX, but the callee
(`int Cap20_Add_Around(int)`) only writes the 32-bit EAX per the Win64
ABI — upper 32 bits of RAX are undefined, corrupting the value the thunk
reads (observed: 0). This is an AP2-class ABI-width bug.

FIX: `SigTypeToJitString` (hook_chain.cpp) now preserves the declared
integer width (i8/i16/i32/i64 + unsigned) instead of collapsing to i64,
so the call_original thunk calls the original with its TRUE ABI
signature. get_type_id already maps i32→kInt32 etc. The width collapse
was harmless for the make_jit_func detour path (slot is uintptr_t-sized)
but wrong for any path that actually INVOKES the function with that
signature — which call_original does.

## Verified by reading (no launch)

- `SigTypeToJitString(I32)` → `"i64"`; `get_type_id("i64")` → `kInt64`
  (GP register, asmjit_helper.cpp:107); `get_type_info_from_string("i64")`
  → `integer_` (type_info_t.cpp:33 else-branch). So the thunk's target
  signature is `int64(int64)` and the integer return-push path
  (lua_bind_dynamic_call.cpp:237-259, cvtsi2sd→lua_pushnumber) DOES run.
  Static reading says it should work — yet orig()=0. The discrepancy is
  something not visible statically (asmjit reg-alloc, 32-bit-return-in-
  64-bit-reg garbage, or the arg arriving wrong).

## Reframe 2026-05-21 (#2): bug is the JIT lua_CFunction call-thunk

PROBE C: `pOriginal(10)` from C = 110; `orig(10)` from Lua (through the
JIT thunk over the SAME pOriginal) = 0. The trampoline is good; the
JIT'd lua_CFunction marshaling is wrong. Within the thunk
(lua_bind_dynamic_call.cpp JitTrampoline), candidates: (a) arg read
yields wrong value passed to the trampoline; (b) return value not read
from the right register / pushed wrong; (c) the asmjit FuncSignature for
the target is built wrong for this rt/pts. NOTE: this thunk is the SAME
code path kcdx.memory.dynamic_call uses (shipped, cap-05) — so either
dynamic_call has a latent bug never exercised with an int-return target,
or our rt/pts strings differ from what cap-05 passes. SignatureToAbiStrings
maps i32→"i64" (SigTypeToJitString); verify get_type_id("i64") +
the int-return push path are correct. Cheapest next: PROBE D — log rt
and pts strings passed to BuildLuaCallThunk, and test kcdx.memory.dynamic_call
on the same pOriginal with explicit "i32"/"int" to see if the type-string
is the discriminator.

## Reframe 2026-05-21: bug is the call_original thunk, not the around dispatch

`orig(10) = 0` (PROBE B) isolates it completely. DispatchExclusive's
stack handling, the `2 * o` multiply, and WriteReturn all work — the
input to them is just wrong because `orig()` returns 0. The
callOriginalThunk (BuildLuaCallThunk over install.pOriginal) is the sole
suspect. Sub-hypotheses:
  - H1: pOriginal was null/wrong at thunk-build time → thunk calls a bad
    VA → returns 0.
  - H2: thunk mis-marshals the arg (reads seed from wrong stack index)
    so the original computes from 0 → 0+100... = 110, not 0. (0 argues
    AGAINST a clean seed-misread, since seed+100 would be ≥100.)
  - H3: thunk mis-marshals the RETURN (original ran, returned 110, but
    the thunk pushes 0). dynamic_call return path pushes via cvtsi2sd→
    lua_pushnumber for int; if the return reg wiring is wrong → 0.
  - H4: thunk's first arg consumed wrong because dynamic_call's raw
    lua_CFunction expects args at index 1.. but we call orig(seed) with
    seed at index 1 — should be right. Verify.
Result 0 (not ~110) argues the ORIGINAL likely never ran with the right
arg, or its return never made it back — H1 or H3 over H2.

## Facts (cont.)

- around's observed result is exactly **0** (PROBE A). Splits to: either
  `orig(seed)` returns 0 (callOriginalThunk ran the original wrong), or
  the callback's return value isn't written back to the slot (slot stays
  zero-init). PROBE B logs orig(seed) from inside the Lua callback to
  split these.
