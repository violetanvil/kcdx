---
paths:
  - "src/**"
  - "include/**"
  - "docs/**"
  - ".claude/rules/**"
  - "CLAUDE.md"
---

# Deletion hygiene — a removed surface leaves no prescriptive doc behind

The subtractive counterpart of `docs-discipline.md`. That rule enforces the
ADDITIVE direction (new capability → its doc entry lands in the same commit).
This one enforces the SUBTRACTIVE direction: deleting a public surface →
no surviving doc, rule, or CLAUDE.md text still describes it as the current way.

A capability is **not deleted** until every prescriptive reference to it moved
with the deletion, in the SAME commit — same footing as `docs-discipline.md`'s
additive rule.

## What counts as a deletion requiring a survivor sweep

A diff that REMOVES any of:

- A `kcdx.*` Lua surface or registration.
- A C++ interface / interface method (`kcdx*Interface`), or an exported entry
  point (`extern "C"`, `kcdxPlugin_Load`).
- A TOML table or key (a `[[...]]` table, a `[section]`, a manifest key).
- A public parser / schema (`ParseOne*`, an Address Library `kEntries[]` row's
  semantics, a console command, a save/cosave field).
- Any engine behaviour an author or user could previously observe.

## The survivor sweep — the obligation

On detecting such a deletion, grep `docs/`, `.claude/rules/`, and `CLAUDE.md`
for surviving references to the removed token. Each survivor is PRESCRIPTIVE
or HISTORICAL:

- **PRESCRIPTIVE** — describes the removed thing as a current authoring path,
  live schema, or thing-you-can-do-now. A survivor in a non-exempt location is
  presumed prescriptive. **Finding** — fix it in the same commit (delete the
  entry, or rewrite it to past/comparative framing).
- **HISTORICAL** — references the removed thing as past or comparative, not as
  the current way. **Not a finding.**

## Exempt locations — historical by construction, never flag

A survivor in one of these is historical by its file's job; do NOT flag:

- `docs/design.md` sections under a SUPERSEDED / superseded banner.
- `**/migration*.md` — documenting legacy→new IS its job.
- `**/known-issues/**`, `**/closed/**`, `**/archive*/**` — diagnostic trails
  and retired records.
- Comparative teaching framing — "succeeds / replaces / supersedes the v0.1
  X", "the legacy X did Y; the current path is Z".

A NEW comparative aside in a non-exempt file (a fresh "like the old `[[patch]]`"
in `docs/lua/hook.md`) is in scope — confirm it reads as past-tense, not as a
live instruction. That confirmation is the reviewer's judgment, not a string
match.

## How to apply

- `step-review` / `code-review` run the survivor sweep when the diff deletes a
  surface (each skill's §2). A surviving prescriptive reference is a finding.
- The warn-only deletion hook (`guard-anti-patterns.ps1`) fires a proactive
  sweep reminder at author-time; the review gates carry the actual check.
- No annotation escape — a survivor is not silenced by a marker; it is fixed or
  it is genuinely historical by location/framing.

Related: `docs-discipline.md` (the additive direction), `toml-schema.md`
(manifest keys), `address-library.md` (`kEntries[]` is append-only — deletion
applies to a row's documented semantics, not its ID).
