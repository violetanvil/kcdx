# Verify Phase 4 — trampoline + function hooks + cross-engine conflicts

Phase 4 ships the bulk of v0.1's runtime injection surface:
- 4a: kcdxTrampolineInterface + per-plugin logging
- 4b.1: [[hook]] schema (raw-bytes function-entry detours)
- 4b.2: [[trampoline]] schema + cross-plugin symbol table
- 4b.3: unified conflict matrix (cross-engine hook/patch interactions)

This recipe covers the Phase 4b.3 acceptance — three synthetic conflict-
test plugins, each exercising one new cross-engine conflict category.
Earlier phase verifications are in [VERIFY_PHASE1.md](VERIFY_PHASE1.md),
[VERIFY_PHASE2.md](VERIFY_PHASE2.md), [VERIFY_PHASE3.md](VERIFY_PHASE3.md).

## Setup

**Before running these tests, disable any other plugins that target the
IsInCombat wrapper (RVA 0x5605B8 in WHGame.dll 1.5.1164953).** That
includes:
- `no-combat-state-hook/` (rename to `.disabled`)
- Any prior conflict-test folders from a previous run

The three conflict tests all target the same function for isolation. They
should be run **one at a time**, never simultaneously — installing two
conflict tests at once produces a multi-way collision that obscures the
specific category each test is meant to exercise.

## Test 1 — HookOnHook

Install `examples/conflict-test-hook-on-hook/kcdx.toml` into the game's
plugins folder. Launch, get past main menu, quit.

**Expected `kcdx.log` content (key lines):**

```
Loaded hook 'conflict_hook_A' (priority 100) from ...
Loaded hook 'conflict_hook_B' (priority 200) from ...
Conflict engine: pre-flight starting (0 patch(es), 2 hook(s))
[conflict_hook_A] pattern matches: 1
[conflict_hook_B] pattern matches: 1
[WARN] Hook 'conflict_hook_B' targets function entry 0x..., but hook
       'conflict_hook_A' already claimed that target. kcdx v0.1 is
       first-wins; 'conflict_hook_A' will install, 'conflict_hook_B'
       will abort. Chained hooks are a v0.2 feature.
Conflict engine: 1 conflict(s) recorded (... HookOnHook=1 ...)
Hook engine: applying 2 hook(s)...
[hook 'conflict_hook_A'] installed at 0x...
[hook 'conflict_hook_B'] aborted: target 0x... already hooked by 'conflict_hook_A' ...
Hook engine: 1 of 2 hook(s) installed
```

**Pass criteria:** the WARN line names BOTH hooks. `conflict_hook_A`
installs successfully; `conflict_hook_B` aborts. No crashes.

## Test 2 — PatchOverlapsEarlierHook

Disable the previous test plugin, install
`examples/conflict-test-patch-on-hook/kcdx.toml`.

**Expected `kcdx.log`:**

```
Loaded patch 'conflict_patch_on_hook_PATCH' (priority 200) from ...
Loaded hook 'conflict_patch_on_hook_HOOK' (priority 100) from ...
Conflict engine: pre-flight starting (1 patch(es), 1 hook(s))
[conflict_patch_on_hook_PATCH] pattern matches: 1
[conflict_patch_on_hook_HOOK] pattern matches: 1
[WARN] Patch 'conflict_patch_on_hook_PATCH' writes inside hook
       'conflict_patch_on_hook_HOOK''s 5-byte rel32 jmp range
       (overlap 0x...). The patch's verify-against-original will see
       the hook's E9 jmp bytes rather than the function's original
       prologue, so the patch will abort cleanly. Remove or reorder
       one of the two if you need both to take effect.
Hook engine: applying 1 hook(s)...
[hook 'conflict_patch_on_hook_HOOK'] installed at 0x... post-install bytes: E9 ...
[outfit_swap_in_combat] applied successfully ... (if present from other plugin)
Applying 1 patch(es)
[conflict_patch_on_hook_PATCH] aborted: bytes at patch site don't match expected original (got E9..., expected 48)
[WARN] [conflict_patch_on_hook_PATCH] note: pre-flight predicted this — earlier entry 'conflict_patch_on_hook_HOOK' modified bytes inside this patch's verify range.
Patch summary: 0 applied, 1 aborted
```

**Pass criteria:** Pre-flight WARN fires with the unified explanation.
Hook installs cleanly. Patch aborts at apply time with the standard
"bytes don't match" diagnostic, supplemented by the conflict_engine
cross-reference. The aborted-patch follow-up names the hook.

## Test 3 — HookOverlapsEarlierPatch

Disable the previous tests, install
`examples/conflict-test-hook-on-patch/kcdx.toml`.

**Expected `kcdx.log`:**

```
Loaded patch 'conflict_hook_on_patch_PATCH' (priority 100) ...
Loaded hook 'conflict_hook_on_patch_HOOK' (priority 200) ...
Conflict engine: pre-flight starting (1 patch(es), 1 hook(s))
[INFO] Hook 'conflict_hook_on_patch_HOOK' will install a 5-byte rel32 jmp
       at 0x..., overlapping bytes already modified by patch
       'conflict_hook_on_patch_PATCH'. MinHook will relocate the patched
       prologue into the trampoline, so the patch survives inside the
       hook's call-original path. Both apply, no action needed.
Applying 1 patch(es)
[conflict_hook_on_patch_PATCH] applied successfully at 0x...: 48 -> 48
Hook engine: applying 1 hook(s)...
[hook 'conflict_hook_on_patch_HOOK'] installed at 0x... post-install bytes: E9 ...
```

**Pass criteria:** INFO-level (not WARN) since this is informational, not
problematic. Both entries apply. The identity 48->48 patch is a no-op
intentionally — this test is about the conflict log, not about the
patch's effect.

## Cleanup after testing

Remove all three conflict-test plugin folders. Restore
`no-combat-state-hook/` if you want to keep using it. No game-state
side effects from these tests because:
- Test 1: hooks target IsInCombat (recoverable, same effect as no-combat-state)
- Test 2: patch aborts, only the hook installs (same effect as no-combat-state)
- Test 3: patch is 48->48 identity, hook installs (same effect as no-combat-state)

## What this verifies

- conflict_engine::RunPreFlight runs the unified pass correctly
- All three new conflict categories trigger when their pattern appears
- WARN/INFO severity is correct per category
- Cross-references between apply-time errors and pre-flight predictions work
- hook_engine and patch_engine both consume conflict_engine's resolved
  data without re-resolving

## What it does NOT verify

- That the conflict_engine handles MORE THAN TWO conflicting entries in a
  single run (a future test could install multiple conflict pairs to
  stress this)
- That patch-vs-patch conflicts still work (those have separate tests in
  examples/conflict-test-incidental and examples/conflict-test-on-original
  from the mempatch lineage — those weren't ported to kcdx yet)
