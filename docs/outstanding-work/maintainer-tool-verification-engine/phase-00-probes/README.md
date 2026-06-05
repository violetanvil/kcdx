# Phase 0 — Probes (de-risk the unknowns before building)

**Intent:** resolve every checkable unknown the build rests on BEFORE writing the consumer
code (`.claude/rules/results-driven.md`, `.claude/rules/incremental-delivery.md` — a probe
is verifiable by its own outcome and is correctly ordered first). Each step is a probe with
an outcome→meaning map; its "test" is that map. No production code lands in Phase 0; the
findings are captured as durable process-output (`.claude/rules/working-artifacts.md`) under
`_research/` and feed the later phases' scoping.

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [0.1 [FE] Probe: 86MB ArrayBuffer + full `.text` AOB scan in-browser](step-1-fe-probe-arraybuffer-scan.md) | DONE | 860a060 |
| [0.2 [FE] Probe: minimal JS x86 decoder follows RIP-relative `disp32`](step-2-fe-probe-x86-decoder.md) | DONE | 384407d |
| [0.3 [ENG] Probe: read the C++ pe_helpers surface (spans + disp32 follower?)](step-3-eng-probe-pe-helpers-surface.md) | DONE | fab2c7b |
| [0.4 [TEST] Probe: the in-game reachability/version-applicability resolves+works/dead/wrong-target signal](step-4-test-probe-live-functional-signal.md) | DONE | (landed) |
| [0.5 [CORE] Establish the cross-impl known-DLL fixture + known per-kind verdicts](step-5-core-cross-impl-fixture.md) | NOT STARTED | — |

## Phase verification gate

Each probe's outcome→meaning map resolved against its captured result (the agent builds,
deploys, and reads the result; the user only launches for the live probe 0.4 —
`.claude/rules/agent-builds-and-deploys.md`). Phase 0 is done when: 0.1 confirms the
in-browser scan is feasible within an acceptable perf budget; 0.2 confirms a minimal JS
decoder follows a `disp32` to a Ghidra-confirmed target; 0.3 returns a scoping finding on
the pe_helpers surface; 0.4 confirms the in-game reachability/version-applicability signal is
observable in-game (and its result corrected D25 — see step 0.4); 0.5
lands the fixture + the known verdicts the agreement tests pin against. No UI is touched in
Phase 0 — no user-facing acceptance applies. A probe that disconfirms its leading assumption
is captured and re-grounds the dependent phase's scope (it does not silently proceed).
