# hook-backend-marriage — shared spec + coverage map

The spec every step leans on. The settled design is
[`docs/design/hook-backend-marriage.md`](../../design/hook-backend-marriage.md)
(v1 + the 2026-06-08 §4 re-grounding, `62046c3`) — the EXECUTOR BUILDS TO THAT
DOC, not to this summary or a step doc's prose (`.claude/rules/spec-conformance.md`).
This file carries the goal, the cross-step invariants, the build-gated unknowns,
and the coverage map.

## Goal

Marry MinHook + safetyhook behind one uniform `IDetourBackend` interface, each
hook-install path routed to the engine whose strengths fit it — automatically, by
install context — so neither engine's weakness can break a path. The chain
(`hook_chain`), conflict model (`CanCoexist`), and Lua/ABI marshaling stay
unchanged above the backend layer.

## The settled decisions (verbatim, with source)

From the design's §11 decision record (`docs/design/hook-backend-marriage.md`),
as re-grounded 2026-06-08:

1. **Backend abstraction at `InstallRuntime` — the install chokepoint;
   `detour_hook` dissolves** (design §4.1, §8) — a uniform `IDetourBackend`
   (create/enable/disable/get_original); MinHook + safetyhook as two
   implementations; the chain + conflict model + marshaling unchanged above. The
   seam lands at `hook_engine::InstallRuntime` (the function every chain install
   funnels through), NOT behind `detour_hook` (which is only the JIT call-original
   slot owner — its `enable()`/`disable()` are dead on the chain path; it
   dissolves and the backend owns its own relocated-original slot). The step-3
   `IDetourBackend` + `MinHookBackend` (commit `64fba7d`) are CORRECT as built —
   only their attachment point relocates; the bodies are reused.
2. **Install-context-driven routing, automatic** (design §4.2) — loader-lock
   (`early_hook`) + the `HookedUpdate` bootstrap pump → MinHook; everything else →
   safetyhook. No author knob. The misroute-impossible predicate is the one
   safety-critical mechanism. The routing decision LIVES at `InstallRuntime` (the
   dispatch site).
3. **One conflict model — `g_installed` retires** (design §4.6) — the chain's
   `FindChain` + `CanCoexist` is the sole conflict model. The first-wins
   `g_installed` map (`src/hook_engine.cpp:51`) was a v0.1 SECOND model for the
   same concern; the chain's `FindChain` already prevents the double-install it
   guarded. Removed, not ported — gated on the U8 caller-set check.
4. **Full `make_jit_midfunc` replacement with `safetyhook::MidHook`** (design §5)
   — the three call-original modes map onto `Context64.rip`; named captures onto
   register writeback. PROVISIONAL, gated on the spike (U1).
5. **Foreign-hook detection + chaining — a CORE v1 pillar** (design §6) — detect a
   pre-existing foreign E9/FF, follow it, chain so both mods' hooks fire.
   Chain-always policy. Elevated from final-phase polish to a core pillar: the
   extreme-mod consumer (multiplayer, heavy TC load orders) hooks the same
   functions other mods hook, so foreign-detour chaining is existential for it.
   Built after the safetyhook swap (the thread-safe install is what makes patching
   a prologue another mod is actively in SAFE — design §6), priority elevated.
6. **kcdx-authored batch install** (design §4.5) — install N detours under ONE
   thread-suspend window instead of N. safetyhook's `enable()` suspends all
   threads per hook (`inline_hook.cpp:383`); at TC/multiplayer scale that is
   hundreds of stop-the-world cycles. No safetyhook batch primitive exists; a
   kcdx-authored path over `StartDisabled` + `trap_threads` is constructible
   (create-all-disabled → one frozen window patches N). The multi-target
   frozen-window patch is PROVISIONAL, gated on the U7 probe.
7. **The mid-hook replacement is gated on a spike** (design §9.1) — the
   `ctx.rip`→three-modes mapping is read-feasible from safetyhook's header but
   UNOBSERVED at runtime (`.claude/rules/results-driven.md`). The spike (port
   cap-04) PROVES it before the full retirement lands. (DONE — Phase 1 step 2.)

## Cross-step invariants

- **The chain stays untouched** (design §4.3). `hook_chain`'s `CanCoexist`, the
  ordered `std::vector<ChainEntry>`, the engine-first stamp, the off-thread
  marshaling — none change. safetyhook is "just the patcher" for the chain.
- **MinHook is PERMANENT** for `early_hook` + the `HookedUpdate` bootstrap pump
  (design §10) — complete removal is off the table on the merits (the loader-lock
  deadlock constraint, design §7), not deferred for effort.
- **`get_original()` contract** (design §4.4) — returns a stable, callable
  pointer to the relocated-original entry, valid for the hook's lifetime; the JIT
  thunks deref a pointer slot and the backend (via `InstallRuntime`) populates it.
  The JIT codegen does NOT change.
