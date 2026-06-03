# Step 5 — docs `kcdx.cvar.*` + glossary "CVar"

## What

Ship the reference documentation for the CVar-read surface (a delivery requirement,
`docs-discipline.md` — the author learns the surface from the docs). All in the same
change:

1. **`docs/lua/cvar.md`** — the per-call reference for `kcdx.cvar.get_int` /
   `.get_bool` / `.get_float`: call shape, the `name` argument (a CVar string the
   author sources from the wiki / `~` console), return value, error behaviour
   (missing CVar / surface not ready), a copy-paste-runnable minimal snippet.
   Common-path-first (there is no expert/hex form here — the surface is name-only).
2. **`docs/lua/index.md`** — add the three calls to the index map (the
   discoverability front door; "if a call is not in this map, it does not exist
   yet").
3. **`docs/cpp/console.md`** — the C++ mirror entries `GetCVarInt/GetCVarBool/
   GetCVarFloat` on `kcdxConsoleInterface` (or a `docs/cpp/cvar.md` if the C++ docs
   split by call; match the existing layout), version-3 note.
4. **Glossary "CVar"** — define the term once: *"a CVar (console variable) is a
   CryEngine engine setting addressable by name — the values you see and set in the
   in-game `~` console. kcdx reads them by their console name."* Placed in the
   relevant doc's Glossary section.

## Scope

`docs/lua/cvar.md`, `docs/lua/index.md`, `docs/cpp/console.md` (or `docs/cpp/cvar.md`
matching the existing structure), the glossary section. Single-commit.

## Test bar

The docs gate (`docs-discipline.md`): reference entry complete (shape + args +
return + error + runnable snippet), index map entry present, C++ mirror entry
present, glossary "CVar" term defined, common-path-first. No build/test plugin (docs
do not compile); the gate is the completeness criterion.

## Dependencies

Steps 2 + 3 (the as-built Lua + C++ surfaces the docs describe — `docs-discipline.md`
"verified, not assumed": the doc describes what is actually registered + callable,
written against the as-built binders). Ordered after them. The cap-71 plugin (step
4) is the worked example the snippet can draw from.

## Public-private boundary

`docs/lua/` + `docs/cpp/` are PUBLIC-facing (published by the allowlist). The
entries reference NOTHING private (`public-private-boundary.md`): no `.claude/`, no
`_research/` provenance, no `AP<n>` citation, no internal entity-id. State the
author-facing fact only — "read a CVar by name" — never the ICVar slot / seed id /
RE provenance (those stay in the private FINDINGS).

## Disassembler-test / author-burden

The docs render the disassembler test: the name-only common path is the ONLY path
shown (no hex form to demote). Snippet is `kcdx.cvar.get_int("sys_pakPriority")` —
glance-and-build.

## Rules

`docs-discipline.md` (the completeness criterion — reference + glossary + index +
mirror, same change, glanceable), `lua-api-surface.md` (the author-facing summary),
`public-private-boundary.md` (no private refs in public docs), `cornerstones.md`
(doc clarity is author UX).
