# Step 4b — SafetyhookBackend + route function-entry; far-target reach

**What.** Implement `SafetyhookBackend` over `safetyhook::InlineHook` and route the
function-entry `hook_chain` install through the relocated `InstallRuntime` seam
(step 4a) to it. This is where the bulk of kcdx's hooks MOVE to safetyhook — a
BEHAVIOR change — and where far-target reach (E9→FF) falls out for free. Covers E3,
E6, E16, E17 (`../context.md`).

**Scope (commit-grain).**
- Implement `SafetyhookBackend` (design §4.1, §8) as a second `IDetourBackend`:
  `create` constructs a `safetyhook::InlineHook` (its E9→FF25 fallback handles any
  target distance); `enable`/`disable` map onto safetyhook's (thread-suspending)
  enable/disable; `get_original()` returns safetyhook's trampoline entry
  (`.original()` / trampoline address). Map safetyhook's typed `Error` enum onto
  kcdx's install-result reason strings (`logging.md` — name the failure with
  context; the typed errors are richer than MinHook's `MH_ERROR_*`).
- **The get_original bridge (design §4.4 — the load-bearing integration detail):**
  `SafetyhookBackend::get_original()` returns safetyhook's trampoline entry, which
  `InstallRuntime` writes into `runtime_func_t`'s call-original slot — the SAME slot
  the asmjit thunks deref, the one 4a moved onto `runtime_func_t` (the backend
  PRODUCES the value, `runtime_func_t` owns the slot; §4.4). The JIT codegen does
  NOT change. **This rests on U2** (the trampoline-callable contract) — proven in
  Phase 1 step 2; a surfaced mismatch is resolved here, not worked around silently.
- Route the function-entry `hook_chain` install (the chain's one-detour-per-target
  path that calls `InstallRuntime`) to use `SafetyhookBackend`. The chain,
  `CanCoexist`, the ordered `ChainEntry` vector, the engine-stamp, the off-thread
  marshaling are UNCHANGED (design §4.3 — safetyhook is "just the patcher").
  Mid-function stays on the old path this step (Phase 3 replaces it).
- early_hook + the update pump + the frealloc canary + `dynamic_hook` stay on
  MinHook (no routing predicate yet — Step 5 formalizes the selection at
  `InstallRuntime`). This step routes ONLY the function-entry chain path to
  safetyhook; for now that is a direct backend choice at the chain install site,
  which Step 5 replaces with the context-driven predicate.

**Test bar.** A regression proof + the far-target proof:
- The function-entry cap-NN rows (the ones that install through the chain's
  function-entry path) pass live on the safetyhook backend — matrix unchanged (E17).
- The cap-22 far-module rows pass with ZERO "not rel32-reachable" failures across
  repeated launches, and the proof is safetyhook's E9→FF fallback reaching the
  target, NOT the per-module branch-pool anchor being the saving edge (E16). A
  FALSIFIABLE claim: cap-22 FAILS if a far-module install reports unreachable.
- The non-function-entry rows (mid + the MinHook bootstrap paths) STILL pass — this
  step moves only the function-entry path; a regression elsewhere means the routing
  bled past its scope. Agent builds + deploys + enables dev mode, user launches,
  agent reads `kcdx-dev.log` (`agent-builds-and-deploys.md`).

**Dependencies.** Step 4a (the seam at `InstallRuntime` + the backend-owned slot
must exist to add a second backend and route to it). Phase 1 step 1 (safetyhook
vendored; DONE). Phase 1 step 2 (U2 — the trampoline-callable contract proven; the
bridge rests on it; DONE).

**Design authority.** [`hook-backend-marriage.md §4.1, §4.3, §4.4, §8`](../../../design/hook-backend-marriage.md)
+ US-1/US-2 — the backend contract, the chain-untouched invariant, and the
far-target criterion are built to the design, not this doc's prose summary.

**Disassembler-test / author-burden note.** None — the backend swap is invisible to
authors (US-1: no `kcdx.toml`/`plugin.lua`/C++ interface change). Far-target reach
is the engine doing more heavy lifting, not a new author burden.

**Reference.** [`../context.md`](../context.md) E3/E6/E16/E17 + U2 + the "chain
untouched" / "get_original contract" invariants.
