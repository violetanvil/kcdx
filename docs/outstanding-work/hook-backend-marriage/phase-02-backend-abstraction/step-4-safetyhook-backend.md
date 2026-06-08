# Step 4 — SafetyhookBackend; seam → InstallRuntime; detour_hook dissolves; g_installed retires

**What.** Implement `SafetyhookBackend` over `safetyhook::InlineHook`, relocate
the backend seam from `detour_hook` to its real home — the install chokepoint
`hook_engine::InstallRuntime` — and route the function-entry `hook_chain` install
through it on the safetyhook backend. This is where the bulk of kcdx's hooks move
to safetyhook, where far-target reach (E9→FF) falls out for free, where the
`detour_hook` slot-owner husk dissolves (the backend owns its relocated-original
slot), and where the redundant `g_installed` first-wins map retires (the chain's
`FindChain`/`CanCoexist` is the sole conflict model). Covers E3, E4, E6, E16, E17,
E21, E22 (`../context.md`).

**Scope (commit-grain).**
- **Resolve U8 FIRST — the `InstallRuntime` caller-set check (E22, design §9.8).**
  Grep every caller of `hook_engine::InstallRuntime` before removing `g_installed`.
  Outcome map: ONLY the chain's first-hook-per-target path (`FindChain`-gated) →
  `g_installed` is redundant, remove it. A non-chain caller exists that the map was
  guarding → re-home that caller's double-install guard before removing the map,
  never drop it silently. This is a checkable fact (read the call sites), resolved
  at the top of the step, not assumed.
- Implement `SafetyhookBackend` (design §4.1, §8): `create` constructs a
  `safetyhook::InlineHook` (its E9→FF25 fallback handles any target distance);
  `enable`/`disable` map onto safetyhook's (thread-suspending) enable/disable;
  `get_original()` returns safetyhook's trampoline entry (`.original()` /
  trampoline address). Map safetyhook's typed `Error` enum onto kcdx's
  install-result reason strings (`logging.md` — name the failure with context;
  the typed errors are richer than MinHook's `MH_ERROR_*`).
- **Relocate the seam to `InstallRuntime` + dissolve `detour_hook` (E4, design
  §4.1, §8 — the load-bearing structural move):** `InstallRuntime(name, target,
  detour)` becomes the backend-dispatching install — `create` → `enable` → write
  the backend's relocated-original into the JIT slot the JIT bakes. The backend
  now OWNS that slot (the `void**`); `detour_hook` — only the JIT-slot-owner whose
  `enable()`/`disable()` were dead on the chain path — is REMOVED, and
  `runtime_func_t`'s dead `m_detour` member + its slot plumbing go with it. The
  step-3 `IDetourBackend`/`MinHookBackend` move out from behind `detour_hook`; the
  bodies are unchanged.
- **The get_original bridge (design §4.4):** the JIT codegen does NOT change — it
  still derefs a pointer slot; `InstallRuntime` writes the right value into the
  backend-owned slot (MinHook's `pOriginal`, or safetyhook's trampoline entry).
  **This rests on U2** (the trampoline-callable contract) — proven in Phase 1
  step 2; a surfaced mismatch is resolved here, not worked around silently.
- **Retire `g_installed` (E21, design §4.6):** with U8 confirming chain-only (or
  the non-chain guard re-homed), remove the first-wins map (`src/hook_engine.cpp`).
  The chain's `FindChain(target)` + `CanCoexist` is the sole conflict model — one
  model for one concern.
- Route the function-entry `hook_chain` install (the chain's
  one-detour-per-target path that calls `InstallRuntime`) to use `SafetyhookBackend`.
  The chain, `CanCoexist`, the ordered `ChainEntry` vector, the engine-stamp, the
  off-thread marshaling are UNCHANGED (design §4.3 — safetyhook is "just the
  patcher"). Mid-function stays on the old path this step (Phase 3 replaces it).
- early_hook + the update pump + the frealloc canary stay on MinHook (no routing
  predicate yet — they keep calling MinHook directly; Step 5 formalizes the
  selection at `InstallRuntime`). This step routes ONLY the function-entry chain
  path to safetyhook.

**Test bar.** A regression proof + the far-target proof + the dissolution proof:
- The function-entry cap-NN rows (the ones that install through the chain's
  function-entry path) pass live on the safetyhook backend, routed through
  `InstallRuntime` — matrix unchanged (E17).
- The cap-22 far-module rows pass with ZERO "not rel32-reachable" failures across
  repeated launches, and the proof is safetyhook's E9→FF fallback reaching the
  target, NOT the per-module branch-pool anchor being the saving edge (E16). A
  FALSIFIABLE claim: cap-22 FAILS if a far-module install reports unreachable.
- With `detour_hook` removed and `g_installed` retired, the FULL existing cap-NN
  suite still passes live, matrix `X/Y passing` unchanged from the step-3 baseline
  — proof the dissolution + the single-conflict-model removed nothing the chain
  needed (E4, E21). A FALSIFIABLE claim: any row that regresses is a FAIL.
- Agent builds + deploys + enables dev mode, user launches, agent reads
  `kcdx-dev.log` (`agent-builds-and-deploys.md`).

**Dependencies.** Step 3 (the `IDetourBackend` interface + `MinHookBackend` must
exist to relocate + add a second implementation; DONE `64fba7d`). Phase 1 step 1
(safetyhook vendored; DONE). Phase 1 step 2 (U2 — the trampoline-callable contract
proven; the bridge rests on it; DONE).

**Design authority.** [`hook-backend-marriage.md §4.1, §4.3, §4.4, §4.6, §7, §8, §9.8`](../../../design/hook-backend-marriage.md)
+ US-1/US-2 — the backend contract, the InstallRuntime seam, the chain-untouched
invariant, the single-conflict-model retirement, and the far-target criterion are
built to the design, not this doc's prose summary.

**Disassembler-test / author-burden note.** None — the backend swap + the seam
relocation are invisible to authors (US-1: no `kcdx.toml`/`plugin.lua`/C++
interface change). Far-target reach is the engine doing more heavy lifting, not a
new author burden.

**Reference.** [`../context.md`](../context.md) E3/E4/E6/E16/E17/E21/E22 + U2 + U8
+ the "chain untouched" / "get_original contract" / "detour_hook dissolves" /
"one conflict model" invariants.
