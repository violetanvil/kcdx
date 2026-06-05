# 4.1 [TEST] The batch verification plugin + JSON report emission + matrix row + 3-tree deploy

## What

Build the batch verification plugin — a kcdx test-suite plugin (**dev-mode-gated, self-skips
outside `dev_mode` like every `cap-NN` / `comp-NN`; runs once at engine startup** — D33) whose ONE
job is to drive the attributing engine startup verification pass (version-applicability +
reachability + the matched-row attribution, Phase 3 step 3) over the **curated USER set only** (the
`address_versions` rows that carry a `kcdx_id` — NOT the ~321k DEV bulk discovery rows; D33) and
emit a structured **JSON verification report** (to the Phase-1 **v2** schema) alongside
`kcdx-dev.log`: per row `kcdx_id`, resolved version, verdict (`resolves_works` / `dead` /
`wrong_target` / `cannot_check`), detail, **and `matched_address_version_id`** (the row whose
fingerprint the bytes matched, null on a non-match/uncheckable — D34). This is the producer of the
cross-repo report contract the FE s08 worklist (Phase 5) consumes — a standing regression + the
batch-sweep producer.

## Scope

One commit in kcdx `test-plugins/`: the batch plugin that iterates the **curated USER-set rows**
(D33), runs the attributing engine live check on each (the verdict + the matched `address_version`
id — D34), writes the **v2 JSON report** (Phase-1 v2 schema, validated) to a known location
alongside `kcdx-dev.log`, and self-reports its suite verdict. The plugin is **dev-mode-gated**
(self-skips outside `dev_mode`, runs at startup — D33). Plus the test-suite **matrix row**
(`test-suite.md`) and **deploy to all 3 plugin trees** (`kcdx-engine/builtin/`, `kcdx-plugins/`,
`kcdx-plugins/test-suite/` — per the deploy-all-trees discipline). No FE ingestion (Phase 5); the
producer + report only.

## Test bar

The plugin IS a test-suite plugin — its matrix row is the test bar (`test-suite.md`): at a live
launch (dev_mode on) it drives the engine checker over every curated USER-set row and emits a
**schema-v2-valid** JSON report carrying the per-row `matched_address_version_id` (a non-null id on
a `resolves_works` row, null on a failing/cannot_check row — D34); the plugin self-reports
`ACCEPT-SUITE` / `ACCEPT-RESULT` to `kcdx-dev.log`. The agent builds (`pwsh ./build.ps1`), deploys
to all 3 trees, hash-verifies each, enables dev mode; the user launches; the agent reads the suite
PASS + confirms the report validates against the Phase-1 **v2** schema
(`.claude/rules/agent-builds-and-deploys.md`, `.claude/rules/acceptance-signal.md`). The matrix row
is recorded. Runnable at this step (the attributing engine live check + the v2 schema exist) —
`.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`.

## Dependencies

- **3.3** — the attributing engine startup verification pass (version-applicability + reachability
  + the matched-row attribution; the plugin drives it per row and emits the matched id — D34).
- **1.3** — the **v2** JSON report schema (the report carries `matched_address_version_id`; emitted
  to v2). (1.3 supersedes the 1.2 v1 dependency.)

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group D (the batch plugin, report emission, write-location,
matrix row + 3-tree deploy); cross-step invariant 4 (the JSON report is the cross-repo seam).

## Design authority

`data/maintainer-tool/design.md` **D28** (revised) — "a kcdx test-suite plugin — dev-mode-gated,
at engine startup, over the curated USER set (D33) — runs BOTH D25 checks per entity and
attributes each result to the `address_version` row whose fingerprint the swept bytes match (D34);
it writes a structured JSON report alongside `kcdx-dev.log` (per row: `kcdx_id`, resolved version,
verdict, detail, and the matched `address_version_id`)" + **D33** (dev-mode gating + curated-USER-set
scope) + **D34** (the report carries `matched_address_version_id`). The verdict tokens are the
frozen snake_case set (`resolves_works` / `wrong_target` / `dead` / `cannot_check`); the meanings
are the D25-corrected version-applicability + reachability (NOT a runtime body hash). The report's
schema is Phase 1 step 1.3 (the **v2** frozen contract carrying the matched id). Build to D28/D33/D34's
named fields + verdict enum, not to this doc's summary.

## UX

Not a maintainer-tool UI step (an in-game plugin). The only user gesture is the game launch
(`.claude/rules/agent-builds-and-deploys.md`) — Launch → reach menu → tell me it ran → Quit; the
agent reads the suite verdict + the report from the log/install, the user never reads a log line.

## Disassembler-test / author-burden

None — a test-suite plugin; adds no author-facing input. The plugin is the engine doing the
whole-DB verify sweep so the maintainer never hand-checks rows one by one.
