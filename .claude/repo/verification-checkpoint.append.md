## Repo additions — verification-checkpoint

- **Full build command + required artifacts** — `pwsh ./build.ps1`; the build-status line asserts exit 0 AND `build/Release/kcdx.exe` + `kcdx.dll` + `kcdx-watchdog.exe` produced.

- **Deploy step** — before manual acceptance the agent deploys the diff-scoped artifacts to `<game-bin>/Bin/Win64MasterMasterSteamPGO/` (engine → `kcdx-engine/kcdx.dll` [+watchdog]; launcher → `kcdx.exe` at bin root; builtin → `kcdx-engine/builtin/<fix>/`; test-plugin → `kcdx-plugins/test-suite/<cap-NN>/`; a manifest/allowlist change syncs `kcdx.toml` across ALL THREE plugin trees — `kcdx-engine/builtin/`, `kcdx-plugins/`, `kcdx-plugins/test-suite/`), hash-verifies each via `Get-FileHash` (hash the file behind the live-install path the user launches), and sets `engine.toml` `dev_mode = true`. The Deploy-status section runs this freshness probe as its first section regardless of a prior deploy. (Mapping detail is the orchestrator-loop append's §C.6.)

- **Manual-acceptance shape** — single-machine: launch → reach main menu → run the declared `console`/`in-game` gestures → quit. The handoff word the user gives when done is "test run" (or "ran" / "review logs"). The user runs ONLY the launch; deploy, dev-mode, and the log read are the agent's.

- **Acceptance signal sink + read recipe** (`.claude/rules/acceptance-signal.md`) — kcdx's existing reporter writes `suite: X/Y passing` + `FAIL <row>:` lines to the newest `<game-bin>/kcdx-engine/logs/kcdx-dev_<ts>.log`; this is a THIN ADAPTER over the canonical grammar (read `suite: X/Y passing` as `ACCEPT-SUITE: <n>/<total> passing`; each `FAIL <row>:` as `ACCEPT-RESULT: FAIL <row>`, every non-failing matrix row PASS). On the user's handoff word the agent reads that newest log directly and greps the `suite:` + `FAIL` lines (scoped — never the whole log). The user never reads it.

- **Anti-pattern audit rows** — the §Anti-pattern-audit section lists the AP1–AP9 manual-audit classes per `.claude/rules/anti-patterns.md` (offset/ABI/vtable evidence, hook-in-conflict-engine, test-plugin present, structured-KV logging, build-green-is-not-sufficient, …).

- **Docs-discipline mirror checklist** — a new capability's doc entry + glossary term + parity row must have moved with the code, same commit (`.claude/rules/docs-discipline.md`); a removed surface leaves no prescriptive survivor (`.claude/rules/deletion-hygiene.md`).
