# Manager-subagent orchestrator loop — used by `/execute`

Holds the per-cycle manager-loop protocol. **Not a standalone skill.** The calling skill (`execute/SKILL.md`) defines entry, iteration shape, and exit; it MUST read this file cover-to-cover before dispatching the first subagent.

**kcdx verification model.** The per-step gate is BUILD-ONLY: the manager runs `pwsh ./build.ps1`, confirms a clean compile + link. Game-launch verification of the test-plugin matrix is batched to the pre-acceptance checkpoint (`verification-checkpoint/SKILL.md`), run once by the user after the cycle's commits land. Build-green is necessary, never sufficient (`anti-patterns.md` §invariants-vs-gates).

---

## Caller-injected parameters — one cycle = one step

Per cycle, the calling skill provides:

- **Step scope** — verbatim description of what the subagent must do (the user-confirmed `/execute` brief).
- **Reading list** — 1–3 load-bearing rule files named explicitly, plus `docs/design.md` sections cited, plus prior deliverables the subagent must read before writing line 1.
- **Authorized scope** — files / regions the subagent may modify (the brief's named files + transitive dependencies the manager pre-approved).
- **Per-step test bar** — the `test-plugins/` regression plugin (existing or new) that will exercise the change, named per `.claude/rules/test-suite.md`. A new feature names a new `cap-NN`/`comp-NN` plugin + matrix row; a behavior change to an existing feature names a sub-test in that feature's plugin. "No test plugin" is never an acceptable answer for new functionality. The subagent must also return a **test-mode declaration** per `.claude/rules/test-suite.md` ("The test procedure") — `boot-only` / `console` / `in-game`, with the exact command / save / gesture + falsifiable observable for the latter two. The subagent declares the mode; it does NOT write the numbered list and does NOT claim the procedure ran.
- **Resolved ambiguities** — any decisions the caller already secured from the user. Empty when none apply.
- **Touches-existing-code flag** — `true` if the step modifies a file already in HEAD or from prior work; triggers the inline impact-analysis procedure in §A.5.

---

## A. Construct the subagent brief

The brief MUST contain:

1. **Step scope** — verbatim from caller.
2. **Resolved ambiguities** — verbatim from caller (omit section if empty).
3. **Reading list** — caller-named rule files + `docs/design.md` sections + prior deliverables. Other path-scoped rules auto-load via `paths:` frontmatter and need not be cited unless load-bearing for this step.
4. **Per-step test bar** — the named test-suite plugin + matrix row per `.claude/rules/test-suite.md`. The deliverable is not complete until the plugin exists and reports.
5. **If touches-existing-code, inline impact-analysis procedure, verbatim:**

   > Before changing the file: grep the workspace for every site that calls / imports / implements / reads-writes the function or type you are about to modify. List every direct caller, every override, every data-path dependent, every test plugin. Surface the list to me before making the change — I will sign off on the blast radius. Make the change atomically across every affected site in one deliverable; do not leave callers on the old API. After implementing, name every caller's behavior you verified still works. (This is the CLAUDE.md hard rule: "Before changing a function signature, grep every caller.")

6. **Authority statement, verbatim:**

   > Do not make any design decisions. If anything is unclear, return the question to me; do not invent an answer. No autonomous design calls on anything not specified in `docs/design.md` or a `.claude/rules/` file. Read the rule files in the reading list cover-to-cover before writing line 1. After implementing, do NOT claim "it builds" or "the matrix passes" in your report — I will run `pwsh ./build.ps1` myself at the commit gate, and game-launch verification is the user's, batched to the checkpoint. Declare your test mode per `.claude/rules/test-suite.md` ("The test procedure"); do NOT write the numbered procedure yourself and do NOT claim it ran.

7. **Anti-pattern self-check before surfacing options, verbatim:**

   > If you escalate to me with options for a decision, use the Decision / Options / Recommendation / Why format from `.claude/skills/_shared/architectural-review.md` §Design decisions surface — plain-English framing, symbols in parens, Recommendation + Why mandatory. Audit each option against `.claude/rules/anti-patterns.md` AP1–9 BEFORE surfacing. For each option, label any AP hit prominently (e.g., `[AP1 hit — raw RVA instead of Address Library ID]`). Options with AP hits cannot be marked Recommended; a rule-compliant alternative must be surfaced first. Any claim about a game-function offset, ABI, or vtable index requires an Address Library ID / abi_walker evidence / Ghidra evidence per `.claude/rules/reverse-engineering.md` — training-data recall of a canonical CryEngine layout is not evidence (AP1/AP2/AP3).

---

## B. Dispatch

Use the `Agent` tool with `subagent_type: "general-purpose"`. Pass the brief as the `prompt`. Wait for the subagent to return.

---

## C. Verify deliverable

Load-bearing step. The manager runs verification, not the subagent.

1. **Build clean?** Run `pwsh ./build.ps1` yourself via the PowerShell tool. Read the actual output. Do NOT take the subagent's word. Confirm exit 0 AND that `build/Release/kcdx.exe` + `kcdx.dll` + `kcdx-watchdog.exe` were produced. Any compile error / link error / warning → step failed.
2. **Test plugin present?** The named `test-plugins/` plugin (or sub-test) from the per-step test bar exists in the diff, is suite-gated (`test_suite_only = true`), calls `ReportTestResult`/`kcdx.test.report`, and has its matrix row in `test-plugins/README.md`. Absent for new functionality → step failed (AP7). The plugin's in-game *result* is verified later at the checkpoint, not here. The subagent's deliverable also carries a test-mode declaration (§A.4); missing → re-task for it before §F.
3. **Step-review clean?** Dispatch step-review per §C.1. Apply its §4 next-action.
4. **Files modified only in authorized scope?** Check `git diff --name-only` against caller-provided authorized scope. Out-of-scope modifications → escalate, do not re-task.
5. **Anything the manager cannot evaluate confidently?** Escalate, do not re-task.

If all five pass → step succeeded. Invoke `/commit` to land the step's changes (cohesion heuristic auto-commits without user approval — a single step is a cohesive chunk). After commit, advance the step-review marker per §C.1. Capture the commit hash for the caller's report.

6. **Deploy the diff-scoped artifacts, verify each copy landed, AND enable dev mode** so the user's launch tests what was just committed and the suite actually runs.

   **a. Copy.** ONLY what this change rebuilt, to its real destination per `.claude/rules/loader-architecture.md`:
   - engine C++ change → `kcdx-engine/kcdx.dll`, + `kcdx-watchdog.exe` if rebuilt;
   - launcher change → `kcdx.exe` at the bin root;
   - builtin-fix change → `kcdx-engine/builtin/<fix>/`;
   - test/user-plugin change → its real live-install path per CLAUDE.md "Game install paths" + memory `project_kcdx_test_suite_deploy_layout` (existing suite plugins live under `kcdx-plugins/test-suite/<cap-NN>/`, not top-level — locate the real folder before redeploying);
   - manifest/allowlist schema change → sync `kcdx.toml` across ALL THREE plugin trees per memory `project_kcdx_deploy_all_plugin_trees` (`kcdx-engine/builtin/`, `kcdx-plugins/`, `kcdx-plugins/test-suite/`) — missing one rejects those plugins at load.

   **b. Verify each copy.** For every artifact copied in §6a, hash-compare the live-install file against its `build/Release/...` source (PowerShell `Get-FileHash`). Mismatch on ANY artifact = deploy failed. Do NOT proceed; surface to user via §E with the failed artifact list (which `build/Release/...` source, which live-install destination, source hash vs live hash). Common causes: file locked by a running game process; permission denied; wrong destination path. Resolve before continuing. Do NOT enable dev mode and do NOT emit the §F report until every artifact hashes equal.

   **c. Enable dev mode.** Ensure `<kcdx-engine>/engine.toml` has `dev_mode = true` (create it if absent, per `docs/dev-mode.md`) — without it the test suite self-skips. Enabling dev mode is the agent's job, never a user step.

   A docs-only / governance-only diff deploys nothing, skips §6a–§6c, and reports "nothing deployed — docs/governance-only diff" in §F. Game-bin root is in CLAUDE.md ("Game install paths"). Report what was deployed AND that hashes verified in the §F report.

Proceed to §F.

---

## C.1 — Step-review dispatch (gates step commit)

After build clean + test plugin present, dispatch step-review BEFORE committing.

**Dispatch via the `Agent` tool, NOT via `Skill`.** `subagent_type: general-purpose`. Prompt template:

```
You are the step-review skill. Read cover-to-cover before producing output:

  1. .claude/skills/step-review/SKILL.md
  2. .claude/skills/_shared/architectural-review.md
  3. Rule files matching the paths the diff touches

Review context:

  Step's diff:
  <paste `git diff HEAD` output for the step in progress>

  Step's authorized scope:
  <paste the authorized scope the manager provided in §A>

  Resolved ambiguities applicable to this step:
  <paste the relevant entries; "none" if none apply>

  Per-step test bar:
  <named test-suite plugin + matrix row per §A.4>

Produce the four-section structured review per step-review/SKILL.md's
output format. Your output returns to me — the orchestrator — as a tool
result. The user will NOT see it directly. §3 (Design decisions surfaced)
is the only forwardable section; write it in plain-English so the user
reads it cold when I forward.
```

**Read the tool result yourself. Apply §4 next-action:**

- **`commit-step`** — Diff clean. Proceed to §C step 4 → §F. After commit lands, write `.git/step-review-state/<sanitized-branch>` (line 1 = new commit hash, line 2 = ISO 8601 UTC timestamp). Sanitize branch name by replacing `/` with `--`.
- **`re-task-subagent`** — Mechanical fixes only. Use step-review's §4 direction to construct the next re-task brief (§D applies; counts as a different failure signal if the prior re-task was for a different cause). Do NOT commit; do NOT advance the marker. Dispatch the step's subagent.
- **`escalate`** — Design issue or rule violation needs user input. Compose §E escalation; §E.1 auto-routes through architect-review first. Do NOT commit; do NOT advance the marker.

**Never paste step-review's raw output to the user.** Raw output = your input. The user reads what YOU compose from it (when §4 says `escalate`, compose using step-review's §1 verdict + §2 findings + §3 design decisions, never §4). If the user explicitly asks for step-review's full reasoning, paste §1+§2+§3 (never §4) verbatim — only on explicit request.

