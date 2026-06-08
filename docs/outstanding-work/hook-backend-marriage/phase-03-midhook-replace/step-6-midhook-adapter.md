# Step 6 — replace make_jit_midfunc with the MidHook adapter

**What.** The production mid-hook replacement. Delete `make_jit_midfunc` (the
~370-line asmjit mid-hook codegen in `src/rom_borrowed/runtime_func_t.cpp`) and
replace it with a `safetyhook::MidHook` adapter: the three call-original modes map
onto `Context64.rip`, named captures map onto `Context64` register writeback, and
the existing `MidDispatch` is rewired to read `Context64` instead of the JIT stack
payload. The Lua-facing author surface (`args.rdx:get/set`, `args._skip`) is
UNCHANGED. Covers E8, E9, E12 (`../context.md`). **GATED on Phase 1 step 2's spike
PASS.**

**Scope (commit-grain).**
- **The three call-original modes (design §5.1), now as the production path the
  spike proved:**
  - True — leave `ctx.rip` alone; safetyhook's trampoline runs the captured
    instruction.
  - False — set `ctx.rip = resume_addr` (past the instruction).
  - Auto — the callback conditionally sets `ctx.rip = resume_addr` from callback
    state (the `args._skip` post-callback read drives it, as today).
  - `resume_addr` ownership follows the spike's U3 finding: if safetyhook exposes
    it, use that; else retain the hde64 accumulate-to-≥5 logic as a small helper
    (NOT the whole codegen).
- **Named captures → `Context64` writeback (design §5.2):** the `CaptureHandle`
  userdata's `:get()`/`:set()` rewire from the JIT 16-byte slot payload onto
  `Context64` fields (`ctx.rdx` for a register capture; `ctx.trampoline_rsp` +
  offset for a stack-expression capture). Per-type marshaling
  (`PushCaptureValue`/`WriteCaptureValue`) reads/writes `Context64` fields. A
  capture form the spike (U4) flagged as having no `Context64` equivalent is
  handled per that finding (surfaced, not silently dropped).
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

**Dependencies.** Phase 1 step 2 (the spike — U1/U2/U3/U4 resolved; THIS STEP IS
PROVISIONAL ON THE SPIKE PASS). Phase 2 step 4 (`SafetyhookBackend` — the mid
path routes through safetyhook). Phase 2 step 5 (the routing predicate selects
safetyhook for mid).

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
