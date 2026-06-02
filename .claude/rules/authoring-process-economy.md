---
paths:
  - ".claude/skills/**"
  - ".claude/rules/**"
  - ".claude/hooks/**"
  - ".claude/**/*.append.md"
---

# Authoring-process economy — the ACT of authoring governance stays lean

Authoring or auditing a governance artifact (a `SKILL.md`, a rule body, a shared fragment, a hook, a repo append) is itself a context-bounded act. The artifact ships lean (`.claude/rules/imperative-only.md` §"Authoring economy"); this rule governs the OTHER half — the reading and reasoning that PRODUCE it must not burn massive context. A 30-line rule authored after reading 4,000 unscoped lines is the failure this names. The disposition is `~/.claude/memory/context-token-economy.md`; this rule is the checkable floor for the authoring path that disposition could not bind.

## Scope reads to load-bearing — never whole-file by default

Before reading, name what THIS change needs and read only that:

- Reach for `Grep`/`Glob`/offset-`Read` first; a whole-file `Read` is justified only for a file short enough that the whole IS the load-bearing part, or one being rewritten end-to-end. A multi-hundred-line file read in full to extract one section is the defect.
- Cite the target as `path §section` or a line range when the need is a section, and read that span — not the file around it.
- Do not re-read a file already read this session, nor one just written/edited (the harness tracks its state). A second read to "double-check" is a checkable fact, not a re-read.

## Delegate broad fan-out — keep the reasoning window lean

When a question spans many files or naming conventions and only the CONCLUSION is needed (a roster sweep, "where is X referenced", "does any rule already cover Y"), dispatch a read-only subagent (`Explore` / a measurement subagent per `_shared/verification-contract.md` §1 Type B) that returns a digest — the matching paths + the answer, not the file contents. The broad reading stays out of the authoring window; the digest enters it. Delegation is the default for breadth, direct reads for the few files the edit actually touches.

## Extract, don't dump — bounded tool output

A search or list returns the matching lines or keys, never a whole-tree dump or a whole-log paste into the window. Pipe a long listing through a count or a scoped filter; extract the specific rows from a large output. The standard a runtime step clears (`_shared/orchestrator-loop.md` §A.3/§B — scoped reading lists, digested deliverables) is the same standard the authoring step clears; that fragment owns the manager-dispatch path, this rule owns the direct-authoring path the architect runs without an orchestrator.

## The audit is reads-scoped from the start

An audit that precedes a proposal scopes its reads the same way — the relevant rule/skill bodies and the specific sections a gap could hide in, named before reading, not the whole tree swept into context on the chance something is relevant. Breadth that genuinely must be covered is delegated (above), not pulled into the authoring window. "Read everything first" is not diligence here — it is the bloat this rule exists to stop.

## What this is NOT

- NOT the lean-ARTIFACT law (`.claude/rules/imperative-only.md` §"Authoring economy") — that owns the body that ships; this owns the reading/reasoning that produces it. The two halves of authoring economy: a lean act yielding a lean artifact.
- NOT the runtime/orchestration economy (`_shared/orchestrator-loop.md` §A.3/§B) — that owns the manager scoping a subagent's reading list and demanding a digest during a build-gated cycle; this owns the same discipline on the direct-authoring path a governance skill runs WITHOUT wrapping that loop. Cited at the boundary, not restated.
- NOT the disposition memory (`~/.claude/memory/context-token-economy.md`) — that is the always-on preference; this is the checkable authoring-path floor it could not enforce.
- NOT a ban on reading what the change needs — read every load-bearing line. The bar is that reads be SCOPED and broad fan-out DELEGATED, not that less be understood.
