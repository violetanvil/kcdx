# Step 3 — IDetourBackend + MinHookBackend (pure refactor)

**What.** Introduce the `IDetourBackend` interface and extract `detour_hook`'s
current MinHook calls into a `MinHookBackend` implementation; `detour_hook`
becomes a thin coordinator that holds a backend and delegates to it. This is a
BEHAVIOR-PRESERVING refactor — every hook still runs on MinHook, exactly as
today — that establishes the seam Step 4 swaps safetyhook into. Covers design
elements E1, E2 (`../context.md`).

> **DONE — `64fba7d`.** Landed correctly: `IDetourBackend` + `MinHookBackend`
> exist, behind `detour_hook`, behavior-preserving (the 187/218 FAIL set is
> identical pre/post). The §4 re-grounding moved the backend's permanent home from
> `detour_hook` to `hook_engine::InstallRuntime` — this step's interface + bodies
> are CORRECT and reused verbatim; Step 4 relocates their attachment point and
> dissolves the `detour_hook` coordinator husk (E4 moved to Step 4). No rework of
> the landed code; only its seam relocates.

**Scope (commit-grain).**
- Define `IDetourBackend` (design §4.1): `create(target, detour) -> original_ptr |
  error`, `enable() -> ok | error`, `disable() -> ok | error`, `get_original() ->
  trampoline_ptr`. Core-layer contract — depends on nothing kcdx-specific
  (`structure-by-responsibility.md` §2: core has no upward deps).
- Extract `MinHookBackend` (design §8): move the bodies of `detour_hook::enable`
  (`MH_CreateHook` + `MH_EnableHook`), `disable` (`MH_DisableHook`), the dtor
  (`MH_RemoveHook`), and `get_original_ptr` (the `original_` slot) into a
  `MinHookBackend` implementing `IDetourBackend`. Verbatim behavior — same
  status-handling, same logging, same `original_` semantics.
- Reshape `detour_hook` into a coordinator (design §8): it owns a
  `unique_ptr<IDetourBackend>` (defaulting to `MinHookBackend` this step — no
  routing yet, that's Step 5), and `set_instance`/`enable`/`disable`/
  `get_original_ptr` delegate to it. The face `runtime_func_t` calls is UNCHANGED
  (the three JIT-thunk sites that deref `get_original_ptr()` see identical
  behavior).
- The `get_original()` contract (design §4.4): `MinHookBackend::get_original()`
  returns MinHook's `pOriginal` exactly as `detour_hook` does today. The JIT
  codegen does NOT change — it still derefs a pointer slot the backend populates.
- No safetyhook reference this step (that's Step 4). No mid-hook change (Phase 3).

**Test bar.** No NEW cap-NN row — this is a refactor of an existing path. The bar
is REGRESSION: the FULL existing cap-NN hook suite (function-entry + mid + around
+ callsite) passes live, matrix `X/Y passing` unchanged from the pre-refactor
baseline at the same commit. Agent builds + deploys + enables dev mode, user
launches once, agent reads `kcdx-dev.log` and confirms the matrix is unchanged
(`agent-builds-and-deploys.md`). Build-green is necessary, not sufficient — a
silently-broken detour compiles fine; only the live matrix proves the seam is
behavior-preserving (`anti-patterns.md` §invariants-vs-gates).

**Dependencies.** None on Phase 1 (this is the interface refactor; it needs no
safetyhook). Could land before or in parallel with Phase 1 — but ordered here so
the backend layer is one coherent phase. (If sequenced strictly, Phase 1 step 1
vendoring does not block this; this blocks Step 4.)

**Design authority.** [`hook-backend-marriage.md §4.1, §4.4, §8`](../../../design/hook-backend-marriage.md)
— the interface signature + the get_original contract + the unit boundaries are
built to the design, not this doc's summary.

**Disassembler-test / author-burden note.** None — internal engine refactor, no
author surface, no game-address resolution.

**Reference.** [`../context.md`](../context.md) E1/E2/E4 + the "chain untouched"
+ "get_original contract" invariants.
