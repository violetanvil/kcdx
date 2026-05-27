# cap-21 / cap-22 hooks fail to install when the target is far from WHGame.dll

## Symptom

In the 2026-05-22 09:56 launch (`kcdx-engine/logs/kcdx-dev_.log`), cap-21
(mode=mid) and cap-22 (mode=callsite) hooks FAILED TO INSTALL — regressing
from PASS in the prior (09:36) launch of the same plugins. Two distinct
install-time errors:

- **cap-21 (mid):** all four hooks aborted with
  `MH_CreateHook failed (MH_ERROR_MEMORY_ALLOC)` at the RWX-stub addresses
  (`~0x000001D865070002`).
- **cap-22 (callsite):** all four redirects failed with
  `callsite redirect failed: trampoline 0x00007FFA808C0XXX is not
  rel32-reachable from the call site 0x00007FFB5F2816XX (distance ~-3.7GB
  > 2GB)`.

The downstream effect: the cap-21/cap-22 `kcdx.test.report` PASS rows became
FAIL ("`:set()` did not change result", "callsite before-arg-mutation did
not take effect", etc.) — but those reports are MISLEADING: the behavior
didn't fail, the hook never installed, so the unhooked target returned its
vanilla value. The real failure is upstream at install.

## Facts

- Branch pool reserved at `0x00007FFA808C0000`, anchored at
  `WHGame.dll base + size/2` (`src/trampoline.cpp` `EnsureBranchPool` /
  `ReserveNearby`). It is a SINGLE WHGame-centric reservation — there is no
  per-target anchoring. (code read, 2026-05-22)
- cap-22 call sites live INSIDE cap-22.dll at `~0x00007FFB5F28…`, ~3.7GB
  from the WHGame-anchored pool → our own `RewriteCallDisplacement`
  rel32-range check (sub-6) correctly refused to write a truncated
  displacement and failed loud. (dev log + code read)
- cap-21 RWX stubs are raw `VirtualAlloc` heap allocations at `~0x1D8…`
  (`test-plugins/cap-21-mid-hook/cap-21.cpp`). MinHook's `MH_CreateHook`
  for a target there searches `±MAX_MEMORY_RANGE` (`0x40000000` = ±1GB) for
  a free trampoline page (`vendor/minhook/src/buffer.c` `GetMemoryBlock`)
  and returned `MH_ERROR_MEMORY_ALLOC` — no free page in range this launch.
  (dev log + code read)
- Deployed cap-21/cap-22 DLLs match their repo builds (timestamps + sizes);
  engine is the sub-7 09:51 build. NOT a stale-artifact issue. (fs check)
- sub-7 (`kcdx.on`/ready, commit 92c0725) did NOT touch `trampoline.cpp`,
  `hook_chain` install, or `hook_engine` — it is an innocent bystander; the
  regression is ASLR address-layout luck (different module placement this
  launch), not a sub-7 code change. (git diff)
- cap-22's two ISOLATION sub-tests (control-caller-unaffected,
  callee-unaffected) still PASS — consistent with "the redirect never
  installed, so of course nothing changed."
- **cap-22 confirmed (PROBE A):** trampoline `0x7FFA808C…` is in the
  WHGame-anchored branch pool; call site `0x7FFB5F28…` is in cap-22.dll;
  distance -3.7GB > 2GB. Cause = the single WHGame-centric pool can't reach
  a far module. REAL ENGINE GAP. (2026-05-22)
- **cap-21 confirmed (PROBE B):** the mid install path
  (`AddMid`→`InstallRuntime`→`MH_CreateHook` at `hook_engine.cpp:516`) uses
  MinHook's OWN trampoline allocator (`buffer.c` ±1GB `GetMemoryBlock`), NOT
  our branch pool. The cap-21 stub is `VirtualAlloc(nullptr, …, RWX)` — a raw
  heap allocation. MinHook's ±1GB scan from that heap address found no free
  page this launch. Cause = MinHook allocator on an unrealistic raw-heap
  target. TEST-FIXTURE ARTIFACT (real plugins hook module code, not heap).
  (2026-05-22)

## Reframe 2026-05-22: this is two different root causes, not one

