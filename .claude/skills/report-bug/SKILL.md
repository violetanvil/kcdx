---
name: report-bug
description: Use this skill when the user wants to file a bug as a persistent, debuggable record WITHOUT investigating or fixing it now. Writes one new KI-NNNN doc under docs/known-issues/ (Symptom + Facts + Open questions + Status OPEN; Trail/Resolution left empty for /debug), appends a KI-index row to docs/known-issues/README.md, commits via /commit, stops. Pure capture — does NOT probe, root-cause, build, deploy, launch, fix, or close. For investigating a hard bug use /debug; for landing a fix use /execute.
---

# Report bug — file a debuggable record, no investigation

User found a defect and wants it filed for later. Capture Symptom + evidence, write one KI-NNNN doc, commit, stop. This is the inverse of `/debug` Phase 1: record what's known, then halt before Phase 2 (probes).

## Scope

**In:** writing `docs/known-issues/KI-NNNN-<slug>.md` (OPEN subset of the known-issue template) + appending one KI-index row to `docs/known-issues/README.md`. Reading the live `kcdx-dev_<ts>.log` read-only to pull evidence. `/commit` to land both.

**Out — refuse and route:**
- Probing, root-causing, or any "the bug is because X" claim beyond a labeled hypothesis → `/debug` later.
- Building, deploying, launching the game → not this skill (no probe is written).
- Fixing or closing the bug, writing the Resolution section, flipping status → `/debug` (Resolution) / `/execute` (fix).
- Editing any file outside `docs/known-issues/` → boundary violation, stop.

## Procedure

1. **Intake from conversation context.** Draft:
   - **One-line title** — `what's broken, where` (becomes `# KI-NNNN — <title>` and the `<slug>`).
   - **Symptom** — what the user observed, plain English, no causal claim. Exception code + faulting RIP if the user gave them.
   - **Runtime defect or design/spec defect?** Ask only if genuinely unclear.

2. **Allocate KI-NNNN.** Scan BOTH dirs and take the highest `KI-####`, increment by 1 (first = `KI-0001`):
   `Get-ChildItem docs/known-issues -Filter 'KI-*.md' -File; Get-ChildItem docs/known-issues/closed -Filter 'KI-*.md' -File`. The 14 pre-KI human-readable files carry no ID — ignore them for allocation.

3. **Gather evidence (read-only, runtime defect only).** Read the newest `<game-bin>/kcdx-engine/logs/kcdx-dev_<ts>.log` yourself; quote the lines that show the failure + any crash frames. Never ask the user to paste log lines (`agent-builds-and-deploys.md` §4). Design/spec defect → `Facts: n/a — design/spec defect`. No usable evidence anywhere → `Facts: None captured yet — symptom-only report.` Do NOT fabricate evidence; do NOT build/deploy/launch to manufacture it.

4. **Capture commit hash.** `git rev-parse HEAD` → `commit_at_filing` (pins the bug to the codebase state at filing time).

5. **Dedup.** Grep BOTH dirs for an existing file on the same symptom (`docs/known-issues/` + `docs/known-issues/closed/`). Found → surface it, ask whether to append context there instead of filing a duplicate. Not found → proceed.

6. **Write the doc** at `docs/known-issues/KI-NNNN-<slug>.md` using the OPEN subset of [`../debug/references/known-issue-template.md`](../debug/references/known-issue-template.md): frontmatter + `# KI-NNNN — <title>` + `**Status:** OPEN — not yet investigated.` + Symptom + Facts + Open questions. Leave Trail, Hard-rule, Active-instrumentation, Resolution as empty section headers for `/debug`. Causal claims go ONLY under Open questions, each labeled `(NOT verified)`.

7. **Append KI-index row** to `docs/known-issues/README.md`'s `## KI index` table (newest first; replace the `(no KI-NNNN bugs filed yet)` placeholder on the first file):
   `| [KI-NNNN](KI-NNNN-<slug>.md) | YYYY-MM-DD | <one-line title> |`

8. **Commit.** Invoke `/commit`. Docs-only under `docs/known-issues/**` is one cohesive chunk → auto-commits without an approval round-trip. Capture the short-hash.

9. **Stop.** Report: `Filed KI-NNNN at docs/known-issues/KI-NNNN-<slug>.md, committed <short-hash>. /report-bug does not investigate or fix — run /debug KI-NNNN to probe, root-cause, and close.`

## Hard rules

- **Pure capture — never investigate.** No probe, no root cause, no build/deploy/launch. Filing ends the cycle; `/debug` picks the file up from its empty Trail.
- **Edits limited to `docs/known-issues/`.** Any Edit/Write outside is a boundary breach — stop and surface.
- **Facts vs hypothesis split is mandatory.** Facts section = quoted observables only (or the explicit `None captured` / `n/a` line). Every causal claim sits under Open questions labeled `(NOT verified)`. Per CLAUDE.md AP17: the agent does not assert root cause.
- **Read the log yourself; never ask the user to paste lines** (`agent-builds-and-deploys.md`).
- **Allocate by scanning both dirs.** A closed `KI-0042` means the next new bug is `KI-0043`, not `KI-0043`-only-if-you-missed-0042.
- **Invoke only on explicit user request.** Subagents and in-line agents do NOT file bugs autonomously; a bug-shaped finding surfaces to the user, who decides to `/report-bug`.

## Anti-patterns

- Drafting a fix or a confident root cause in the doc. The empty Trail/Resolution is load-bearing — it signals `/debug` the investigation hasn't started.
- Fabricating evidence to fill the Facts section. Thin-but-honest (`None captured yet`) beats invented log lines.
- Building/deploying/launching "to get evidence." That's a `/debug` probe, not a report.
- Allocating KI-NNNN from the active dir alone (ignores closed IDs).
- Putting a causal claim in Facts. Facts are observables; hypotheses are labeled and live under Open questions.
