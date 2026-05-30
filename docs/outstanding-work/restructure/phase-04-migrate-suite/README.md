# Phase 4 — migrate test suite + engine builtin

**Status: DONE for plugins.** Detail: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 4".

The full plugin corpus migrated to the manifest-only schema; audit confirms 0
legacy behavior tables in production manifests.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| migrate 21 test-suite plugins + corpus to manifest-only | DONE | — |
| bugsplat builtin DLL | BLOCKED — bundled with Phase 11 (manifest-only stub ships today) | — |

The bugsplat builtin DLL is blocked on Phase 11's before_game-hooks consumer; a
manifest-only stub ships in the interim. See [`../before-game-hooks.md`](../../before-game-hooks.md)
and [`../phase-11-shim-vm/README.md`](../phase-11-shim-vm/README.md).
