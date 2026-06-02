# CLAUDE.md — kcdx

SKSE-class extender for Kingdom Come: Deliverance 2. Function hooks, trampolines, console commands, save serialization, plugin loader.

**Authoritative design:** `docs/design.md`. Read before writing implementation code or making schema/interface decisions.

**Status:** Phases 1–7 complete and live-verified.

## Build / test

- `pwsh ./build.ps1` → `build/Release/kcdx.exe` (launcher) + `kcdx.dll` (engine) + `kcdx-watchdog.exe`.
- `./package-release.ps1 -Version X.Y.Z` → drag-drop zip (authoritative artifact→destination mapping).
- **Live-test deploy (diff-scoped, agent-run).** Copy ONLY what your change rebuilt to the live install under `<game>/Bin/Win64MasterMasterSteamPGO/` (layout mirrors the zip): engine change → `kcdx-engine/kcdx.dll` (+ `kcdx-watchdog.exe` if rebuilt); launcher change → `kcdx.exe` at the bin root; test/user-plugin change → `kcdx-plugins/<name>/`; builtin-fix change → `kcdx-engine/builtin/<fix>/`. The agent runs every copy and hash-verifies via PowerShell `Get-FileHash`; the user does not deploy. No `.asi`, no `dinput8.dll` (Phase 1+).

## Rules (auto-load on matching paths)

| Concern | File |
|---|---|
| Cornerstones (UX > Capability > Perf; the disassembler test) | [cornerstones.md](.claude/rules/cornerstones.md) |
| SKSE / F4SE parity (naming, interfaces) | [skse-parity.md](.claude/rules/skse-parity.md) |
| TOML schema conventions | [toml-schema.md](.claude/rules/toml-schema.md) |
| Hook engine invariants (MinHook, conflict_engine, apply order) | [hook-engine.md](.claude/rules/hook-engine.md) |
| Git concurrency — no auto-branch; parallel chats share one tree; destructive ops re-confirm live state. Hooks: PreToolUse system `guard-git-lock.py` (shared-index race → block+retry), repo `guard-push-target.ps1` (block hand push public/--all/--mirror), system `guard-force-push.py` (auto-safen `--force`→`--force-with-lease`), system `guard-destructive-ops.py` (scope-fence + warn/block); PostToolUse system `tripwire-dynamic-deletion.py` (script/dynamic-rm deletions), system `track-touched-files.py` (session touched-set for the scope-fence) | [concurrency-git.md](.claude/rules/concurrency-git.md) |
| Public/private boundary — a public-facing file references NOTHING private (paths or AI-dev vocabulary); `guard-public-private-refs.ps1` warns at author-time | [public-private-boundary.md](.claude/rules/public-private-boundary.md) |
| Results-driven — test/probe the unknown, don't theorize | [results-driven.md](.claude/rules/results-driven.md) |
| Anti-patterns (passes-every-gate-yet-wrong; AP1–16) | [anti-patterns.md](.claude/rules/anti-patterns.md) |
| Anti-pattern *whys* — audit-only, NOT auto-loaded; writes are user-consent-gated by an ASK accept-prompt (`permissionDecision:"ask"`): the repo `guard-rationale-consent.ps1` gates this file; the system consent guard gates `anti-patterns.md`. An AP whose rationale here was not blessed is unauthorized. | [anti-pattern-rationale.md](.claude/anti-pattern-rationale.md) (not path-scoped) |
| Address Library (use IDs, not RVAs) | [address-library.md](.claude/rules/address-library.md) |
| Lua bridge — dual-Lua sentinel hazard (FIX C, PROBE Q) | [lua-bridge.md](.claude/rules/lua-bridge.md) |
| Authoring surface (Lua + C++) — learnable sublanguage; `kcdx.*` naming + call-shape; surfaces mirror | [lua-api-surface.md](.claude/rules/lua-api-surface.md) |
| Shared-namespace naming — `<pluginname>.<name>` model; engine-derived prefix; self>engine>other precedence; canonical dot; `kcdx.alias` | [naming-namespaces.md](.claude/rules/naming-namespaces.md) |
| Documentation discipline — doc entry + glossary term + parity row move with the capability, same commit | [docs-discipline.md](.claude/rules/docs-discipline.md) |
| Deletion hygiene — a removed surface leaves no prescriptive doc/rule/CLAUDE.md behind (subtractive mirror of docs-discipline) | [deletion-hygiene.md](.claude/rules/deletion-hygiene.md) |
| Lua numeric precision (LUA_NUMBER=float, pointer push rules) | [lua-precision.md](.claude/rules/lua-precision.md) |
| Lua callback threading (engine auto-marshals off-thread hits) | [lua-callback-threading.md](.claude/rules/lua-callback-threading.md) |
| Logging API (structured KV format) | [logging.md](.claude/rules/logging.md) |
| Loader / install layout (Phase 1+ own-launcher; deploy mapping) | [loader-architecture.md](.claude/rules/loader-architecture.md) |
| RE methodology (Ghidra, predecessor sigs, wiki) | [reverse-engineering.md](.claude/rules/reverse-engineering.md) |
| Pak mod test fixtures | [pak-mods.md](.claude/rules/pak-mods.md) |
| Test suite — every feature ships a permanent regression plugin | [test-suite.md](.claude/rules/test-suite.md) |
| Agent builds and deploys; the user only launches — `build.ps1`, deploy copies, hash verify, dev-mode enable, log read are agent actions | [agent-builds-and-deploys.md](.claude/rules/agent-builds-and-deploys.md) |

