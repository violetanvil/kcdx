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
- [cpp-console-command-wrapper.md](cpp-console-command-wrapper.md) — empowered `kcdx::console::Command(K, "name", "help", lambda)` wrapper over `kcdxConsoleInterface::RegisterCommand` + typed `kcdx::console::Args` proxy with default-bearing `Get(n, default)`. Lowest-ROI row (#8) of the 2026-05-28 wrapper-improvements audit's surface ranking — deferred from Phase 12's scope because the raw form's ceremony is renamed rather than removed. Revisit on either user-shipped plugin friction at scale OR Phase 12's docs flip leaving `docs/cpp/command.md` as the only common-path lead still on a raw form.
- [declare-scoped-clear.md](declare-scoped-clear.md) — scoped `kcdx.declare.clear_own()` + `kcdx.declare.clear(name)` API (Lua + C++ peers on `kcdxDeclareInterface`) so TC mods can drop their OWN declarations without breaking cross-plugin contracts. The engine-wide `declared_targets::Reset()` stays engine-internal — no public nuke that lets one plugin destroy every other plugin's declarations. Designed during the unified Phase 9.2 H1c test discussion when the test plugin needed Reset and the threat model surfaced; revisit when a TC mod surfaces friction OR a hot-reload flow needs per-plugin teardown.
- [declare-value-string-arena.md](declare-value-string-arena.md) — **committed completion of the `kcdxDeclareInterface::Get` string-pointer-lifetime contract.** Route `VersionEntry::valueStr` storage through a process-lifetime `std::deque<std::string>` arena so a same-triple re-`Declare` from the owning plugin no longer destroys prior `stringValue` pointers (today's `g_entries[existing] = e;` copy-assign at `src/declared_targets.cpp:564` destroys the inner string — silent UAF for any author who cached the pointer). The contract sites currently carry interim narrow wording ("same-triple invalidates, re-`Get` after re-`Declare`"); arena lands → broad "process-lifetime; survives any subsequent Declare from any plugin" wording restored. Trigger: the narrowing change that landed the interim wording is the trigger; ready to land in the next change that touches the declared-targets store.
