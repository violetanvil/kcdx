# Hook-backend marriage design — changelog (newest first)

## 2026-06-08 — §4 re-grounded against the real install architecture

The code revealed the v1 design had the install layering inverted: §4.1 named
`detour_hook` as "the single chokepoint for every chain hook," but `detour_hook`
is only the JIT call-original SLOT owner (its `enable()`/`disable()` are dead on
the chain path). The real install chokepoint every chain hook funnels through is
`hook_engine::InstallRuntime` → raw `MH_CreateHook` (`src/hook_engine.cpp:66`).
Four settled revisions:

- **Backend seam relocates to `InstallRuntime`; `detour_hook` dissolves.** The
  backend dispatches at the install chokepoint and owns its own relocated-original
  slot (the `void**` the JIT bakes) — no separate slot-owner adapter remains. The
  step-3 `IDetourBackend` + `MinHookBackend` (commit 64fba7d) are CORRECT as built;
  only their attachment point moves (out from behind `detour_hook`), and
  `MinHookBackend`'s bodies are reused verbatim.
- **`g_installed` retires — one conflict model.** The first-wins map
  (`src/hook_engine.cpp:51`) was a v0.1 second model; the chain's
  `FindChain`/`CanCoexist` already prevents the double-install it guarded. Removed,
  not ported (pending the §9.8 caller-set check).
- **Foreign-hook coexistence is now a core v1 pillar (§6), not final-phase polish.**
  The extreme-mod consumer (multiplayer, heavy TC load orders) hooks the same
  functions other mods hook, so chaining onto foreign detours is existential for
  it. Chain-always policy unchanged; only priority/placement elevated.
- **Batch install designed now (§4.5).** safetyhook's `enable()` suspends all
  threads per hook (`vendor/safetyhook/src/inline_hook.cpp:383`) — N stop-the-world
  cycles at TC/multiplayer scale. No safetyhook batch primitive exists (verified
  this session); a kcdx-authored batch path is constructible over `StartDisabled`
  + `trap_threads` (create-all-disabled → one frozen window patches N). The
  multi-target frozen-window patch is a marked assumption-to-probe (§9.7); the
  path is provisional on it.

**Integrated in:** §1, §2, §4.1, §4.4, §4.5 (new), §4.6 (new), §6, §7, §8, §9
(unknowns 7–8 added), §11.
**Why:** the v1 design named the wrong layer as the install seam — a backend
behind `detour_hook` would not sit on the install path at all. Re-grounding the
seam at `InstallRuntime`, retiring the redundant conflict model, and elevating the
foreign-hook + batch concerns the extreme-mod consumer depends on aligns the
design with the architecture the code actually has.

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