## Hard rules (always-on)

- **Stop and ask if unsure.** No autonomous design decisions on anything not specified in `docs/design.md` or a rule file.
- **Results-driven — test the unknown, don't theorize it.** A checkable question (will this hook fire? how many args? does this offset resolve? does the game still boot?) gets a probe/test with its outcome→meaning map BEFORE acting on inference. Theorizing on a checkable unknown, or fix #2 on a fresh theory after fix #1 failed, is a process violation. See [results-driven.md](.claude/rules/results-driven.md) (AP10).
- **The disassembler test — the engine does the heavy lifting.** A design making the author supply an address / offset / register / instruction length / hand-written signature for a common task is a UX defect — the name must supply address AND verified ABI; the disassembler is an expert-only, labeled escape hatch. "The author can just provide the signature/address" is the tell. See [cornerstones.md](.claude/rules/cornerstones.md) (AP12).
- **Adding an Address Library DB row requires explicit user approval (AP18).** Appending a NEW entity/version row to `data/seeds/address_names_seed.csv` or `address_versions_seed.csv` grows the DB — a new curated game-binary target (RVA / AOB pattern / vtable slot / game-struct offset) the project commits to maintaining across game versions. STOP and get the user's explicit sign-off on the specific entity BEFORE writing the row; an addition that lands unapproved is unauthorized. Gates the seed-row ADDITION only — NOT resolving an address by name/id in code (AP1's always-on expectation), NOT an UPDATE to an existing row (re-verify / deprecate / supersede). Warn-only `guard-seed-approval.ps1` flags it at author-time; the review gates carry the hard check. See [address-library.md](.claude/rules/address-library.md) + [data/seeds/policy.md](data/seeds/policy.md).
- **Read before edit.** Read the full file before modifying. Before changing a function signature, grep every caller.
- **Do NOT auto-create branches — commit to the current branch.** Multiple chats share ONE working tree; `git checkout -b` is global state that tangles histories. Never branch/switch/stash on your own initiative (`main` is correct here). Stage by specific path, never `git add -A`. This **overrides** the harness "branch first on the default branch" default. See [concurrency-git.md](.claude/rules/concurrency-git.md).
- **`git push` → private (comprehensive); public is reached ONLY via `pwsh ./publish-public.ps1`.** This repo IS the private repo (remote `private` = `violetanvil/kcdx-private`; `main` tracks it). `.gitignore` is its ordinary ignore list — everything not ignored is tracked and goes to private on a bare `git push`. The public remote (`public` = `violetanvil/kcdx`) is a fresh single-commit snapshot, force-pushed ONLY by `publish-public.ps1`. The script is an **allowlist**: it publishes only an explicit set of public dirs (`src`, `include`, `vendor`, `data`, `examples`, `kcdx-engine`, `test-plugins`, `tools`, `docs`) + root files (`README.md`, `LICENSE`, `CMakeLists.txt`, `build.ps1`, `package-release.ps1`) — **anything not on that list (incl. `.claude/`, `CLAUDE.md`, `_research/`, `third-party-ghidra/`, `test-fixtures/`, `.gitignore`, the script itself) defaults to private and is never published.** Adding a new top-level dir keeps it private unless you add it to the script's allowlist. **NEVER `git push public` / `git push --all` by hand** — that ships the comprehensive tree (the private materials are tracked, not gitignored) and leaks it. The public repo deliberately shows no trace of AI-assisted development; the allowlist is the dir/file/history layer of that (in-file `.claude/`-link / AP-vocabulary scrubbing is a separate, not-yet-done layer). See [concurrency-git.md](.claude/rules/concurrency-git.md).
- **A public-facing file references NOTHING private.** A file that ships to the public remote (under an allowlisted public dir — `src/`, `include/`, `vendor/`, `data/`, `examples/`, `kcdx-engine/`, `test-plugins/`, `tools/`, `docs/` — or an allowlisted root file) may reference no private document or AI-development trace, in prose, comment, doc link, or identifier: not `.claude/`, `CLAUDE.md`, `_research/`, `third-party-ghidra/`, `test-fixtures/`, `publish-public.ps1`, nor the words Claude/Anthropic/subagent/orchestrator/`AP<n>`-as-citation/governance slash-commands. Such a reference is a broken link on public AND a build-trace. Fix by stating the fact directly without the private citation (the private rule keeps the *why*; the public file restates the *what*). Private files may reference anything. Warn-only `guard-public-private-refs.ps1` flags it at author-time; reviews gate it. See [public-private-boundary.md](.claude/rules/public-private-boundary.md).
- **One-file, one-concern.** Past ~300 lines in any single source file: review for split. (Warn-only hooks flag new/growing files at ≥200 / ~300 lines.)
- **mempatch is deprecated.** All byte-rewrite, hook, trampoline, and engine-fix work ships through kcdx. Do not propose new mempatch work.
- **Every feature ships a regression test.** New functionality is not done until a permanent `test-plugins/` plugin exercises it and a matrix row is recorded. See [test-suite.md](.claude/rules/test-suite.md) (AP7 in [anti-patterns.md](.claude/rules/anti-patterns.md)).
- **Build-green is necessary, not sufficient.** A clean `pwsh ./build.ps1` proves compile + link, not that the feature works in-game or that an offset/ABI/vtable is right. The matrix is confirmed by a game launch; invariants by review. See [anti-patterns.md](.claude/rules/anti-patterns.md) §invariants-vs-gates.
- **The agent builds and deploys; the user only launches.** `pwsh ./build.ps1`, every deploy copy to the live install, hash verification of each artifact, dev-mode enablement, and the `kcdx-dev.log` read after the run are AGENT actions invoked via the agent's tool surface. The user runs ONE thing in the loop: the game launch (and tells you it ran). A skill or output that asks the user to build, copy a file, or paste a log line is a FLOW defect — fix the surface, do not perform the ask. Probes are not an exception (a `/debug` probe is agent-written, agent-built, agent-deployed; only the launch is the user's). See [agent-builds-and-deploys.md](.claude/rules/agent-builds-and-deploys.md).
- **Root cause required before fix; symptom-only patches are forbidden (AP17).** A bug is not closed until the `docs/known-issues/<title>.md` Resolution section's `Root cause:` paragraph states the mechanism in falsifiable terms — what value was wrong, who wrote it, in what order, why the original code path made that wrong write inevitable. "X no longer crashes" / "now boots" / "AV is gone" are restatements of the symptom going away, NEVER root cause. A passing repro is indistinguishable between a real fix and masking; only the mechanism paragraph rules out masking. Cannot answer "why?" in mechanism terms → another probe is owed (`results-driven.md`); the fix does not land. The single legitimate escape is an explicit user-approved "Provisional mask, root cause unknown" Resolution label, with the issue staying OPEN. See [anti-patterns.md](.claude/rules/anti-patterns.md) AP17.
- **Probes leave NO residue in live source — capture the finding + wiring, then remove the probe.** When a probe's question is answered, capture its finding + reusable wiring (the instrumentation recipe / script) into `_research/probe-archive/` as durable process-output, THEN remove the probe from the source file — the live source returns to pure production logic: no `#if 0` block, no commented-out corpse, no runtime-disabled flag. A probe adds ZERO cost to live code; the next investigation reconstructs it from the artifact tree, never from source. The captured finding ships alongside its `docs/known-issues/<title>.md §Resolution`. The no-two-LIVE-probes-in-the-tree constraint (`results-driven.md` §"Probe leaves no residue in live source"; warn-only `guard-probe-stack.ps1`) is unchanged. (This adopts the system `working-artifacts.md` no-residue invariant — superseding the prior `#if 0`-archive-in-place convention; existing `#if 0` archived blocks are migrated out of source via `/execute` per `docs/outstanding-work/system-baseline-reconciliation.md` P5.)
- **Orchestrators dispatch unbiased subagents for design forks AND root-cause verification — never decide alone, never surface raw to the user.** `/execute` does this for code-landing escalations (`_shared/orchestrator-loop.md` §E.1 → `architect-review`); `/debug` does the same for bug closures (`debug/SKILL.md` §2.5 Gate A → `architect-review` for design forks; §3d Gate B → `root-cause-verifier` for the Resolution paragraph). A design call surfaced raw to the user is a FLOW defect; a Resolution paragraph landed without verifier `land-fix` is a FLOW defect; the manager NEVER pastes the subagent's raw output and NEVER decides between landing vs not — only the verifier/architect verdict gates landing; only the user decides surfaced design forks. Every verifier run reads with WITHHELD context (the working agent's chain of reasoning is not input). Unbiased subagents checking each other's work is the discipline; the manager is the dispatcher, not the decider.
- **Commit at coherent milestones, not every edit, not "ask first."** A durable artifact closes a loop → invoke `/commit` without asking. Milestones: a written-up bug investigation, a probe that fired with evidence captured, a verified fix landed with its test, a finalized rule/doc rewrite, a captured RE finding, a completed orchestrator step. Non-milestones (no commit): a probe that didn't fire / mid-design, an unanswered question, a discarded hypothesis, an in-flight discussion. The bar is "closed loop, captured outcome," not "how it got produced." This **overrides** the harness default "Only create commits when requested" — `/commit` self-invokes at the milestone; the user decides direction, the agent decides cadence. Confirm-then-commit is reserved for `/commit`'s cohesion check (cross-chunk diffs) and sensitive-file warnings.
- **No native code loading beyond plugin DLLs.** No `.obj` loading, no `.so` for Linux Proton (KCD2 has no native Linux build).

