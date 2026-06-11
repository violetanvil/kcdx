---
id: KI-0017
opened: 2026-06-11
status: Open
commit_at_filing: 08e2f2a7fe629f00cbbdfa41ba81e2c7c1a325e6
---

# KI-0017 — boundary-time statement registration rejected: original-byte capture empty (length mismatch 0 vs N)

## Symptom

A `kcdx.statement.replace_with` registered from a behavior implementation AT the
behavior apply boundary (9.5 P1 s3's worklist drain, post-`RunPostGameLoad` /
pre-InputLoaded) queues and drains, but is rejected at apply with an
original/replacement length mismatch where the ORIGINAL side is length 0. The
identical statement shape on the identical target registered at LOAD time
applies cleanly in the same session.

## Evidence (facts)

From `kcdx-dev_2026-06-11_15-38-01.log` (suite run at commit `08e2f2a`):

- The boundary-time entry queues and drains (the boundary's own ApplyZone pass):
  - `[15:38:16.072][INFO][engine][LEGACY] lua_registry: queued kind=2 name='comp20_decl_stmt' plugin='comp_20_behavior_declarer' site=...comp-20-behavior-declarer\plugin.lua:91 (handle=86)`
  - `[15:38:16.072][ERROR][engine][LEGACY] [comp20_decl_stmt] aborted: original/replacement length mismatch (0 vs 3)`
  - `[15:38:16.072][ERROR][engine][LEGACY] lua_registry: entry 'comp20_decl_stmt' (plugin='comp_20_behavior_declarer') failed at apply: kcdx.statement.replace_with 'comp20_decl_stmt': the byte write was rejected at the statement VA (...)`
- The one-variable control PASSES in the SAME run: cap-92 (`kcdx.statement.replace_with`,
  same `WHGame.dll` SaveGame target via `kcdx.locator.function_entry()` +
  `kcdx.op.replace_with_noop()`, registered during the load wave) — all four rows
  `RESULT name=cap-92-* verdict=PASS`.
- The fixture's registration site: `test-plugins/comp-20-behavior-declarer/plugin.lua:91`
  (inside the behavior's `implementation`, invoked by
  `behavior_registry::RunApplyBoundary` — `src/behavior_registry.cpp`, the s3
  worklist drain; the boundary's ApplyZone call is `src/hooks.cpp:~586-610`).
- The probe ground truth that boundary-queued registrations DO drain pre-ready:
  `_research/behavior-startup-recon/FINDINGS.md` §5 (F1/F2) — the drain itself is
  not the failure; the entry's content is.
- The suite row's own discrimination: the FAIL reason states the verdict was
  "neither applied, a deploy-state miss, nor the co-located-entry arm — a real
  apply-path regression" (the co-location arm did NOT fire; the rejection reason
  is the length mismatch, not a conflict).

## Hypothesis (NOT verified)

- Hypothesis only — not verified: statement entries capture their ORIGINAL bytes
  during a resolution stage that runs for load-wave registrations but has
  already passed (or is skipped) for entries queued at the apply boundary — so a
  boundary-time entry reaches apply with an empty original and the
  length-preserving check (`replacement.length == original.length`) rejects it.
- Hypothesis only — not verified: the same gap would affect ANY post-load-wave
  statement registration path, including 9.5 P2 s2's queued off-thread set
  (whose toggles execute implementations after boot) — making this a
  boundary-class defect, not a comp-20 fixture quirk.

## Reproduction (if known)

Deterministic: boot-to-menu with the deployed suite (dev mode on) at commit
`08e2f2a` — `comp-20-declarer-statement` FAILs every run; cap-92 passes. The
fixture pair is `test-plugins/comp-20-behavior-declarer/` + `-consumer/`.

## Trail

| Date | Action | Result |
|------|--------|--------|
| 2026-06-11 | PROBE A: static read — trace who fills PatchEntry.original on the statement apply path | Hypothesis DISPROVEN. No original-capture stage exists for ANY statement entry; the defect is registration-time-independent. |

## Facts

- The statement apply handler resolves at apply time, emits the replacement
  (NOP-padded to `byte_range_len`), and builds the carrier `PatchEntry` with
  `resolvedVa` + `replacement` — `pe->original` is NEVER assigned
  (`src/lua_bind_statement.cpp:282-297`). (PROBE A)
- `patch::Resolve` rejects `original.size() != replacement.size()` as its FIRST
  check, before the `resolvedVa` carrier path (`src/patch_engine.cpp:251-257`)
  — every statement write with a non-empty emit is rejected `(0 vs N)`. (PROBE A)
- `refdb::StatementResolution` exposes kind / callee / string_ref / pseudo_text /
  byte_range / captures — NO instruction-bytes field (`src/refdb.h:393-416`);
  the deferred-op comment states the layer "does NOT expose the apply-time
  statement's actual bytes" (`src/lua_bind_statement.cpp:195-199`). (PROBE A)
- cap-92 was never a control for the APPLY: its four rows assert registration +
  error paths + zero-dispatch, none asserts `applied()==true` — the live-apply
  proof is the deliberately-deferred TD-0010 (bucket-2). comp-20-declarer-statement
  is the FIRST fixture to drive `replace_with` through a real apply. (PROBE A)

## Reframe (2026-06-11)

The filed title/hypothesis ("boundary-time registration misses a capture
stage") is wrong — disproven by PROBE A. The mechanism: `kcdx.statement.
replace_with`'s apply path has never successfully applied ANY entry; the
carrier `PatchEntry` ships an empty `original`, and the patch engine's
length-preserving precondition unconditionally rejects it. The boundary-time
registration in comp-20 was merely the first caller to reach the full apply.
TD-0010's deferred live proof is the reason this shipped unexercised.

## Open questions

- The fix fork (design surface — Gate A owed): what fills `original`?
  (a) read the LIVE bytes at the statement VA at apply time (no DB baseline —
  weaker tamper check), (b) extend the statement-resolution layer / DB to
  expose the statement's instruction bytes (the deferred-op comment implies
  this is the planned direction; also unblocks the site-dependent ops), or
  (c) an explicit no-verify carrier mode in `patch::Resolve` for
  resolvedVa-carrier entries. Labeled — not decided here.

## What this report does NOT do

- Does not propose a fix.
- Does not assign root cause beyond labeled hypothesis.
- Closure handled by `/debug KI-0017` (which lands the fix and closes per
  doc-organization.md).
