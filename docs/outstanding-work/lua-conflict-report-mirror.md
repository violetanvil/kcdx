# Owed feature: Lua conflict-report introspection mirror (`kcdx.conflict`)

**Status:** designed-not-built. Owed by the Lua↔C++ full-parity invariant
(`.claude/rules/lua-api-surface.md`). Its own `/feature` cycle; due before
Phase 5 (legacy-parser deletion) and certainly before the end-state
full-parity bar.

## The gap

The **C++** plugin surface exposes conflict-engine introspection —
`GetConflictReport(...)` + `kcdxConflictEntry` on the C++ interface
(`include/kcdx/Interfaces.h` ~:182-196, 328-330; impl `src/interfaces.cpp`).
A C++ author can query, for a target, which entries registered, who won, who
was aborted, and why. This is production capability and it IS tested: the
`comp-02-hook-on-patch` and `comp-03-hook-on-hook-B` DLL verifiers call
`GetConflictReport` and assert its output (the conflict-engine regression
net).

**C++-side hook_chain coverage now closed (2026-05-25).** The C++
`GetConflictReport` originally only walked the legacy conflict_engine resolved
patch/hook lists — a `kcdx.hook` (hook_chain) target returned 0 entries. The
"GetConflictReport covers hook_chain" feature (commits `f91a7d4` hook_chain
records `chain->rejected` + `GetParticipantsAtTarget`; `5f7d997`
`Thunk_GetConflictReport` merges them as a third source; `comp-14-conflict-
report-hook-chain` the C++ regression test) closes that gap: the C++ report now
reports `kcdx.hook` winners (applied) AND CanCoexist-rejected losers (aborted).
So the work this doc tracks is now **purely the Lua mirror** — the C++ surface
is complete (legacy patch/hook + kcdx.hook). `[[mid_hook]]`/mode=mid conflicts
remain unreported on BOTH the future Lua mirror and the C++ side by the same
contract (mid rejects via sole-ownership, not the chain's CanCoexist path).

**C++-side `kcdx.bytes` (Register) coverage now closed too (2026-05-25).** A
fourth source was added: a `kcdx.bytes` patch registered via
`kcdxBytesInterface::Register` routes through the `lua_registry` `Kind::Bytes`
path (NOT the legacy `[[patch]]` `g_patches` list) and was therefore invisible
to `GetConflictReport`. `Thunk_GetConflictReport` now folds those entries in as
a fourth source (`kind == kcdxConflictEntryKind_Patch`); `cap-41-cpp-bytes-
conflict-report` is the C++ regression that proves it (asserts its named
bytes-Register entry appears in the report at the patched VA, alongside the
co-located cap-01 `g_patches` + cap-39 bytes-Register entries). The C++
`GetConflictReport` now covers all four sources (legacy patch + legacy hook +
kcdx.hook + kcdx.bytes Register). This does **not** close the item: the owed
work remains the **Lua mirror** (`kcdx.conflict`) — the C++ additions only
widen what that mirror must eventually expose.

There is **no Lua mirror.** Grep confirms `GetConflictReport` /
`ConflictReport` / `conflict_report` appear in zero `docs/lua/` files and zero
`lua_bind_*.cpp`. The Lua hook handle exposes only
`name/applied/reason/wait_applied/uninstall` (`docs/lua/hook.md` ~:262-268) —
a handle reports its OWN fate (`:applied()`/`:reason()`), but a Lua author
cannot enumerate the other entries on a target the way `GetConflictReport`
does. `docs/lua/index.md` has no `kcdx.conflict` entry, so by that file's own
contract ("if a call is not in this map, it does not exist yet") it does not
exist — not even as an NYI entry.

## Why it's owed (not single-surface)

Conflict-report introspection is NOT a thing the Lua runtime provides
natively — it's engine state the C++ surface deliberately exposes. Lua↔C++
parity is a **hard invariant** (`lua-api-surface.md`: "Anything achievable in
one language is achievable in the other. NO Lua-only or C++-only
capability."). A C++-only conflict-report surface is therefore an *incomplete
feature*, not a sanctioned single-surface case. The Lua author writing a
total-conversion needs to know "did my hook win or get aborted, and against
whom" exactly as the C++ author does.

Per the user's rule (2026-05-25): **any path that will be used in production
must have a real regression test.** The C++ path satisfies this (comp-02/03).
The Lua mirror, once built, must ship with its own regression plugin (a Lua
`comp-NN` asserting the conflict report from the Lua side) — the feature is
not "done" until that test exists.

## Sketch (decide at the /feature audit, not here)

Candidate shapes (the audit picks):
- `kcdx.conflict(target)` → a table of entries `{ {name, plugin, status,
  reason}, … }` mirroring `kcdxConflictEntry` — the direct mirror.
- OR a richer hook-handle method (`h:conflicts()` → the entries on h's
  target) if per-handle is the more Lua-idiomatic shape.
- The C++ `GetConflictReport` ABI is the source of truth for the fields.
- Ships with: the `docs/lua/` entry (flips the C++ side's owed NYI mirror to
  built-both-sides), a glossary term if "conflict report" is new vocabulary,
  and a Lua `comp-NN` regression test asserting a known overlap's report.

## Trigger to build

Before Phase 5 (legacy-parser deletion) at the latest — but ideally
whenever the next conflict-surface work happens. Not a Phase 4b blocker:
comp-02/03 migrate their behavior blocks ([[hook]]/[[patch]] →
kcdx.hook/kcdx.bytes) now and KEEP their C++ `GetConflictReport` verifier as
the conflict-report regression test; this owed feature adds the Lua mirror
alongside, it does not gate the migration.

Related: `lua-api-surface.md` (the parity invariant), `docs-discipline.md`
(the owed-mirror rule), `smart-replace-conflict-detection.md` (the
conflict-engine's future work), the comp-02/comp-03 plugins (the C++ test).