**Marker independence.** `.git/step-review-state/<sanitized-branch>` is orchestrator-side state. `/code-review` (user-invoked) keeps a separate marker at `.git/code-review-state/<sanitized-branch>`. The user can run `/code-review` at any time and see everything since their last user-run review, regardless of orchestrator clears.

---

## D. Re-task on progress; escalate on stuck

**Re-task freely while the subagent is making progress** — different failures each round, build advancing toward clean. Failed builds during normal iteration are common and do NOT carry a numeric budget. No invocation cap.

**Escalate (per §E) when the subagent is STUCK** — defined as: §C failed twice in a row with the SAME root-cause signal. Stuck means another re-task won't help; the subagent isn't learning from the prior failure surface.

**Same root-cause signal (stuck — escalate):**
- Same compiler/linker error on the same file/line span twice.
- Same test plugin failing to build the same way twice.
- Same anti-pattern hit on the same file/line (e.g., AP4 on `src/hooks.cpp:42` twice).
- Same scope violation (same file edited outside authorized scope) twice.
- Subagent's response shape regresses (self-reports build clean without command output, makes an ABI/offset claim without Address Library/Ghidra evidence) twice — same agent-framing pattern from `_shared/architectural-review.md` §5.

**Different signal (progress — keep iterating):**
- New compile error after fixing the prior one.
- Different file, different rule, different anti-pattern.
- Build advancing past a stage that previously failed.

