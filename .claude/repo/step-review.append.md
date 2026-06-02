## Repo additions — step-review

- **Per-step test bar shape** — a `cap-NN` / `comp-NN` `test-plugins/` regression plugin (suite-gated `test_suite_only = true`, calling `ReportTestResult` / `kcdx.test.report`) + its `test-plugins/README.md` matrix row (`.claude/rules/test-suite.md`). A compliant step adds/updates the plugin in the diff; a missing one for new functionality is a finding (AP7).

- **Extra discipline rows (beyond §2)** —
  - **source-ledger flip** — a tracked step flips its `docs/outstanding-work/` ledger row to `DONE` + `(landed)` in the step's own commit (`.claude/rules/doc-organization.md`).
  - **deletion-hygiene sweep** — a diff that removes a `kcdx.*` surface / `kcdx*Interface` / TOML key / parser / console command / save field must sweep surviving prescriptive references in `docs/`, `.claude/rules/`, `CLAUDE.md` in the same commit (`.claude/rules/deletion-hygiene.md`).
  - **docs-discipline mirror** — a new capability's doc entry + glossary term + parity row move with the code, same commit (`.claude/rules/docs-discipline.md`).
  - **public/private boundary** — a surviving private reference in a public-facing file is a finding (`.claude/rules/public-private-boundary.md`).
