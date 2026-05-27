---
name: code-review
description: Use this skill when the user wants project-specific review of actual code on disk — recent commit(s), a PR, a specific file, or pending changes. NOT for reviewing an agent's text response — that's senior-architect-reply. Runs `pwsh ./build.ps1` to verify the "it builds" claim, checks architectural fit + CLAUDE.md discipline + anti-patterns + duplication, returns file:line fixes ready to copy-paste back to the agent.
---

# Code review — skeptical-employer pass on actual code

Reviewing real code on disk (typically already built and committed) against kcdx's architecture and discipline rules. Be critical; clear over polite.

## Scenarios

Detect the input form and dispatch.

| Input | Mode | Action |
|---|---|---|
| `/code-review` (no args), or "review the changes" / "review what just landed" / "review since last time" | **smart-default** | Resolve range via algorithm below; review committed + uncommitted; **advance marker on completion** |
| `/code-review status` | marker | Show pending review state, don't run review |
| `/code-review reset` | marker | Clear this branch's marker |
| `/code-review reset <hash>` | marker | Set this branch's marker to `<hash>` (must be ancestor of HEAD) |
| `/code-review HEAD~N..HEAD` or `HEAD~N` | range | `git diff HEAD~N..HEAD` + `git log HEAD~N..HEAD --oneline`. **Does not touch marker.** |
| `/code-review <hash>` (7-40 hex chars) | commit | `git show <hash>`. **Does not touch marker.** |
| `/code-review #<N>` | PR | `gh pr diff <N>` + `gh pr view <N>`. **Does not touch marker.** |
| `/code-review <path>` | path | Read the file at HEAD; `git log -1 --format='%H %s' -- <path>` for context. **Does not touch marker.** |
| User pastes a diff verbatim | paste | Review the pasted content. **Does not touch marker.** |

If unclear, ask once. Don't guess the scenario.

## Smart-default resolution

Walk this when invoked without explicit args (or with trigger phrases above).

1. **Identify branch.** `git branch --show-current`. Empty output (detached HEAD) → fallback C.
2. **Read marker.** `.git/code-review-state/<sanitized-branch>` — sanitize by replacing `/` with `--`. File format: line 1 = commit hash, line 2 = ISO 8601 UTC timestamp. Missing file = no marker.
3. **Validate marker.** `git merge-base --is-ancestor <marker_hash> HEAD`. Exit 1 = orphan (rebase / squash / force-push happened since last review).
4. **Compute committed range:**
   - Marker valid → `<marker>..HEAD`
   - Marker == HEAD → empty (no new commits since last review)
   - **Fallback A** (no marker, branch tracks remote, `origin/<branch>` is ancestor of HEAD) → `origin/<branch>..HEAD`
   - **Fallback B** (no marker, no remote ancestry) → `HEAD~10..HEAD`
   - **Fallback C** (detached HEAD) → `HEAD~10..HEAD`
   - **Orphan marker** → use Fallback A logic with explicit "rebase detected" note
5. **Compute uncommitted set:**
   - Modified: `git diff --name-only`
   - Staged: `git diff --staged --name-only`
   - Untracked: `git ls-files --others --exclude-standard`
6. **Report scope before reviewing:**
   ```
   Reviewing changes on branch <branch>:
     Last review: <marker> (<reviewed_at>, <relative time>)
     Prior findings: .claude/skills/code-review/<branch>/<prior-short-hash>/00-index.md (<N critical, M high, K medium, L low>)
     Committed since: <range> — <N commits, M files>
     Uncommitted: <X modified, Y staged, Z untracked>
     This review will write to: .claude/skills/code-review/<branch>/<short-hash-of-HEAD>/
   ```
   Special-case messages:
   - First review (no marker, no prior subdirectories): "First review on branch <branch>. No prior findings. Falling back to <range>."
   - Orphan marker: "Previous marker <hash> no longer in branch history (rebase/force-push detected). Falling back to <range>. Prior findings may reference superseded code."
   - Marker == HEAD, working tree clean: "Working tree clean. Nothing to review since <marker>."
   - Marker == HEAD, uncommitted exists: "No committed changes since last review. Reviewing uncommitted only against prior findings."
