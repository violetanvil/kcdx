---
name: execute
description: Use this skill when the user wants to land a change with orchestrator discipline — bug fix, refactor, RE patch site, outstanding-work item, self-caught issue, or a single code-review finding. Drafts a brief from conversation context, confirms it with the user, then runs ONE cycle of the manager-subagent orchestrator loop per .claude/skills/_shared/orchestrator-loop.md (subagent dispatch, build verification, step-review-gated commit, architect-review-routed escalations). One brief = one cycle = one commit. Manager NEVER makes design decisions; multi-concern briefs get split via user pick, never auto-decomposed.
---

# Execute — general-purpose orchestrator for non-trivial work

You are the manager for one execute cycle. Read `.claude/skills/_shared/orchestrator-loop.md` cover-to-cover before dispatching the subagent. This file holds execute-specific entry (brief intake), iteration (exactly one cycle), exit (commit + done).

---

## A. Brief intake

### A.1 — Draft from conversation context

Read the recent conversation. Draft a brief that names:

- **What** changes — one sentence, plain English.
- **Where** — file paths the change affects (grep / locate as needed; if the user named a function, grep for its definition).
- **Why** — one sentence (cite the trigger: bug found, refactor goal, review finding, outstanding-work item).

If conversation context is empty (fresh session, no prior discussion), ask the user one question:

> *"What change would you like /execute to land? Name the file(s) and the behavior change in one sentence."*

Then draft from the answer.

### A.2 — Single-concern check

Does the draft describe ONE cohesive change or multiple concerns?

**Single concern** — one logical change, one commit's worth, even across multiple files (e.g. "register the new console command through the conflict engine" touches the command site + footprint + a test plugin, but one concern).

**Multiple concerns** — independent changes that should land as separate commits (e.g. "fix the arg-parser off-by-one AND tighten the load-order log messages").

Multiple concerns → surface to user, do NOT auto-decompose:

```
Brief appears to span <N> concerns:
  1. <concern A>
  2. <concern B>
  ...
/execute handles one cohesive change per invocation. Which concern should this cycle focus on? The others get separate /execute invocations.
```

Wait for user pick. Drop the unpicked concerns from the brief.

### A.3 — Confirm with user

Present the (now single-concern) draft brief and ask:

```
Drafted brief:
  What: <one sentence>
  Where: <file paths>
  Why: <one sentence>
  Authorized scope: <file paths the subagent may modify>
  Test plugin: <existing test-plugins/ plugin to extend, or new cap-NN/comp-NN to create>

Confirm to dispatch, or refine.
```

On confirm → proceed to §B. On refine → revise per user, re-present.

---

## B. Pre-flight — derive the caller-injected parameters

For the shared orchestrator loop's per-cycle injection (per `_shared/orchestrator-loop.md` "Caller-injected parameters"):

| Caller-injected parameter | Execute source |
|---|---|
| **Step scope** | The confirmed brief verbatim (§A.3 output). |
| **Reading list** | 1–3 load-bearing rule files for the brief's path scope — pick from `.claude/rules/` based on what the change touches (e.g., hook change → `hook-engine.md` + `anti-patterns.md`; Lua surface → `lua-bridge.md` + `lua-callback-threading.md`; new offset → `address-library.md` + `reverse-engineering.md`; TOML → `toml-schema.md`). Other path-scoped rules auto-load. |
| **Authorized scope** | The file paths in the confirmed brief. |
| **Per-step test bar** | The `test-plugins/` plugin (existing sub-test or new `cap-NN`/`comp-NN`) per `.claude/rules/test-suite.md`. If the brief is vague on the test plugin, surface to user during §A.3. |
| **Resolved ambiguities** | None — execute has no resolved plan. Section reads "none" in the subagent brief. |
| **Touches-existing-code flag** | `true` (always — execute targets existing code by definition). Inline impact-analysis procedure per `_shared/orchestrator-loop.md` §A.5 is always injected. |

---

## C. Run one cycle

Run exactly one cycle of the shared orchestrator loop per `.claude/skills/_shared/orchestrator-loop.md` §A through §E. The shared loop handles brief construction (§A), dispatch (§B), verification (§C), step-review gate (§C.1), re-task / escalate (§D / §E), and architect-review routing (§E.1 / §E.2).

---

## D. Per-cycle report

After the cycle succeeds AND has been committed AND deploy hashes verified per `_shared/orchestrator-loop.md` §C.6, emit per `_shared/orchestrator-loop.md` §F.2.

Lead: `Execute brief landed and committed as <short-hash>.`

Tail: `Execute cycle complete. Stopping. Run the procedure above, then tell me it ran (e.g. "test run") — I'll read the matrix from kcdx-dev.log and report.`

The checkpoint dispatch is mechanical per §F.1 — the manager evaluates the diff against the threshold and either renders the verification-checkpoint output inline or emits the trivial-launch block. The user is never asked which.

Stop. The skill does not auto-chain into another cycle. If the user has more work, they invoke `/execute` again.

---

## Hard rules

Inherits all hard rules from `.claude/skills/_shared/orchestrator-loop.md`. Execute-specific additions:

- **One brief = one cycle = one commit.** No multi-concern auto-decomposition. Multi-concern briefs are split via user pick (§A.2), never silently.
- **No auto-chaining.** After the cycle commits, stop. User re-invokes for the next change.
- **Manager picks the reading list and test plugin; both are mechanical choices grounded in the brief's path scope + `test-suite.md`.** If either requires a design call, surface to user via §A.3.
- **Build-green does not close the cycle as "working."** The cycle commits compiling, reviewed code with a test plugin in place; the user confirms the in-game result at the checkpoint launch.

## What's out of scope

- A hard bug whose cause isn't obvious from the symptom — that's `/debug`.
- Trivial single-file edits the user already made by hand — that's `/commit` direct.
- Architectural questions before code — that's `/senior-architect-consult`.
- Deeper review of the subagent's output beyond build/test-plugin/step-review checks — invoke `/code-review` (manager can suggest this in its per-cycle report).
