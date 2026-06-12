---
id: TD-0012
opened: 2026-06-11
status: Open
area: src/lua_bind_statement.cpp + src/conflict_engine — statement-carrier conflict visibility
closure_gate: the next statement-surface or conflict-engine cycle that registers a WriteFootprint for the statement carrier (so the pairwise matrix + GetConflictReport see statement writes and a statement-vs-statement clobber names the other party)
owner: continuous (whoever next touches the statement surface or the conflict engine)
commit_at_filing: 3421de2
related:
  - KI-0017 (closed — the statement-apply fix that surfaced this; KI-0017 fork-3)
  - TD-0010 (statement replace_with live-execution readback — adjacent statement-surface debt)
affected_sites:
  - src/lua_bind_statement.cpp  (the apply handler calls patch::ApplyPatch directly; the carrier PatchEntry is never in patch::g_patches, so it never registers a conflict_engine WriteFootprint)
  - src/patch_engine.cpp:556-565  (the verify-failure enrichment loop scans g_patches only — a statement-vs-statement clobber rejects loud but cannot NAME the other statement carrier)
  - src/conflict_engine.h  (g_writes / WriteFootprint / the pairwise matrix — statement carriers are absent)
---

# TD-0012 — statement carriers bypass the conflict engine (loud-but-unnamed clobber; conflict report omits statement writes)

## Context

KI-0017 (closed `3421de2`) made `kcdx.statement.replace_with` actually apply. Its
fork-3, settled by the user as carried debt rather than folded into the fix:
statement carriers are **not** registered with the conflict engine.

The apply handler (`src/lua_bind_statement.cpp`) routes its write through
`patch::ApplyPatch` **directly** — the carrier `PatchEntry` is never inserted
into `patch::g_patches`, and `conflict_engine::RunPreFlight` (which once
populated `g_writes` + the pairwise `Conflict` matrix) was retired. So a
statement write has **no `WriteFootprint`** and is invisible to:

- the pairwise overlap matrix (`g_writes` / `Conflict` in `src/conflict_engine.h`);
- `GetConflictReport` (statement writes are omitted entirely);
- the verify-failure enrichment loop (`src/patch_engine.cpp:556-565`), which scans
  `g_patches` only.

## The carried gap (precise)

The integrity invariant is **already preserved** by KI-0017's fix: a second
writer at a statement VA with **differing** bytes is rejected loud by the
inline `replacement`-vs-site verify (`patch_engine.cpp:548`). What is missing is
the **NAMING** — the loud reject cannot say WHICH other statement entry owns the
site (the enrichment loop only knows `g_patches`). And the conflict report has a
blind spot for statement writes.

This is loud-but-unnamed, not silent — so it is carried debt, not a correctness
hole.

## Closure gate

The next cycle that touches the statement surface or the conflict engine
registers a `WriteFootprint` for the statement carrier (begin = statement VA,
len = `byte_range_len`, priority + name from the carrier), so the pairwise
matrix + `GetConflictReport` see statement writes and a statement-vs-statement
clobber names the other party. When that lands: the footprint registration +
a regression row exercising a named statement-vs-statement conflict + this TD
closed, same change.

## What this entry does NOT do

- Does not fix the gap (that is the closure-gate cycle's job).
- Does not weaken KI-0017's fix — the loud reject already prevents silent clobber.
