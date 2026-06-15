# maintainer-tool FE: `src/App.test.tsx` s09 step-3.3 tests are non-deterministically flaky under parallel-worker load

**Status:** open

**Component:** the maintainer-tool frontend repo (`data/maintainer-tool/frontend/`, the separate
gitignored nested repo), test suite — NOT the kcdx engine. Belongs to the
`maintainer-tool-lifecycle-completeness` feature's step 3.3 (the s09 resolution-action deep-link +
back-stack work), not to the divergence-diff feature that surfaced it.

## Symptom

A full `npx vitest run` of the FE suite intermittently reds **0, 1, or 2** tests in
`src/App.test.tsx`, all in the two `s09` step-3.3 describe blocks:

- `App — s09 resolve-action DEEP-LINKS open the target s02 sub-surface (step 3.3, law 3/10) > the
  auto-open is ONE-SHOT — a ‹ back re-entry does NOT re-expand a section the maintainer collapsed`
- `App — s09 resolution actions PUSH the canonical flow (step 3.3, law 10/3) > on return the view
  RE-QUERIES and the resolved entity drops off the list (no auto-navigation)`

The count of failing tests **varies run-to-run** (observed: a run with 0, a run with 1, a run with 2).
Each failing test runs ~1100–1180 ms (long for a unit test) and the run emits
`An update to @mantine/notifications/Notifications inside a test was not wrapped in act(...)` warnings.

## Diagnosis (probed, not assumed)

A one-variable probe (`results-driven`) isolated it as a **test-isolation / real-timer timing flake**,
NOT a logic defect and NOT caused by any feature change:

- `npx vitest run src/App.test.tsx` **alone** → 13/13 green, every time. The tests are correct in isolation.
- `npx vitest run --exclude '**/fixDivergence.test.ts'` (the full suite minus the file whose addition
  first surfaced it) → 565/565 green. The flake is latent in the suite, not in any one added file.
- Full suite with all files → 0, 1, or 2 of the s09 tests red, varying per run; consecutive re-runs
  often pass 572/572.

The signature (varying failure count, ~1.1 s durations, `act()` warnings, surfaces only under the
41-file parallel-worker load) is a **real-timer race** in the auto-open / re-query assertions — the
assertion observes a timing-sensitive Mantine state transition that occasionally has not settled when
the test asserts. Adding a 41st test file (any file) shifts the worker scheduling enough to expose it;
the added file's CONTENT is irrelevant (`fixDivergence.test.ts` is a pure-function test with no React,
no timers, no module side effects).

## Why it matters

A flaky gate that intermittently reds the suite erodes trust in the gate and trains rubber-stamping of
red runs (the erosion `skeptical-expert` / AP8 guard against). It is not a logic bug in the s09
feature (those behaviors pass in isolation), but the test as written is not deterministic under load.

## Fix (deferred to its own cycle — not the divergence-diff feature's scope)

Harden the two s09 `App.test.tsx` assertions to be deterministic under parallel load — likely one of:
fake timers (`vi.useFakeTimers()`) around the auto-open/re-query transition; an explicit
`await waitFor(...)` / `findBy*` for the settled state instead of a real-timer-dependent assertion;
or wrapping the Mantine `Notifications` state update in `act(...)`. Owner: the
`maintainer-tool-lifecycle-completeness` feature (its step 3.3 authored these tests). The fix is a
test-infra change in the FE repo (gated by `npm run build` + `npx vitest run`), committed there with
an `FE:<hash>` ledger reference, not a kcdx engine change.

## Update 2026-06-14 (divergence-diff Phase 3.1)

Phase 3.1 added new tests to `src/App.test.tsx` (the no-DLL `[Fix ▸]`-arrival end-to-end tests). The
added load RAISED the flake frequency — `App.test.tsx` now occasionally reds even in isolation (it was
13/13 alone before), and the full-suite flake fires more often (observed across consecutive runs: a
mix of 575/575 green and a single s09/back-stack test red, the failing test VARYING run-to-run —
`auto-open ONE-SHOT`, `on return the view RE-QUERIES`, `RESETS to a fresh needs-action root`). Still
the SAME defect (a real-timer race in the s09/back-stack `App.test.tsx` assertions), still
non-deterministic (the failing tests pass in isolation; the suite passes 575/575 on clean runs), NOT a
3.1 logic regression — 3.1's own no-DLL-arrival tests never fail. The raised frequency makes the
hardening cycle more urgent: the gate now reds intermittently on most full runs.

## Resolution

_(open — to be filled when the flake is hardened in its own cycle)_
