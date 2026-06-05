# Phase 3 — C++ engine survival extension (the authority)

**Intent:** extend the C++ engine survival checker (`src/survival.cpp` /
`src/survival_pass.cpp`, function-hash-only today) to all 9 kinds — the **authority** the JS
browser checker mirrors (D27). Build bottom-up: per-kind dispatch + payload model + the
`Ambiguous` status, then the 5 static non-function kind checks + anchor ordering, then the LIVE
functional check (resolve via the real engine path, confirm it lands on real working code), then
the **JS↔C++ cross-impl agreement test** that pins the engine == the browser on the same bytes.
All in kcdx `src/` — gated by `pwsh ./build.ps1` + a live launch (`.claude/rules/agent-builds-and-deploys.md`:
the agent builds, deploys, hash-verifies, enables dev mode, reads the log; the user launches).

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [3.1 [ENG] Per-kind dispatch + payload model + the Ambiguous status](step-1-eng-dispatch-payload-ambiguous.md) | NOT STARTED | — |
| [3.2 [ENG] The 5 static non-function kind checks + anchor-dependency ordering](step-2-eng-static-non-function-checks.md) | NOT STARTED | — |
| [3.3 [ENG] The LIVE functional check (resolve via the real engine path)](step-3-eng-live-functional-check.md) | NOT STARTED | — |
| [3.4 [ENG] The cross-impl agreement test (JS browser == C++ engine)](step-4-eng-cross-impl-agreement.md) | NOT STARTED | — |

## Phase verification gate

Phase 3 is done when: the engine checker (steps 1–3) builds clean (`pwsh ./build.ps1`,
exit 0 + the 3 artifacts) AND a kcdx test-suite plugin exercising the per-kind static + live
checks PASSES at a live launch (the matrix row reads PASS in `kcdx-dev.log`, read by the agent —
`.claude/rules/agent-builds-and-deploys.md`); and step 4's JS↔C++ agreement test confirms the
engine and the browser checker return the SAME verdict on the SAME bytes (D27). Not a UI phase —
no maintainer-tool UI acceptance; the user gesture is the game launch only. Build-green is
necessary, not sufficient — the matrix is confirmed by the launch (`.claude/rules/skeptical-expert.md`).
