## Repo additions — orchestrator-loop

**kcdx verification model.** The per-step §C gate is BUILD-ONLY: the manager runs `pwsh ./build.ps1` and confirms a clean compile + link. Game-launch verification of the test-plugin matrix is batched to the §F.1 checkpoint (`/verification-checkpoint`), run once by the user after the cycle's commits land. Build-green is necessary, never sufficient (`anti-patterns.md` §invariants-vs-gates).

- **Build command (full + fast)** — `pwsh ./build.ps1` for both (there is no separate fast variant). The §C item-1 verification bar asserts exit 0 AND that all three artifacts were produced: `build/Release/kcdx.exe` + `kcdx.dll` + `kcdx-watchdog.exe`.

- **Design anchor** — `docs/design.md` (CLAUDE.md "Authoritative design"); the brief cites its §sections. Cornerstones (`.claude/rules/cornerstones.md`) decide ties.

- **Anti-pattern range (§A.7)** — `AP1–AP18`, audited against `.claude/rules/anti-patterns.md`. Evidence rule the §A.7 self-check ADDS: any claim about a game-function offset / ABI / vtable index requires an Address Library ID, `abi_walker` evidence, or Ghidra evidence per `.claude/rules/reverse-engineering.md` — training-data recall of a canonical CryEngine layout is not evidence (AP1/AP2/AP3). Label an AP-hit option `[AP1 hit — raw RVA instead of Address Library ID]`; AP-hit options cannot be Recommended.

- **§A.6 claim-phrasing addition** — the subagent declares its test mode per `.claude/rules/test-suite.md` ("The test procedure") — `boot-only` / `console` / `in-game`, with the exact command / save / gesture + falsifiable observable for the latter two — and does NOT write the numbered procedure and does NOT claim "the matrix passes" (game-launch verification is the user's, batched to the checkpoint).

- **Plans ledger root** — `docs/outstanding-work/`; the §C.3 / §F.4 source-ledger contract is `docs/outstanding-work/README.md` §"Status ledger".

- **Per-step test bar** — a `test-plugins/` regression plugin (existing or new) per `.claude/rules/test-suite.md`. A new feature names a new `cap-NN` / `comp-NN` plugin + a `test-plugins/README.md` matrix row; a behavior change names a sub-test in that feature's plugin. The plugin must be suite-gated (`test_suite_only = true`) and call `ReportTestResult` / `kcdx.test.report`. "No test plugin" is never acceptable for new functionality (AP7).

- **§C.6 deploy-and-verify** — the agent (not the user) deploys the diff-scoped artifacts to `<game-bin>/Bin/Win64MasterMasterSteamPGO/` per `.claude/rules/loader-architecture.md`:
  - engine C++ change → `kcdx-engine/kcdx.dll` (+ `kcdx-watchdog.exe` if rebuilt);
  - launcher change → `kcdx.exe` at the bin root;
  - builtin-fix change → `kcdx-engine/builtin/<fix>/`;
  - test/user-plugin change → its live path (existing suite plugins live under `kcdx-plugins/test-suite/<cap-NN>/`, not top-level — locate the real folder + remove stale artifacts before redeploying);
  - manifest/allowlist schema change → sync `kcdx.toml` across ALL THREE plugin trees (`kcdx-engine/builtin/`, `kcdx-plugins/`, `kcdx-plugins/test-suite/`) — missing one rejects those plugins at load.

  Then (b) hash-verify each copy against its `build/Release/...` source via PowerShell `Get-FileHash` — a mismatch is a deploy failure (surface via §E with the failed-artifact list: source path, destination, source-hash vs live-hash; do NOT emit §F). Then (c) ensure `<game-bin>/kcdx-engine/engine.toml` has `dev_mode = true` (create per `docs/dev-mode.md` if absent) — without it the suite self-skips. A docs-only / governance-only diff deploys nothing and reports "nothing deployed — docs/governance-only diff" in §F.

- **§F.1 checkpoint auto-invoke threshold (mechanical, agent-owned — never ask)** — render `/verification-checkpoint` as the §F body if ANY holds for the cycle's diff: spans 2+ distinct testable behaviors; adds 1+ new failure/error/abort branch; touches a hook surface / ABI signature / vtable slot / Address Library entry / save-cosave field / `[[...]]` schema; or modifies code from a prior phase. Skip (emit a trivial-launch tail) only when ALL of: one testable behavior, no new failure path, no hook/ABI/save/schema/prior-phase touch. The checkpoint runs its deploy-freshness probe as its first section regardless of §C.6 having just run.

- **§F game-launch acceptance** — the user runs ONE thing: the launch (launch → reach menu → declared `console`/`in-game` gestures → quit → "tell me it ran"). On the user's run signal ("test run" / "ran" / "review logs"), the agent reads the newest `<game-bin>/kcdx-engine/logs/kcdx-dev_<ts>.log` directly, finds `suite: X/Y passing` + any `FAIL <row>:` lines, and reports the verdict against the "What I'll look for" claim. Never ask the user to read or paste a log line (`.claude/rules/agent-builds-and-deploys.md`).

- **Domain verifier (added to the §C.1 / §E.1 baseline)** — `root-cause-verifier`, dispatched by `/debug` Gate B on a Resolution paragraph (its trigger + gate + verdict mapping are in `debug/SKILL.md` §D.1); it obeys the same `_shared/verification-contract.md` dispatch discipline (WITHHELD block + independence citation + gated verdict).