- **`detour_hook` dissolves; the backend owns its slot** (design §4.1, §8) — the
  former JIT-slot-owner adapter is removed (its `enable()`/`disable()` were dead
  on the chain path); the slot the JIT bakes becomes the backend's. No separate
  adapter layer remains between the chain and the patcher. Step 3 built
  `IDetourBackend`/`MinHookBackend` BEHIND `detour_hook`; the seam-relocation step
  moves them out and removes the husk.
- **One conflict model** (design §4.6) — `g_installed` is removed, the chain's
  `FindChain`/`CanCoexist` is the sole gate. Two conflict models for one concern
  is the drift this re-grounding exposed.
- **Build-green is necessary, not sufficient** (`anti-patterns.md`
  §invariants-vs-gates) — every cap-NN row re-verifies live; the agent builds +
  deploys, the user launches, the agent reads `kcdx-dev.log`
  (`agent-builds-and-deploys.md`).
- **No author surface moves** (design §1) — US-1: no `kcdx.toml` / `plugin.lua` /
  C++ interface change is required of any existing plugin. This is a pure
  internal-quality change.

## Build-gated unknowns (design §9 — probes, not design forks)

Each resolved by a probe at/before its dependent step (`results-driven.md`):

- **U1 — `ctx.rip`→three-modes (gates Phase 3).** Does `ctx.rip = resume_addr`
  skip the captured instruction (CAP-04b/c), and leaving it run it (CAP-04a/d)?
  Resolved by Phase 1 step 2 (the spike — DONE). **The whole Phase 3 rewrite is
  provisional on this.**
- **U2 — trampoline-callable contract.** Does safetyhook's `.original()` /
  trampoline entry deref-and-call cleanly from the existing asmjit call-original
  thunk, like MinHook's `pOriginal`? Probed alongside U1 (an around-mode hook
  through the safetyhook backend).
- **U3 — resume_addr ownership.** Does safetyhook expose the past-the-instruction
  resume address, or does kcdx still compute it (hde64 accumulate-to-≥5)? If kcdx
  still computes it, the existing hde64 logic is retained as a small helper.
- **U4 — stack-expression capture coverage.** Does every `[rbp+0x10]`-style
  capture form have a `Context64` equivalent? A form with no equivalent is
  surfaced, not dropped.
- **U5 — routing-predicate mechanism.** HOW the install context is read at
  `InstallRuntime` (a threaded flag / an explicit engine-internal backend arg / a
  context probe). The build picks the mechanism that makes a loader-lock-install
  misroute IMPOSSIBLE, not merely unlikely. The OUTCOME (the §4.2 table) is
  settled; the mechanism is the executor's.
- **U6 — safetyhook license + vendoring.** Verify safetyhook's license against
  the repo allowlist BEFORE vendoring; record the manifest row same-change
  (`dependencies.md`). Re-confirm the header facts (`Context64`, `enable()`
  thread-suspend, E9→FF) against the source once on disk. (DONE — Phase 1 step 1.)
- **U7 — multi-target batch window (gates the batch path, design §9.7).** Does a
  single `trap_threads` frozen window safely patch N independent targets
  (iterating N prologue writes inside one `run_fn`), the way `enable()` patches
  one? The primitive is documented for one `[from, to)` range; the multi-target
  reuse is feasible-from-the-primitive but UNOBSERVED end-to-end. A `comp-NN`
  fixture creates N>1 hooks `StartDisabled`, patches all in one window, confirms
  all fire. Outcome: all fire → the batch path proceeds; any miss/instability →
  fall back to per-hook `enable()` (unbatched but correct). **The batch path is
  provisional on this.**
- **U8 — `InstallRuntime` caller-set (gates the `g_installed` removal, design
  §9.8).** Is `InstallRuntime`'s ONLY caller the chain's first-hook-per-target
  path (which `FindChain` already gates), or does a non-chain caller exist that
  `g_installed` was guarding? A checkable fact — grep the `InstallRuntime` call
  sites before removing the map. Outcome: chain-only → remove `g_installed`; a
  non-chain caller exists → that caller's double-install guard is re-homed before
  the map is removed, never dropped silently.

## Independence from Phase 11 (shim-VM / FIX A) — recorded for the executor

This effort is **orthogonal to Phase 11** and does NOT block on it (verified
2026-06-07):

- **Different layers.** Phase 11 owns the Lua VM (kcdx builds it, engine adopts
  it, the dual-Lua hazard dies). This owns the detour byte-patcher beneath the
  chain install. No build-order dependency between them.
- **The one shared file is settled.** Phase 11's `early_hook` relocation (its
  Phase-3 step 1) is ALREADY DONE (`3b99fea`) — `src/early_hook.{h,cpp}` is at its
  permanent home. This effort only ROUTES `early_hook` to MinHook (a routing-table
  entry, design §8: "early_hook's body is unchanged"); it does not edit
  `early_hook` itself.
