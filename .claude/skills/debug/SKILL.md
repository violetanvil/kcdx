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

### 2f. The "still need this?" audit between probes

- Did the probe answer its question? Update the Trail row.
- Is the probe's code change still needed for the next probe? If not, **revert it** before the next launch. Stale diagnostics in place compound confusion.
- Are any probes worth keeping as durable instrumentation? Fingerprinting code often is; outright bypasses are not.

### 2g. Historical-commit probes use worktrees

`git worktree add -d <tmpdir> <commit-sha>`. Do NOT `git checkout` or `git stash pop` in the live tree mid-investigation — `git stash push -u` silently captures unseen doc edits; popping later may surprise the user. After the probe finishes, `git worktree remove` and stay on main with live diagnostic state.

### 2h. When to stop probing and reframe

- Three probes in a row eliminated suspects without narrowing → probing the wrong axis. Step back, re-read the original crash dump.
- cdb output suggests the fault is in a module you have no source for → instrument around the call site, not the callee.
- A probe contradicts an earlier conclusion → both can't be true. Figure out which probe was wrong and rerun.
- **Hopped theories 2+ times on the same symptom** (theory A killed → B → …) → frame lost. Dispatch a **fresh-frame subagent** per `results-driven.md` §"Fresh-frame escalation": hand it the raw facts + killed-theories list, WITHHOLD your leading theory, ask it to design the most direct ground-truth observation.

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

### 3a. Smallest fix the evidence supports

If you find yourself rewriting a whole subsystem to "fix" one corrupting call, stop — you're probably masking the bug. Sometimes the fix is "don't do the thing" — that's still a real fix; document the design trade-off in the known-issue's "Decision" section.

### 3b. Verify on the same repro

Same plugin set, same save-load steps, same engine config that produced the original crash. Pass = previously-failing path now succeeds.

### 3c. Verify against the full test suite

Run all `kcdx-engine/builtin/` + `plugins/` enabled at least once. Each fix must be checked against unrelated tests, not just the one it targets.

### 3d. Update the known-issue file

- Add final probe results to the Trail.
- Add "Resolution": which probe pinpointed cause, what the fix changed, what commit landed it.
- Move "Open questions" → "Closed questions with answers" or strike through.
- If a `CLAUDE.md` hard rule or `.claude/rules/*.md` was wrong, **update it in the same commit**.
- Decide which diagnostic code becomes durable and remove the rest; record in "Active diagnostic instrumentation".

### 3e. Update memory if the lesson is durable

A CryEngine quirk, Windows behavior, or mod-author footgun — write a new memory or update existing. Don't write memories for "fixed the typo on line 42".

---

## Communication

- **Before each launch:** one line stating what the probe tests and what each outcome means.
- **After each crash report:** read the new log, summarize what advanced/eliminated, update the Trail, propose the next probe. Don't ask "shall I proceed" unless there's a real fork.
- **Update the known-issue file every probe**, not in batches.
- **Strategy pivots** (e.g., within-build bisection → across-commit) stated explicitly.

## Anti-patterns

- Quoting an existing hard rule as a diagnosis. Rules describe past observations under specific conditions. Verify before invoking.
- Designing a probe by re-reading the same code three times. Add an observation probe — even one `LOG_DEBUG_KV` tells you more than another readthrough.
- Stashing without telling the user. `git stash push -u` is silent and bundles unsaved edits. Commit first, or tell them.
- Reverting "obvious" code without an audit pass. List every diagnostic-only change; for each, decide keep/revert with a one-line reason. Tell the user.
- Vague predictions ("might be the log system or the watchdog"). Pick the cheapest candidate, write the probe.
- Multiple variables per probe. Decompose per 2b.

## References

- [`references/known-issue-template.md`](references/known-issue-template.md) — template for each `docs/known-issues/*.md` file.
- [`references/probe-patterns.md`](references/probe-patterns.md) — common probe shapes (read-only, bypass, fingerprint, time-bisect, ABI-check) with example code.
- `docs/known-issues/kcdx lua_newtable corrupts the process heap.md` — worked example of a 17-probe investigation.
- `docs/logging.md` — structured logging API (`LOG_DEBUG_KV`, `KV()`, category tags).
