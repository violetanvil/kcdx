# Outstanding work

Designed-but-not-built items, each with a named revisit trigger. Counterpart to `docs/known-issues/` (open bugs with diagnostic trails).

## Contract for entries

Each file holds exactly:

1. **Status** — one line: state + what's blocking ship.
2. **Trigger to revisit** — concrete condition under which to pick this up.
3. **Design** — the settled decisions (schema, contract, mechanism).
4. **Files that need to change** — when the trigger fires, the working agent has the change set ready.

No history. No "why we deferred this v3 of the discussion". Pointer to deeper docs (`design-gaps.md`, recon dossiers) at the bottom if needed.

## Current entries

- [restructure-plan.md](restructure-plan.md) — **active, in progress.** Manifest-only TOML, Lua-first authoring, owned launcher (kcdx.exe), unified ordered list, kcdx absorbs pak mods. 12-phase plan; Phases 1-10 ship the new author surface; Phase 11 consumes FIX A. Authoritative spec for kcdx v0.2+; supersedes large sections of `docs/design.md`.
- [fix-a-drop-static-lua.md](fix-a-drop-static-lua.md) — drop static-linked `vendor/lua`, route every `lua_*` through WHGame.dll's symbols. Independently in progress at `_research/phase8-fix-a/` (~38% RVAs mapped as of plan finalization). Phase 11 of the restructure plan consumes this.
- [phase6-listener.md](phase6-listener.md) — CryEngine `IGameFrameworkListener` as second-source-of-truth for save/load
- [hook-api-positional-shorthand.md](hook-api-positional-shorthand.md) — possible shorthand call form for kcdx.hook (`kcdx.hook(target, callback)` for the trivial case); table-of-keys form ships first
- [ide-stubs-from-address-library.md](ide-stubs-from-address-library.md) — build-time generator emits `kcdx-stubs.lua` from `data/seeds/address_names_seed.csv` so editors show hover-docs on `kcdx.addr.<name>`; revisit when shipping public releases
- [smart-replace-conflict-detection.md](smart-replace-conflict-detection.md) — kcdx.hook ships chained hooks for non-replace modes day-one; replace+anything-else falls back to load-order-loses. Smart footprint-based conflict detection lands when real plugins need it.
- [ready-event-and-handle-assert.md](ready-event-and-handle-assert.md) — post-ApplyZone `kcdx.on("ready", ...)` event so a plugin can run code once its hooks are live + assert handle:applied()/reason(). Unblocks the deferred CAP-20 miss-asserts (bad address_id name; conflict-rejection side).
- [lua-callback-main-thread-guard.md](lua-callback-main-thread-guard.md) — runtime `GetCurrentThreadId()` guard in the Lua dispatchers (skip-and-log off-thread fires). Known correctness gap (AP13), currently mitigated only by the authoring contract.
- [before-game-hooks.md](before-game-hooks.md) — author-facing hooks installed at DllMain/LDR-notification timing on a foreign module's export (zone-driven, self-registration via the plugin's own DllMain, first-applied-wins). Design-settled + live-de-risked (PROBE T). **Phase 11** (Lua tier needs FIX A's DllMain VM; C++ tier buildable earlier but bundled by user choice). The BugSplat colon-filename fix is its first consumer. Resolves the zone_gate synthetic-row collision (flip `kcdx.hook` to Either + rework comp-13 at Phase 11).
- [hook-capturing-lambda-context-slot.md](hook-capturing-lambda-context-slot.md) — per-hook `void* userdata` slot on `kcdxHookOptions` so the `Kcdx.h` empowered helpers can accept a capturing lambda. Engine ABI extension (AP11 append-only + JIT thunk branch); the current non-capturing fast path stays as the zero-cost form. Tracked from the 2026-05-28 wrapper-improvements audit (item D); revisit on either user-shipped plugin friction OR an unrelated engine change that adds a per-hook context slot.
