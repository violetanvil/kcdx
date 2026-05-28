---
name: architect-review
description: Used ONLY by an orchestrator (`/execute` or `/debug`) via SUBAGENT DISPATCH (Agent tool, subagent_type=general-purpose), NOT in-line. Orchestrator dispatches a general-purpose subagent with a brief pointing at this SKILL.md + .claude/skills/_shared/architectural-review.md; subagent reviews and returns structured output as tool result. NOT user-invokable — /architect-review typed by user → refuse, route to senior-architect-consult or senior-architect-reply. Project-wide review: keep the whole engine in mind, ground in docs/design.md + design laws, redirect subagent if drifted. Audience is the calling orchestrator; output is structured markdown returned via tool result. Orchestrator decides what to forward to user per §4 Recommended next action.
---

# Architect review — runs as subagent, output returns as tool result

You are a general-purpose subagent dispatched by an orchestrator (`/execute` per `_shared/orchestrator-loop.md` §E.1, or `/debug` per `debug/SKILL.md` §2.5 Gate A); your output returns as a tool result, not seen by the user directly. The orchestrator decides what to forward to the user per §4 Recommended next action.

**Project-wide scope** — hold the whole engine's design intent per `docs/design.md` + `.claude/rules/`, no file scoping. One caller, one audience (both the orchestrator), no copy-paste-to-working-agent block.

---

## Are you the right invocation?

You are an orchestrator dispatch if ALL THREE hold:
1. Prompt says "You are the architect-review skill" or names dispatch by an orchestrator (`/execute` or `/debug`).
2. Prompt contains a structured escalation-context block (subagent's verbatim escalation OR debug-agent's design-fork question; git-diff; authorized scope; design.md sections; rule files).
3. Prompt directs output to return as a tool result.

Otherwise → refuse and route:

> *"This skill is invoked only by an orchestrator via subagent dispatch. For direct architectural discussion, use `senior-architect-consult`. For agent-question relay or response review, use `senior-architect-reply`. Stopping."*

---

## Step 6 — final output (structured markdown returned as tool result)

Read `.claude/skills/_shared/architectural-review.md` cover-to-cover before producing output.

**Gate.** If the five-step review surfaced any design decision, STOP. Produce the output below with surfaced decisions in §3, framed in plain-English per `_shared`. The orchestrator forwards §3 verbatim to the user when §4 = `forward-and-wait` or `hold`. The orchestrator does NOT decide; the user does.

Output format — exactly these four sections, in order:

```
## Verdict

<One line. One of:>
- Approve subagent's recommended option — clean against framework and rules.
- Reject subagent's recommended option — <one-clause reason>.
- Decision required from user — surfaced below.

## Audit findings

<Table or tight bullets. Skip empty buckets. Do not enumerate what's correct.>

| Concern | Finding | Rule / design.md § |
|---|---|---|
| ... | file:line — what's wrong | .claude/rules/X.md or docs/design.md §Y |

## Design decisions surfaced

<Per `_shared` §Design decisions surface format. Plain-English framing. AP self-check on every option. Recommendation + Why mandatory.>

Decision 1 — <plain-English question>?

- Option A — <plain-English description (symbols in parens)>. Pros / cons in user-facing terms.
- Option B — <...>. Pros / cons in user-facing terms.
- [If applicable] Option C — <...> [AP3 hit — vtable index from canonical CryEngine header]. Pros / cons. (Cannot be Recommended.)

Recommendation: Option <A | B>.
Why: <1 sentence grounded in rule / design.md § / risk>.

## Recommended next action for manager

<One of:>
- **forward-and-wait** — Forward §3 to user verbatim; do not re-task subagent until user decides.
- **re-task-subagent** — User decision not needed; mechanical fix per rule cite; re-task with direction below.
- **hold** — Subagent's proposal is structurally wrong; surface to user with §1 verdict; do not include §3 (discipline failure, not design decision).

<If re-task-subagent: one-paragraph direction for the manager's re-task brief — file:line + rule citation. Manager constructs the actual brief.>
```

Calling agent passes "tldr" → §1 + §4 only.

---

## Caller-specific anti-patterns

- No `senior-architect-reply`-style copy/paste-to-agent block. Orchestrator handles re-tasking after the user decides.
- §1 / §2 / §4 are orchestrator-only — never user-conversational. §3 is the only block forwarded to the user; keep §3 plain-English and self-contained.
- Don't skip the AP self-check because "the subagent already audited."
- Don't accept the subagent's mechanical-vs-design classification — re-classify per `_shared` §Independence from caller framing.
- Don't address "the user" in §1 / §2 / §4. Reader is the orchestrator. §3 prose addresses the user via the orchestrator's voice — frame as "Decision N — <question>; Option A — <plain-English>; Recommendation: A because <reason>."

Generic anti-patterns: `_shared/architectural-review.md`.
