# Step 4a — relocate seam → InstallRuntime; dissolve detour_hook; retire g_installed

**What.** Move the backend seam from `detour_hook` to its real home — the install
chokepoint `hook_engine::InstallRuntime` — dissolve the `detour_hook` slot-owner
husk (the slot STORAGE moves onto `runtime_func_t`; the backend POPULATES it, per
the design's settled model §4.4), and retire the redundant role of the `g_installed`
first-wins map while RE-HOMING its unique cross-registry guard into `InstallRuntime`
(the U8 check resolved a non-chain caller, `dynamic_hook`, needs it — §4.6). This is
a BEHAVIOR-PRESERVING structural refactor — EVERY hook is STILL MinHook, exactly as
today; only WHERE the slot lives, WHERE the backend attaches, and WHERE the
cross-registry guard lives moves. The step-3 `IDetourBackend`/`MinHookBackend` move
out from behind `detour_hook`; their bodies are unchanged. Covers E4, E21, E22
(`../context.md`).

> **The two U8/slot-model gaps are SETTLED (design commit `02221f3`).** Build to
> the resolved §4.4 + §4.6: (a) `runtime_func_t` owns the slot storage, the backend
> populates the value — NOT "the backend owns the slot" (the callsite path installs
> no backend yet needs the slot, so the slot cannot live in a backend); (b)
> `g_installed`'s unique cross-registry double-install guard re-homes into
> `InstallRuntime` as a minimal per-target installed-set preserving the loud
> owner-naming refusal — it is NOT removed outright (`dynamic_hook` is the non-chain
> caller that needs it).

**Scope (commit-grain).**
- **Resolve U8 FIRST — the `InstallRuntime` caller-set check (E22, design §9.8).**
  Grep every caller of `hook_engine::InstallRuntime` before removing `g_installed`.
  KNOWN (read this session, `src/hook_engine.cpp:14-22`): TWO callers reach it —
  the `kcdx.hook` / `hook_chain.cpp` path AND `kcdx.memory.dynamic_hook`. Confirm
  the full set and whether either relies on `g_installed`'s first-wins refusal for
  a guarantee the chain's `FindChain` does NOT already provide. Outcome map: both
  paths gated by the chain's per-target logic → `g_installed` is redundant, remove
  it. A caller that genuinely needs the first-wins refusal (e.g. `dynamic_hook`
  installing outside the chain) → re-home that guard before removing the map, never
  drop it silently (`logging.md` — the refusal stays loud).
- **Relocate the seam to `InstallRuntime` + dissolve `detour_hook` (E4, design
  §4.1, §4.4, §8):** `InstallRuntime(name, target, detour)` becomes the
  backend-dispatching install — it owns an `IDetourBackend` (a `MinHookBackend`
  this step, no routing yet — that is Step 5), calls `create` → `enable`, and writes
  the backend's relocated-original into `runtime_func_t`'s call-original slot.
  **`runtime_func_t` OWNS the slot STORAGE** (a plain member with a stable address
  the JIT bakes — moved off `detour_hook`); the backend POPULATES it (§4.4 — the
  callsite path installs no backend yet writes the slot directly, so the slot
  cannot live in a backend). `detour_hook` — only the JIT-slot-owner whose
  `enable()`/`disable()` are dead on the chain path — is REMOVED, and
  `runtime_func_t`'s dead `m_detour` member is removed (the slot it held becomes
  `runtime_func_t`'s own member). The `MinHookBackend` bodies (the
  `MH_CreateHook`/`Enable`/`Disable`/`Remove` calls) are reused VERBATIM — only
  their call site moves from `detour_hook` into the `InstallRuntime` dispatch.
- **The get_original bridge stays MinHook this step (design §4.4):** the JIT codegen
  does NOT change — it still derefs a pointer slot; `InstallRuntime` writes MinHook's
  `pOriginal` into `runtime_func_t`'s slot exactly as it does today. No safetyhook
  this step (that is 4b).
- **Retire `g_installed`'s redundant role + re-home its unique guard (E21, design
  §4.6):** the U8 check confirmed `dynamic_hook` is a non-chain caller for which
  `g_installed` is the ONLY cross-registry double-install refusal. So: remove the
  redundant first-wins consultation for the chain's `FindChain`-gated path, and
  RE-HOME the cross-registry guard into `InstallRuntime` as a minimal per-target
  installed-set — preserving the EXACT loud owner-naming refusal ("already hooked by
  '<first owner>'", `logging.md`). The chain's `FindChain` front-runs it for chain
  hooks (it never fires redundantly there); the seam guard fires only for the
  `dynamic_hook` cross-registry collision. Do NOT drop the loud refusal (AP14).
- **Touches-existing-code — the grep-every-caller rule applies** (CLAUDE.md hard
  rule): `InstallRuntime`'s behavior changes and `detour_hook` is removed, so grep
  every caller/consumer of `InstallRuntime`, `detour_hook`, and
  `runtime_func_t::m_detour` / `get_original_ptr` before the change; update every
  site atomically; leave no caller on the old API.

**Test bar.** This is a behavior-preserving refactor — the bar is REGRESSION, the
exact step-3 proof shape. The FULL existing cap-NN suite (function-entry + mid +
around + callsite) passes live, matrix `X/Y passing` unchanged from the step-3
baseline at the same commit — EVERY hook is still MinHook, the seam just relocated
and the slot moved onto `runtime_func_t`. A FALSIFIABLE claim: any cap-NN row that
regresses, OR a cross-registry double-install (a `dynamic_hook` colliding with a
chain hook on a shared VA) that the re-homed guard fails to refuse with its loud
owner-naming message, is a FAIL. Build-green is necessary, not sufficient — a
silently-broken detour or a lost conflict-guard compiles fine; only the live matrix
+ the cross-registry refusal check prove the refactor preserved behavior
(`anti-patterns.md` §invariants-vs-gates). Agent builds + deploys + enables dev
mode, user launches, agent reads `kcdx-dev.log` (`agent-builds-and-deploys.md`).

**Dependencies.** Step 3 (the `IDetourBackend` interface + `MinHookBackend` must
exist to relocate; DONE `64fba7d`). NOT dependent on Phase 1 (no safetyhook this
step — it stays all-MinHook). Blocks Step 4b (the safetyhook swap routes through the
relocated `InstallRuntime` seam this step establishes).

**Design authority.** [`hook-backend-marriage.md §4.1, §4.4, §4.6, §8, §9.8`](../../../design/hook-backend-marriage.md)
— the InstallRuntime seam, the detour_hook dissolution, the single-conflict-model
retirement, and the get_original contract are built to the design, not this doc's
prose summary.

**Disassembler-test / author-burden note.** None — internal engine refactor, no
author surface, no game-address resolution.

**Reference.** [`../context.md`](../context.md) E4/E21/E22 + U8 + the "detour_hook
dissolves" / "one conflict model" / "get_original contract" invariants.
