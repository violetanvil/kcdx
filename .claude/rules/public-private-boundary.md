---
paths:
  - "src/**"
  - "include/**"
  - "vendor/**"
  - "data/**"
  - "examples/**"
  - "kcdx-engine/**"
  - "test-plugins/**"
  - "tools/**"
  - "docs/**"
  - "README.md"
---

# Public/private boundary — a public-facing file references NOTHING private

This repo is the comprehensive private repo. `publish-public.ps1` projects an
**allowlist** of public dirs + root files to the public remote (see
`concurrency-git.md` §Remotes). The public repo deliberately shows no trace of
AI-assisted development.

**Hard rule.** A file that ships to public (under an allowlisted public dir, or
an allowlisted root file) may reference NOTHING that stays private — in prose,
in a comment, in a doc link, in a code identifier, anywhere. A private reference
in a public file is both a **broken link on the public repo** (the target isn't
there) and a **trace of how the repo is built** (the thing it most must hide).

This applies to BOTH directions of authoring: writing a public file, and moving
content into one. It is the author's job to state the fact directly, without the
private citation.

## What is "public-facing"

A path published by `publish-public.ps1`'s allowlist:

- Dirs: `src/`, `include/`, `vendor/`, `data/`, `examples/`, `kcdx-engine/`,
  `test-plugins/`, `tools/`, `docs/` (all of `docs/`, including
  `docs/outstanding-work/` and `docs/known-issues/`).
- Root files: `README.md`, `LICENSE`, `CMakeLists.txt`, `build.ps1`,
  `package-release.ps1`.

Anything else is private (`.claude/`, `CLAUDE.md`, `_research/`,
`third-party-ghidra/`, `test-fixtures/`, `.gitignore`, `publish-public.ps1`).
Private files may reference anything — the rule constrains only public files.

## What is a "private reference" (forbidden in public files)

1. **A literal private path** — `.claude/...`, `CLAUDE.md`, `_research/...`,
   `third-party-ghidra/...`, `test-fixtures/...`, `publish-public.ps1`.
2. **AI-development vocabulary** — `Claude`, `Anthropic`, `subagent`,
   `orchestrator`, a bare `AP<n>` used as a rule citation (e.g. "AP12"), or a
   governance skill slash-command (`/execute`, `/feature`, `/code-review`, …).

## How to fix a finding — state the fact, drop the citation

The fix is never "delete the sentence." It is "say what the sentence taught,
without pointing at the private source."

- ❌ "The name resolves address AND signature (`.claude/rules/cornerstones.md`,
  the disassembler test / AP12)."
- ✅ "The name resolves the address AND the verified signature — the engine
  carries both, so the author never hand-writes an ABI."

- ❌ "Supersedes the legacy line; see `.claude/rules/hook-engine.md`."
- ✅ "Supersedes the legacy first-wins behaviour: conflicts resolve by load
  order."

The private rule/doc still holds the *why* for an internal reader; the public
file restates the *what* in its own words. A design decision does not need its
governance provenance to be understood by a mod author.

## Enforcement

- **Author-time:** `guard-public-private-refs.ps1` (PreToolUse on Write/Edit)
  WARNS when a public-facing file gains a private reference. Warn-only — it does
  not block, but a flagged write is a finding to fix before publish.
- **Review-time:** `code-review` / `step-review` treat a surviving private
  reference in a public file as a finding (same footing as a broken doc link).
- The boundary list here MUST stay in sync with `publish-public.ps1`'s allowlist
  and `concurrency-git.md` §Remotes. If the allowlist changes, update all three.

Related: `concurrency-git.md` (the remote topology + allowlist), `docs-discipline.md`
(doc-entry obligations), `deletion-hygiene.md` (survivor sweeps on deletion).
