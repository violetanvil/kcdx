# Phase 3 step 10 — permanent asset-system test plugin(s) + matrix rows

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 10.

## What

The standing regression net for the asset system (`test-suite.md`, AP7): a
suite-gated (`test_suite_only = true`) `test-plugins/` plugin (or a small set) that
exercises every asset-system capability and self-reports via the canonical
acceptance signal, so a future change can never silently regress it. This
consolidates the per-step coverage the earlier steps shipped into the permanent
matrix, and adds the cross-capability cases (the chain, the conflict, the
backward-compat pak) that no single earlier step owned.

## Scope

- One or more `test-plugins/<row-id>-<short-name>/` plugins (`cap-NN` for a
  capability, `comp-NN` for a conflict/interaction), suite-gated, self-checking via
  `ReportTestResult(...)` / `kcdx.test.report(...)`. Matrix rows in
  `test-plugins/README.md`. Cases:
  - **US-1 (replace-vanilla, pak/mount lane)** a declarative overlay of a vanilla
    (pak-resident) asset renders — HOOK 1's resolver redirect makes the pak/mount
    lane serve kcdx's file; the overlay-HIT log line present.
  - **add-new (loose lane, HOOK 2)** a declared loose asset the game never requests
    is served via the own-`FILE*` open (a handle-consumed `.lua`/`.xml` reads
    kcdx's bytes) — the lane the prior path-redirect failed.
  - **US-3** a cross-plugin reference resolves (`kcdx.plugin.<a>.<p>.assets.get_by_name`
    returns a loadable path).
  - **US-4** the chain/conflict path: two plugins replace the same target → the
    load-order winner serves, the loser is reported (the §4.4 conflict line).
  - **US-7** a stock Nexus/Workshop pak (a `test-fixtures/pak-mods/` fixture)
    resolves unchanged through HOOK 1's MISS fall-through to the pak-membership
    leaf (id 153).
- Each row carries a FALSIFIABLE claim (AP15 — state what makes it FAIL); prefer an
  auto-pass boot check, flag `[manual]` only where an in-game gesture is irreducible.

## Test bar

The suite is green at the landing commit — `ACCEPT-SUITE: N/N passing` /
`suite: X/Y passing` read by the agent from `kcdx-dev.log` (`acceptance-signal.md`,
`agent-builds-and-deploys.md` — the agent builds + deploys + reads; the user only
launches). Each new row's claim is falsifiable (would go red if the capability
regressed). The rows are recorded in `test-plugins/README.md`.

## Dependencies

**Phase 1 (steps 3–5)** — HOOK 1 (decision), HOOK 2 (open), sidecar/conflict exist
to test. **Phase 2 (steps 6, 8, 9)** — the namespace + Lua + C++ surfaces exist to
test cross-plugin reference + parity. Ordered last so every capability it asserts
is built (`.claude/rules/incremental-delivery.md`). (Earlier steps each ship their
own same-change coverage per `test-suite.md`; this step is the consolidated
permanent matrix + the cross-capability cases.)

## Reference

Design authority: [`../../../design/asset-replacement.md`](../../../design/asset-replacement.md)
§3 (all US acceptance criteria) + §4.4 (conflict) + §7 (US-7 fall-through). Shared
spec: [`../plan-spec.md`](../plan-spec.md) §"Coverage map". Test convention:
`test-plugins/README.md`.

## Disassembler-test / author-burden

The test plugins are authored entirely through the public author surface (sidecar +
`kcdx.assets.*` + `kcdx.plugin.*`) — if a test needs an engine internal to exercise
a capability, that is a surface gap to surface, not a test workaround
(`cornerstones.md`). No hand-written hex/ABI in a test plugin.
