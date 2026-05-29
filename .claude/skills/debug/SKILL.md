---
name: debug
description: Investigate and document a hard bug in kcdx — typically a crash, hang, memory corruption, or wrong-output regression where the cause isn't obvious from the symptom. Use whenever the user reports a reproducible failure that takes more than one obvious fix attempt: "X is crashing", "save-load broke after Y", "the test suite started failing", "this used to work". Also use proactively after the second failed quick-fix attempt for the same issue. Heavy emphasis on observation-before-hypothesis, single-variable probes, structured logging, and writing every step into a per-issue file under `docs/known-issues/`. Do NOT use this for trivial one-line bug fixes whose cause is visible from the stack trace.
---

# Debug a hard bug in kcdx

For bugs where the cause isn't obvious from the stack trace. Easy bugs (typo, off-by-one with a clear test) — just fix them.

Three phases, in order.

---

## Phase 1: Observe (before writing any code or designing any probe)

### 1a. Get ground truth

A symptom report is not ground truth. Get one of these before forming a hypothesis:

- **Crash dump:** `"C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe" -z <dump> -c ".ecxr; !analyze -v; k 30; q"`. Read `EXCEPTION_CODE_STR` + `FAILURE_BUCKET_ID`.
- **Log tail:** last 30 lines of `kcdx-engine/logs/kcdx_<ts>.log` and `kcdx-dev_<ts>.log` from the failing session.
- **Repro rate:** ask "1/1, 1/3, 1/10?" if not stated.

If user gives you "it crashed", ask for the crash zip path or log tail. Don't proceed on intuition alone.

### 1b. Map what changed

`git log --oneline -20` and `git diff <last-known-good>..HEAD --stat`. Candidate set of suspects starts here, not from intuition about which subsystem is involved.

### 1c. Check `MEMORY.md` for similar past investigations

Cite past memories as **context**, not **conclusions**. Re-verify before invoking — bugs that look similar to past ones often aren't.

### 1d. Open the per-issue file

`kcdx/docs/known-issues/<title>.md`. Title is human-readable. Template at [`references/known-issue-template.md`](references/known-issue-template.md). Fill Symptom, Facts (start small), Trail, Open questions.

If the bug already has a file, append to its Trail rather than starting a new one.

---

## Phase 2: Investigate

### 2.0. You write, build, and deploy the probe. The user only launches.

You author the probe code (`// === DIAGNOSTIC (PROBE X)` site or `test-plugins/` probe plugin). You run `pwsh ./build.ps1`, copy the rebuilt artifact to the live install per `loader-architecture.md` deploy mapping, hash-verify the copy, and enable dev mode. The user runs ONE thing in the loop: the game launch. After the run, you read `kcdx-dev.log` directly.

Asking "should I write the probe?", "should I build it?", or "should I deploy it?" is a FLOW defect — none of those are decisions (rule: `agent-builds-and-deploys.md`; probe-specific procedure: `results-driven.md` §"Live-game unknowns"). Surface a decision ONLY when there are two valid probe DESIGNS the user owns the call on (which site, which value to log, which subsystem to bypass).

### 2a. One variable per probe — and the probe must be theory-INDEPENDENT

Each probe changes **exactly one thing**. A probe is build + game launch + observation, ~2 minutes. If you change three things, you've learned nothing.

Before designing each probe, run the `results-driven.md` self-check: *Is there an outcome that proves my current theory WRONG?* If no, redesign to **observe the raw ground-truth fact directly** (dump the actual bytes / arg / register / stack-index value) rather than testing a *theory about* it. Observe ground truth FIRST; on disconfirmation, re-observe rather than hop to the next theory (AP10). Full discipline: `results-driven.md` §"The probe must be theory-INDEPENDENT".

### 2b. Multi-change iterations decompose into sub-rows

A launch that changes N independent things produces N Trail rows sharing a launch letter: `PROBE K.1`, `K.2`, `K.3`. Each row records one variable and its observable contribution. If you can't decompose, the probe wasn't designed cleanly — stop, split on paper before writing any row.

