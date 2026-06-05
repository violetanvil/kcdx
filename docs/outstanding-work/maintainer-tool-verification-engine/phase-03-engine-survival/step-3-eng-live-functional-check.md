# 3.3 [ENG] The reachability check + the on-disk version-applicability hash (the engine startup verification pass)

## What

Implement the engine startup verification pass (D25) — for every row, run BOTH checks once at
engine startup (never during gameplay): **(1) version-applicability** — hash the body at the
row's `rva` from the **ON-DISK DLL file** and compare to the DB `content_hash` (match → valid for
this build → apply; mismatch → the build diverged → avoid). This is already what
`survival.cpp::SurvivalCheck` does (read the on-disk backing file, NOT live memory). **(2)
reachability** — the half the browser cannot do: resolve the row's address via the REAL engine
resolve path and confirm it resolves into the live loaded module's executable `.text` at all (not
`0`/garbage/off-image). This catches an entry whose on-disk hash matches but whose live resolve is
dead/wrong — the signal the static on-disk check alone cannot produce. It is **NOT a hash of the
live runtime body** (the superseded framing; the live image is relocated + kcdx-detoured). Per row
it yields `resolves_works` / `dead` / `wrong_target` / `cannot_check`. **ATTRIBUTION (D34):** the
version-applicability check matches the swept bytes against EACH candidate `address_version` row's
fingerprint (the `content_hash` for a function, the per-kind datum otherwise) and reports WHICH row
matched — the engine entry point returns the **matched `address_version` id** (or none → the bytes
match no candidate → `wrong_target`). This is what lets the importer (Phase 5) attribute an
uncovered version to the right row. This is the engine-only authority the in-game batch plugin
(Phase 4) drives.

## Scope

One commit in kcdx `src/`: the startup verification pass — the on-disk version-applicability hash
+ the loaded-image reachability resolve, classified per kind into the verdict, **AND the attribution
(D34): the entry point returns the matched `address_version` id** (which candidate row's fingerprint
the swept bytes matched, or none) alongside the verdict — as an engine entry point a test plugin
can call. No batch plugin (Phase 4); no JSON report (Phase 4); no agreement test (step 4).

## Test bar

A kcdx test-suite plugin row asserting: for a known-good row → the on-disk hash matches AND the
address resolves into live `.text` (= `resolves_works`) **AND the entry point reports the matched
`address_version` id** (the row whose fingerprint matched); for a row whose on-disk hash mismatches
NO candidate → `wrong_target` (matched id = none); for an address that does not resolve into live
`.text` → `dead`. The bar does NOT assert a hash of the live runtime body (the superseded model —
the live image is relocated + detoured). The discriminating signal 0.4 de-risked. The agent builds,
deploys, hash-verifies, enables dev mode; the user launches; the agent reads PASS from
`kcdx-dev.log` (`.claude/rules/agent-builds-and-deploys.md`). A matrix row is recorded. Runnable at
this step (the engine resolve path + on-disk hash + the 0.4-confirmed signal exist) —
`.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`.

## Dependencies

- **3.1** — the per-kind dispatch (the startup pass joins it).
- **0.4** — the reachability/version-applicability signal probe (confirmed the in-game signal
  exists + discriminates resolves+works / dead / wrong-target).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group B (the reachability check + the on-disk
version-applicability hash); cross-step invariant 3 (the loaded-image reachability check is
engine-only; both checkers hash on-disk for version-applicability, D27).

## Design authority

`data/maintainer-tool/design.md` **D25** ("verify" = is the entry safe to APPLY on the build the
user is running — **(1) version-applicability**: hash the body at the `rva` from the **on-disk DLL
file** vs the DB `content_hash`, match → apply / mismatch → avoid; **(2) reachability**: resolve
into the live loaded image's `.text` — NOT a hash of the live runtime body; verdicts
`resolves_works` / `dead` / `wrong_target` / `cannot_check`, both run ONCE at engine startup) +
**D34** (the check ATTRIBUTES the result to the `address_version` row whose fingerprint the swept
bytes match — the entry point returns the matched row id) + **US-11** §"In-bulk, in-game, at
startup". Build to the CURRENT D25 + D34 definitions, not to this doc's summary.

## UX

Not a UI step (engine code). The only user gesture is the game launch
(`.claude/rules/agent-builds-and-deploys.md`).

## Disassembler-test / author-burden

None — the engine resolves + confirms; the author authors nothing new. The live check is the
strongest evidence the engine produces FOR the author, not a burden ON them.
