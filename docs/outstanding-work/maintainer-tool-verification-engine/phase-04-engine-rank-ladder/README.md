# Phase 4 — engine rank-ladder + per-kind matrix (the active-attempt authority)

**Intent:** extend the Phase-3 engine survival checker (`src/survival_verify.{h,cpp}`,
4-verdict static today) into D36's **active-attempt model** — the 7-state verdict enum, the
5-rank proof ladder (incl. the rank-1 observed-execution tier + the rank-2 safe-read tier), and
the per-kind ceiling matrix (§11.6). The rule: a verdict is the CEILING of the strongest method
that ran; only rank-1 (observed live execution) earns `verified_working`, ranks 2–5 cap at
`passed_not_verified`. Engine-only — synthetic self-tests at boot, no game state needed. The
producer (the console sweep, Phase 5) drives this authority. All in kcdx `src/` — gated by
`pwsh ./build.ps1` + the extended cap-84 self-test at a boot launch
(`.claude/rules/agent-builds-and-deploys.md`: the agent builds, deploys, hash-verifies, enables
dev mode, reads the log; the user launches).

**Design authority:** `data/maintainer-tool/design.md` D36 (the enum + ladder + ceiling rule) +
§11.6 (the per-kind matrix) — build to those, not this README's summary.

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [4.1 [ENG] The 7-state verdict enum + the ceiling rule in survival_verify](step-1-eng-verdict-enum-ceiling.md) | DONE | 75ddd8c |
| [4.2 [ENG] The rank-1 observed-execution tier (hook-fire + pass-through; kcdx's own production call)](step-2-eng-rank1-observed-execution.md) | DONE | 36d61a5 |
| [4.3 [ENG] The rank-2 safe-read tier (cvar read + read-only vtable_base walk)](step-3-eng-rank2-safe-read.md) | DONE | cdabde8 |
| [4.4 [ENG] The per-kind ceiling matrix wiring (§11.6)](step-4-eng-per-kind-matrix.md) | DONE | (landed) |

## Phase verification gate

Phase 4 is done when: the extended checker (steps 4.1–4.4) builds clean (`pwsh ./build.ps1`,
exit 0 + the 3 artifacts) AND the extended cap-84 self-test PASSES at a boot launch — its
sub-checks assert: the 7-state enum + ceiling mapping (a `failed` at any rank overrides downward;
only rank-1 awards `verified_working`); the rank-1 observation (a known-hooked row reads
`verified_working` from an observed fire, NOT a synthetic invoke); the rank-2 safe-read (a curated
cvar row reads `passed_not_verified` via rank-2, not via static); and each of the 9 kinds reaching
its §11.6 ceiling. The agent reads the matrix PASS from `kcdx-dev.log`
(`.claude/rules/agent-builds-and-deploys.md`). Not a maintainer-tool UI phase — the only user
gesture is the boot launch (no save-load needed; the rank-1 observation tier is exercised live by
the Phase-5 sweep, the engine self-test asserts the ladder mechanics on synthetic data). Build-green
is necessary, not sufficient — the matrix is confirmed by the launch
(`.claude/rules/skeptical-expert.md`).