Only escape: two changes genuinely inseparable (e.g. "build historical commit + backport probe E" — neither tests anything alone). Collapse to one row whose Action names both with `+`. Rare.

### 2c. Trail row format — strict

- **Action cell:** `PROBE <letter>[.N]: <≤15 words describing the one variable changed>`. No hypothesis. No prediction.
- **Result cell:** `<outcome verdict>. <≤1 clause interpretation>.` Two sentences max. 200-char hard cap. Interpretation clause names which hypothesis the result eliminated or supported — nothing else.

Everything else has a named destination:

| Content | Destination |
|---|---|
| Hypothesis the probe is testing | **Open questions** section, before launch as `**<hypothesis>** — Probe: <experiment>`. Move/delete after probe runs. |
| Predictions (`P1 → X`) | **Open questions**, attached to same hypothesis bullet |
| Methodology details (fields snapshotted, helper added, file modified) | **Active diagnostic instrumentation** table, one row per file touched |
| New empirical observation | **Facts** section, bullet ending with `(PROBE <letter>[.N])` |
| Reframe (changes what the bug is) | New `## Reframe <YYYY-MM-DD>: <title>` section, OR edit Symptom/Facts directly |
| Side-by-side comparisons, multi-paragraph reasoning | New `## Analysis: <topic>` section. Trail row links with `(see Analysis: <topic>)` |

**One-pass authoring.** Write Action **before** launch. Write Result **only after** probe runs. Never speculative. Never edit Result after the next probe runs — if a later probe reframes interpretation, add a new Fact citing both probes, do not rewrite the earlier row.

**Audit at probe close.** After updating the Trail row but before designing the next probe: *"Does this row contain anything other than (a) the one variable changed and (b) outcome verdict + one-clause interpretation?"* If yes, cut and move per the table above.

### 2d. Probe naming and code labelling

- Letter-order: PROBE A, B, C, … New launch → new letter. Multi-change launch → sub-number per 2b.
- Code comment at every modified site: `// === DIAGNOSTIC (PROBE X[.N]): <one-line hypothesis + outcome interpretation>`
- Stable category tag in log lines (`MID_HOOK`, `SCRIPTING`, etc.)
- Trail rows added **before launching** with `pending` Result, updated **immediately after** with strict-format result.

Use the workspace logging API: `LOG_DEBUG_KV("CATEGORY", "action", KV("key", value), ...)`. KV format greps reliably; freeform printf doesn't.

### 2e. Read-only probes before mutating probes

Always cheaper to learn from a probe that can't change behavior. When you can answer the question with a read, read.

### 2f. The "still need this?" audit between probes — disable + archive, never revert

- Did the probe answer its question? Update the Trail row.
- Is the probe's code change still needed for the next probe? If not, **disable it in place** (`#if 0` block with the archive header per §3d) before the next launch. **Do NOT revert / delete the probe.** Stale LIVE diagnostics in place compound confusion (the no-two-live-probes rule, `guard-probe-stack.ps1`); ARCHIVED diagnostics are inert at compile time and don't.
- Are any probes worth keeping as `durable` instrumentation (left enabled past bug close)? Pure-read fingerprinting code often is. Bypasses are NEVER durable — they go to `archived` status when their question is answered (revival would mean re-enabling the bypass deliberately, which the `#if 0` makes explicit).

The archive shape is in §3d. The cost-paid investigative wiring (hook target, fingerprint method, captured fields, log category) is the cheapest jumping-off point for the next investigation into the same subsystem — throwing it away is waste (CLAUDE.md hard rule: probes are disabled + archived, never reverted or deleted).

### 2g. Historical-commit probes use worktrees

`git worktree add -d <tmpdir> <commit-sha>`. Do NOT `git checkout` or `git stash pop` in the live tree mid-investigation — `git stash push -u` silently captures unseen doc edits; popping later may surprise the user. After the probe finishes, `git worktree remove` and stay on main with live diagnostic state.

### 2h. When to stop probing and reframe