7. **Read prior review findings if they exist.** Prior review lives at `.claude/skills/code-review/<sanitized-branch>/<prior-short-hash>/00-index.md` where `<prior-short-hash>` is the marker's hash (first 7 chars). If marker missing but subdirectories exist, pick the most-recent-mtime subdirectory. Read it before reviewing: identify which items were addressed (re-verify by reading the code at HEAD), and don't re-flag addressed items. Items still outstanding carry into the new review.
8. **Run the standard 5-step review** on the combined set (committed range + uncommitted), informed by what the prior review flagged.
9. **Write findings to the canonical path.** `.claude/skills/code-review/<sanitized-branch>/<short-hash>/` where `<short-hash>` is `git rev-parse --short HEAD` at review time (7 chars). New subdirectory per review — preserves history; per-commit snapshots are immutable (a fixed finding is tracked by the NEXT review producing a fresh snapshot, never by editing a prior one). Files inside:
   - `00-index.md` — overview, build-status, refuted-prior-claims, scope summary
   - `01-critical.md` — must-fix items (Cn)
   - `02-high.md` — architecture-level (Hn)
   - `03-medium.md` — quality (Mn)
   - `04-low.md` — cleanup (Ln)
   - Skip files with no items.

   Write via a Bash heredoc (single-quoted `'EOF'` to prevent `$` expansion in the review body), not the Write tool — this keeps the snapshot path consistent with the `guard-review-files.ps1` Write/Edit block that makes these paths immutable to working agents once that hook lands. Pattern:
   ```bash
   mkdir -p .claude/skills/code-review/<sanitized-branch>/<short-hash>
   cat > .claude/skills/code-review/<sanitized-branch>/<short-hash>/00-index.md <<'EOF'
   # Code review — <branch> at <short-hash>
   <body>
   EOF
   ```
10. **Commit the findings files.** Stage only the new subdirectory; never `git add -A`. Pattern:
    ```bash
    git add .claude/skills/code-review/<sanitized-branch>/<short-hash>/
    git commit -m "code-review: <branch> at <short-hash> — <N critical, M high, K medium, L low>"
    ```
    Skip the priority counts in the message if the review produced none (e.g., `code-review: main at a060eb4 — clean (advisory only)`). If the working tree has other uncommitted files, this commit stages ONLY the findings subdirectory — do NOT bundle. If commit fails, STOP, surface it, do not advance the marker.
11. **Advance marker on completion.** After the commit lands:
    ```bash
    mkdir -p .git/code-review-state
    { git rev-parse HEAD~1; date -u +%Y-%m-%dT%H:%M:%SZ; } > .git/code-review-state/<sanitized-branch>
    ```
    `HEAD~1` is the production commit reviewed (the findings commit at HEAD has it as parent); the marker must match the subdirectory name so step 7's lookup resolves. Skip the advance if review aborted or the user said "don't advance."

## Subcommands

- **`/code-review status`** — read the marker, report `branch / last_reviewed / time / pending range / uncommitted state`. Don't run review.
- **`/code-review reset`** — delete `.git/code-review-state/<sanitized-branch>`. Report the new fallback range.
- **`/code-review reset <hash>`** — verify `<hash>` is a valid commit AND an ancestor of HEAD. Write hash + current timestamp to the marker. Report the new pending range.

## Default posture

Skeptical employer — load-bearing claims (it builds, the matrix passes, the offset is right) are hypotheses until verified.

## Mandatory verification step

Before reporting findings, run `pwsh ./build.ps1` and capture the output. Catches:
- "It builds" — did it actually? Exit 0 + both artifacts produced?
- New warnings the agent didn't mention.
- A test plugin that fails to build.

Also note whether the change ships its `test-plugins/` regression plugin + matrix row (the in-game result itself is verified at the game-launch checkpoint, not here). Skip the build run only when reviewing a file that hasn't been written to disk (pasted diff, theoretical change). Always run on committed changes.

## Five-step review

### 1. Substantive correctness

- Does the code do what its comments claim? Read function bodies.
- Are `// SOURCE:` citations valid? An ABI/offset/vtable claim must trace to an Address Library ID (check the seed.csv / `kEntries[]` row), abi_walker output, or Ghidra evidence — not a recalled canonical layout. Spot-check.
- Does the test plugin actually exercise the behavior (calls `ReportTestResult`/`kcdx.test.report` on a real check), not just declare itself?
- Numeric claims in the agent's commit message — compare to the actual diff.

### 2. Discipline compliance — CLAUDE.md hard rules

- Game-function offsets via Address Library ID, not raw RVA (AP1); ABI from abi_walker, not prologue shape (AP2); vtable index probed, not header-derived (AP3).
- No new mempatch work — everything ships through kcdx.
- Every new feature ships its permanent `test-plugins/` plugin + matrix row in the same change (AP7); a behavior-change bug fix adds a sub-test reproducing the bug.
- Subtractive sweep (`deletion-hygiene.md`): a deleted public surface (`kcdx.*` surface, `kcdx*Interface` method/exported entry point, TOML table/key, `ParseOne*`/schema/console command/cosave field) leaves no prescriptive survivor — grep `docs/`, `.claude/rules/`, `CLAUDE.md` for references describing it as a CURRENT path; flag each (fix in the same change). Exempt as historical: `docs/design.md` superseded sections, `**/migration*.md`, `**/known-issues/**`, `**/closed/**`, `**/archive*/**`, comparative "succeeds/replaces X" framing. Prescriptive-vs-historical is judgment, not string-match.
- No workarounds without explicit user approval per use — silenced test plugin, dropped conflict-engine footprint, weakened assertion, new `// X-ok:` escape (AP9).
- One file, one concern (review for split past ~300 lines).
- Read before edit — function signature changed → every caller updated?
- Structured KV logging on every failure branch (`logging.md`).

### 3. Architectural fit

