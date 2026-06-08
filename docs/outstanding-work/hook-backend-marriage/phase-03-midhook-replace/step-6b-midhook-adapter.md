# Step 6b — replace make_jit_midfunc with the MidHook adapter

**What.** The production mid-hook replacement. Delete `make_jit_midfunc` (the
~370-line asmjit mid-hook codegen in `src/rom_borrowed/runtime_func_t.cpp`) and
replace it with a `safetyhook::MidHook` adapter: the three call-original modes map
onto `Context64.rip`, named captures map onto `Context64` register writeback (per
the **6a capture map**), and the existing `MidDispatch` is rewired to read
`Context64` instead of the JIT stack payload. The Lua-facing author surface
(`args.rdx:get/set`, `args._skip`) is UNCHANGED. Covers E8, E9, E12
(`../context.md`). **GATED on Phase 1 step 2's spike PASS; builds on the 6a capture
enumeration.**

**Scope (commit-grain).**
- **The three call-original modes (design §5.1), now as the production path the
  spike proved (Phase 1 step 2 PASS):**
  - True — leave `ctx.rip` alone; safetyhook's trampoline runs the captured
    instruction.
  - False — set `ctx.rip = resume_addr` (past the instruction).
  - Auto — the callback conditionally sets `ctx.rip = resume_addr` from callback
    state (the `args._skip` post-callback read drives it, as today).
  - **`resume_addr` ownership — RESOLVED by the spike (U3): safetyhook does NOT
    hand back the resume address; kcdx computes `resume_addr = captured-instruction
    VA + its instruction LENGTH`** (the next original instruction). Keep a SMALL
    length-decode helper (the existing hde64 decode, or the new Zydis decoder) — NOT
    the whole `make_jit_midfunc` codegen. **Critical (spike caveat): resume is
    computed from the instruction LENGTH, NEVER from the patch width** — safetyhook
    may patch with a 5-byte E9 or a 14-byte FF (the E9→FF fallback); the resume
    offset is independent of that.
- **Named captures → `Context64` writeback (design §5.2), per the 6a map:** the
  `CaptureHandle` userdata's `:get()`/`:set()` rewire from the JIT 16-byte slot
  payload onto the `Context64` fields the **6a enumeration mapped** (`ctx.rdx` for a
  register capture; `ctx.trampoline_rsp` + offset for a stack-expression capture —
  NOT `ctx.rsp`, which is read-only). Per-type marshaling
  (`PushCaptureValue`/`WriteCaptureValue`) reads/writes `Context64` fields per the
  6a map. **The writeback half (the spike proved only READ) is proven LIVE here** by
  the cap-21 writeback sub-test. A capture form 6a flagged with no `Context64`
  equivalent (U4) is handled per the user's 6a decision (surfaced, not silently
  dropped) — 6b does NOT re-decide it.
- **`MidDispatch` rewired (design §5.3):** the off-thread filter, the
  engine-bootstrap carve-out, the re-entrancy depth tracking, the pin-arena — ALL
  UNCHANGED. safetyhook's `MidHook` calls a bare `void(*)(Context64&)` callback;
  that callback routes (via the trampoline pool below) to the existing
  `MidDispatch(target)` path, doing the same Lua marshaling it does today, reading
  from `Context64`. The replacement is the codegen + register-capture mechanism,
  NOT the dispatch/marshaling layer.