- Three probes in a row eliminated suspects without narrowing → probing the wrong axis. Step back, re-read the original crash dump.
- cdb output suggests the fault is in a module you have no source for → instrument around the call site, not the callee.
- A probe contradicts an earlier conclusion → both can't be true. Figure out which probe was wrong and rerun.
- **Hopped theories 2+ times on the same symptom** (theory A killed → B → …) → frame lost. Dispatch a **fresh-frame subagent** per `results-driven.md` §"Fresh-frame escalation": hand it the raw facts + killed-theories list, WITHHOLD your leading theory, ask it to design the most direct ground-truth observation.

### 2.5. Gate A — every design fork auto-routes through architect-review (subagent dispatch, never raw to user)

The `/debug` orchestrator is YOU; the discipline is the same as `/execute` §E.1. A design decision surfaced during `/debug` is NEVER surfaced raw to the user — dispatch `architect-review` as a subagent FIRST, read the structured review yourself, then compose the user-facing message from architect §3 if forwarding. The user decides; you do NOT decide; the architect verifies before either of you sees the fork.

**Gate A fires when the debug agent is about to propose a fix that EITHER:**
- Adds a new file under `src/` or `include/`, OR
- Modifies more than one `src/` file, OR
- Changes a function signature (grep every caller first per CLAUDE.md), OR
- Touches a `.claude/rules/` file or `docs/design.md`, OR
- Adds or changes a hook surface, ABI signature, vtable slot, Address Library entry, save/cosave field, or `[[...]]` schema, OR
- The Trail-row interpretation cites "we should do X instead of Y" as a fork (a real design call).

Trivial fixes (typo in a log message, comment fix, single-file ≤10 line change with no signature change and no rule touch) do NOT trigger Gate A — they go straight to §3a–§3d.

**Dispatch via the `Agent` tool, NOT via `Skill`.** `subagent_type: general-purpose`. Prompt template:

```
You are the architect-review skill. Read cover-to-cover before producing output:

  1. .claude/skills/architect-review/SKILL.md
  2. .claude/skills/_shared/architectural-review.md
  3. docs/design.md sections cited below + rule files matching the proposed fix's path scope

Escalation context (from /debug orchestrator):

  Bug under investigation:
  <docs/known-issues/<title>.md path + Symptom section verbatim>

  Trail summary (probe outcomes only, no chain-of-thought):
  <verbatim Trail rows that survived the audit>

  Proposed fix (the design call the debug agent wants to make):
  <one-paragraph plain English: what change, where, why the agent thinks it addresses the bug>

  Proposed fix diff (if any code already written):
  <git diff, or "no diff — proposal-stage">

  docs/design.md sections cited by the proposal:
  <list, or "none cited">

  Rule files matching the proposed fix's path scope:
  <list>

Produce the four-section structured review per architect-review/SKILL.md's
output format. Your output returns to me — the /debug orchestrator — as
a tool result. The user will NOT see it directly. §3 (Design decisions
surfaced) is the only forwardable section; write it in plain-English so
the user reads it cold when I forward.
```

**Read the tool result yourself. Apply architect §4:**

- **`re-task-subagent`** (debug case = re-design the probe or revise the proposal) — Mechanical fix per architect's direction; do NOT surface to user. Re-frame the design proposal per the architect's §4 direction; if a new probe is needed, run §2a–§2g. Next user-facing message is the §F-style debug report (success path) or a different escalation if one arises.
- **`forward-and-wait`** — Compose a user-facing escalation in the §E.2 shape from `_shared/orchestrator-loop.md` (lead from architect §1; findings line from architect §2; decision block from architect §3 with Recommendation + Why). NEVER include §4. NEVER paste architect's raw output. Wait for user decision; do not re-task or land the fix until user responds.
- **`hold`** — Compose user-facing escalation: lead + findings + one-line *"architect declined to surface options; awaiting your direction."* Omit the decision block. Wait.

**Hard rule — never paste architect's raw output to the user.** Raw output = your input; the user reads what YOU compose. If the user explicitly asks for the architect's reasoning, paste §1+§2+§3 (never §4) verbatim — only on explicit request.

