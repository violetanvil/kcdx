# CLAUDE.md — kcdx

SKSE-class extender for Kingdom Come: Deliverance 2. Function hooks, trampolines, console commands, save serialization, plugin loader.

**Authoritative design:** `docs/design.md`. Read before writing implementation code or making schema/interface decisions.

**Status:** Phases 1–7 complete and live-verified.

## Build / test

- `pwsh ./build.ps1` → `build/Release/kcdx.exe` (launcher) + `kcdx.dll` (engine) + `kcdx-watchdog.exe`.
- `./package-release.ps1 -Version X.Y.Z` → drag-drop zip (authoritative artifact→destination mapping).
- **Live-test deploy (diff-scoped, manual).** Copy ONLY what your change rebuilt to the live install under `<game>/Bin/Win64MasterMasterSteamPGO/` (layout mirrors the zip): engine change → `kcdx-engine/kcdx.dll` (+ `kcdx-watchdog.exe` if rebuilt); launcher change → `kcdx.exe` at the bin root; test/user-plugin change → `kcdx-plugins/<name>/`; builtin-fix change → `kcdx-engine/builtin/<fix>/`. No `.asi`, no `dinput8.dll` (Phase 1+).

## Rules (auto-load on matching paths)

| Concern | File |
|---|---|
| Cornerstones (UX > Capability > Perf; the disassembler test) | [cornerstones.md](.claude/rules/cornerstones.md) |
| SKSE / F4SE parity (naming, interfaces) | [skse-parity.md](.claude/rules/skse-parity.md) |
| TOML schema conventions | [toml-schema.md](.claude/rules/toml-schema.md) |
| Hook engine invariants (MinHook, conflict_engine, apply order) | [hook-engine.md](.claude/rules/hook-engine.md) |
| Git concurrency — no auto-branch; parallel chats share one tree | [concurrency-git.md](.claude/rules/concurrency-git.md) |
| Results-driven — test/probe the unknown, don't theorize | [results-driven.md](.claude/rules/results-driven.md) |
| Anti-patterns (passes-every-gate-yet-wrong; AP1–13) | [anti-patterns.md](.claude/rules/anti-patterns.md) |
| Anti-pattern *whys* — audit-only, NOT auto-loaded; writes are user-consent-gated (`guard-anti-pattern-consent.ps1` forces an accept-prompt on this file + `anti-patterns.md`). An AP whose rationale here was not blessed is unauthorized. | [anti-pattern-rationale.md](.claude/anti-pattern-rationale.md) (not path-scoped) |
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

## Hard rules (always-on)

- **Stop and ask if unsure.** No autonomous design decisions on anything not specified in `docs/design.md` or a rule file.
- **Results-driven — test the unknown, don't theorize it.** A checkable question (will this hook fire? how many args? does this offset resolve? does the game still boot?) gets a probe/test with its outcome→meaning map BEFORE acting on inference. Theorizing on a checkable unknown, or fix #2 on a fresh theory after fix #1 failed, is a process violation. See [results-driven.md](.claude/rules/results-driven.md) (AP10).
- **The disassembler test — the engine does the heavy lifting.** A design making the author supply an address / offset / register / instruction length / hand-written signature for a common task is a UX defect — the name must supply address AND verified ABI; the disassembler is an expert-only, labeled escape hatch. "The author can just provide the signature/address" is the tell. See [cornerstones.md](.claude/rules/cornerstones.md) (AP12).
- **Read before edit.** Read the full file before modifying. Before changing a function signature, grep every caller.
- **Do NOT auto-create branches — commit to the current branch.** Multiple chats share ONE working tree; `git checkout -b` is global state that tangles histories. Never branch/switch/stash on your own initiative (`main` is correct here). Stage by specific path, never `git add -A`. This **overrides** the harness "branch first on the default branch" default. See [concurrency-git.md](.claude/rules/concurrency-git.md).
- **One-file, one-concern.** Past ~300 lines in any single source file: review for split. (Warn-only hooks flag new/growing files at ≥200 / ~300 lines.)
- **mempatch is deprecated.** All byte-rewrite, hook, trampoline, and engine-fix work ships through kcdx. Do not propose new mempatch work.
- **Every feature ships a regression test.** New functionality is not done until a permanent `test-plugins/` plugin exercises it and a matrix row is recorded. See [test-suite.md](.claude/rules/test-suite.md) (AP7 in [anti-patterns.md](.claude/rules/anti-patterns.md)).
- **Build-green is necessary, not sufficient.** A clean `pwsh ./build.ps1` proves compile + link, not that the feature works in-game or that an offset/ABI/vtable is right. The matrix is confirmed by a game launch; invariants by review. See [anti-patterns.md](.claude/rules/anti-patterns.md) §invariants-vs-gates.
- **No native code loading beyond plugin DLLs.** No `.obj` loading, no `.so` for Linux Proton (KCD2 has no native Linux build).

## Workflow skills — picking the right one

| Situation | Skill |
|---|---|
| Trivial single-file edit already made by hand (markdown typo, comment fix, matrix-row status update) | `/commit` |
| New multi-part feature (a `[[...]]` TOML primitive, a `kcdx.*` Lua surface, a new interface — parser + engine + binding + test plugin spanning several commits) | `/feature` |
| Non-trivial **single-commit** change (bug fix, refactor, RE patch site, outstanding-work item, one code-review finding) — one brief = one cycle = one commit | `/execute` |
| Architectural question before code (you are the audience) | `/senior-architect-consult` |
| Relaying a downstream agent's question / proposal / claim (the agent is the audience of the reply) | `/senior-architect-reply` |
| Skeptical review of code on disk (commit, PR, file, pending changes) | `/code-review` |
| Pre-launch checklist before the one game launch that confirms the test-suite matrix | `/verification-checkpoint` |
| Hard bug whose cause isn't obvious from the symptom | `/debug` |
| Verify a game-function fact from the binary (address, ABI, return type, vtable slot) — reuse-first, fresh Ghidra last | `/research-disassembly` |
| Designing / auditing the governance infrastructure itself (`.claude/`, CLAUDE.md) | `/governance-architect` |

`/execute` runs the manager-subagent loop in [`_shared/orchestrator-loop.md`](.claude/skills/_shared/orchestrator-loop.md): the manager dispatches a subagent per step, runs `pwsh ./build.ps1` **itself** at the commit gate (never trusts the subagent's "it builds"), gates each commit on `step-review`, routes every escalation through `architect-review` in plain English, and commits each green step. The per-step gate is build-only — game-launch verification of the matrix is batched to `/verification-checkpoint`, which the user runs once. `architect-review` / `step-review` are agent-callees (subagent dispatch only), never user-invoked. The review skills share [`_shared/architectural-review.md`](.claude/skills/_shared/architectural-review.md).

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
