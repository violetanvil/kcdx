# Hook-backend marriage design — changelog (newest first)

## 2026-06-07 — v1 settled (initial authoring)

The marriage of MinHook + safetyhook behind one backend interface, settled
through the design dialogue. The decisions:

- **Backend abstraction behind `detour_hook`.** A uniform `IDetourBackend`
  interface (create/enable/disable/get_original); MinHook + safetyhook as two
  implementations; the chain + conflict model + Lua/ABI marshaling unchanged
  above it.
- **Routing is install-context-driven, automatic.** Loader-lock (`early_hook`) +
  the `HookedUpdate` bootstrap pump → MinHook (safetyhook's unconditional
  thread-suspend deadlocks under the loader lock — a hard correctness constraint,
  not effort). Everything else → safetyhook. No author knob.
- **`make_jit_midfunc` fully replaced by `safetyhook::MidHook`.** The three
  call-original modes map onto `Context64.rip` (True = run trampoline, False =
  `ctx.rip = resume_addr`, Auto = callback conditionally sets it); named captures
  map onto register writeback. The fragile asmjit mid-hook codegen (the cap-04
  scar tissue) retires.
- **Foreign-hook detection + chaining added on top.** kcdx detects a pre-existing
  foreign E9/FF on a target, follows it, and chains so both mods' hooks fire —
  true cross-mod coexistence, built above the patcher. Chain-always policy; a
  configurable policy is reserved.
- **The mid-hook replacement is provisional, gated on a spike.** The
  `ctx.rip`→three-modes mapping is read-feasible from safetyhook's header but not
  yet observed at runtime — marked assumption-to-probe, proven by porting cap-04
  before the full retirement lands (`results-driven.md`).

**Integrated in:** §1–§11 (initial authoring — the whole doc).
**Why:** there is no single best detour engine for kcdx — safetyhook is strictly
better for the bulk but its thread-suspend install is unsafe under the loader
lock, which kcdx's `early_hook` path requires. Routing each path to the engine
whose strengths fit it, behind one interface, captures both engines' strengths
and neither's weakness.
