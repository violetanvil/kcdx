# Phase 2 — Frontend static checker + per-author UI

**Intent:** build the browser static checker (the per-author mirror of the engine authority,
D27) bottom-up — the PE-section foundation + verdict types, then the x86 decoder sub-unit, then
the pure-byte kind checks (pinned to the Python reference), then the derivation kinds + the
anchor DAG — and the per-author UI it feeds (s02 link table + version-match gate, s04 verdict
badge, s02 link-to-create). Each step lands its own test runnable at its position
(`.claude/rules/incremental-delivery.md`): the checker steps land Vitest unit tests + the
JS↔Python agreement test against the Phase-0 fixture; the UI steps build to their screen specs.
All in the SEPARATE gitignored frontend repo (D23) — gated by `npm run build` + Vitest, NOT
kcdx's `build.ps1`.

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [2.1 [FE] PE-section scanning foundation + the 4 verdict types](step-1-fe-pe-section-foundation.md) | DONE | FE:1459367 |
| [2.2 [FE] The minimal in-browser x86 decoder (RIP-relative `disp32` follow)](step-2-fe-x86-decoder.md) | DONE | FE:66f4716 |
| [2.3 [FE] The 4 pure-byte kind checks + the JS↔Python agreement test](step-3-fe-pure-byte-checks.md) | DONE | FE:d611c21 |
| [2.4 [FE] The 2 derivation-kind checks + the anchor-dependency DAG ordering](step-4-fe-derivation-checks-dag.md) | DONE | FE:e83a57c |
| [2.5 [FE] s02 per-module link table + version-match gate + the 7 verify states](step-5-fe-s02-link-table.md) | NEEDS REWORK | FE:6e704ba (milestone UAT: reflow bug + all-DLLs /design + s02-layout /ui-design) |
| [2.6 [FE] s04 per-author verdict badge + Ambiguous steer + the 6 check verdict states](step-6-fe-s04-verdict-badge.md) | NOT STARTED | — |
| [2.7 [FE] s02 link-to-create prompt → s05 prefill + evidence_kind-from-check](step-7-fe-link-to-create.md) | NOT STARTED | — |

## Phase verification gate

A UI-touching phase — the gate INCLUDES user-facing acceptance (`.claude/rules/ux-first-class.md`),
not only build/test green. Phase 2 is done when: the checker (steps 1–4) passes its Vitest
units AND the JS↔Python agreement test reproduces the Phase-0 fixture verdicts for all
in-scope kinds (D27); the UI (steps 5–7) builds to the s02 + s04 screen specs and passes
`npm run build` + Vitest. The **milestone user-acceptance checkpoint** fires for the
substantive + under-specified UI built in steps 5–7 (the link table, the verdict badge, the
link-to-create on-ramp render + behave per the screen specs against a real linked DLL) — the
maintainer experiences linking a version-matching DLL and seeing a live per-author verdict, the
version-match gate, the degraded "not linked" state, and the link-to-create on-ramp. Advisory
throughout — no verdict ever blocks authoring (law 4).