### 2i. cdb one-liners

```
# Quick triage
"C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe" -z <dump> -c ".lines -e; !analyze -v; q"

# Stack walk of crashing thread
"C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe" -z <dump> -c ".ecxr; k 30; q"

# All threads
"C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe" -z <dump> -c "~* k 25; q"

# Loaded modules
"C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe" -z <dump> -c "lm; q"
```

Watchdog crash zips: `<game>/Bin/.../kcdx-engine/logs/crash/crash_<ts>.zip`. BugSplat dumps: `%LOCALAPPDATA%/CrashDumps/` (SEH-trappable) or NTFS alternate data streams in `%TEMP%` (see `docs/known-issues/BugSplat dmp files don't reach disk for AV crashes.md`).

**Don't enable Application Verifier page heap without strong reason.** Page Heap's perturbation can produce different unrelated crashes; treat as emergency-only.

---

## Phase 3: Fix and verify

### 3a. Smallest fix the evidence supports — AND the evidence MUST include root cause

The evidence supporting the fix must include the **root cause**, not just a repro that passes. A passing repro that comes from masking the bug is visually identical to a passing repro that comes from a real fix; the matrix passes either way. The Resolution section's `Root cause:` paragraph answers "why did this happen?" in mechanism terms — what value was wrong, who wrote it, in what order, why the original code path made that wrong write inevitable. *"X no longer crashes"* / *"now boots to menu"* / *"AV is gone"* are restatements of the symptom going away, NOT root cause (AP17 — non-negotiable per CLAUDE.md hard rule).

Cannot write the mechanism paragraph → you do NOT know the cause yet; another probe is owed (`results-driven.md`). Do not land the fix. The single legitimate escape is an explicit user-approved "Provisional mask, root cause unknown" Resolution label, with the issue staying OPEN.

If you find yourself rewriting a whole subsystem to "fix" one corrupting call, stop — you're probably masking the bug. Sometimes the fix is "don't do the thing" — that's still a real fix IF the mechanism paragraph names why "doing the thing" was always going to corrupt. Document the design trade-off in the known-issue's "Decision" section.

### 3b. Verify on the same repro

Same plugin set, same save-load steps, same engine config that produced the original crash. Pass = previously-failing path now succeeds.

### 3c. Verify against the full test suite

Run all `kcdx-engine/builtin/` + `plugins/` enabled at least once. Each fix must be checked against unrelated tests, not just the one it targets.

### 3d. Update the known-issue file + archive every closed probe in place

**Gate B — root-cause-verifier dispatch before the Resolution lands.** The Resolution paragraph IS the AP17 artifact; the debug agent and the Trail share a context that produced the mechanism theory. An unbiased verifier must read the known-issue file + the planned fix diff + the planned archive-header drafts BEFORE the Resolution is final. This mirrors `/execute` §C.1's step-review gate.

**Gate B fires when ANY of the following holds for the planned fix:**
- The fix diff touches `src/` or `include/` (any C++ change), OR
- The Trail has 3+ probes (PROBE A, B, C, …), OR
- The fix touches a hook surface, ABI signature, vtable slot, Address Library entry, save/cosave field, or `[[...]]` schema (the `/execute` §F.1 threshold).

A doc-only / matrix-row-only / typo-fix close skips Gate B and proceeds straight to the checklist below.

**Dispatch via the `Agent` tool, NOT via `Skill`.** `subagent_type: general-purpose`. Prompt template:

```
You are the root-cause-verifier skill. Read cover-to-cover before producing output:

  1. .claude/skills/root-cause-verifier/SKILL.md
  2. .claude/skills/_shared/architectural-review.md
  3. .claude/rules/results-driven.md
  4. .claude/rules/anti-patterns.md (AP10, AP17)
  5. docs/design.md sections cited below + rule files matching the fix diff's path scope

Verification context (from /debug orchestrator):

  Known-issue file (full path; read in full):
  <docs/known-issues/<title>.md path>

  Planned fix diff:
  <git diff HEAD of the in-flight working tree>

  Planned archive-header drafts (one per probe being closed):
  <each `// === ARCHIVED PROBE <X>` header verbatim as the debug agent intends to write>

  docs/design.md sections cited by the proposal:
  <list, or "none cited">

  Rule files matching the fix diff's path scope:
  <list>

