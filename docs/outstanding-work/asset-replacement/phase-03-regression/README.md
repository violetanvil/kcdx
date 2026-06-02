# Phase 3 — regression coverage

The permanent `test-plugins/` regression coverage for the asset surface —
exercising override (declarative), cross-plugin reference (navigable namespace),
and the chain/conflict path, from both the Lua and C++ surfaces.

Shared spec: [`../plan-spec.md`](../plan-spec.md). Design authority:
[`../../restructure/phase-08.5-asset-replacement/asset-design.md`](../../restructure/phase-08.5-asset-replacement/asset-design.md).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| [8 — `cap-XX-asset-replace` regression plugin(s) + matrix rows](step-8-test-plugins.md) | NOT STARTED | — |

## Verification gate (whole phase)

The `cap-XX` plugin(s) ship under `test-plugins/`, suite-gated, self-reporting via
`kcdx.test.report` / `ReportTestResult`; the matrix rows cover override (a
known-safe vanilla replacement visible in-game), cross-plugin reference (one
plugin resolving another's published + by-path asset), and the chain/conflict
("lost to plugin X") path; both surfaces (Lua + C++) get a row per the
grow-the-suite + parity rules. The suite line confirms on the user's launch.