**Tracking mechanics.** The manager records each invocation's §C failure surface (or success). Before dispatching the next invocation, compare the planned re-task brief's failure surface to prior. Same signal → don't re-task; escalate per §E. Different signal → re-task is fine, no budget cost.

**Re-task brief shape:**

1. **Verbatim failure surface** — the actual `pwsh ./build.ps1` output excerpt OR the specific anti-pattern hit (file:line + which pattern from anti-patterns.md) OR the missing-test-plugin note.
2. **Reminder:** "Do not make design decisions. If the fix requires a design call, surface to me — that's not a failure, it's the expected behavior."
3. **Reading list refresher** for any rule that was violated.

Dispatch. Wait. Re-run §C.

---

## E. Escalation triggers — immediate

Escalate to the user immediately on:

- **Subagent surfaced a design question** (subagent did its job — surfaced rather than invented).
- **Anti-pattern hit** of any kind from `.claude/rules/anti-patterns.md` — raw RVA, prologue-shape ABI, header-derived vtable index, hook outside the conflict engine, new Lua sentinel, off-thread callback, etc.
- **File modified outside the authorized scope.**
- **Manager's review finds something it cannot evaluate confidently** (subagent claims a new approach is needed; deliverable contains code the manager doesn't recognize as matching the brief).
- **Subagent is stuck** — §C failed twice in a row with the same root-cause signal per §D.

