---
paths:
  - ".claude/skills/**"
  - ".claude/rules/**"
  - ".claude/hooks/**"
  - ".claude/**/*.append.md"
---

# Imperative-only + lean — agent-facing bodies are tight instructions, reasons go separate

A governance authoring law for every agent-facing body — a `SKILL.md`, a rule body, a shared fragment, a hook header, a repo append. The body carries instructions, tightly; the rationale lives elsewhere.

## The law

Agent-facing governance text = **instructions only, imperative, no narrative.** The "why" → a separate per-artifact `reasons.md`, NOT linked from the body, organized per `.claude/rules/no-monolith.md`.

- Keep a sentence that prescribes behavior; cut one that explains why, names an incident, or soft-justifies ("better X than Y", "worse than none because").
- Cut "why this exists" sections and "we chose X because Y" — keep X, drop the because.
- Keep canonical template examples (✅/❌ pairs, output-format blocks); max 2–3 per concept.
- Frontmatter `description:` is direct too — no rationale / incident history.
- Don't restate other rules — name / link them. Measure trims by phrase rewrites, not `wc -l`.

## Never name your own injected append

A skill/fragment body MUST NOT name, describe, or point the agent at its own `.claude/repo/<name>.append.md` — not by path, not as "its append" / "the append names X" / "stated in its append" / "from the append". That file is injected ahead of the body at render time by the skill's first-line script; its contents are ALREADY present above when the repo provides it, and its absence is a silent no-op. Naming it makes the agent hunt a file it should never know about — redundant when present, a fruitless multi-turn search when absent.

- State a repo's specifics as **already-present-or-default**: "the repo's <X> (present above if the repo provides it; else the generic default below stands)" — never "the repo's `.claude/repo/<name>.append.md` names <X>".
- The first-line injection script (`!`f=.claude/repo/<name>.append.md; [ -f "$f" ] && cat "$f"; :``) is the ONLY place the path may appear; the prose body never repeats it.
- A meta-skill whose SUBJECT is appends (it generates / audits / authors `.claude/repo/*.append.md` for a repo) may name them as its operand — the ban is on a body pointing at ITS OWN append for its own config, not on discussing the append system.

## Authoring economy — bodies are lean

Every token in an agent-facing body is **re-read on every invocation** — its cost is paid repeatedly, and bloat dilutes the instruction. So write the tightest body that still prescribes the behavior:

- Prefer the shortest phrasing that is unambiguous; no padding, no preamble, no restating a point already made.
- Cite a shared rule/fragment instead of repeating its content — one canonical source, named not duplicated.
- Cut a second example that teaches nothing the first didn't.
- This is authoring discipline (write the artifact lean). The RUNTIME discipline — scoping subagent reading lists, returning digested deliverables, summaries over transcripts during execution — lives in `_shared/orchestrator-loop.md` §A/§B and the preference `~/.claude/memory/context-token-economy.md`; it is not this rule's concern.

## What this is NOT

- NOT a ban on `reasons.md` — the rationale is REQUIRED, just placed in the separate per-artifact reasons doc, not in the agent-facing body.
- NOT applicable to human-facing docs (design docs, the decisions-log, `reasons.md` files themselves) — those are where the narrative belongs.
- NOT file-granularity (`.claude/rules/no-monolith.md`) — this governs the CONTENT of an agent-facing body (instruction vs rationale), not how files are split.
