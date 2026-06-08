# Step 4a — relocate seam → InstallRuntime; dissolve detour_hook; retire g_installed

**What.** Move the backend seam from `detour_hook` to its real home — the install
chokepoint `hook_engine::InstallRuntime` — dissolve the `detour_hook` slot-owner
husk (the backend owns its relocated-original slot now), and retire the redundant
`g_installed` first-wins map (the chain's `FindChain`/`CanCoexist` is the sole
conflict model). This is a BEHAVIOR-PRESERVING structural refactor — EVERY hook is
STILL MinHook, exactly as today; only WHERE the backend attaches and which conflict
model gates moves. The step-3 `IDetourBackend`/`MinHookBackend` move out from behind
`detour_hook`; their bodies are unchanged. Covers E4, E21, E22 (`../context.md`).

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
  §4.1, §8):** `InstallRuntime(name, target, detour)` becomes the
  backend-dispatching install — it owns an `IDetourBackend` (a `MinHookBackend`
  this step, no routing yet — that is Step 5), calls `create` → `enable`, and writes
  the backend's relocated-original into the JIT slot. The backend now OWNS that slot
  (the `void**` the JIT bakes); `detour_hook` — only the JIT-slot-owner whose
  `enable()`/`disable()` are dead on the chain path — is REMOVED, and
  `runtime_func_t`'s dead `m_detour` member + its slot plumbing go with it. The
  `MinHookBackend` bodies (the `MH_CreateHook`/`Enable`/`Disable`/`Remove` calls)
  are reused VERBATIM — only their call site moves from `detour_hook` into the
  `InstallRuntime` dispatch.
- **The get_original bridge stays MinHook this step (design §4.4):** the JIT codegen
  does NOT change — it still derefs a pointer slot; `InstallRuntime` writes MinHook's
  `pOriginal` into the backend-owned slot exactly as it does today. No safetyhook
  this step (that is 4b).
- **Retire `g_installed` (E21, design §4.6):** with U8 confirming the chain's
  `FindChain` is the sole gate (or the non-chain guard re-homed), remove the
  first-wins map (`src/hook_engine.cpp`). One conflict model for one concern.
- **Touches-existing-code — the grep-every-caller rule applies** (CLAUDE.md hard
  rule): `InstallRuntime`'s behavior changes and `detour_hook` is removed, so grep
  every caller/consumer of `InstallRuntime`, `detour_hook`, and
  `runtime_func_t::m_detour` / `get_original_ptr` before the change; update every
  site atomically; leave no caller on the old API.

**Test bar.** This is a behavior-preserving refactor — the bar is REGRESSION, the
exact step-3 proof shape. The FULL existing cap-NN suite (function-entry + mid +
around + callsite) passes live, matrix `X/Y passing` unchanged from the step-3
baseline at the same commit — EVERY hook is still MinHook, the seam just relocated
and `g_installed` is gone. A FALSIFIABLE claim: any cap-NN row that regresses, or a
double-install that the removed `g_installed` would have caught now slipping
through, is a FAIL. Build-green is necessary, not sufficient — a silently-broken
detour or a lost conflict-guard compiles fine; only the live matrix + the
double-install check prove the refactor preserved behavior (`anti-patterns.md`
§invariants-vs-gates). Agent builds + deploys + enables dev mode, user launches,
agent reads `kcdx-dev.log` (`agent-builds-and-deploys.md`).

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
