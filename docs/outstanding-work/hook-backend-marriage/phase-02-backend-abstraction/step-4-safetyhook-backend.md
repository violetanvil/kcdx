# Step 4 — SafetyhookBackend + get_original bridge

**What.** Implement `SafetyhookBackend` over `safetyhook::InlineHook`, bridge the
get_original contract (§4.4) so the existing JIT thunks read safetyhook's
trampoline through the same pointer slot they read MinHook's, and route the
function-entry `hook_chain` install path to it. This is where the bulk of kcdx's
hooks move to safetyhook — and where far-target reach (E9→FF) falls out for free.
Covers E3, E6, E16, E17 (`../context.md`).

**Scope (commit-grain).**
- Implement `SafetyhookBackend` (design §4.1, §8): `create` constructs a
  `safetyhook::InlineHook` (its E9→FF25 fallback handles any target distance);
  `enable`/`disable` map onto safetyhook's (thread-suspending) enable/disable;
  `get_original()` returns safetyhook's trampoline entry (`.original()` /
  trampoline address). Map safetyhook's typed `Error` enum onto kcdx's
  install-result reason strings (`logging.md` — name the failure with context;
  the typed errors are richer than MinHook's `MH_ERROR_*`).
- **The get_original bridge (design §4.4 — the load-bearing integration detail):**
  `SafetyhookBackend::get_original()` writes safetyhook's trampoline entry into
  the SAME slot the asmjit call-original thunks deref (`runtime_func_t`'s three
  sites — the call-original deref, the around path, the mid-resume slot for
  function-entry). The JIT codegen does NOT change. **This rests on U2** (the
  trampoline-callable contract) — proven in Phase 1 step 2; if the spike surfaced
  a mismatch, that finding is resolved here, not worked around silently.
- Route the function-entry `hook_chain` install (the `AddC` / `InstallRuntime`
  path that today calls `detour_hook` → MinHook) to use `SafetyhookBackend` for
  the chain's one-detour-per-target install. The chain, `CanCoexist`, the ordered
  `ChainEntry` vector, the engine-stamp, the off-thread marshaling are UNCHANGED
  (design §4.3 — safetyhook is "just the patcher"). Mid-function stays on the old
  path this step (Phase 3 replaces it).
- early_hook + the update pump + the frealloc canary stay on MinHook (no routing
  predicate yet — they keep calling MinHook directly; Step 5 formalizes the
  selection). This step routes ONLY the function-entry chain path to safetyhook.

**Test bar.** A regression proof + the far-target proof:
- The function-entry cap-NN rows (the ones that install through the chain's
  function-entry path) pass live on the safetyhook backend — matrix unchanged.
- The cap-22 far-module rows pass with ZERO "not rel32-reachable" failures across
  repeated launches, and the proof is safetyhook's E9→FF fallback reaching the
  target, NOT the per-module branch-pool anchor being the saving edge (E16). A
  FALSIFIABLE claim: cap-22 FAILS if a far-module install reports unreachable.
- Agent builds + deploys + enables dev mode, user launches, agent reads
  `kcdx-dev.log` (`agent-builds-and-deploys.md`).

**Dependencies.** Step 3 (the `IDetourBackend` interface + the `detour_hook`
coordinator must exist to add a second implementation). Phase 1 step 1 (safetyhook
vendored). Phase 1 step 2 (U2 — the trampoline-callable contract proven; the
bridge rests on it).

**Design authority.** [`hook-backend-marriage.md §4.1, §4.3, §4.4, §8`](../../../design/hook-backend-marriage.md)
+ US-1/US-2 — the backend contract + the chain-untouched invariant + the
far-target criterion are built to the design.

**Disassembler-test / author-burden note.** None — the backend swap is invisible
to authors (US-1: no `kcdx.toml`/`plugin.lua`/C++ interface change). Far-target
reach is the engine doing more heavy lifting, not a new author burden.

**Reference.** [`../context.md`](../context.md) E3/E6/E16/E17 + U2 + the
"chain untouched" / "get_original contract" invariants.