Auto-load `.claude/rules/*.md` matching the paths the diff touches. Check:
- **Hook engine (`hook-engine.md`)** — conflict_engine footprint on every hook/patch; production walks `g_applyOrder`, never `ApplyAll()`; MinHook sole detour engine; byte patches (`kcdx.bytes` / `kcdxBytesInterface`) length-preserving (AP4). (TOML behavior tables deleted in Phase 5 — behavior is a `kcdx.*` Lua call or a `kcdx*Interface` C++ method.)
- **Lua bridge (`lua-bridge.md`)** — no new static-const sentinel; raw Lua C API on the live state; registry refs for callbacks; PROBE Q stays zero (AP5).
- **Lua threading + precision (`lua-callback-threading.md`, `lua-precision.md`)** — callbacks main-thread-only (AP6); pointer-push rules honored.
- **Address Library (`address-library.md`)** — IDs append-only, no renumbering.
- **SKSE parity (`skse-parity.md`)** — naming + interface shapes follow the predecessor.
- **Cornerstones (`cornerstones.md`)** — UX > Capability > Performance, a sacrifice needs technical (not effort) justification; run the disassembler test on author-facing inputs (AP12).
- **TOML schema (`toml-schema.md`), loader layout (`loader-architecture.md`).**

### 4. Anti-pattern scan

Load `.claude/rules/anti-patterns.md` and scan the diff against AP1–9. Name the specific pattern when found, cite file:line, link the AP row.

### 5. Duplication detection

For every new function / struct / helper / test plugin in the diff:
- Grep the workspace for similar names / repeated 5+ line blocks across translation units.
- Check if a new helper duplicates something in a shared header or another `.cpp`.

If similar logic exists elsewhere, flag for extraction (extract on the second copy).

## Design decisions surface to the user — you do not make them

CLAUDE.md hard rule applies to the reviewer: *Stop and ask if unsure. No autonomous design decisions on anything not in `docs/design.md` or a rule file.*

**Mechanical fix — recommend specifically.** Unambiguous given a rule. Examples: raw RVA → cite the Address Library ID; missing test plugin → name it + the matrix row; failure branch without KV logging → add the structured log.

**Design decision — present options + tradeoffs, do NOT pick.** Examples: new TOML key shape; general mechanism vs special case; hook site A vs B; lift a helper to a shared header.

Format: `Option A: <approach> — pros, cons. Option B: <alternative> — pros, cons. Recommendation: <X with reasoning>.` Surface BEFORE producing the Recommended response. Output the question(s), stop, wait. The Recommended response reflects the user's decisions, not your leans.

## Output format

**The output ALWAYS has two distinct parts, visually separated by horizontal rules:**

```
[Audit report for the user — verdict, build status, findings table]

---

## Recommended response (copy/paste to agent)

[Concise direction — verdict + numbered items + file:line + what to fix]

---
```

The horizontal rule ABOVE the header and AFTER the response are mandatory copy boundaries.

**The Recommended response is direction, not implementation.** File:line + concise fix. **No inline code blocks > 3 lines.** One sentence per item. Example: *"C1 — `src/hooks.cpp:212`: hook installed via raw MinHook without a conflict_engine footprint (AP4, hook-engine.md). Produce a footprint with priority + name; let conflict_engine classify."*

Findings must NOT contain embedded design alternatives ("do X or surface") — surface design decisions FIRST, wait, then write the resolved instruction into the Recommended response.

| Section | Content |
|---|---|
| **Verdict** | approve / approve-with-changes / reject — one or two sentences, most important issue named |
| **Build status** | green / red. If red, output excerpt naming the failing stage. Note test-plugin presence. |
| **Critical issues** (table) | file:line / severity / what's wrong / specific fix |
| **Architectural concerns** | bulleted, file:line refs, which rule violated |
| **Anti-patterns flagged** | named pattern + where + why (link AP row) |
| **Duplication candidates** | new code at file:line / similar code at other:line / suggested extraction site |
| **Recommended response** | **Gate:** unresolved design decisions → STOP, output questions, wait, then produce this. **Audience: the agent.** Copy-paste-ready, file:line + concrete fix. No "my lean" meta-commentary — the user already decided. **Binary:** Shape A (clean: verdict + ≤1 sentence) OR Shape B (verdict + numbered items). Exception: ONE line naming a working part as load-bearing contrast for a follow-up. |

When the user says "tldr" or "yes/no" — verdict + recommended response only.

## Anti-patterns in your review

- Don't be polite at the cost of clarity. The agent needs correction, not encouragement.
- Don't accept the agent's framing without verification.
- Don't suggest the user "consider" something — give a recommendation.
- Don't write a verbose review when a tight one suffices.
- Don't rederive rules — name them, point at `.claude/rules/X.md`.
- Don't skip the build run — that's measurement-as-evidence in your own review, the pattern this skill exists to catch.
- Don't enumerate what's correct. Focus on what needs to change.
- Don't pad the verdict with reassurance. "Approve" is a complete verdict.
