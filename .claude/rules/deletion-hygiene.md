---
paths:
  - "src/**"
  - "include/**"
  - "docs/**"
  - ".claude/rules/**"
  - "CLAUDE.md"
---

# Deletion hygiene — removing a surface sweeps its survivors in the same change

Deleting a public surface (an API, config key, command, observable behavior, documented contract) removes or repoints every prescriptive reference to it in the SAME change. The subtractive mirror of `docs-discipline.md` (a new capability's doc entry lands with the code); this enforces the SUBTRACTIVE direction — a removed surface leaves no doc, rule, or `CLAUDE.md` text still describing it as the current way.

A capability is **not deleted** until every prescriptive reference to it moved with the deletion, in the SAME commit — same footing as `docs-discipline.md`'s additive rule.

## What counts as a deletion requiring a survivor sweep

A diff that REMOVES any of:

- A `kcdx.*` Lua surface or registration.
- A C++ interface / interface method (`kcdx*Interface`), or an exported entry point (`extern "C"`, `kcdxPlugin_Load`).
- A TOML table or key (a `[[...]]` table, a `[section]`, a manifest key).
- A public parser / schema (`ParseOne*`, an Address Library seed row's semantics, a console command, a save/cosave field).
- Any engine behaviour an author or user could previously observe.

## The survivor sweep — the obligation

**Sweep before the deletion lands.** On detecting such a deletion, grep `docs/`, `.claude/rules/`, and `CLAUDE.md` for surviving references to the removed token + its aliases — across docs, rules, guidance, comments, examples, not just code. The scope is **prescriptive references the build does not catch** — a reference doc, how-to, `CLAUDE.md`/README passage, glossary term, rule/convention, comment, doc-comment, or example that names the removed surface. (Callers are a build concern — a caller of a deleted function fails to compile; the compiler owns it, not this rule.) Each survivor is PRESCRIPTIVE or HISTORICAL:

- **PRESCRIPTIVE** — describes the removed thing as a current authoring path, live schema, or thing-you-can-do-now. A survivor in a non-exempt location is presumed prescriptive. A dangling prescriptive reference is the defect; "clean up docs later" is the failure mode (a stale doc actively sends the reader down a removed path). **Finding** — fix it in the same commit.
  - **Replaced surface** → the reference moves to the replacement (a migration note where readers depend on the old name, repo's call) — rewrite to past/comparative framing.
  - **Removed outright** → the reference is deleted, not repointed.
- **HISTORICAL** — references the removed thing as past or comparative, not as the current way. **Not a finding.**

## Exempt locations — historical by construction, never flag

A survivor in one of these is historical by its file's job; do NOT flag:

- `docs/design.md` sections under a SUPERSEDED / superseded banner.
- `**/migration*.md` — documenting legacy→new IS its job.
- `**/known-issues/**`, `**/closed/**`, `**/archive*/**` — diagnostic trails and retired records.
- Comparative teaching framing — "succeeds / replaces / supersedes the v0.1 X", "the legacy X did Y; the current path is Z".

A NEW comparative aside in a non-exempt file (a fresh "like the old `[[patch]]`" in `docs/lua/hook.md`) is in scope — confirm it reads as past-tense, not as a live instruction. That confirmation is the reviewer's judgment, not a string match.

## How to apply

- `step-review` / `code-review` run the survivor sweep when the diff deletes a surface (each skill's §2). A surviving prescriptive reference is a finding.
- The warn-only deletion hook (`guard-anti-patterns.ps1`) fires a proactive sweep reminder at author-time; the review gates carry the actual check.
- No annotation escape — a survivor is not silenced by a marker; it is fixed or it is genuinely historical by location/framing.

## What this is NOT

- NOT the additive mirror (`docs-discipline.md`) — this is its subtractive counterpart; cited as the direction this one inverts.
- NOT lifecycle-doc movement (`doc-organization.md`) — a deleted surface is not a `<TYPE>-NNNN` closure.
- NOT the destructive-ops safety guard (`concurrency-git.md`) — deleting a file safely is the hook layer; this owns the stale prescriptive survivors a safe deletion leaves.
- NOT a ban on deletion — the bar is that removal be complete: surface + every prescription of it go together.

Related: `docs-discipline.md` (the additive direction), `toml-schema.md` (manifest keys), `address-library.md` (seed rows are append-only — deletion applies to a row's documented semantics, not its ID).
