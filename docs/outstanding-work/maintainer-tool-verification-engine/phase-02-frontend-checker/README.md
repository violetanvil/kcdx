# Phase 2 — Frontend static checker + per-author UI

**Intent:** build the browser static checker (the per-author mirror of the engine authority,
D27) bottom-up — the PE-section foundation + verdict types, then the x86 decoder sub-unit, then
the pure-byte kind checks (pinned to the Python reference), then the derivation kinds + the
anchor DAG — and the per-author UI it feeds (the s02 **install-set link surface** — a Bin-folder
pick + per-module rows in a compact-header/collapsible layout, D30 + the s02 layout — + the
version-match gate, the s04 verdict badge, the s02 link-to-create). Each step lands its own test
runnable at its position
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
| [2.5 [FE] s02 install-set link surface (Bin-folder pick) + version-match gate + the layout + verify states](step-5-fe-s02-link-table.md) | DONE | FE:0ed135d + FE:bfdff6f (install-set D30 + compact-header/collapsible s02 layout + reflow-safe row; doc-header fix) |
| [2.6 [FE] s04 per-author verdict badge + Ambiguous steer + the 6 check verdict states](step-6-fe-s04-verdict-badge.md) | DONE | 2.6a 9d84fcf + FE:00b2e78/64424a6/2cb2cee/27aa470 (a–e: verify-only content_hash, extractor, badge, cross-row DAG, cross-entity fetch) — milestone UAT accepted; see the step doc's sub-step ledger |
| [2.7 [FE] s02 link-to-create prompt → s05 prefill + evidence_kind-from-check](step-7-fe-link-to-create.md) | NOT STARTED | — |

## Phase verification gate

A UI-touching phase — the gate INCLUDES user-facing acceptance (`.claude/rules/ux-first-class.md`),
not only build/test green. Phase 2 is done when: the checker (steps 1–4) passes its Vitest
units AND the JS↔Python agreement test reproduces the Phase-0 fixture verdicts for all
in-scope kinds (D27); the UI (steps 5–7) builds to the REVISED s02 + s04 screen specs and passes
`npm run build` + Vitest. The **milestone user-acceptance checkpoint** fires for the
substantive + under-specified UI built in steps 5–7 (the install-set link surface, the verdict
badge, the link-to-create on-ramp render + behave per the screen specs against a real linked Bin
folder) — the maintainer experiences **linking the game Bin folder**, the compact-header +
collapsible "Verify against a DLL" layout (the work surface keeps the room), the per-module
version-match (incl. a non-WHGame module inheriting the install version), the reflow-safe row, a
live per-author verdict in s04, the degraded "no folder linked"/"DLL not found" states, and the
link-to-create on-ramp at the install version. Advisory throughout — no verdict ever blocks
authoring (law 4).
