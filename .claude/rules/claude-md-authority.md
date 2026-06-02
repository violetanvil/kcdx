---
paths:
  - "**/CLAUDE.md"
---

# CLAUDE.md authority — the cold-start preamble, governance-architect's alone to edit

`CLAUDE.md` is the instruction preamble injected in full into every session in its directory tree, before any work, unconditionally. Its cost is paid every session; its job is to make an agent act correctly from cold start.

## What CLAUDE.md is — and is not

It carries exactly three things: **repo identity** (what this is, what's tracked), the **hard rules** an agent must obey from cold start, and a **routing map** (where to look for everything else). It is a table of contents, not the book.

It is NOT a changelog, a status field, a build narrative, or a per-item rationale log. Anything needed only when working on a specific thing lives where that thing is, reached by a pointer — never preloaded into every unrelated session.

- **What's live** → the `skills/` + `rules/` trees and `hooks/INDEX.md` (each self-describes); a pointer, not a copy.
- **Build history** → `git log`.
- **Decisions + rationale** → the decisions-log (`doc-organization.md`).
- **Step/plan completion** → its ledger in its own tree (`plan-persistence.md`), never narrated here.

A well-formed CLAUDE.md changes rarely between sessions. Per-cycle growth is the tell that non-preamble content has leaked in.

## Only governance-architect edits CLAUDE.md

A change to any `CLAUDE.md` is a governance edit. **Only `governance-architect` makes it.** Any other skill or agent that would write `CLAUDE.md` STOPS and routes to `governance-architect` — including recording a new artifact: its record is its own dir + INDEX, never a CLAUDE.md entry. Add a routing pointer to the map only for a genuinely new category, and only through `governance-architect`.

## What this is NOT

- NOT the authoring-economy law (`imperative-only.md`) or file-granularity (`no-monolith.md`) — cited for why the preamble stays lean and single-concern; this rule owns CLAUDE.md's purpose + write-authority.
- NOT lifecycle-ledger structure (`doc-organization.md`, `plan-persistence.md`) — cited as the homes status/completion content routes to; this rule owns keeping it OUT of CLAUDE.md.
- NOT a block on reading CLAUDE.md — every agent reads it; only writing is governance-architect's.
