# Outstanding work

Designed-but-not-built items, each with a named revisit trigger. Counterpart to `docs/known-issues/` (open bugs with diagnostic trails).

## Contract for entries

Each file holds exactly:

1. **Status** — one line: state + what's blocking ship.
2. **Trigger to revisit** — concrete condition under which to pick this up.
3. **Design** — the settled decisions (schema, contract, mechanism).
4. **Files that need to change** — when the trigger fires, the working agent has the change set ready.

No history. No "why we deferred this v3 of the discussion". Pointer to deeper docs (`design-gaps.md`, recon dossiers) at the bottom if needed.

## Status ledger — the canonical "I completed this" surface

A multi-step entry (a phased plan, an item that lands across several commits) carries ONE status ledger and no other status surface. It is the single source of truth for step completion — not prose paragraphs, not inline `— DONE` header tags, not the commit log alone.

Fixed shape, exactly these columns:

| Step | Status | Commit |
|---|---|---|
| `<step id — short label>` | `<NOT STARTED \| IN PROGRESS \| DONE \| BLOCKED \| SUPERSEDED>` | `<(landed) until backfilled, then the short hash; — when not DONE>` |

Rules:
- **One ledger per doc.** A doc with a ledger does NOT also carry a status-prose paragraph or per-header `— DONE` tags — those drift against the table. The ledger wins.
- **The orchestrator writes the row, not a human.** A landed step's row flips to `DONE` in the SAME commit as the step's diff. This is a gated step of the orchestrator cycle (`../../.claude/skills/_shared/orchestrator-loop.md` §C), not a follow-up edit.
- **The hash backfills one commit later — a commit cannot contain its own hash.** The step's own commit lands `DONE` + `(landed)` in the Commit cell (NOT a hash — the landing commit's hash does not exist yet when that commit is built). The real short hash is filled by the NEXT ledger-touching commit: the next step flips its own row AND replaces the prior row's `(landed)` with the now-known hash. The final step of a plan leaves a trailing `(landed)`; a tiny follow-up commit backfills it. Never write a hash into the commit that the hash names — that yields the PRIOR commit's hash (the stale-hash bug).
- **Status vocabulary is closed.** `BLOCKED`/`SUPERSEDED` rows state the blocker/successor in the step label (e.g. `11b force-load — blocked on 11a`), not in an extra column.
- **The active "go-to" plan doc is born with a ledger.** Whatever doc currently drives sequenced work carries this table from creation; its retirement does not retire the convention — the next active plan inherits it. A multi-phase plan tree (top README + `phase-NN-<slug>/` subdirs + per-step docs + `context.md`) is authored by `/plan` from a settled goal; the executing cycle (`/feature` / `/execute`) reads a step doc as its `Source work-item` and flips its ledger row.

## Current entries