- **The integration STRUCTURE + MECHANISM (settled via senior-architect-consult —
  verified against safetyhook's source):** the mid path is a DEDICATED adapter unit
  (`src/safetyhook_midhook.{cpp,h}` or similar), installed DIRECTLY from
  `AddMid`/`AddCMid` — NOT through `IDetourBackend`/`InstallRuntime` (design §5.3/§8:
  "a `safetyhook::MidHook` adapter that calls the existing `MidDispatch`", distinct
  from `SafetyhookBackend`). **The `InstallKind::ChainMid` seam RETIRES** (it only
  ever faked a function-entry install; the step-5 `select_backend` `ChainMid` arm +
  its `static_assert` are REMOVED, not flipped — `spec-conformance.md`: build to the
  design, not the step doc's earlier "flip the marker" wording).
  - **Target-identity recovery — a fixed C-trampoline pool (NOT codegen).**
    `safetyhook::MidHookFn` is a bare `void(*)(Context&)` with NO userdata channel
    (`vendor/safetyhook/include/safetyhook/mid_hook.hpp:22`), and `ctx.rip` is the
    SAFETYHOOK TRAMPOLINE, not the target VA (`context.hpp:27`) — so neither a
    userdata closure nor a `ctx.rip` key recovers identity. The mechanism: a STATIC
    source-written array of N tiny C trampolines `mid_trampoline_0..N-1`, each
    literally `void mid_trampoline_K(Context& c){ MidDispatchFromContext(c, K); }`
    (written ONCE in C++, compiled normally — ZERO runtime codegen, the whole point
    of retiring the per-target JIT). Install claims a free slot K, binds
    `slot K → (targetVA)`, and registers `mid_trampoline_K` as the `MidHookFn`. Fire:
    `mid_trampoline_K` → `MidDispatchFromContext(ctx, K)` → look up targetVA →
    the existing `MidDispatch(target)` path, reading captures from `ctx` per the 6a
    map. **Pool: a fixed generous cap (e.g. 64 or 128); exhaustion fails LOUD**
    (a surfaced error, never a silent drop — AP14), since mid-targets are few and a
    fixed array keeps the trampolines compile-time-constant.
  - kcdx holds each per-target `safetyhook::MidHook` for the session (kcdx never
    unhooks — SKSE "no FreeLibrary, no teardown" model).
- `make_jit_midfunc` + its dead helpers are DELETED — no `#if 0` corpse, no
  dormant branch (`working-artifacts.md`: live source returns to pure production
  logic). The deletion sweeps any prescriptive doc reference to `make_jit_midfunc`
  as the mid mechanism (`deletion-hygiene.md`).

**Test bar.** The mid-hook regression suite live on the production adapter:
- cap-04 (CAP-04a=110, CAP-04b=10, CAP-04c=10, CAP-04d=110) — the three
  call-original modes, each with its FALSIFIABLE row (CAP-04b FAILS if 110).
- cap-21 (mid hook with named captures) — read + WRITEBACK confirmed: a sub-test
  mutates a captured register via `:set()` and asserts the mutation took effect
  downstream (FALSIFIABLE: FAILS if the original register value survives).
- **New-form coverage (the same-change bar — user-settled; the 6a map flagged
  F2–F9 as supported-but-untested):** add at least one **memory-capture** row
  (`[base+disp]`, e.g. `[rbp-0x8]`) and one **XMM-capture** row (an `xmm` lane) to
  an existing mid-hook plugin (cap-21 or cap-04), each read + writeback asserted, so
  the new `Context64` memory-deref + XMM-lane machinery ships PROVEN, not just F1
  (the 64-bit-GPR form the existing rows already cover). FALSIFIABLE: the memory row
  FAILS if the deref reads/writes the wrong address; the XMM row FAILS if the lane
  read/writeback is wrong. (`test-suite.md` — the new mechanism ships with its test.)
- Agent builds + deploys + enables dev mode, user launches once, agent reads
  `kcdx-dev.log` for the cap-04 + cap-21 rows (`agent-builds-and-deploys.md`).
- Build-green is necessary, not sufficient — the codegen has asm subtleties; only
  the live matrix proves the adapter (`anti-patterns.md` §invariants-vs-gates; the
  cap-04 KI's own lesson: "codegen has asm subtleties easy to get wrong by
  speculation").

**Dependencies.** **Step 6a (the capture map — the `Context64` rewire builds
against it; a U4 gap is the user's 6a decision, not re-litigated here).** Phase 1
step 2 (the spike — U1/U3 + the register-READ half resolved; THIS STEP IS
PROVISIONAL ON THE SPIKE PASS, and proves the writeback half live). Phase 2 step 4
(`SafetyhookBackend` — the mid path routes through safetyhook). Phase 2 step 5 (the
routing predicate selects safetyhook for mid — currently MinHook with the "until
Phase 3" marker; THIS step flips that marker to route mid → safetyhook).

**Design authority.** [`hook-backend-marriage.md §5, §5.1, §5.2, §5.3`](../../../design/hook-backend-marriage.md)
— the three-mode mapping + the capture-writeback + the dispatch-unchanged
invariant are built to the design, not this summary.

**Disassembler-test / author-burden note.** None — the author surface is
unchanged (`args.rdx:get/set`, `args._skip`, `captures = {...}`); only what those
read/write underneath moves from a JIT slot to a `Context64` field. No new
author-supplied hex/offset.

**Reference.** [`../context.md`](../context.md) E8/E9/E12 + U1/U3/U4. Prior
cap-04 scar tissue:
`docs/known-issues/closed/cap-04 skip-original codegen does not skip the original instruction.md`.