## Workflow skills — picking the right one

| Situation | Skill |
|---|---|
| Close a coherent milestone with a commit (a written-up investigation, a working probe with captured evidence, a finalized rule rewrite, a trivial hand-edit) — the closing step of any skill that produced a durable artifact | `/commit` |
| Structure a SETTLED goal into a trackable work-plan tree (multi-phase, authored-not-built) — `docs/outstanding-work/<slug>/` with context.md + phase-grain top ledger + per-phase step ledgers + commit-grain step docs | `/plan` (structure-only; design forks route to `/senior-architect-consult`; does NOT build — `/feature`/`/execute` consume a step doc as their `Source work-item`) |
| New multi-part feature (a `[[...]]` TOML primitive, a `kcdx.*` Lua surface, a new interface — parser + engine + binding + test plugin spanning several commits) — built NOW in one motion | `/feature` |
| Non-trivial **single-commit** change (bug fix, refactor, RE patch site, outstanding-work item, one code-review finding) — one brief = one cycle = one commit | `/execute` |
| Architectural question before code (you are the audience) | `/senior-architect-consult` |
| Relaying a downstream agent's question / proposal / claim (the agent is the audience of the reply) | `/senior-architect-reply` |
| Skeptical review of code on disk (commit, PR, file, pending changes) | `/code-review` |
| Pre-launch checklist before the one game launch that confirms the test-suite matrix | `/verification-checkpoint` |
| File a bug for later — record symptom + evidence, no investigation now | `/report-bug` (writes one `KI-NNNN` known-issue doc, OPEN, Trail/Resolution empty; commits; stops — `/debug KI-NNNN` picks it up) |
| Hard bug whose cause isn't obvious from the symptom | `/debug` (auto-dispatches `architect-review` on design forks per §2.5 Gate A; auto-dispatches `root-cause-verifier` on Resolution per §3d Gate B; never surfaces raw to user) |
| Verify a game-function fact from the binary (address, ABI, return type, vtable slot) — reuse-first, fresh Ghidra last | `/research-disassembly` |
| Designing / auditing the governance infrastructure itself (`.claude/`, CLAUDE.md) | `/governance-architect` |

