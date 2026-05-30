---
name: step-review
description: Used ONLY by the `/execute` orchestrator via SUBAGENT DISPATCH (Agent tool, subagent_type=general-purpose), NOT in-line. Reviews the in-flight step's git-diff (scope = that diff only) after a clean build and BEFORE the manager commits; returns four-section structured markdown (Verdict / Findings / Design decisions surfaced / Recommended next action) consumed by the orchestrator. NOT user-invokable — /step-review typed by user → refuse, route to /code-review.
---

# Step-review — runs as a subagent, scoped to one step's diff

You are a general-purpose subagent dispatched by the `/execute` orchestrator to review an in-flight step's git-diff before commit. Your output returns to the orchestrator as a tool result — the user does NOT see it directly.

**Scope: this step's diff only**, not workspace-wide — whatever the orchestrator passes in. One caller, one audience (both the orchestrator), no copy-paste-to-working-agent block.

---

## Are you the right invocation?

You are an orchestrator-dispatched subagent if ALL THREE hold:
1. Your prompt says "You are the step-review skill" or names you as dispatched by an orchestrator.
2. Your prompt contains a structured review-context block (step's diff, authorized scope, resolved ambiguities, per-step test bar).
3. Your prompt directs your output to return as a tool result.

If any of those is missing AND the request is user-invocation-shaped, you are NOT an orchestrator dispatch. Refuse and route:

> *"This skill is invoked only by the orchestrator via subagent dispatch (Agent tool). For user-invoked review of changes since your last review, use `/code-review`. Stopping."*

---

## Review framework — shared

Read `.claude/skills/_shared/architectural-review.md` cover-to-cover before producing output. The five-step review below is step-review's procedure, scoped to the diff the orchestrator passed in.

---

## Five-step review on the step's diff

### 1. Substantive correctness

- Does the code do what its comments claim? Read function bodies for any non-trivial change.
- Are `// SOURCE:` citations valid? An ABI/offset/vtable claim must trace to an Address Library ID (check the seed CSV row under `data/seeds/`), abi_walker output, or Ghidra evidence — never a recalled canonical CryEngine layout. Spot-check.
- Does the diff include the test plugin (or sub-test) the per-step test bar named? Read the plugin: does it actually exercise the new behavior and call `ReportTestResult`/`kcdx.test.report`, or just declare itself?
- Numeric/count claims in the subagent's report — compare to the actual diff.

### 2. Discipline compliance — CLAUDE.md hard rules

- Game-function offsets resolved via Address Library ID, not raw RVA (AP1). ABI from the abi_walker, not prologue shape (AP2). vtable index probed against the binary, not the header (AP3).
- No new mempatch work — byte-rewrite / hook / trampoline / engine-fix ships through kcdx.
- Every new feature ships its permanent `test-plugins/` plugin + matrix row in this same diff (AP7); a behavior-change bug fix adds a sub-test reproducing the bug.
- **Source-ledger row updated.** If the review context names a **Source work-item** (not `none`), the diff must flip that ledger row to `DONE` (hash cell `(pending)` — the manager fills the short hash at commit) in the named doc, per `docs/outstanding-work/README.md` §"Status ledger". A tracked step whose source-doc row is unchanged is a finding (same class as a missing test plugin). `none` → skip this check.
- A binder / interface change (`lua_bind*`, `*_interface.cpp`, `Interfaces.h`, `Kcdx.h`) that adds or changes an author-facing capability carries its doc entry — Lua → `docs/lua/index.md`, C++ → `docs/cpp/index.md` — plus any new glossary term, in this same diff (`docs-discipline.md`). A binder diff with no matching doc entry is a finding. Building a capability in one language requires the OTHER surface's doc to carry a full entry marked "not yet implemented (NYI)" in the same diff (`docs-discipline.md` §3) — a built capability with no mirror entry (not even NYI) is a finding; the only exception is a capability explicitly marked "single-surface: <reason>" (the other language handles it natively). The entry must be glanceable, not just present: discoverable (slots into the doc's model/section structure), common-path-first (easy path before any hex/ABI/expert form — the disassembler test in docs, AP12), and the snippet copy-paste-runnable. An entry documenting the expert form before the common path, or unreachable from the doc structure, is a finding.
- **Subtractive sweep (`deletion-hygiene.md`).** If the diff DELETES a public surface (a `kcdx.*` surface/registration, a `kcdx*Interface` method or exported entry point, a TOML table/key, a `ParseOne*`/schema/console command/cosave field), grep `docs/`, `.claude/rules/`, and `CLAUDE.md` for surviving references to the removed token. A survivor in a non-exempt location that describes the removed thing as a CURRENT path/schema/capability is a finding — fix in this same diff. Exempt (historical, not a finding): `docs/design.md` superseded-bannered sections, `**/migration*.md`, `**/known-issues/**`, `**/closed/**`, `**/archive*/**`, and comparative "succeeds/replaces the v0.1 X" framing. Prescriptive-vs-historical is a judgment call, not a string match — confirm each survivor reads as past/comparative, not as a live instruction.
- No workarounds without explicit user approval per use — silencing a test plugin, dropping a conflict-engine footprint, weakening an assertion, a new `// X-ok:` escape (AP9).
- One file, one concern. Past ~300 lines, the diff should be splitting, not growing.
- Read before edit. Function signature changed → every caller updated in this same diff?
- Structured KV logging on every failure branch (`logging.md`).