---

## E.1 — Every §E escalation routes through architect-review first

Before forwarding any §E escalation to the user, dispatch architect-review as a subagent. Every §E escalation, no exceptions, no classification. You do NOT decide "needs architect" vs "doesn't."

**Dispatch via the `Agent` tool, NOT via `Skill`.** `subagent_type: general-purpose`. Prompt template:

```
You are the architect-review skill. Read cover-to-cover before producing output:

  1. .claude/skills/architect-review/SKILL.md
  2. .claude/skills/_shared/architectural-review.md
  3. docs/design.md sections cited below + rule files matching the step's path scope

Escalation context:

  Subagent's escalation (verbatim):
  <paste the subagent's full escalation message>

  Subagent's git-diff (if any code was written):
  <paste git diff, or "no diff — surfaced before any code">

  Authorized scope:
  <paste the authorized scope the manager provided in §A>

  Resolved ambiguities applicable:
  <paste the relevant entries; "none" if none apply>

  docs/design.md sections cited:
  <list>

  Rule files matching the step's path scope:
  <list>

Produce the four-section structured review per architect-review/SKILL.md's
output format. Your output returns to me — the orchestrator — as a tool
result. The user will NOT see it directly. §3 (Design decisions surfaced)
is the only forwardable section; write it in plain-English so the user
reads it cold when I forward.
```

**Read the tool result yourself. Apply §4 Recommended next action:**

- **`re-task-subagent`** — Surface NOTHING to user. Build next re-task brief from architect's §4 direction + §A.1–§A.7 structure. Dispatch the step's subagent. Next user-facing message is §F per-step report (success path) or a different escalation if one arises.
- **`forward-and-wait`** — Compose user-facing §E.2 escalation. Use architect §1 (verdict) to write the lead, architect §2 (audit findings) for the findings line, architect §3 (decision + options + Recommendation + Why) for the decision block. NEVER include §4. NEVER paste raw architect output verbatim — fill §E.2's scaffold using §3's content. Wait for user decision; do not re-task until user responds.
- **`hold`** — Compose user-facing §E.2 escalation. Lead + findings + one-line: *"architect declined to surface options; awaiting your direction."* Omit the decision block. Wait for user direction.

**Hard rule — never paste the architect's raw output to the user.** Raw output = your input; the user reads what YOU compose into §E.2. If user explicitly asks for the architect's reasoning, paste §1+§2+§3 (never §4) verbatim — only on explicit request.

---

## E.2 — Escalation format

Emit exactly this shape:

```
Escalating <caller-id>: <one-line plain-English impact (symbols in parens)>.

What the subagent did: <one short paragraph, plain English, factual; symbols in parens>.
What's blocking: <one sentence in plain English naming what failed and why it matters>, followed by the raw surface (verbatim build output / anti-pattern hit at file:line).

Decision needed — <plain-English question (symbols in parens)>?

- Option A — <plain-English description (symbols in parens)>. Pros / cons in user-facing terms.
- Option B — <plain-English description (symbols in parens)>. Pros / cons in user-facing terms.
- [If applicable] Option C — <...> [AP<N> hit — <pattern>]. Pros / cons. (Cannot be Recommended.)

Recommendation: Option <A | B>.
Why: <1 sentence grounded in rule / docs/design.md / risk>.

Stopping until you respond.
```

`<caller-id>` is the caller's identifier (execute case: `execute brief`).

The Options / Recommendation / Why block is populated from architect-review §3 (per §E.1 `forward-and-wait`). Recommendation + Why are mandatory — no "neutral", no "user preference call"; if architect §3 is missing them, that's an architect-review defect — re-dispatch architect-review before forwarding to the user.

**Plain-English framing — required for every line above.** The user reads the escalation cold. Every symbol (function name, type, vtable index, Address Library ID, file path, TOML key, hook site) must be glossed in plain English before or alongside the symbol. Symbols belong in parentheses as anchors, not as the message itself.

- ❌ *"`AddCommand` at vtable[32] without conflict_engine footprint"*. ✅ *"Subagent wants to register the console command through what it believes is the engine's AddCommand slot (vtable index 32), but that's the script-string overload — the function-pointer overload kcdx needs is slot 33 (AP3), and the call also skips the conflict engine (AP4)."*
- Raw output (compiler errors) is fine to paste verbatim under "What's blocking", but the preceding sentence must say what failed and why it matters at the user's level.