`/execute` runs the manager-subagent loop in [`_shared/orchestrator-loop.md`](.claude/skills/_shared/orchestrator-loop.md): the manager dispatches a subagent per step, runs `pwsh ./build.ps1` **itself** at the commit gate (never trusts the subagent's "it builds"), gates each commit on `step-review`, routes every escalation through `architect-review` in plain English, and commits each green step. The per-step gate is build-only — game-launch verification of the matrix is batched to `/verification-checkpoint`, which the user runs once.

`/debug` runs the SAME unbiased-subagent discipline (`.claude/skills/debug/SKILL.md`): a design fork that surfaces mid-investigation auto-routes through `architect-review` at §2.5 Gate A; the Resolution paragraph at §3d auto-routes through `root-cause-verifier` at Gate B (the verifier reads the known-issue file + fix diff + archive headers with the debug agent's chain-of-reasoning WITHHELD). The manager NEVER surfaces a design fork raw to the user and NEVER lands the Resolution without verifier `land-fix`. Gate A mirrors `/execute` §E.1; Gate B mirrors `/execute` §C.1's step-review gate.

`architect-review` / `step-review` / `root-cause-verifier` are agent-callees (orchestrator dispatch only), never user-invoked. The review skills share [`_shared/architectural-review.md`](.claude/skills/_shared/architectural-review.md).

## Per-task routing

| Task | Where to start |
|---|---|
| Write a kcdx plugin | `kcdx.toml` is **manifest-only** (`[kcdx]` / `[plugin]` / `[entrypoints]` — identity + metadata, no behavior tables; the legacy `[[patch]]`/`[[hook]]`/`[[mid_hook]]`/`[[trampoline]]`/`[[scan]]` parsers were deleted in Phase 5). Behavior ships in code: **Lua** → `plugin.lua` with `kcdx.bytes`/`kcdx.hook`/`kcdx.code`/`kcdx.command`/`kcdx.on`/`kcdx.publish`/`kcdx.scan`; **C++** → a sibling DLL exporting `kcdxPlugin_Load`, using `kcdxBytesInterface`/`kcdxHookInterface`/`kcdxTrampolineInterface` (link `include/kcdx/Interfaces.h`, or the `Kcdx.h` wrapper). See `docs/lua/` + `docs/cpp/`. |
| Write a first-party engine fix | `kcdx-engine/builtin/<fix-name>/kcdx.toml`. Same schema as user plugins. Ships in the kcdx release zip. See `docs/loader-architecture.md` §"Engine-fix plugins". |
| Resolve a new game function offset / verify an ABI fact | `/research-disassembly` (runs the reuse-first ladder per [reverse-engineering.md](.claude/rules/reverse-engineering.md): existing seed/`_research` → predecessor sigs → wiki → fresh Ghidra last). |
| Reverse-engineer a new patch site | `/research-disassembly`; methodology in [reverse-engineering.md](.claude/rules/reverse-engineering.md). |
| Hard bug investigation | Invoke `/debug` skill. |

## Reverse-engineering assets (in-repo)

All RE inputs live inside this repo:

- `_research/` — Ghidra dumps, ABI walker scripts, predecessor sigs, Warhorse wiki cache, phase-N recon notes. `_research/...` paths in rule files resolve here.
- `third-party-ghidra/` — pre-analyzed WHGame.dll Ghidra project (`ghidra_project/KCD2.gpr`), the Ghidra install, helper scripts (`ghidra_scripts/`), and `WHGame.dll`. Heavy binaries here are git-ignored; clone-and-go expects them already present locally.
- `docs/re-reference/` — RE methodology references (`finding-patch-sites.md`, `writing-safe-patches.md`).
- `test-fixtures/pak-mods/` — read-only pak-mod test fixtures (Lua probe sources + built `.pak`s). See [pak-mods.md](.claude/rules/pak-mods.md).

Don't propose changes outside this repo.

## Game install paths (verify before relying on)

- Game: `E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\`
- Game-bin root (deploy target): `<game>\Bin\Win64MasterMasterSteamPGO\` — `kcdx.exe` at root; `kcdx-engine\` (engine DLL + watchdog + builtin fixes + data); `kcdx-plugins\` (user plugins). Layout per [loader-architecture.md](.claude/rules/loader-architecture.md).
- Game version verified-against: `release_1_5_1164953_841` (April 2026)
- No anti-cheat — attaching debuggers is safe (modulo x64dbg instability per [reverse-engineering.md](.claude/rules/reverse-engineering.md)).

## Outstanding work

- `docs/known-issues/` — open bugs with diagnostic trails (see `/debug` skill).
- `docs/outstanding-work/` — designed-but-not-built items with revisit triggers.

## Memory

`~/.claude/projects/c--Users-Michael-Documents-KCD2-Mods-kcdx/memory/` for cross-session memories. `MEMORY.md` indexes them. Update when you learn something durable; use this CLAUDE.md as the primary onboarding doc since it's checked in.
