---
paths:
  - "src/hook_engine.*"
  - "src/patch_engine.*"
  - "src/conflict_engine.*"
  - "src/hooks.cpp"
  - "src/rom_borrowed/**"
  - "src/config.cpp"
---

# Hook engine — invariants

## Detour engine

MinHook is the sole detour engine. Already vendored. Do not introduce PolyHook2 or any other detour library.

## RoM-borrowed code

`vendor/rom-borrowed/` holds four MIT-licensed files lifted from `return-of-modding/ReturnOfModding` (`dynamic_hook` binding, `runtime_func_t`, `asmjit_helper`, `type_info_t`). Each carries an attribution header. Adapted from PolyHook2 to MinHook (~50 LOC of glue per binding). If upstream fixes a bug, port forward by hand — these are forks at a specific commit, not a submodule.

## Conflict engine ownership

`kcdx::conflict_engine` is the single source of truth for pairwise overlap detection. patch_engine and hook_engine **produce** footprints (writes + reads, with priority + name); conflict_engine **classifies** and **logs**.

- When adding a new engine (e.g. a future vtable-hooking primitive), register footprints with conflict_engine.
- Do NOT add cross-engine knowledge to the new engine.
- Do NOT modify patch/hook engines to know about the new engine.
- Extend `WriteKind` and `Category` enums; do not add ad-hoc overlap loops.

## Apply order

Production orchestration walks `kcdx::conflict_engine::g_applyOrder` (single list of `EntryRef` sorted by `(priority asc, name asc)` across patches + hooks) and dispatches `patch::ApplyResolvedPatch` or `hook_engine::ApplyOneHook` per entry.

- There is NO "apply all patches then all hooks" sequence.
- A high-priority hook applies before a low-priority patch.
- Engine-fix plugins under `kcdx-engine/builtin/` always apply first; conflicts with user plugins at the same address resolve in the engine fix's favor.

## ApplyAll fallback paths

`patch::ApplyAll()` and `hook_engine::ApplyAll()` exist for the Lua-runtime path and test harnesses. **Production orchestration in `hooks.cpp` does NOT call them.** If you call `patch::ApplyAll()` from production code, you've reintroduced the patches-always-before-hooks bug.

## First-hook-wins (legacy `hook_engine::InstallRuntime` — TOML entry point deleted)

`hook_engine::InstallRuntime` is first-hook-wins: first installs, second aborts
with a plain-English log line naming the first. The TOML `[[hook]]` entry point
that fed this was **deleted in Phase 5** (`95854fe`); the `hook_engine` code
itself still exists (a dead-but-present consumer kept by Phase 5's narrow cut,
slated for a later removal pass) but **nothing reaches it from the author
surface anymore.** `kcdx.hook` + `src/hook_chain.cpp` (below) is the only hook
path authors use; it supersedes first-wins with load-order chaining.

## `kcdx.hook` chaining — `hook_chain` is the mediator (supersedes first-wins)

`hook_chain` (`src/hook_chain.cpp`) is the single install path used by the rest of the engine + every plugin. The single exception is `update` (the engine's per-frame dispatch pump): `HookedUpdate` calls `hook_chain::SetLuaState` AND drives the chain's per-frame `DispatchPre`, so it cannot itself be a chain entry without self-deadlocking. It stays direct `MH_CreateHook`, named and documented; no other engine site does. **Load order decides, full stop.** Multiple compatible hooks (multiple `before`, multiple `after`, `before`+`after`, etc.) coexist on one target — ONE MinHook detour, an ordered chain of mode-tagged callbacks fired in unified load order. A plugin can hook a target and another can piggyback on the same target.

When two hooks cannot coexist (incompatible signature on the shared thunk, or `replace`/`around` which v1 treats as worst-case-exclusive), the **later-in-load-order** one is rejected — its handle goes `Failed` with a loud reason; the earlier wins. The single predicate `hook_chain::CanCoexist` decides; the chain is `std::vector<ChainEntry>` (N entries, any mode), with NO distinguished "the replace" slot. Footprint-based smart coexistence (two replaces touching disjoint slots) is future work — `docs/outstanding-work/smart-replace-conflict-detection.md` (implementation-grade spec; do not deviate). Chaining IS the `kcdx.hook` model — the "do not invent chaining" guidance applies to the legacy paths only.

### Engine-owned chain entries

Engine-internal hooks register with the chain as engine-stamped entries via `hook_chain::AddCEngine` (`pluginName = "kcdx"`, names like `"engine.lua_pcall"`). The chain's `InsertOrdered` comparator short-circuits on the engine stamp BEFORE the priority/name tiebreak, so engine entries ALWAYS sort to the FRONT of the chain regardless of declared priority. Plugin `before` callbacks therefore see arguments after the engine `before` has run — preserving the diagnostic ground-truth the frealloc canary's image-range fingerprint and the `lua_pcall` L-capture both depend on. Engine `replace` entries reject all plugin coexistence (the existing `CanCoexist` rule), with a teaching error that names the target as an engine bootstrap point and points at `docs/cpp/hook.md` § "Bootstrap targets" — the canonical error substring is "engine bootstrap point" (kept stable so the cap-NN-modmanager-reject regression rows can substring-match it). The engine-stamp identity is type-enforced via `ChainEntry::isEngine`, not a reserved-priority-sentinel convention any plugin could spoof. Future engine sites use the same convention (`AddCEngine` from a kcdx-internal install site, with `pluginName = "kcdx"` and an `engine.<site>` name).

PROBE Q canary preservation: `frealloc`'s `block ∈ kcdx.dll image` check is a read-only fingerprint that survives chain mediation — the chain's `DispatchPre` calls the registered Before callback, which still sees every call; nothing the chain does between MinHook's trampoline entry and the callback's read of `block` perturbs PROBE Q's contract. (Same shape applies to any other read-only fingerprint a future engine-direct site needs to preserve.)

## Mid hooks are a first-class primitive (`kcdx.hook.mid`)

Mid-function hooks with named register/stack captures are strictly more powerful than function-entry hooks — a first-class primitive, not a niche feature. Authored via `kcdx.hook.mid(module, target, offset, captures, callback)` (Lua) / `kcdxHookInterface::Mid` (C++); both can skip the captured instruction (`return "skip"` / `kcdxMidResult_Skip`). (The legacy `[[mid_hook]]` TOML entry point was deleted in Phase 5.)

## Byte-patch semantics (`kcdx.bytes` / `kcdxBytesInterface`)

`replacement.length == original.length` is required for byte patches (`kcdx.bytes` Lua / `kcdxBytesInterface::Register` C++). Code injection (adding bytes, not replacing) goes through a hook or `kcdx.code` (trampoline allocation), not a byte patch. (The legacy `[[patch]]` / `[[trampoline]]` TOML entry points were deleted in Phase 5; the length-preserving rule carries to the `kcdx.bytes` surface.)
