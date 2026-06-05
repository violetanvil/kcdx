# Phase 3 step 10 — permanent asset-system test plugin(s) + matrix rows

**Status: DONE.** Ledger: [`README.md`](README.md) → step 10.

**Scope (settled 2026-06-04):** 4 of 6 cases (US-1, add-new, US-3, US-4) already
covered by earlier steps' same-change tests; step 10 built the 2 genuine gaps —
serve-AND-EXECUTE (cap-77) + US-7 (comp-17, the stock-pak MISS fall-through via an
existing pak fixture's self-report) — plus the consolidated coverage matrix. Per
`test-suite.md` / `feedback_test_suite_must_grow`. US-7/comp-17 is the Option-A
falsifiable `.lua` row: it deploys the existing `test-fixtures/pak-mods/lua-sandbox-probe`
stock pak and the agent reads its in-pak `[KCDX_PROBE]` marker from `kcd.log` —
the marker reaching the log proves HOOK 1's resolver MISS fell through to the
pak-membership leaf (id 153) and the engine's mount/stream lane served + ran the
pak's `.lua` UNCHANGED. No new test-plugin dir (deploy existing fixture + the
matrix row); no kcdx-side MISS log line (the resolver MISS path stays log-free by
design — it is the hot path).

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
  - **handle-consumed `.lua` serve-AND-EXECUTE (the Phase-1 acceptance residual).**
    Phase-1 acceptance PROVED HOOK 2 *serves* a handle-consumed `.lua` overlay
    (own-`FILE*` returned for `scripts/main.lua`, `map=HIT` +
    `probe_fopen_hc_served` live 2026-06-04). What it did NOT prove is the served
    chunk EXECUTING — `scripts/main.lua` is the already-init'd boot chunk (opened
    by HOOK 2, not re-run mid-game). This row keys the overlay on a **startup
    script the engine RUNS on a save load** (`scripts/startup/sl_saveload.lua` or
    a peer the FOpen observer saw run on load — NOT `main.lua`), with an in-chunk
    marker, and asserts the marker reaches `kcd.log` (`[manual]` `in-game` — the
    save-load gesture is irreducible). FALSIFIABLE: no marker after the save load →
    FAIL (served-but-not-executed). Closes the only Phase-1 residual. Recon:
    `_research/asset-fopen-handle-recon/seamA-handle-consumed-served-LIVE.md`.
  - **US-3** a cross-plugin reference resolves (`kcdx.plugin.<a>.<p>.assets.get_by_name`
    returns a loadable path).
  - **US-4** the chain/conflict path: two plugins replace the same target → the
    load-order winner serves, the loser is reported (the §4.4 conflict line).
  - **US-7 (comp-17)** a stock Nexus/Workshop pak (the existing
    `test-fixtures/pak-mods/lua-sandbox-probe` fixture) resolves unchanged through
    HOOK 1's MISS fall-through to the pak-membership leaf (id 153) + the engine's
    mount/stream lane. The fixture's in-pak Lua self-reports `[KCDX_PROBE]` to
    `kcd.log` at boot; the agent reads that marker. FALSIFIABLE: marker absent →
    FAIL (the MISS branch blocked/corrupted a non-overlay pak asset). `[manual]`
    (boot gesture; the marker lands in the game log, not via `kcdx.test.report`).
    Deploy-existing-fixture + matrix row only — no new test-plugin dir.
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
