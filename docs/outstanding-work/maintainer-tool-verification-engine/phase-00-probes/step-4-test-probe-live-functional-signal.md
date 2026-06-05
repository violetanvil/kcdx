# 0.4 [TEST] Probe — the live-functional resolves+works / dead / wrong-target signal in-game

## What

Probe, in the running game, that the LIVE functional check (D25) produces an observable,
discriminating signal: for a known-good row → `resolves+works`, and for a deliberately-broken
row → `dead` / `wrong-target`. This de-risks the Phase 3 step 3 LIVE check + the Phase 4 batch
plugin BEFORE they are built — confirming the in-game signal exists and distinguishes the
verdicts. A live-launch probe per `.claude/rules/agent-builds-and-deploys.md` +
`.claude/rules/results-driven.md`: the agent writes the probe plugin, builds it
(`pwsh ./build.ps1`), deploys it, enables dev mode, and reads the log; the user only launches.

## Scope

One commit in kcdx `test-plugins/`: a throwaway probe plugin that runs the live functional
check against (a) a known-good row and (b) a synthetically-broken row, self-reporting the
verdict to the dev log via the canonical acceptance signal. The agent reads the log verdict
after the user's launch. Finding captured to `_research/probe-archive/`; probe removed from
the live tree after (no residue — `.claude/rules/working-artifacts.md`).

## Test bar

A probe step's "test" is its outcome→meaning map (`.claude/rules/results-driven.md`). The
probe self-reports via the canonical signal (`.claude/rules/acceptance-signal.md`) — the agent
greps `ACCEPT-RESULT` / `ACCEPT-SUITE` in `kcdx-dev.log`; the user only launches:

| Outcome (read from `kcdx-dev.log`) | Meaning | Next action |
|---|---|---|
| good row → `resolves+works`; broken row → `dead`/`wrong-target` | The live signal exists + discriminates | Proceed; Phase 3 step 3 + Phase 4 build the real check on this signal |
| Both rows report the same verdict (no discrimination) | The check cannot tell working from dead code as modeled | STOP — re-observe ground truth, re-design the live check before Phase 3 (`results-driven.md`) |
| No signal fires (the check never runs in-game) | The engine-path resolve isn't reachable from a plugin yet | Surface the missing seam to the user (`headless-testable.md` / `design-authority.md`) |

## Dependencies

None directly (de-risks Phase 3 step 3 + Phase 4). Best run after 0.3's scoping finding informs
how the live check reaches the engine path, but it gates no earlier step.

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group D (the live-functional signal probe row); TRD D25
(live functional) + D28 (the batch plugin); `.claude/rules/agent-builds-and-deploys.md`.

## UX

Not a UI step (an in-game probe plugin). No user-facing surface; the only user gesture is the
game launch (`.claude/rules/agent-builds-and-deploys.md`) — Launch → reach menu → tell me it
ran → Quit. The agent reads the verdict from the log; the user never reads a log line.

## Disassembler-test / author-burden

None — a probe plugin; adds no author-facing input.