- **The frealloc-canary interaction (a gift, not a blocker).** The frealloc canary
  (`src/hooks.cpp` `ArmFreallocProbe` / `HookedFrealloc`) is a MinHook detour that
  exists only to watch the dual-Lua hazard (`lua-bridge.md` PROBE Q). When Phase
  11 lands and the hazard dies by construction, the canary becomes removable. This
  effort routes the canary to MinHook (a bootstrap-timing hook) regardless of
  whether it exists — Phase 11 landing first means one fewer MinHook hook to
  carry; landing later means the canary rides MinHook until retired. EITHER ORDER
  WORKS; the marriage never blocks on the canary's retirement.

These two efforts may run in parallel (the repo runs parallel chats on one tree —
`concurrency-git.md`); stage by exact path.

## Coverage map — every design element → its step

`Covered by` is a step ref or `DEFERRED`/`OUT-OF-SCOPE`. The design's own §10
reserved items are the design's settled out-of-scope (decided by the user during
`/design`), recorded here as such — NOT new `/plan` deferrals.

| Design element | Source § | Covered by | Notes |
|---|---|---|---|
| E1 — `IDetourBackend` interface | §4.1, §8 | Phase 2 step 3 | create/enable/disable/get_original; built behind `detour_hook`, relocated in step 4 |
| E2 — `MinHookBackend` | §4.1, §8 | Phase 2 step 3 | the MinHook bodies, reused verbatim |
| E3 — `SafetyhookBackend` | §4.1, §8 | Phase 2 step 4b | over `safetyhook::InlineHook` |
| E4 — backend seam relocates to `InstallRuntime`; `detour_hook` dissolves | §4.1, §8 | Phase 2 step 4a | the seam moves out from behind `detour_hook`; the husk is removed (behavior-preserving, still MinHook) |
| E5 — install-context routing predicate @ `InstallRuntime` | §4.2, §9.5 | Phase 2 step 5 | misroute-impossible (U5) |
| E6 — call-original `get_original` bridge | §4.4 | Phase 2 step 4b (+ proven in P1 step 2) | both backends populate the backend-owned JIT slot |
| E7 — mid-hook spike (cap-04 port) | §9.1, §9.2 | Phase 1 step 2 | gates Phase 3 (U1, U2, U3, U4) |
| E8 — `make_jit_midfunc` full replacement | §5, §5.1 | Phase 3 step 6 | the three modes via `ctx.rip` |
| E9 — named captures → Context64 writeback | §5.2 | Phase 3 step 6 | `args.rdx:get/set` rewired |
| E10 — resume_addr ownership | §5.1, §9.3 | Phase 1 step 2 | resolved by the spike (U3) |
| E11 — stack-expression capture coverage | §5.2, §9.4 | Phase 1 step 2 | resolved by the spike (U4) |
| E12 — Lua dispatch rewired onto Context64 | §5.3 | Phase 3 step 6 | `MidDispatch` reads Context64 |
| E13 — foreign-hook detection (classifier) | §6.1 | Phase 4 step 7 | clean / kcdx-tramp / foreign |
| E14 — foreign-hook chaining | §6.2, §6.3 | Phase 4 step 8 | follow jmp, capture as original |
| E15 — safetyhook license + vendoring | §9.6 | Phase 1 step 1 | U6; manifest row same-change |
| E16 — far-target reach (E9→FF, cap-22) | US-2, §1 | Phase 2 step 4b | falls out of `SafetyhookBackend` |
| E17 — function-entry parity (all cap-NN) | US-1, §1 | Phase 2 step 4b | all rows green on safetyhook |
| E18 — loader-lock safety preserved | US-5, §1 | Phase 2 step 5 | early_hook stays MinHook |
| E19 — backend-layer reference doc | §8 | Phase 6 step 11 | new unit gets its subsystem doc |
| E20 — comp-NN two-mod foreign fixture | US-4, §1 | Phase 4 step 8 | both detours fire, defined order |
| E21 — `g_installed` retires (one conflict model) | §4.6 | Phase 2 step 4a | gated on U8 caller-set check |
| E22 — `InstallRuntime` caller-set probe | §9.8 | Phase 2 step 4a (sub-check) | U8; chain-only → remove; non-chain → re-home guard |
| E23 — batch install (`StartDisabled` + `trap_threads`) | §4.5, §1 | Phase 5 step 9 | create-all-disabled → one frozen window |
| E24 — multi-target batch-window probe | §9.7 | Phase 5 step 9 (probe) | U7; gates the batch path, per-hook fallback |
| E25 — `IDetourBackend` batch API (both backends) | §4.5, §8 | Phase 5 step 9 | safetyhook via trap_threads; MinHook via queue API |
| E26 — comp-NN N-hook batch fixture | §1 | Phase 5 step 10 | N installs through one suspend window |
| (reserved) full MinHook removal | §10 | OUT-OF-SCOPE | design-settled; loader-lock constraint |
| (reserved) configurable foreign policy | §10 | OUT-OF-SCOPE | design-settled; reopens if unsafe-to-chain target surfaces |
| (reserved) foreign unhook/install-later | §6.3, §10 | OUT-OF-SCOPE | design-settled; documented limitation |
| (reserved) a third backend | §10 | OUT-OF-SCOPE | design-settled; the interface future-proofs it |