- [maintainer-tool-db-direct/](maintainer-tool-db-direct/README.md) — **active, not started.** Build the maintainer tool: DB-direct authoring of the Address Library with CSV auto-export, MVP = Job 2 (re-verify one curated entity) end-to-end. 3-phase plan (headless data-core → PySide6 GUI shell → PyInstaller .exe). Phase tree authored by /plan; canonical phase-grain ledger in [maintainer-tool-db-direct/README.md](maintainer-tool-db-direct/README.md).
- [importer-no-prose-derivation/](importer-no-prose-derivation/README.md) — **active, not started.** Eliminate ALL prose-derivation / regex / inference from the refdata importer — every field the importer needs is its own explicit authored seed CSV column; `notes` is human commentary only. 3-phase plan (audit → columns → author+rewire+delete). Phase tree authored by /plan; canonical phase-grain ledger in [importer-no-prose-derivation/README.md](importer-no-prose-derivation/README.md).
- [db-updator-phase1/](db-updator-phase1/README.md) — **active, not started.** Incremental `apply` mode for `import_to_sqlite.py` — land hand-edited seed-CSV deltas into both reference DBs without a full rebuild, sharing one row-builder with rebuild. Phase 1 of the maintainer-tool flow. Phase tree authored by /plan; canonical phase-grain ledger in [db-updator-phase1/README.md](db-updator-phase1/README.md).
- [restructure/](restructure/README.md) — **active, in progress.** Manifest-only TOML, Lua-first authoring, owned launcher (kcdx.exe), unified ordered list, kcdx absorbs pak mods. 12-phase plan; Phases 1-10 ship the new author surface; Phase 11 consumes FIX A. Authoritative spec for kcdx v0.2+; supersedes large sections of `docs/design.md`. The folder is the navigable form — one subdir per phase, one document per shippable step, the canonical phase-grain status ledger in [`restructure/README.md`](restructure/README.md); the original monolithic plan is preserved verbatim at [`restructure/00-original-plan.md`](restructure/00-original-plan.md).
- [fix-a-drop-static-lua.md](fix-a-drop-static-lua.md) — drop static-linked `vendor/lua`, route every `lua_*` through WHGame.dll's symbols. Independently in progress at `_research/phase8-fix-a/` (~38% RVAs mapped as of plan finalization). Phase 11 of the restructure plan consumes this.
- [phase6-listener.md](phase6-listener.md) — CryEngine `IGameFrameworkListener` as second-source-of-truth for save/load
- [hook-api-positional-shorthand.md](hook-api-positional-shorthand.md) — possible shorthand call form for kcdx.hook (`kcdx.hook(target, callback)` for the trivial case); table-of-keys form ships first
- [ide-stubs-from-address-library.md](ide-stubs-from-address-library.md) — build-time generator emits `kcdx-stubs.lua` from `data/seeds/address_names_seed.csv` so editors show hover-docs on `kcdx.addr.<name>`; revisit when shipping public releases
- [smart-replace-conflict-detection.md](smart-replace-conflict-detection.md) — kcdx.hook ships chained hooks for non-replace modes day-one; replace+anything-else falls back to load-order-loses. Smart footprint-based conflict detection lands when real plugins need it.
- [ready-event-and-handle-assert.md](ready-event-and-handle-assert.md) — post-ApplyZone `kcdx.on("ready", ...)` event so a plugin can run code once its hooks are live + assert handle:applied()/reason(). Unblocks the deferred CAP-20 miss-asserts (bad address_id name; conflict-rejection side).
- [before-game-hooks.md](before-game-hooks.md) — author-facing hooks installed at DllMain/LDR-notification timing on a foreign module's export (zone-driven, self-registration via the plugin's own DllMain, first-applied-wins). Design-settled + live-de-risked (PROBE T). **Phase 11** (Lua tier needs FIX A's DllMain VM; C++ tier buildable earlier but bundled by user choice). The BugSplat colon-filename fix is its first consumer. Resolves the zone_gate synthetic-row collision (flip `kcdx.hook` to Either + rework comp-13 at Phase 11).
- [hook-capturing-lambda-context-slot.md](hook-capturing-lambda-context-slot.md) — per-hook `void* userdata` slot on `kcdxHookOptions` so the `Kcdx.h` empowered helpers can accept a capturing lambda. Engine ABI extension (AP11 append-only + JIT thunk branch); the current non-capturing fast path stays as the zero-cost form. Tracked from the 2026-05-28 wrapper-improvements audit (item D); revisit on either user-shipped plugin friction OR an unrelated engine change that adds a per-hook context slot.
- [cpp-console-command-wrapper.md](cpp-console-command-wrapper.md) — empowered `kcdx::console::Command(K, "name", "help", lambda)` wrapper over `kcdxConsoleInterface::RegisterCommand` + typed `kcdx::console::Args` proxy with default-bearing `Get(n, default)`. Lowest-ROI row (#8) of the 2026-05-28 wrapper-improvements audit's surface ranking — deferred from Phase 12's scope because the raw form's ceremony is renamed rather than removed. Revisit on either user-shipped plugin friction at scale OR Phase 12's docs flip leaving `docs/cpp/command.md` as the only common-path lead still on a raw form.
- [declare-scoped-clear.md](declare-scoped-clear.md) — scoped `kcdx.declare.clear_own()` + `kcdx.declare.clear(name)` API (Lua + C++ peers on `kcdxDeclareInterface`) so TC mods can drop their OWN declarations without breaking cross-plugin contracts. The engine-wide `declared_targets::Reset()` stays engine-internal — no public nuke that lets one plugin destroy every other plugin's declarations. Designed during the unified Phase 9.2 H1c test discussion when the test plugin needed Reset and the threat model surfaced; revisit when a TC mod surfaces friction OR a hot-reload flow needs per-plugin teardown.
