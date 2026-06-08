# Step 9 — batch-install mechanism (probe-gated, U7)

**What.** Add a batch-install path to `IDetourBackend` so N detours install under
ONE thread-suspend window instead of N. safetyhook's per-`enable()` `trap_threads`
suspend does not scale (hundreds of stop-the-world cycles at TC/multiplayer boot).
The path: create all N hooks `StartDisabled` (trampoline allocated + relocated,
prologue NOT yet patched), then run ONE `trap_threads`-equivalent whose `run_fn`
writes all N prologue jumps inside a single frozen window. The U7 probe gates the
whole step — the multi-target frozen-window reuse is feasible-from-the-primitive
but UNOBSERVED end-to-end. Covers E23, E24 (the probe), E25 (`../context.md`).

**Scope (commit-grain).**
- **Resolve U7 FIRST — the multi-target-window probe (E24, design §9.7).** Before
  wiring the batch path into the engine, a `test-plugins/` probe (or engine-side
  diagnostic) creates N>1 `safetyhook::InlineHook`s with `StartDisabled`, then
  patches all N inside one `trap_threads` window, and confirms all N fire live.
  Outcome map: ALL fire → the batch path is sound, proceed to wire it; ANY
  miss/crash/instability → the multi-target reuse is unsafe, FALL BACK to per-hook
  `enable()` (the unbatched baseline — correct, just unscaled), record the finding,
  and the batch API degrades to sequential enable. The probe leaves no residue in
  live source (`working-artifacts.md` — capture finding + wiring to
  `_research/`, remove the probe).
- **The batch API on `IDetourBackend` (E25, design §4.5, §8):** add a batch entry
  point (e.g. `enable_batch(span<backend*>)` or an engine-level "install set"
  collector → one apply). Each backend implements it natively:
  - `SafetyhookBackend` — create-all-disabled (`StartDisabled`), then one
    `trap_threads` window patches the set (gated on U7 PASS; else sequential).
  - `MinHookBackend` — `MH_QueueEnableHook` per hook + one `MH_ApplyQueued`
    (MinHook batch-applies; verify against `vendor/minhook/include/MinHook.h` that
    the apply imposes no per-hook suspend the batch would still pay — a marked
    assumption-to-probe at this step, design §4.5).
- **The batch boundary** — wire the batch path at a known install boundary (boot,
  a plugin's install set) where N hooks are created together. A single install
  still uses the per-hook path (the batch is additive, never the only path).
- The chain / conflict model / routing predicate are UNCHANGED — the batch path
  is a different way to APPLY the same per-target installs the chain already
  decided, not a new conflict surface.

**Test bar.** The probe + a regression:
- **U7 probe (the gate):** N>1 hooks installed through one `trap_threads` window
  all fire live — a FALSIFIABLE claim: any hook in the batch that does not fire,
  or any instability, is a FAIL and triggers the per-hook fallback. (The probe is
  throwaway; its finding is captured to `_research/` and its result drives the
  wiring decision.)
- **Regression:** with the batch path wired, the full cap-NN suite still passes
  live (single installs unaffected — the batch path is additive).
- Agent builds + deploys + enables dev mode, user launches, agent reads
  `kcdx-dev.log` (`agent-builds-and-deploys.md`).

**Dependencies.** Phase 2 step 4 (`SafetyhookBackend` + `MinHookBackend` must
exist to add a batch method to; the seam is at `InstallRuntime`). Phase 2 step 5
(the routing predicate — the batch path routes the same way per-install does).
The U7 probe is the FIRST thing this step does (a checkable unknown resolved by an
earlier probe within the step, `incremental-delivery.md` + `results-driven.md`).

**Design authority.** [`hook-backend-marriage.md §4.5, §8, §9.7`](../../../design/hook-backend-marriage.md)
— the batch mechanism (`StartDisabled` + `trap_threads`), the per-backend native
implementation, and the probe-gate are built to the design, not this doc's prose.

**Disassembler-test / author-burden note.** None — batch install is engine-
internal (the engine decides when a boot/plugin install set batches); no author
knob, no game-address resolution. The author still just declares hooks.

**Reference.** [`../context.md`](../context.md) E23/E24/E25 + U7 + the "batch is
additive, single install unaffected" invariant.