Then STOP. Do not proceed. Do not dispatch another subagent.

---

## F. Per-cycle report to user

After a cycle succeeds AND has been committed AND deploy hashes verified per §C.6, the caller emits its own report shape.

### F.1 — Checkpoint dispatch decision (mechanical, agent-owned)

BEFORE emitting the §F report, the manager decides whether to auto-invoke `verification-checkpoint`. Do NOT ask the user. The threshold is mechanical:

**Auto-invoke `verification-checkpoint`** if ANY of the following holds for the cycle's diff:
- Spans 2+ distinct testable behaviors (more than one falsifiable claim).
- Adds 1+ new failure/error/abort branch.
- Touches a hook surface, ABI signature, vtable slot, Address Library entry, save/cosave field, or `[[...]]` schema.
- Modifies code from a prior phase (any file in HEAD before this cycle began).

**Skip the checkpoint, emit the trivial-launch tail** only when ALL of:
- One testable behavior.
- No new failure path.
- No hook/ABI/save/schema/prior-phase touch.

When the threshold fires, render the verification-checkpoint output as the §F body (per `verification-checkpoint/SKILL.md` "Format"), prefixed with the caller's lead and followed by the caller's tail. The checkpoint runs its `### Deploy status` freshness probe as its first section regardless of §C.6 having just run — defense in depth against partial deploys, locked files, and stale live-install state from another chat.

### F.2 — Report shape

```
<Caller-specific lead — e.g., "Execute brief landed and committed as <short-hash>">

What landed: <one paragraph from subagent's deliverable, factual>
Build: clean (`pwsh ./build.ps1` exited 0; kcdx.exe + kcdx.dll + kcdx-watchdog.exe produced)
Test plugin: <test-plugins/<row-id>-<name> added/updated; matrix row recorded — in-game result pending the checkpoint launch>
Files modified: <list>
Deployed: <diff-scoped artifacts copied + destinations + hashes verified per §C.6; dev mode enabled (engine.toml); "nothing — docs/governance-only diff" if none>

[If F.1 fires: render verification-checkpoint output here, starting with ### Deploy status.]
[If F.1 skips: render the trivial-launch block below.]

What this proves: <one plain-English falsifiable sentence — what the run confirms or denies>.
What I'll look for: <the exact log signal — the matrix row(s) that must read PASS, plus the `FAIL <row>:` text that would deny it>.

Test procedure (run verbatim):
<Render the user-keyboard-only procedure per `.claude/rules/test-suite.md` ("The test procedure"): the canonical launch-to-menu (Launch → reach menu → Quit → tell me it ran), with the subagent's declared `console`/`in-game` user gestures inserted between "Reach the main menu" and "Quit", each tagged with the matrix row it confirms. A `boot-only` declaration renders the canonical steps unchanged. Do NOT include deploy, dev-mode, or log-read steps — those are already done (deploy + dev mode + hash-verified) or are mine to do after you signal (log read).>

<Caller-specific tail — e.g., "Execute cycle complete; stopping. Run the procedure above, then tell me it ran (e.g. \"test run\") — I'll read the matrix from kcdx-dev.log and report.">
```

The "or invoke /verification-checkpoint first" hint is REMOVED from the caller tail. The manager already made that call mechanically at F.1; offering it to the user re-delegates an agent-owned decision.

**On the user's run signal** (e.g. "test run" / "review logs"): read the newest `<kcdx-engine>/logs/kcdx-dev_<ts>.log`, find the `suite: X/Y passing` line + any `FAIL <row>:` lines, and report the verdict against the "What I'll look for" claim. Do NOT ask the user to read the log.

---

## Hard rules

- **Manager never makes design decisions.** Every design question goes to the user.
- **Manager always runs `pwsh ./build.ps1` itself at the §C commit gate.** Subagent's claim of build status is irrelevant until the manager runs the command and sees the output (AP8).
- **Build-green is necessary, not sufficient.** It does not prove the feature works in-game. Game-launch verification of the matrix is the user's, batched to the checkpoint.
- **Step-review gates every commit** per §C.1.
- **Pattern-detection re-task per §D, no numeric cap.** Re-task freely while making progress; escalate when stuck.
- **Auto-route every §E escalation through architect-review** per §E.1.
- **No batching cycles.** One cycle at a time, with verification + per-cycle report between each.
- **No skipping verification.** Even if the subagent's deliverable looks fine, run build.ps1.
