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
  UNCHANGED. safetyhook's `MidHook` calls a `void(Context64&)` C callback; that
  callback IS the `MidDispatch` adapter, doing the same Lua marshaling it does
  today, reading from `Context64`. The replacement is the codegen +
  register-capture mechanism, NOT the dispatch/marshaling layer.
- **Route mid-function installs through the safetyhook backend** (the §4.2 table
  already routes mid to safetyhook; Step 5 set the predicate — this step makes the
  mid path actually use `MidHook`).
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