The two failures share a symptom ("hook didn't install because the
trampoline can't reach the target") but have DIFFERENT allocators and
DIFFERENT ownership:

| | Trampoline allocator | Whose limit | Realistic for a real plugin? |
|---|---|---|---|
| cap-21 (mid) | MinHook `buffer.c` (±1GB scan from target) | MinHook's allocator found no free page near a low-heap stub | NO — real targets are in modules, not raw heap; the stub-in-VirtualAlloc-heap is a TEST-FIXTURE artifact |
| cap-22 (callsite) | kcdx `AllocateBranch`, anchored to WHGame.dll | kcdx's pool is WHGame-only; a far module is unreachable | YES — a TC author hooking another loaded DLL (e.g. BugSplat64.dll) placed >2GB from WHGame hits this for real |

The underlying constraint (trampoline must be within ±2GB of the target for
a 5-byte E9 rel32 jmp) is x86-64 physics, not a MinHook or kcdx defect. What
differs is the allocator STRATEGY, and that is ours to choose.

## Open questions

- **OQ1 (cap-22, lead hypothesis — verify):** is the cap-22 failure fully
  explained by "branch pool anchored to WHGame, target in a far module"? —
  Probe A (read-only): the dev log already shows trampoline VA, call-site
  VA, and the computed distance. If the trampoline is in the WHGame-anchored
  pool and the call site is in cap-22.dll >2GB away, OQ1 is confirmed by the
  existing log + the `EnsureBranchPool` code (no new launch needed). Outcome
  map: (A1) log shows trampoline ∈ WHGame pool & callsite ∈ cap-22.dll, dist
  >2GB → confirmed, it's our pool anchoring → engine-gap candidate. (A2) dist
  <2GB or trampoline not in WHGame pool → something else; re-observe.
- **OQ2 (cap-21 — verify it's MinHook's allocator, not ours):** does the mid
  install path go through MinHook's `MH_CreateHook` (its allocator) and NOT
  our branch pool? — Probe B (read-only, code): trace `AddMid` →
  `InstallRuntime` → `MH_CreateHook` in `hook_engine.cpp`; confirm the
  trampoline for a mid hook is MinHook's, and that the stub VA is the raw
  VirtualAlloc heap address. Outcome map: (B1) mid uses MH_CreateHook over
  the heap stub → cap-21's MH_ERROR_MEMORY_ALLOC is MinHook's ±1GB allocator
  failing on a heap target → test-fixture artifact. (B2) mid uses our pool →
  reframe (same class as cap-22).
- ~~**OQ3 (fix scope — FOR THE USER, after OQ1/OQ2 pinned)**~~ RESOLVED
  2026-05-22 (user): CAP-21 → test-fixture fix (allocate the stub from the
  kcdx branch pool, near WHGame, so MinHook's ±1GB allocator finds a
  trampoline deterministically). CAP-22 → ENGINE fix (per-module trampoline
  pool: `AllocateBranch` anchors near the TARGET's module, not just
  WHGame.dll — real far-module hook capability; cap-22 stays as the
  regression proving far-module callsite works). Two separate concerns →
  two `/execute` cycles.

## Resolution (BOTH FIXES LANDED + LIVE-VERIFIED 2026-05-22)

1. **CAP-21 (test-fixture) — commit `c25b622`, verified 10:22 launch.**
   `cap-21.cpp`'s `AllocStub` now allocates from the kcdx branch pool
   (`kcdxTrampolineInterface::AllocateFromBranchPool`) instead of raw
   `VirtualAlloc(nullptr, …)`. The stub sits in MinHook's window, so
   `MH_CreateHook` finds a trampoline deterministically. Verified: cap-21
   read/write/skip/run 4/4 PASS, ZERO `MH_ERROR_MEMORY_ALLOC`.
2. **CAP-22 (engine capability) — commit `1d86217`, verified 10:43 launch.**
   `trampoline::AllocateBranch` gained an engine-internal `nearVa` param
   (default 0 = WHGame anchor; existing callers unchanged). `runtime_func_t`'s
   two `make_jit_*` sites pass `target_func_ptr`, so a hook's JIT trampoline
   is placed near its target. Reservations record their anchor +
   `whGameAnchored`; reuse requires the whole range be rel32-reachable from
   the requested anchor (placement + reuse share `kRel32Margin`). Plugin ABI
   untouched (no author-facing rel32 knob). Verified: cap-22 6/6 PASS
   (4 callsite + 2 isolation), ZERO "not rel32-reachable"; the dev log shows
   TWO branch-pool reservations — one near WHGame (anchor 0x7FFA79EF…) and a
   NEW one near cap-22.dll (anchor 0x7FFB2E77…) — i.e. the far target got its
   own in-range reservation, exactly as designed.

Both fixes confirmed in the 10:43 launch: suite 39/40 passing, total 43,
the ONLY failure CAP-04c (the pre-existing legacy mid-hook auto-skip bug,
unrelated). No regressions.

Non-blocking follow-up (step-review L): make the placement/reuse rel32-window
agreement explicit (shared inline helper or assertion) so a future edit to
one can't silently desync the other — the single `kRel32Margin` constant
already mitigates it.

## Trail

| Action | Result |
|---|---|
| (Phase 1) Read dev log + trampoline.cpp anchor + minhook buffer.c, fs-check deployed DLLs | Ground truth gathered (see Facts). Two distinct failures, both ASLR-exposed; sub-7 exonerated. |
| PROBE A (read-only): confirm cap-22 trampoline ∈ WHGame pool, callsite ∈ cap-22.dll, dist >2GB | A1 CONFIRMED. Log: trampoline 0x7FFA808C… (WHGame-anchored pool), callsite 0x7FFB5F28… (cap-22.dll), dist -3.7GB. cap-22 = our WHGame-only pool vs far module — real engine gap. |
| PROBE B (read-only, code): does mid install use MinHook's allocator (not our pool)? + is cap-21 stub raw heap? | B1 CONFIRMED. InstallRuntime calls MH_CreateHook (hook_engine.cpp:516) → MinHook's ±1GB buffer.c allocator owns the trampoline; cap-21 stub is VirtualAlloc(nullptr, RWX) raw heap. cap-21 = MinHook allocator on a heap target — test-fixture artifact. |
| FIX cap-21 (c25b622): stub from branch pool. Verify launch 10:22. | FIXED. cap-21 4/4 PASS, zero MH_ERROR_MEMORY_ALLOC. |
| FIX cap-22 (1d86217): per-module pool, runtime_func_t passes target as nearVa. Verify launch 10:43. | FIXED. cap-22 6/6 PASS, zero "not rel32-reachable"; dev log shows a 2nd branch reservation anchored near cap-22.dll (0x7FFB2E77…) alongside the WHGame one — far target got its own in-range pool. No regressions (39/40, only CAP-04c fails). |