WITHHELD (per fresh-frame discipline):
  - My (the debug agent's) chain of reasoning.
  - Any "I'm confident the root cause is X" preamble.
  - Prior verifier runs on this same bug.

Produce the four-section structured review per root-cause-verifier/SKILL.md's
output format. Your output returns to me — the /debug orchestrator — as
a tool result. The user will NOT see it directly. Findings drive my next
action per your §4.
```

**Read the tool result yourself. Apply verifier §4:**

- **`land-fix`** — Resolution complete; land the fix verbatim, then proceed to the checklist below.
- **`probe-required`** — Resolution does NOT land. Run the verifier's named probe per §2a–§2g. After the probe answers, redraft the Resolution paragraph from the new evidence and re-dispatch Gate B (each verifier run is independent — the next verifier reads the new Trail, not the prior verdict).
- **`rewrite-resolution`** — Resolution paragraph claims more than the evidence supports. Rewrite the Resolution paragraph using the verifier's §4 verbatim text (it grounds the rewrite in actual Trail rows). Re-dispatch Gate B with the rewritten paragraph.
- **`escalate-design`** — A design fork is hiding behind the fix. Go back to §2.5 Gate A; dispatch architect-review FIRST; resume Gate B only after the architect's verdict + user direction.

**Hard rule — never paste verifier's raw output to the user.** Raw output = your input; the user reads what YOU compose from the §F-style debug report or escalation. If the user explicitly asks for the verifier's reasoning, paste §1+§2+§3 (never §4) verbatim — only on explicit request.

**The verifier verdict is gated, not advisory.** A `probe-required` / `rewrite-resolution` / `escalate-design` verdict halts the close — the manager does NOT land the fix on a partial verifier verdict. "The verifier mostly agrees" is `Root cause incomplete` (per the verifier's own §1 verdict shape); treat as halt.

**Gate A ↔ Gate B ping-pong guard.** If verifier returns `escalate-design` AND the subsequent architect-review returns `re-task-subagent` AND the re-tasked proposal returns to Gate B with the SAME design fork ⇒ the two gates disagree on the routing. Surface the disagreement to the user as an escalation (use the `_shared/orchestrator-loop.md` §E.2 shape; lead with "Gate A and Gate B disagree on routing — surfacing for direction"). Do NOT loop further. The user picks: a probe, a Resolution rewrite, or a design fork to land. Each gate's verdict pasted under "What's blocking" (verbatim §1 + §4 from each, only).

After Gate B returns `land-fix`, proceed:

- Add final probe results to the Trail.
- Add "Resolution": Root cause (mechanism paragraph — gated per §3a; cannot land without it), Fix (commit + what changed + why it addresses the mechanism), Verification, Diagnostic archive, Doc updates.
- Move "Open questions" → "Closed questions with answers" or strike through.
- If a `CLAUDE.md` hard rule or `.claude/rules/*.md` was wrong, **update it in the same commit**.
- **Archive every closed probe in place.** For each `// === DIAGNOSTIC (PROBE X)` site, decide:
  - **Durable** (left enabled past bug close): pure-read instrumentation that costs nothing when not called. Keep wired in; record in the known-issue's "Active diagnostic instrumentation" table with Status: `durable`.
  - **Archived** (compile-disabled, kept in tree): everything else. Wrap the probe block in `#if 0` with the four-line header:

    ```cpp
    // === ARCHIVED PROBE <X> (<YYYY-MM-DD>): <outcome verdict from the Trail row>.
    // === Root cause: <one-line mechanism from Resolution>.
    // === See: docs/known-issues/<title>.md §Resolution.
    // === Revive by flipping #if 0 -> #if 1.
    #if 0
        // (the original probe code — unchanged)
        LOG_DEBUG_KV("CATEGORY", "probe_x.event", ...);
    #endif
    ```

    Record in the known-issue's "Active diagnostic instrumentation" table with Status: `archived` + one-line root cause. Reverting (deleting the diagnostic site) is forbidden — the wiring is the cheapest jumping-off point for the next investigation into the same subsystem (CLAUDE.md hard rule).
- **Migration to `_research/probe-archive/<probe-id>-<short>/<original-file>.cpp`** is reserved for source files exceeding 2 archived probes (one-file-one-concern accumulation guard). Default = in-place `#if 0`. Surface the migration decision to the user; do not auto-migrate.

### 3e. Update memory if the lesson is durable

A CryEngine quirk, Windows behavior, or mod-author footgun — write a new memory or update existing. Don't write memories for "fixed the typo on line 42".

### 3f. Commit at each milestone (in-investigation cadence)

`/debug` is iterative — commit cadence follows the milestone rule per CLAUDE.md "Commit at coherent milestones." Apply per turn:

| Turn outcome | Commit? |
|---|---|
| Probe fired + evidence captured + Trail row written | YES — invoke `/commit` on the probe code + known-issue file |
| Probe didn't fire / returned nothing useful, no doc written | NO — leave the probe in the tree as live state for the next iteration |
| Probe didn't fire BUT you wrote the Trail row explaining why + what to try next | YES — the *understanding* is the durable artifact |
| Verifier returned `land-fix`, fix lands + Resolution + archive | YES — the per-§3d in-same-commit rule (Resolution + rule edits + probe archive headers together) |
| Verifier returned `probe-required` / `rewrite-resolution` | NO — investigation continues; the working tree carries the in-flight state |
| Verifier returned `escalate-design` | NO until architect + user resolve the fork |

The investigation is a sequence of milestone commits, not one terminal commit. Do not batch — a written-up Trail row is its own commit; the eventual fix is a separate commit landing per §3d.

---

## Communication

- **Before each launch:** one line stating what the probe tests and what each outcome means.
- **After each crash report:** read the new log, summarize what advanced/eliminated, update the Trail, propose the next probe. Don't ask "shall I proceed" — a real fork (a design decision the user owns) is auto-routed through Gate A (§2.5), not surfaced raw. A probe-design choice is a fact you read the code to answer (`_shared/architectural-review.md` §"Independence from caller framing"), not a question for the user.
- **Update the known-issue file every probe**, not in batches.
- **Strategy pivots** (e.g., within-build bisection → across-commit) stated explicitly.

## Anti-patterns

- Quoting an existing hard rule as a diagnosis. Rules describe past observations under specific conditions. Verify before invoking.
- Designing a probe by re-reading the same code three times. Add an observation probe — even one `LOG_DEBUG_KV` tells you more than another readthrough.
- Stashing without telling the user. `git stash push -u` is silent and bundles unsaved edits. Commit first, or tell them.
- **Reverting / deleting a probe.** Probes are disabled + archived in place (`#if 0` block per §3d), NEVER reverted (CLAUDE.md hard rule). The investigative wiring stays. Auditing each diagnostic-only change happens — but the verdict is `durable` (keep enabled), `archived` (`#if 0`), or — never — `revert`.
- **Closing a bug without a root-cause mechanism paragraph.** AP17 — non-negotiable. Cannot write the mechanism → another probe is owed. "X no longer crashes" is not root cause.
- Vague predictions ("might be the log system or the watchdog"). Pick the cheapest candidate, write the probe.
- Multiple variables per probe. Decompose per 2b.

## References

- [`references/known-issue-template.md`](references/known-issue-template.md) — template for each `docs/known-issues/*.md` file.
- [`references/probe-patterns.md`](references/probe-patterns.md) — common probe shapes (read-only, bypass, fingerprint, time-bisect, ABI-check) with example code.
- `docs/known-issues/kcdx lua_newtable corrupts the process heap.md` — worked example of a 17-probe investigation.
- `docs/logging.md` — structured logging API (`LOG_DEBUG_KV`, `KV()`, category tags).
