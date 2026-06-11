---
paths:
  - "src/**"
  - "include/**"
  - "kcdx-engine/**"
  - "test-plugins/**"
---

# No hardcoded game addresses in source — resolve by name/id through the DB

Every game-binary target that shifts per KCD2 version — an RVA, an AOB/byte
pattern, a vtable slot index, a game-struct field offset — resolves at runtime
by **name or stable id through the Address Library**, never as a literal in
`.cpp`/`.h`. The Address Library reference DB (`data/reference.sqlite`, authored
via the maintainer tool, exported to `data/db-export/`) is the single source of
truth for these facts so a new game version updates ONE place. A literal address
in source is the defect this rule exists to stop — it is `AP1`.

## The rule

- **Resolve, never hardcode.** Engine code reaches a game function/field via
  `kcdx::refdb::ResolveAddrByName("<name>")` / `ResolveAddrById(id)`; plugin-
  facing code via `kcdx::address_library::ResolveByName` /
  `kcdx::ResolveAddress(id)`; a plugin author writes a `target = "<name>"` or
  `address_id = N`. The name supplies the address AND the verified ABI
  (`cornerstones.md` — the disassembler test); the author never hand-writes a
  signature.
- **What is a per-version-volatile target** (must come from the DB): an RVA /
  module offset, an AOB or byte pattern used as a locator, a vtable slot INDEX,
  a game-struct member offset, a mid-function callsite offset — anything whose
  correct value can change when KCD2 patches. Each is a DB `kind`
  (`address-library.md` §"Address kinds").
- **No new entity without approval.** Resolving by name/id is the always-on
  expectation. AUTHORING a new DB entity/version (the seed-row addition) is the
  gated act — explicit user approval first (`AP18`; `data/maintainer-tool/policy.md`).
  This rule is the in-code side; that gate is the data side.
- **A new game version updates the DB, not source.** A shifted RVA is a new
  `address_versions` row (`address-library.md` §"New game version workflow") —
  source code, which resolves by name, never changes.

## The narrow exception — NOT a per-version-volatile address

A hex literal in source is legitimate ONLY when it is **not** a game-binary
target that shifts per version, and that is stated at the site:

- A bit mask, flag, sentinel, or alignment constant (`x & 0xFFFFFFFF`).
- A probe/test value or an example inside an error string / doc comment.
- A protocol/format constant fixed by a spec, not by the game binary.

When a literal looks address-like but is one of these, a `// SOURCE:` or a
one-line note saying why it is version-stable keeps the reviewer from reading it
as an `AP1` hit. An actual game address with such a note is still `AP1` — the
note does not launder a hardcoded target.

## What this is NOT

- NOT `address-library.md` — that owns the DB shape, resolution API, kinds,
  and the seed-authoring/versioning law; this is the standalone in-source
  prohibition that names `src/**` in its `paths:` so it loads where hardcoding
  happens. Cited, not restated.
- NOT `AP18` / `data/maintainer-tool/policy.md` — those gate ADDING a DB entity (the data
  side, user-approved); this forbids hardcoding in CODE (the always-on side).
- NOT `cornerstones.md` AP12 — that owns the author-facing UX (a name resolves
  address + ABI so the author does no hex work); this owns engine + plugin
  source not embedding a literal at all. A name-based surface that still
  hardcodes the resolved RVA internally violates this rule.
- NOT a ban on every hex literal — masks, sentinels, probe/test/example values,
  and spec-fixed format constants are fine; the bar is "a per-version-volatile
  game-binary target comes from the DB," not "no hex in source."