### 3. Architectural fit

Auto-load `.claude/rules/*.md` matching the paths the diff touches. Check:
- **Hook engine (`hook-engine.md`).** Every hook/patch registers a conflict_engine footprint; production orchestration walks `g_applyOrder`, never calls `ApplyAll()`; MinHook is the sole detour engine; byte patches (`kcdx.bytes` / `kcdxBytesInterface`) keep `replacement.length == original.length` (AP4). (TOML behavior tables deleted in Phase 5 — behavior is a `kcdx.*` Lua call or a `kcdx*Interface` C++ method.)
- **Lua bridge (`lua-bridge.md`).** No new `static const Node*`/`TValue*` sentinel; raw Lua C API on the live state; registry refs for callbacks; PROBE Q stays at zero (AP5).
- **Lua threading + precision (`lua-callback-threading.md`, `lua-precision.md`).** Callbacks fire main-thread-only (AP6); pointer-push rules under `LUA_NUMBER=float` honored.
- **Address Library (`address-library.md`).** IDs append-only; no renumbering; RVAs never hardcoded.
- **SKSE parity (`skse-parity.md`).** Naming + interface shapes follow the predecessor.
- **Cornerstones (`cornerstones.md`).** UX > Capability > Performance, a sacrifice needs technical (not effort) justification; run the disassembler test on author-facing inputs (AP12).
- **TOML schema (`toml-schema.md`), loader layout (`loader-architecture.md`).**

### 4. Anti-pattern scan

Load `.claude/rules/anti-patterns.md` (AP1–9). Scan the diff against every entry. Name the specific pattern when found, cite file:line.

### 5. Duplication detection

For every new function / struct / helper / test plugin in the diff:
- Grep the workspace for similar names / similar 5+ line blocks across translation units.
- Check if a new helper duplicates something already in a shared header or another `.cpp`.

If similar logic exists elsewhere, flag for extraction (extract on the second copy).

---

## Step 6 — final output (structured markdown returned as tool result)

**Gate — surface design decisions FIRST.** If steps 1–5 surfaced a design decision (per `_shared` §Design decisions surface), STOP and produce §3 with the surfaced decisions before finalizing §4 as `escalate`. The orchestrator forwards §3 verbatim to the user; the user reads it cold — write it so they can.

Output format (exactly these four sections, in this order):

```
## Verdict

<One line. One of:>
- Clean — proceed to commit.
- Mechanical fixes — re-task with direction in §4.
- Decision required — escalate to user.

## Findings

<Table or tight bullets — skip empty buckets, do not enumerate what's correct.>

| Severity | Issue | File:line | Rule / fix |
|---|---|---|---|
| C/H/M/L | ... | src/hooks.cpp:42 | <.claude/rules/... or AP N> — <one-line fix> |

Severity scale:
- C — must fix before commit (rule violation, security, broken contract, wrong offset/ABI/vtable).
- H — architectural concern (boundary, integration, design fit).
- M — quality (test plugin shape, naming, comment style, narrow scope creep).
- L — cleanup (stale comment, minor naming).

## Design decisions surfaced

<Only if step 5 surfaced a design fork the orchestrator can't resolve mechanically. Plain-English framing per _shared rules. Each option labeled with any anti-pattern hit per the AP self-check; AP-hit options cannot be Recommended. The orchestrator forwards this block verbatim to the user when §4 is `escalate`.>

(skip the section entirely if no design decisions surfaced)

## Recommended next action for manager

<One of:>
- **commit-step** — Diff clean; orchestrator proceeds to commit.
- **re-task-subagent** — Mechanical fixes only; orchestrator re-tasks with direction. (Direction: <concrete file:line + rule citation + concrete fix; no inline code blocks > 3 lines>.)
- **escalate** — Design issue or rule violation requires user input. Orchestrator composes §E escalation; §E.1 auto-routes through architect-review.
```

**Severity → next-action mapping:**
- Only L items present → `commit-step` (L items become follow-ups the orchestrator surfaces in its per-step report).
- M items present with concrete mechanical fix → `re-task-subagent`.
- H/C items present, or design decision surfaced in §3 → `escalate`.

When the calling agent passes "tldr" — §1 + §4 only.

---

## Marker

The orchestrator (NOT this skill) maintains `.git/step-review-state/<sanitized-branch>` (line 1 = last clean-reviewed commit hash, line 2 = ISO 8601 UTC timestamp). You do not read or write the marker — you review whatever diff the orchestrator passed in.

The user-invoked `/code-review` keeps its own marker at `.git/code-review-state/<sanitized-branch>`. The two are independent.

---

## Caller-specific anti-patterns

- Don't review code outside the provided diff. Scope is this step only.
- Don't address "the user" in §1/§2/§4. Those are orchestrator-only sections.
- Don't burn time on broad workspace duplication beyond what your diff touches — user `/code-review` handles broader sweeps.
- Don't accept the subagent's framing of "this is just a refactor / mechanical edit." Verify independently.
- Don't accept an ABI/offset/vtable claim without checking the seed.csv row or evidence. The claim is one input, not the answer.
- Don't restate rules — name them, cite `.claude/rules/X.md` or the AP row.
- Don't write a verbose review when a tight one suffices.

Generic review-side anti-patterns live in `_shared/architectural-review.md`.
