# Plan — kcdx restructure: manifest-only TOML, Lua-first authoring, owned launcher

> **PRESERVED REFERENCE — the original monolithic plan, kept verbatim.** The
> live, navigable breakdown is the phase subdirectories under
> [`restructure/`](../README.md): one subdir per phase, one document per shippable
> step, with the canonical status ledger in [`README.md`](../README.md). This file
> is retained because (a) it holds the shared spec the per-step docs lean on
> (the model, the Lua/C++ surface reference, lifecycle, launcher, the Critical
> files / Risk register / Verification plan tail) and (b) deep cross-links from
> peer docs cite its sections and line ranges. Its content is NOT re-edited as
> work lands — step status lives in the ledger, not here.

## Context

kcdx today couples behavior declaration to TOML schemas (`[[patch]]`, `[[hook]]`, `[[mid_hook]]`, `[[trampoline]]`, `[[scan]]`, `[[command]]`, `[[event]]`). Authors learn seven entry types, three mutually-exclusive locator fields, two body shapes (`bytes` vs `lua_callback`), and a separate `[plugin]` manifest section — just to express "intercept this function." The growth was organic; now it shows. A SKSE veteran reads it and asks "where is the function-call API?"; a new modder reads it and asks "which verb do I use?"

The BugSplat investigation closed today on this point: the user explicitly asked "shouldn't we be writing these as functions so it would be like `hook(target, when, signature)`?" The honest answer is yes, and the discussion that followed established three constraints in strict priority order:

1. **UX** — for both mod authors and consumers; "feels natural" with minimal jargon.
2. **Capability** — total conversions (Fallout-London-class mods) must be possible.
3. **Performance** — game speed not sacrificed.

Implementation difficulty is **never** a valid reason to drop any of the three. "X is hard, Y does 80% of X and is easier — let's ship Y" is explicitly rejected. We do X.

Backward compatibility is also explicitly **not** a constraint: kcdx is unshipped (lives on one machine), so the schema, layout, and load model are all free to change. Mempatch compatibility (the kcdx `[[patch]]` shape matched mempatch's verbatim) is dropped.

This plan restructures kcdx end-to-end:

- **TOML becomes manifest-only.** `kcdx.toml` declares identity, version, dependencies, load-order hints, and entrypoints. No behavior.
- **Lua becomes a first-class authoring language.** A pure-Lua plugin (`kcdx.toml` + `plugin.lua`) ships a working mod without a compiler.
- **C++ DLLs stay first-class.** Same function-call API surface as Lua; SKSE veterans get what they expect.
- **kcdx ships its own launcher exe** (`kcdx.exe`) that injects `kcdx.dll`. Ultimate ASI Loader is no longer required.
- **The engine is the dispatcher of intent**, not the parser of declared entries. Plugins declare entries by calling `kcdx.hook(...)`, `kcdx.bytes(...)`, etc.; the engine sorts, dedupes, runs conflict pre-flight, and applies in unified load order.

The BugSplat fix that motivated this conversation arc resumes at the end of the restructure as the first user of the new API (see Phase 6) — it's the canonical "intercept a function in a non-WHGame DLL, mutate a string arg, call original" case the new API was designed for.

## Confirmed design decisions

The discussion that produced this plan resolved ten load-bearing questions. Each is stated here so the implementation has a single source of truth for "which choice did we make and why."

1. **No fixed "Lua wave" vs "DLL wave."** All plugins (Lua, DLL, mixed, builtin, user) sit in **one unified ordered list** sorted by `(zone, plugin_priority, plugin_name)`. The launcher walks every manifest, reads the load order, spins up only what's required (e.g. the Lua VM only when an enabled plugin has Lua entrypoints), then dispatches plugin initialization in the unified order. Engine builtins are not preferred — they earn their early position via `[load_order].priority`, same as everyone else.
2. **API calls register intent; engine applies them in one pass after all plugins have registered.** When `plugin.lua` calls `kcdx.hook(opts)`, the engine validates the call (locator format, signature, zone capability) and queues the registration; nothing is written to game memory yet. After all enabled plugins in the unified ordered list have run their plugin.lua / DLL Preload+Load (i.e. after every plugin in the current zone has had its turn to register), conflict_engine runs pre-flight ONCE across all queued entries, classifies conflicts, decides who wins via unified load order, then the apply pass walks the registrations in order and installs them. The Lua API call returns a *handle* immediately — but `handle:applied()` only returns true after the apply pass. Plugins that need to act on apply outcomes register via `kcdx.on("ready", function() check_my_handles() end)`. Why deferred not immediate: lets conflict_engine see ALL intent before applying ANY entry, surfaces all conflicts in one coherent report, makes "plugin A wins over plugin B" a deterministic function of load order rather than which-call-ran-first-by-microseconds.
3. **There is one Lua VM in the process: WHGame.dll's. kcdx spins it up earlier than CryEngine would normally.** kcdx.dll's DllMain force-loads WHGame.dll (`LoadLibraryW`) and then calls WHGame's compiled `luaL_newstate` via the FIX A symbol shim. The returned `lua_State` is allocated by WHGame's compiled Lua code, with WHGame's sentinels, configured exactly the way CryEngine's compiled Lua configures any state — because it IS CryEngine's compiled Lua doing the configuring. When CryEngine's startup code later tries to call `luaL_newstate` itself, we hook the call and return our pre-allocated state. **There is no second VM to manage or mimic. The dual-Lua sentinel hazard is dead by construction** — kcdx no longer has its own compiled Lua at all (post-FIX-A), so there's only one set of sentinels in the process: WHGame's.
4. **The VM overhaul (Phase 11) runs in parallel with FIX A.** Phases 1-10 ship the manifest-only TOML + Lua API + launcher + DLL parity. Lua-in-before_game arrives in Phase 11. Phase 11 builds on FIX A (currently underway at `_research/phase8-fix-a/`, ~38% RVAs mapped as of plan finalization). The mechanism: kcdx.dll's DllMain force-loads WHGame.dll via `LoadLibraryW`, then uses the FIX A shim to call WHGame's compiled `luaL_newstate`. One compiled Lua body in the process (WHGame's). Dual-Lua sentinel hazard dies by construction. Phases 1-10 keep today's static-linked Lua + FIX C; Phase 11 drops both.

(Refinement to #2: for **hooks** the per-target conflict mediator is `src/hook_chain.cpp` with **load order deciding** — see "Chaining + conflicts" below; `conflict_engine` mediates byte-patch overlap. Same principle — all intent seen before any apply, load order is truth — different engine per kind.)

5. **Author surface is tiered, not flat — pick by intent.** Four namespaces serve four genuinely different jobs and authors pick by what they want to do: `kcdx.behavior.*` (the simple-modder surface — named declarative behaviors, one line of Lua); `kcdx.hook.*` (callback-based interception — industry-standard meaning of "hook"; per-call Lua cost; use when per-call logic is needed); `kcdx.statement.*` (static-bytes modification — zero per-call cost; bytes execute natively; use when behavior is static); `kcdx.bytes` (raw byte rewrites outside functions + labeled-expert `pattern` hatch for AOBs without function context). Each primitive has one obvious use; no overlap; engine has one dispatch path per concept. `kcdx.behavior.*` grows by community PR — each new named behavior maps to underlying `kcdx.hook.*` / `kcdx.statement.*` calls.

6. **Per-version survival via per-function hash comparison, never version comparison.** A plugin authored against game version X running on game version Y is silent if every function it touched has byte-identical content in X and Y. A warning fires only when a SPECIFIC function the plugin touched genuinely changed. Plugins do NOT break on every game update — they break only when the function they target actually changed. The engine carries the SQLite reference DB with per-version hashes; authors carry only `authored_against_game_version = "..."` in their manifest as the baseline anchor, and only when their plugin uses a hash-checked primitive. Authors never see hashes, IDs, or internal mechanism — only the survival promise and (when a target actually changed) a teaching error naming the function.

   > **Update 2026-05-28 — partially SUPERSEDED.** The original "auto-track every function" mechanism was reduced to the **three-track streamline** (Phase 7 supersession marker has the full story; quick version: ~140 curated targets are kcdx-maintained and get this survival check; Track-2 author-declared targets carry their own per-version patterns; the bulk 321K is never cross-version-tracked). The DB tables that implement it changed shape (see Phase 9.1 supersession below + `docs/outstanding-work/parallel-ghidra-research.md` §11.9 for the current schema). The *user-facing promise* in this tenet — "the plugin keeps working unless its specific target changed" — survives, scoped to curated targets.

7. **SQLite as the engine's static reference store.** One shipped file (`engine/hashes/reference.sqlite`) carries all per-function hashes, per-statement metadata, applicable-ops tables, and the behavior catalog. Single lookup primitive (`hash_at(function_name, game_version)`); per-plugin verification cache (`engine/cache/version_check.bin`) avoids re-running checks across launches. Vendored SQLite amalgamation (~150 KB). No CSV, no diff chain, no install-time assembly step. Re-baseline every ~12 months by dropping oldest-version rows.

   > **Update 2026-05-28 — partially SUPERSEDED.** The shipped USER DB is `data/reference/reference.sqlite` (not `engine/hashes/`), ~0.1 MB on disk (the ~150 KB estimate was an early guess; the actual size is smaller after the curated-only narrow). Schema is now `address_names` + `address_versions` + registries + `meta` (see `parallel-ghidra-research.md` §11.9); the `hash_at(name, version)` primitive becomes a single-join `SELECT` over `address_names`→`address_versions` per §11.9's resolution path.

8. **Multi-DLL coverage from day one.** The SQLite schema uses `module` as a first-class column. `kcdx.hook.*` / `kcdx.statement.*` / `kcdx.bytes` accept a module argument (defaulting to `"WHGame.dll"` for the common case). Reference dump covers WHGame.dll primary plus CrySystem, Cry3DEngine, BugSplat64, and any other modules kcdx hooks. Future port of the engine to another game ships another `reference.sqlite` keyed on that game's module names; same engine code, no fork.

   > **Update 2026-05-28.** The `module_id` column now lives on `address_versions` (per-version, allowing a future re-architecture to move an entity between modules trivially); the `modules` registry is a small id→name lookup. CryEngine submodules (CrySystem / Cry3DEngine) turn out to be statically linked INTO WHGame.dll, not separate DLLs in this build — only the secondary DLLs named here (BugSplat64, etc.) are genuine separate modules.

9. **`kcdx.hook.*` and `kcdx.statement.*` are GENUINELY DIFFERENT namespaces, not aliases.** They serve mechanistically distinct work: `hook` is callback-based interception (trampoline + dispatcher + per-call Lua marshal); `statement` is static-bytes modification (bytes rewritten, CPU executes natively, zero per-call cost). Different composition rules (`hook_chain` for hooks, `conflict_engine` for byte overlap), different debug stories (per-call breakpoints vs static bytes in memory), different cost shapes. Authors pick by intent; the surface reflects engine reality honestly.

10. **Industry-standard meaning of `hook` is preserved.** Across SKSE / F4SE / Frida / Cheat Engine / MinHook / detours, `hook` means callback-based function interception. kcdx uses `kcdx.hook.*` for exactly that meaning — no surprises for authors coming from any of those ecosystems. Static-bytes modification gets its own namespace (`kcdx.statement.*`) named for what it is, rather than overloading `hook` to mean something it has never meant elsewhere.

## As-built status + governance (read this before "continuing in order")

**Built + live-verified:** Phase 1 (launcher, paths) and Phase 2 subs 1–9 + the AP12 disassembler-test batch + the multi-file-plugin feature + `kcdx.command` + the per-entry-zone (both-phase) feature + `kcdx.code` + `kcdx.cosave.*` + `kcdx.scan` + `zone_gate` (the `kcdx.hook` surface with all six modes — before/after/around/replace + mid + callsite — chaining, locators incl. address_id-by-name; the name-carries-the-ABI `target=` form; `kcdx.on` lifecycle bridge + `ready`; `kcdx.publish` cross-plugin pub/sub; plugin-scoped `require` + complete source attribution; `kcdx.log.*`; C++ `ResolveAddressByName`; `kcdx.command` + `kcdx.console.execute` over the proven console path; the both-phase execution model — deferred-then-immediate command arming, load-order-priority `lua`/`lua_after` entrypoints, `kcdxPlugin_PostGameLoad` C++ export, kcdx-owned `require`; `kcdx.code` trampoline allocation over the pool + symbol table; `kcdx.cosave.*` save/load persistence with a name-derived UID + string-tag records — interface Version 1→2, CAP-12 migrated to the named path; `kcdx.scan` diagnostic-AOB-validation verb over `scan_engine::ResolveScan` with per-match module attribution; `zone_gate` per-API `requireZone` capability gating with the two-flag `userEnabled && engineAccepted` model, single-gate single-checkpoint enforcement, the `kcdx.plugin.is_rejected` accessor, and PLUGIN_REJECTED teaching log line per plan:165-168 shape). Suite verified per feature checkpoint (per `test-plugins/README.md`); `cap-04-c` is the one known pre-existing FAIL (legacy mid-hook auto-skip — the new `hook_chain` did NOT inherit it; CAP-21-skip proves the fresh dispatcher is clean), the rest are standing `[manual]`/in-game rows. The authoritative sub-by-sub ledger is the Phase 2 section below; per-row live evidence is `test-plugins/README.md`; chronology is `git log`. **Phase 2 is now COMPLETE — all seven core verbs (`kcdx.hook`/`bytes`/`code`/`on`/`command`/`publish`/`scan`) + the `kcdx.*` domains + `docs/lua/` reference + `zone_gate` capability gating + the `kcdx.plugin.*` introspection domain are DONE.**

**Phase ledger (2026-05-28 audit):**

| Phase | Status |
|---|---|
| 1 — launcher exe + paths | **DONE** (live-verified) |
| 2 — Lua API skeleton (7 core verbs + domains + `docs/lua/` + zone_gate + `kcdx.plugin.*`) | **DONE** |
| 3 sub-1 — `kcdxHookInterface` v1 + `Kcdx.h` wrapper + sig-mismatch gate | **DONE** (`cdd5e7a` / `b5e548a` / `d5c3314`) |
| 3 sub-2 — `kcdxBytesInterface` | **DONE** (`2b2e6f5`) |
| 3 sub-3 — `kcdxTrampolineInterface` v2 (extends raw pool floor with `Allocate`+`Export`, the `kcdx.code` C++ mirror) | **DONE** (`38f9dd5`) |
| 4 — migrate test suite + engine builtin | **DONE for plugins** (corpus migrated; audit confirms 0 legacy behavior tables in production manifests). **bugsplat builtin DLL is BLOCKED on Phase 11** (manifest-only stub ships today). |
| 5 — delete old TOML behavior parsers | **DONE** (`95854fe`) |
| 6 — probe code cleanup (narrow subset) | **DONE** (`3f66c47`) |
| 7 — zone-rework subset | **DONE** (`54d7d4d`) |
| 7 — `before_game` doc widening (round-2 decision 4 from init-cycle-ownership) | **DONE post-step-4** (`9264d6a` + `43a9c14`) |
| 7 — `authored_against_game_version` / `on_changed_function` manifest fields | **SUPERSEDED 2026-05-27** by §11.8 STREAMLINE three-track model. Original spec WILL NOT BE BUILT; replacement is `kcdx.declare` (under Phase 9.2). |
| 8 — ASI-loader cleanup (docs) | **DONE** (2026-05-26) |
| 8.5 — Asset replacement (pak overlay) | **NOT STARTED** (8.5a partial: `CCryPak_FOpen` named in refdb + observe-only FOPEN probe live; PRODUCTION asset-overlay map / hook / Lua surface NOT BUILT) |
| 9 — High-level Lua surface (player.health/.position + inventory.add + stubs) | **NOT STARTED** |
| 9.1 — SQLite reference DB + lookup primitive + per-plugin verification cache | **DB + ENGINE CONSUMER DONE** (`address_names` + `address_versions` schema ships; refdb owns the cache, commit `498934c`; `refdb::ResolveByName/ResolveById` is the lookup primitive — `hash_at` is not a separate symbol). `version_check.bin` cache plumbing ships; production FEED awaits Phase 9.2's `kcdx.declare`. |
| 9.2 — **UNIFIED named-target surface**: `kcdx.declare` (Track-2 author entries) + smart-resolver sub-verb shape `kcdx.<verb>.<name>.<mode>` on hook/bytes/code over the unified table (curated refdb rows + author declarations, same lookup table, same `__index` resolver, routed through the existing owner-aware `address_library::ResolveByName`) + C++ mirror + `kcdx_scan` console command (in-game iterative AOB discovery — the discover-then-declare loop is gated behind it). `kcdx.scan` is excluded from the smart-resolver shape by design (it PRODUCES inputs to the table, doesn't consume them). | **CURATED SUBSTRATE DONE** (refdb cache, commit `498934c`); **DECLARE STORE + SMART RESOLVER + SUB-VERB SURFACE + C++ MIRROR DONE** (commits `2dac79b` declare store + smart resolver + sub-verb + C++ mirror; `1c01c9d` engine-direct AP4 carve-out unblocking cap-59-fires + cap-64 C++ peer + cap-65 classifier-bootstrap regression row; cap-28 Lua bytes smart-resolver + cap-59 Lua hook smart-resolver + cap-62 C++ declare interface + cap-63 C++ bytes wrapper + cap-64 + cap-65 all PASS); **`kcdx.scan{...}` Lua diagnostic DONE** (`src/lua_bind_scan.cpp`); **`kcdx_scan` CONSOLE COMMAND NOT BUILT** (the explicit `_scan` in-game iterative discovery form remains — author-facing console verb distinct from the `kcdx.scan{...}` Lua diagnostic). `src/survival_pass.{cpp,h}` machinery is built but it's the **curated-track** safety mechanism only (per §11.8.3); Track-2 declared entries do not feed survival_pass, they go through the badge / recovery-rollback path. |
| 9.3 — `kcdx.hook.*` / `kcdx.statement.*` split + `kcdx.locator.*` / `kcdx.op.*` + multi-region trampoline | **NOT STARTED** |
| 9.4 — `kcdx.find{...}` + `kcdx_dev_inspect` console | **NOT STARTED** |
| 9.5 — `kcdx.behavior.*` named-behavior catalog | **NOT STARTED** |
| 9.6 — `kcdx.bytes` narrowing + rule 4/4a update + final migration | **NOT STARTED** |
| 9.7 — Curated-target sub-verb resolver | **MERGED into Phase 9.2** (2026-05-28) — declare + smart resolver were two halves of one surface (a named-entry table populated from curated refdb + author declare; smart resolver over that table); landing them sequentially would have shipped a transitional UX. |
| 10 — `[[event]]` → `kcdx.on(...)` event catalog | **LIFECYCLE EVENTS DONE** (`messaging.cpp` wires every save/load/post-load/input-loaded/etc.); **GAMEPLAY EVENT CATALOG NOT STARTED** (the 10–15 NEW gameplay events damage_taken / dialogue_line_spoken / item_picked_up / etc. are NOT RE'd or hooked) |
| 11a — FIX A shim integration | **NOT STARTED** (depends on `_research/phase8-fix-a/` RE — ~38% RVAs mapped at last writing) |
| 11b — Force-load WHGame.dll from kcdx.dll DllMain | blocked on 11a |
| 11c — Lua VM startup via shim | blocked on 11a |
| 11d — Drop static Lua | blocked on 11a–c |

**Substantive next-pickups (per the table above):** **Phase 9.2 residual — `kcdx_scan` console command** (in-game iterative AOB discovery; the discover-then-declare loop is gated behind it; the `kcdx.scan{...}` Lua diagnostic equivalent already ships) | **engine-direct hook migration — 5 remaining sites** (`frealloc` canary, `ModManager_ctor`, `MiniDmpSender` ctor, `SaveGame`, `LoadGame`; engine machinery + canonical first site `engine.lua_pcall` shipped at commit `1c01c9d`; each remaining site is one `/execute` cycle paired with Lua + C++ test plugins per `lua-api-surface.md` parity — full spec at [`../../tech-debt/TD-0003-engine-direct-hook-migration.md`](../../tech-debt/TD-0003-engine-direct-hook-migration.md)) | Phase 8.5 asset overlay (independent; high user-visible leverage) | Phase 9 high-level Lua surface (independent; pure RE + binder work). Each is one `/feature` cycle except the engine-direct sites which are one `/execute` cycle each; pick by leverage. Phase 11 stays blocked on the FIX A RE.

**Governance that POSTDATES the original plan prose — these RULES win where the prose below conflicts:**
- `.claude/rules/lua-api-surface.md` — the authoring surface (Lua AND C++) is a **learnable sublanguage**; one `kcdx` global, core verbs top-level + grouped domains, configuring=`{table}`/doing=positional, and **full Lua↔C++ feature parity** (invariant on the shipped product; restructure builds Lua-first then backfills C++ per-phase).
- `.claude/rules/results-driven.md` + `anti-patterns.md` AP10 — test/probe a checkable unknown before changing code; theory-independent probes.
- `anti-patterns.md` AP11 — plugin-facing interface structs are **append-only** (insert mid-struct → pre-built plugins AV on load).
- `.claude/rules/hook-engine.md` — `kcdx.hook` conflicts resolved by `hook_chain`/load-order (supersedes the legacy "first-wins, chained is v0.2" line for the new surface).
- `docs/outstanding-work/smart-replace-conflict-detection.md` — implementation-grade spec for the footprint-based conflict upgrade (future; `hook_chain` is built footprint-ready).
- `docs/outstanding-work/ready-event-and-handle-assert.md` — the `kcdx.on("ready")` post-apply event (future; unblocks deferred handle:applied() asserts).
- **Recovery + rollback for Track-2 plugins on undeclared game versions** — LOAD-BEARING for the streamlined per-version survival model (`docs/outstanding-work/parallel-ghidra-research.md` §11.8). The pre-version-detection survival model (tenets 6/7/8 + Phase 7/9.1 below) assumed the engine could pre-check every hooked function's hash and refuse-to-install on drift. The streamline drops that ambition (the bulk auto-tracking that backed it was infeasible — see §11.8) and replaces "prevent the unsafe install" with "survive the unsafe install gracefully + roll back." Default-ON "attempt on undeclared versions" is safe ONLY IF: (a) install-time failures (pattern doesn't resolve, ABI mismatch, sanity check fails) cleanly roll back any partial installation for the offending plugin and leave the game state clean; (b) runtime callback failures (wrong args, crash inside a Lua/C++ callback) are caught at the boundary, disable the offending plugin for the rest of the session, and the game keeps running. NOT in place today — `conflict_engine` + apply-zones exist but the rollback-on-partial-install path + SEH-bracketed callback dispatch + the auto-disable-and-badge mechanism are unbuilt. Default-ON shipping waits on this. Belongs alongside the §11.8 streamline; spec to live in `docs/outstanding-work/track2-recovery-rollback.md` (not yet written).

**RESOLVED — `[plugin].name` + `[plugin].author` enforcement at discovery + 2-dot corpus migration.** Originally tracked as the 1-dot enforcement follow-up from the author-targets feature; superseded by the 2-dot namespace refactor (`naming-namespaces.md`). End-state landed 2026-05-23: `ValidatePluginName` (charset `[a-z0-9_]`, length 2–128) AND `ValidateAuthorName` (same shape, same length, rejects the reserved `kcdx` engine root) are now both wired into `config.cpp` `ParsePluginManifest` as HARD load-rejections. The full plugin corpus has been re-migrated to the 2-dot `<author>.<plugin>.<bare>` model: all 39 test plugins under `[plugin].author = "ts"` + valid bare `[plugin].name` (e.g. `ts.cap_01_patch`, `ts.comp_09_pubsub_a`), the builtin under `[plugin].author = "kcdx_builtin"`, hello-plugin under `[plugin].author = "violetanvil"`. The 1-dot enforcement (commit `cf9053f`) was the stepping stone; the 2-dot refactor is the end-state. See CAP-34 in [`../../test-plugins/README.md`](../../../test-plugins/README.md) for the model's regression coverage.

## The model

### Single global ordered list, two zones, one sentinel

```
== before_game zone ==
[ kcdx_builtin.bugsplat_filename_fix ]  zone=before_game  priority=30   dll
[ some_author.early_bytes            ]  zone=before_game  priority=50   lua  (phase 11+)
[ kcdx_builtin.engine_builtin_foo    ]  zone=before_game  priority=70   dll

─── game.exe ───────────────────────────────────────  (immovable sentinel)

== after_game zone ==
[ ts.cap_04_midhook                  ]  zone=after_game   priority=10   lua
[ some_author.tweak_mod              ]  zone=after_game   priority=50   lua
[ some_author.complex_mod            ]  zone=after_game   priority=50   dll+lua  (both entrypoints)
[ some_author.late_mod               ]  zone=after_game   priority=90   lua
```

**Sort key:** `(zone asc, plugin_priority asc, plugin_name asc)`. Lua and DLL plugins interleave by priority within their zone. Ties broken alphabetically.

**Priority semantics:** 0 = earliest in zone, 100 = latest in zone, 50 = middle (default). Sparse range gives user / author room to insert "definitely before X" without renumbering.

**Enabled flag** is the single toggle (per [`.claude/rules/loader-architecture.md`](../../../.claude/rules/loader-architecture.md) work shipped 2026-05-21 in commit `4c0bcab`). The `.disabled` folder-rename mechanism is already gone.

### Zones and what runs in them

| Zone | When | What can run |
|---|---|---|
| `before_game` | Between kcdx.dll DllMain and WHGame.dll DllMain. LDR notification fires for any DLL mapped here. Phase 11+ adds: kcdx.dll force-loads WHGame.dll synchronously inside DllMain, fires the LDR notification, then spins up Lua VM via WHGame's compiled luaL_newstate before any before_game plugin runs. | Phases 1-10: DLL plugins only (Lua VM not started until first-update-tick). Phase 11+: DLL plugins AND Lua plugins (VM is up before any plugin runs). |
| `after_game` | After WHGame.dll's DllMain completes, threaded into first-update-tick orchestration. | Everything. |

`before_game` is a TIMING window, not a target-DLL gate. The LDR notification mechanism that drives the window (`src/ldr_notify.cpp::ApplyEntriesForModule`) applies a resolved before_game patch to **any** DLL mapped during it — WHGame.dll, other game-bin DLLs, third-party preloads — not just WHGame.dll. WHGame.dll's DllMain is the canonical timing anchor for *when* the window closes; the window's patches themselves may target any module mapped between kcdx.dll's DllMain and that close. The bugsplat-filename-fix builtin is the standing example: it declares `zone = "before_game"` but targets BugSplat64.dll, applied by the LDR notification when BugSplat64.dll maps. Wire-diagram steps below that name "WHGame" in an LDR-notification sequence step are describing the WHGame-mapping event specifically (the Phase 11+ force-load triggers it), not asserting LDR-window-targets-WHGame-only.

A plugin declares its zone in `kcdx.toml` `[load_order].zone`. If omitted, kcdx defaults to `after_game` (the conservative choice — most plugins want WHGame alive when they run). **As-built today** (Phase-7 zone-rework subset, DONE): the per-plugin manifest parser (`src/config.cpp` `ParsePluginManifest`) reads `[load_order].zone` + `[load_order].priority` from the plugin's own doc, populating the internal `Manifest.defaultPosition` / `Manifest.defaultPriority` fields (the internal field names were left unchanged; only the TOML keys read FROM were renamed). The legacy `[plugin].default_position` / `[plugin].default_priority` keys are NO LONGER read (HARD rename, no transitional fallback — consistent with the prerelease fix-forward no-WARN stance). This per-plugin `[load_order]` table is parsed by `ParsePluginManifest`; the engine-wide override file `kcdx-engine/load_order.toml`'s top-level `[[plugin]]` rows are read by a SEPARATE parser (`load_order.cpp::Read`) — different files, no collision. (Before the rename, the per-plugin key was `[plugin].default_position`, and `[load_order]` was reserved for the override file only — that was the trap the COMP-13 fixture hit before this footnote was added; the rename closed it.)

A Lua plugin trying to declare `zone = "before_game"` in Phases 1-10 produces a manifest-load error ("Lua-in-before_game requires phase 11+ FIX A shim + early-VM startup"). Phase 11+ removes that constraint.

## The new TOML manifest shape

`kcdx.toml` collapses to ~25 lines of pure metadata. Every previous behavior-declaring table-array (`[[patch]]`, `[[hook]]`, `[[mid_hook]]`, `[[trampoline]]`, `[[scan]]`, `[[command]]`, `[[event]]`) is removed.

```toml
# kcdx.toml — every plugin (Lua, DLL, or both) declares one.

# ---- Identity ----------------------------------------------------------
[plugin]
name              = "violetanvil.outfit-swap"     # required. Unique stable ID.
display_name      = "Outfit Swap In Combat"
author            = "violetanvil"
version           = "1.0.0"                        # semver string.
description       = "..."
summary           = "Allows outfit swap in combat; intercepts IsInCombat and returns false."
                                                   # NEW: one-line, user-facing summary of what
                                                   # this plugin DOES. Shown in the launcher UI
                                                   # plugin list and in the kcdx_list_plugins
                                                   # console output. Recovers the "cat kcdx.toml
                                                   # to know what a plugin does" surface that
                                                   # was lost when behavior moved to plugin.lua.
                                                   # Optional but strongly encouraged for any
                                                   # publicly-distributed plugin.
url               = "https://..."
support_email     = "..."
license           = "MIT"                          # optional, free-form.

# Engine compatibility
kcdx_min_version  = "0.2.0"
supports          = ["1.5*"]                        # game-version patterns; trailing-* prefix
                                                   # wildcard, matched against the running
                                                   # KCD2 version. Empty/absent = any version
                                                   # (version-independent, e.g. ResolveAddress
                                                   # plugins pin no build).

# Per-plugin log level floor
log_level = "info"                                 # trace|debug|info|warn|error|off

# ---- Dependencies on other plugins ------------------------------------
[[plugin.dependencies]]
name        = "other.lib"
min_version = "0.3.0"
optional    = false

# ---- Load-order author hints ------------------------------------------
[load_order]
zone     = "after_game"        # "before_game" | "after_game"
priority = 50                  # 0..100 (sparse; 0=earliest, 100=latest)

# ---- Entrypoints — what kcdx loads to run this plugin -----------------
# At least one of `lua` or `dll` must be present. If both: DLL Preload
# fires in plugin's slot, then DLL Load + Lua entrypoints fire in plugin's
# slot. Multiple Lua files allowed (run in declared order).
[entrypoints]
lua = ["plugin.lua", "scripts/extras.lua"]
dll = "bin/my-plugin.dll"

# ---- Test-suite gating (existing, retained) ---------------------------
[kcdx]
test_suite_only = true                             # gated off in non-dev mode
```

### Capability gating

Capability gating runs at plugin-init time (when the plugin's plugin.lua/DLL Load fires). It is NOT a parse-time vector scan over declared entries (because there are none in TOML anymore). Each kcdx.* API call is annotated with its `requireZone`:

| API | requireZone | Why |
|---|---|---|
| `kcdx.bytes(...)` | Either | Pure VirtualProtect + memcpy. |
| `kcdx.hook(...)` | After (phases 1-10), Either (phase 11+) | Needs MinHook + JIT (works in either zone since MinHook is initialized in DllMain). The Lua-callback variant additionally needs the Lua VM; Phase 11+ has the VM up in DllMain (via force-load + FIX A shim + WHGame.luaL_newstate), so all kcdx.hook modes work in before_game from Phase 11. |
| `kcdx.code(...)` | Either | Trampoline allocation works pre-game. |
| `kcdx.command(...)` | Either | Registration is queued. The actual `IConsole::AddCommand` call is deferred internally until `gEnv->pConsole` becomes available (kcdx::console::Init runs during the worker-thread phase). From the author's perspective, `kcdx.command(...)` from a before_game plugin "just works" — the engine handles the timing. |
| `kcdx.cosave.*` | Either | Registering callbacks (set_uid, on save_game, on post_load_game) doesn't require the save system to be alive — just the message bus, which is up from kcdx.dll DllMain onward. Actual save/load happens at event-fire time, which is always after_game by definition. |
| `kcdx.on(lifecycle_event, ...)` | Either | Just registers a callback; cheap. |
| `kcdx.publish(event, ...)` | Either | Lua-native pub/sub layer over the existing subscriber registry; just stamps `<author>.<plugin>.<event>` and fires subscribers in registration order. No subsystem dependency. |
| `kcdx.scan{...}` | Either | Reads executable sections of an already-loaded module to validate an AOB pattern. WHGame.dll is mapped from kcdx.dll DllMain onward, so the scan path is reachable from either zone. |

**The deferred-registration pattern**: where an API can be CALLED early but its work has to happen later, kcdx handles the deferral internally rather than forcing authors to learn timing details. The principle: **the plugin author calls the API at any time; the engine ensures the work happens when it CAN happen.** This applies to `kcdx.command` (deferred until pConsole alive), `kcdx.cosave` (deferred until save system alive), and any future "needs subsystem X" API. Authors never need to know when `gEnv->pConsole` is populated; they just call `kcdx.command`.

**A capability/zone mismatch is a HARD error, not a downgrade.** If a plugin's `[load_order].zone = "before_game"` but its plugin.lua or DLL calls an API that requires `requireZone = After` (in the current engine version), the plugin is **refused to load**. Its registrations are discarded; its handles never apply; the log emits a clear engine-side error line:

```
[ERROR][engine][PLUGIN_REJECTED] plugin 'author.modname' rejected: declared zone='before_game' but
calls kcdx.hook (requires zone='after_game' in kcdx 0.2.0). Change [load_order].zone to
'after_game' in this plugin's kcdx.toml to fix. Plugin will not load until manifest is fixed.
```

The launcher (when shipped) consults each plugin's manifest-declared zone PLUS the engine's static capability table (per-API requireZone) and prevents the user from selecting an illegal zone in the UI. Hand-edited misconfigurations still get the engine-side rejection; no silent downgrade in any path.

**The `kcdx.on("ready", ...)` event semantics (single name, per-plugin routing):**

There is ONE event name, `"ready"`. The engine routes it per-plugin based on the plugin's own declared zone:

- A `before_game` plugin's `kcdx.on("ready", ...)` callback fires at the end of the before_game apply pass.
- An `after_game` plugin's `kcdx.on("ready", ...)` callback fires at the end of the after_game apply pass.

Authors write `kcdx.on("ready", ...)` regardless of which zone they're in; the engine handles the routing. This is the same name in both directions because the meaning is the same to the plugin author: "my registered hooks/bytes/etc. are now applied; do post-setup work here."

### Error isolation — one plugin's mistake doesn't break the engine

**Every plugin runs inside a guard.** Lua plugins: `plugin.lua` execution is wrapped in `lua_pcall`. C++ plugins: `kcdxPlugin_Preload` / `kcdxPlugin_Load` execution is wrapped in `crash_guard::Call` (existing kcdx primitive). If a plugin's entry-point code throws, panics, or causes any guard-trappable fault, the engine:

1. Logs the error to **the plugin's own log file** (`plugins/<name>/logs/<name>_<ts>.log` or `engine/builtin/<name>/logs/<name>_<ts>.log`) — the author's first stop when their plugin breaks.
2. Logs the SAME error to the **engine log** under the plugin's name so users debugging "why didn't my mod load" see it without digging into per-plugin logs.
3. **Disables the plugin for the rest of this session** — its registrations are discarded; subsequent kcdx.* API calls from that plugin's lingering callbacks are rejected; the plugin is marked `loaded = false` in the enumeration surface so other plugins querying for it see it didn't load.
4. **Continues loading other plugins.** A bad plugin doesn't take down the engine or any other plugin.

This applies uniformly: typos like `kcdx.huk(...)` (undefined function → Lua error), divide-by-zero, infinite loops (caught by a watchdog timer if a callback runs too long), AV crashes inside a plugin DLL — all isolated to the offending plugin. The engine never propagates a plugin's failure to other plugins or to the game itself.

The Lua API call validation (locator format check, signature parse, zone check) ALSO logs to both the plugin's log and the engine log when it fails, with the call-site (file:line for Lua, caller info for C++) named in the error.

### Plugin introspection — `kcdx_list_plugins` console command

The manifest-only TOML costs us the "cat kcdx.toml to know what a plugin does" debugging path. Users who hit a problem (mod conflict, game behaves weirdly) need a way to see what's installed and what each plugin TOUCHES — without reading source.

`kcdx_list_plugins` is a built-in console command (always available, not test-suite-gated) that dumps every loaded plugin with:

- `name` (stable ID from manifest)
- `display_name` and `version`
- `summary` (the new field above)
- `zone` and effective `priority`
- `enabled` state
- Number of registrations by kind: `(3 hooks, 1 byte rewrite, 2 commands, 5 cosave records, 1 asset overlay)`
- For each hook: the resolved target address (from its locator — address / address_id / target_symbol / pattern) and mode

This is the engine's live introspection of every plugin's registered intent. It uses the same data the conflict_engine has after the apply pass; surfacing it is cheap. A user pasting the output into a bug report gives the maintainer instant visibility into what's installed.

The future launcher UI consumes the same data structure (probably via a CLI flag like `kcdx.exe --list-plugins-json`) to power its "what does this plugin do" panel.

## load_order.toml — the user-override manifest (unchanged shape from today)

Lives at `<game-bin>/engine/load_order.toml` (after Phase 1 the engine folder is renamed from `kcdx-engine/` to `engine/`). Launcher writes; kcdx reads. Plugins not listed → use the manifest's `[load_order]` block. Missing fields on a row → fall back to manifest defaults.

**Precedence rule (explicit): the user's load_order.toml ALWAYS WINS over the plugin's manifest hint.** A plugin author sets `[load_order].zone = "after_game" priority = 30` in their kcdx.toml as a default suggestion; the user can override by writing `[[plugin]] name="author.modname" zone="after_game" priority=90` in load_order.toml and the user's `priority = 90` is what the engine uses. This applies field-by-field: if load_order.toml sets `zone` but not `priority`, the user's `zone` overrides + the plugin's `priority` carries through. `enabled` follows the same rule (default true via manifest absence; user can flip to false).

```toml
[[plugin]]
name     = "kcdx_builtin.bugsplat_filename_fix"
zone     = "before_game"
priority = 30
enabled  = true

[[plugin]]
name     = "some_author.tweak_mod"
zone     = "after_game"
priority = 50
enabled  = true
```

## The Lua API surface

This is the author-facing surface. Every former TOML entry-type collapses into a function call. The Lua API is the *primary* author surface; the C++ DLL API mirrors it (full feature parity — see `.claude/rules/lua-api-surface.md`).

> **As-built note (sub-1..4b).** The `kcdx.hook` surface below is what shipped and is live-verified (test-plugins/cap-20-hook-modes, 9/9). It SUPERSEDES the older sketch in earlier drafts of this doc: the callback surface is **mode-as-key + mutate-by-return** (not `mode="..."` + an `args` table with `:get()/:set()`), and the locators are **address / address_id (name or number) / target_symbol / pattern** (the `function_name = "Module.dll!Export"` locator was DROPPED — raw/mangled names are unusable UX). The naming + call-shape conventions are now a checked-in rule: `.claude/rules/lua-api-surface.md`.

### Naming and lifecycle

- One global, lowercase `kcdx`. The legacy uppercase `KCDX` global (`KCDX.ScanAndWrite` etc. from [src/lua_bind.cpp](../../../src/lua_bind.cpp)) is deleted. Surface shape per `lua-api-surface.md`: core authoring verbs are top-level (`kcdx.hook`, `kcdx.bytes`, `kcdx.code`, `kcdx.on`, `kcdx.command`, `kcdx.scan`); everything else is a grouped domain (`kcdx.log.*`, `kcdx.memory.*`, `kcdx.addr.*`, `kcdx.test.*`, `kcdx.cosave.*`, gameplay `kcdx.player.*` etc.). Call shape: configuring something → `{named table}`; doing something → positional args.
- A plugin's `plugin.lua` runs at the plugin's slot in the unified ordered list. By default that's after WHGame.dll init (after_game zone). Phase 11+ enables before_game timing.
- **API calls queue intent.** `kcdx.hook{...}` returns a handle immediately; the actual install happens at the end-of-zone apply pass (`lua_registry::ApplyZone`) after every plugin in the zone has registered — so conflict resolution sees all intent before any byte changes. The handle exposes `:applied()` (nil = Pending, true = Applied, false = Failed), `:reason()`, `:name()`.
- **Acting on apply outcomes is via the `ready` event (NOT YET BUILT).** A plugin that needs to run code once its hooks are live — or assert `handle:applied()` — registers `kcdx.on("ready", fn)`, fired after the plugin's zone apply pass. This is tracked as outstanding work (`docs/outstanding-work/ready-event-and-handle-assert.md`); `handle:wait_applied()` is a stub until then. (The earlier "each plugin.lua runs in its own coroutine + `wait_applied()` yields" design is deferred; not built.)

### Function reference

```lua
-- ---- Zone declaration ----
-- DROPPED. Zone is declared in kcdx.toml's [load_order].zone, never at runtime.
-- The runtime API has no kcdx.zone() function. The engine reads zone from the
-- manifest at plugin discovery and applies the requireZone capability check
-- against every kcdx.* API call automatically. Authors set zone ONCE in TOML
-- and forget about it.

-- ---- Logging --------------------------------------------------------
kcdx.log.info  (category, fmt, ...)
kcdx.log.warn  (category, fmt, ...)
kcdx.log.error (category, fmt, ...)
kcdx.log.debug (category, fmt, ...)              -- dev-mode-only
kcdx.log.trace (category, fmt, ...)              -- dev-mode-only

-- ---- kcdx.bytes  (succeeds [[patch]]) ----
local h, err = kcdx.bytes{
    name        = "outfit_swap_in_combat",
    description = "Allow outfit switch in combat",
    pattern     = "48 81 C1 60 0B 00 00 ...",   -- OR address_id=, OR target_symbol=
    module      = "WHGame.dll",
    offset      = 13,
    original    = "44 8A F0",                    -- optional verify
    replacement = "45 31 F6",
    idempotent  = true,
    context     = "...",                          -- optional context pattern
    anchor_string = "...",                        -- optional anchor
}
-- (entry-level `priority` is NOT honored — load order is the single
--  source of truth; see "Load order" + .claude/rules/lua-api-surface.md.)

-- ---- kcdx.hook  (succeeds [[hook]] + [[mid_hook]] + dynamic_hook) ----
-- Mode-as-key: you attach the callback under the MODE NAME itself
-- (before/after/around/replace), one mode per call. Params arrive as
-- POSITIONAL callback args named by your own function(...) list (the
-- signature supplies TYPES). Mutation is by RETURN — "what you return is
-- what flows forward". You write the target function, in Lua.
local h = kcdx.hook{
    name       = "outfit_gate",
    -- locator (exactly one): address (raw VA/pointer), address_id (a
    -- readable Address Library NAME or numeric id), target_symbol,
    -- pattern. (function_name is NOT a locator — dropped; use address_id
    -- by name. target_callsite is for mode="callsite" below.)
    address_id = "IsInCombat",                    -- by readable name (or address_id = 1234)
    signature  = "bool (ptr self)",
    before     = function(self)
        -- massage inputs; return the (possibly changed) args. `before`
        -- ALWAYS runs the original — to skip it use replace/around.
        return self
    end,
}

-- before  : return nothing → original runs unchanged; return values → they
--           replace the args the original receives.
-- after   : after = function(retval) return changed end  (mutate the return)
-- replace : replace = function(self) return false end     (original never runs)
-- around  : around  = function(orig, self) return orig(self) end  (wrap: call
--           the original 0/1/N times via the `orig` callable, transform inline)

-- ---- kcdx.hook mode="mid"  (mid-function capture; succeeds [[mid_hook]]) ----
-- NOT YET BUILT (later sub). Capture register/memory state at an offset
-- inside a function. Shape (subject to the as-built design when it lands):
--   kcdx.hook{ address_id = "...", mode = "mid", offset = 0x42,
--              captures = { "r14b", "[rcx+0x10]:i32" },
--              before = function(caps) ... end }

-- ---- kcdx.hook mode="callsite"  (single-callsite redirect; design-gap #1) ----
-- NOT YET BUILT (later sub). Redirect ONE specific call instruction
-- (rewrite its rel32) while every other caller of the same function is
-- unaffected. Locator is target_callsite (pattern / address_id / rva).

-- ---- kcdx.code  (succeeds [[trampoline]]) ----
local r = kcdx.code{
    name   = "outfit_gate_logic",
    bytes  = "48 83 EC 28 ...",                   -- optional initial contents
    size   = 256,                                  -- defaults to bytes-length, NOP-tail
    pool   = "branch",                             -- "branch" (±2GB) | "local"
    export = "violetanvil.outfit_gate_logic",      -- optional symbol table publish
}

-- ---- kcdx.on  (succeeds [[event]] + kcdxMessage_*) ----
kcdx.on("post_load",         function() end)
kcdx.on("post_post_load",    function() end)
kcdx.on("input_loaded",      function() end)
kcdx.on("new_game",          function() end)
kcdx.on("pre_load_game",     function() end)
kcdx.on("post_load_game",    function() end)
kcdx.on("save_game",         function(basename) end)
kcdx.on("load_game_selected",function(basename) end)
kcdx.on("delete_game",       function(basename) end)
kcdx.on("ready",             function() end)
kcdx.on("other.plugin:my_custom_event", function(payload) end)

-- ---- kcdx.publish  (cross-plugin pub/sub) ----
kcdx.publish("my_event", { x = 1, y = "hello" })

-- ---- kcdx.command  (succeeds [[command]]) ----
kcdx.command{
    name        = "outfit_dump",
    description = "Dump outfit state to log.",
    callback    = function(args)                  -- args is array of strings
        kcdx.log.info("CMD", "argc=%d", #args)
    end,
}

-- ---- kcdx.cosave  (save/load persistence) ----
kcdx.cosave.set_uid(0xC0FFEE01)
kcdx.on("save_game", function()
    kcdx.cosave.write("counter", 1, my_counter)
end)
kcdx.on("post_load_game", function()
    for tag, ver, data in kcdx.cosave.records() do
        if tag == "counter" then my_counter = data end
    end
end)

-- ---- kcdx.scan  (diagnostic-only, succeeds [[scan]]) ----
local addr = kcdx.scan{
    name = "find_outfit_swap",
    pattern = "48 81 C1 60 0B 00 00 ...",
}

-- ---- kcdx.address  (Address Library lookup; pointer userdata) ----
local p = kcdx.address(1234)

-- ---- kcdx.memory.*  (existing primitives, retained) ----
local p = kcdx.memory.pointer(va)
local b = kcdx.memory.module_base("WHGame.dll")
local a = kcdx.memory.scan_pattern("WHGame.dll", "48 8B ...")
-- (dynamic_hook is REMOVED; its job is kcdx.hook now)

-- ---- v0.2+ gameplay surface (incremental, stubs first) ----
kcdx.player.health      -- :get() / :set(n) / :add(n)
kcdx.player.position
kcdx.inventory:add("herb_x", 5)
kcdx.world.spawn("npc_id", {x=0, y=0, z=0})
kcdx.dialogue.replace(line_id, "new text")
kcdx.quest.set_stage(quest_id, stage_n)
```

### Signature syntax for kcdx.hook

```
"<return> (<arg>, <arg>, ...)"

return ::= primitive | "void"
arg    ::= [reg_or_slot:] type [name]

primitive ::= i8 | i16 | i32 | i64 | u8 | u16 | u32 | u64
            | f32 | f64 | ptr | bool | wstr | cstr | void
```

The signature supplies the TYPES the engine uses to marshal each arg/return to/from Lua. The author names the callback's parameters in their own `function(...)` list — names in the signature string (`wstr szApp`) are documentation; the binding is positional. Register-pinned args (`rcx: ptr`) override the default Win64 fastcall slot. `wstr`/`cstr` round-trip correctly (UTF-16↔UTF-8, with lifetime pinning); see the as-built marshaler in `src/hook_chain.cpp`.

### Hook modes (as built)

The callback is attached under the **mode name as the key**; you write the target function in Lua, params arrive positionally, and **what you return is what flows forward**.

| Mode | Callback shape | Semantics |
|---|---|---|
| `before` | `before = function(a, b, ...)` | Massage inputs. Return nothing → original runs with unchanged args; return N values → they replace the args. **`before` ALWAYS runs the original** (it never skips). |
| `after` | `after = function(retval)` | Observe / transform the result. Return a value → it replaces the original's return. |
| `replace` | `replace = function(a, b, ...)` | Original never runs; the return is the result. `replace = function() end` is the zero-overhead "suppress" (no dedicated mode needed). |
| `around` | `around = function(orig, a, b, ...)` | The full wrap: the callback BECOMES the function and receives the original as a callable `orig`. Call `orig(...)` 0/1/N times, transform its result inline, return the final result. The only mode that conditionally skips/wraps. Pays one extra Lua→C crossing per `orig()` call — intrinsic to the capability. |
| `mid` | (later sub) | Mid-function capture at `offset`; `captures` names register/mem reads. NOT YET BUILT. |
| `callsite` | (later sub) | Redirect one specific call instruction via `target_callsite`. NOT YET BUILT. |

One mode per `kcdx.hook` call. To put several modes on one target, make several calls — they chain (see "Chaining + conflicts" below).

### When to use each mode

- **`before`** — most common. "Inspect/massage the args, then the original runs." Return the changed args (or nothing to leave them). It cannot skip the original — that's `replace`/`around`.
- **`after`** — observe or transform the result. Receives the return value; return a replacement.
- **`replace`** — full override / suppress. `replace = function(...) return result end`; an empty body suppresses the call. The original never runs.
- **`around`** — **conditionally call the original at runtime, and/or transform its result inline.** Strictly more expressive than before+after+replace combined. The callback receives `orig` as a callable:

  ```lua
  -- cache wrapper: skip the expensive call when the result is known
  kcdx.hook{
      address_id = "LoadTexture", signature = "ptr (cstr path)",
      around = function(orig, path)
          local cached = my_texture_cache[path]
          if cached then return cached end          -- never call orig
          local result = orig(path)                 -- run original on miss
          my_texture_cache[path] = result
          return result
      end,
  }

  -- conditional gating: only run the original under certain game state
  kcdx.hook{
      address_id = "UpdateQuestStage", signature = "void (ptr quest, i32 stage)",
      around = function(orig, quest, stage)
          if kcdx.player.is_in_tutorial() then return end  -- skip
          orig(quest, stage)                                -- run normally
      end,
  }
  ```

  Without `around` you'd need two `before`+`after` hooks sharing state. `around` is the single-callback "wrap" pattern from AOP / Frida / SKSE.
- **`mid`** / **`callsite`** — not yet built (later subs); see the entries above.

### Chaining + conflicts (as built — `src/hook_chain.cpp`)

Multiple `kcdx.hook` calls on the SAME target **coexist** — one MinHook detour, an ordered chain of mode-tagged callbacks fired in **unified load order** (earlier-in-load-order first). This is the "plugin A hooks; plugin B piggybacks" capability, day-one. `hook_chain` is the mediator; **load order decides, full stop.**

When two hooks genuinely cannot coexist — incompatible signature on the shared thunk, or `replace`/`around` (treated as worst-case-exclusive in v1) — the **later-in-load-order** one is rejected: its handle goes `Failed` with a loud reason; the earlier wins. This is the safe-but-blunt v1 rule. Footprint-based smart coexistence (two replaces touching disjoint slots) is documented future work — `docs/outstanding-work/smart-replace-conflict-detection.md` (implementation-grade spec). (The earlier "v0.1 first-wins, chained hooks are v0.2" framing is superseded for `kcdx.hook`; that line in `hook-engine.md` now applies only to the legacy `[[hook]]` path.)

## The C++ DLL API surface

Mirrors the Lua surface function-for-function. Same names, same semantics. Sub-interfaces fetched via existing `kcdxInterface::QueryInterface`.

```cpp
enum kcdxInterfaceID {
    kcdxInterface_Messaging      = 1,   // existing
    kcdxInterface_Trampoline     = 2,   // existing
    kcdxInterface_Task           = 3,   // existing
    kcdxInterface_Scripting      = 4,   // existing
    kcdxInterface_Serialization  = 5,   // existing
    kcdxInterface_Memory         = 6,   // existing
    kcdxInterface_Console        = 7,   // existing
    kcdxInterface_Hook           = 8,   // NEW — kcdx.hook equivalent
    kcdxInterface_Bytes          = 9,   // NEW — kcdx.bytes equivalent
    // (kcdx.code is the high-level peer of the existing kcdxInterface_Trampoline
    // raw-pool floor — Allocate + Export were appended to kcdxTrampolineInterface
    // v2 rather than spawning a parallel interface. No new enum entry was added.
    // See the Phase 3 sub-3 ledger below for the extend-not-new-interface decision.)
};
```

`kcdxHookInterface::Install`, `kcdxBytesInterface::Write`, `kcdxTrampolineInterface::Allocate` (v2) take options structs that parallel the Lua opts tables exactly. Detour callbacks have the same `args/call_original` shape — kcdx's JIT thunk (`runtime_func_t`) is the dispatch surface for both Lua and C++.

The legacy `kcdxPluginVersionData` exported data block (from [src/plugin_loader.h:84](../../../src/plugin_loader.h#L84) — already replaced by `kcdx.toml` per the existing design) is fully removed. One source of identity: the manifest.

Plugin exports unchanged: `kcdxPlugin_Preload` and `kcdxPlugin_Load`. Both receive `const kcdxInterface*`. From within them, the plugin calls `api->QueryInterface(kcdxInterface_Hook, kcdxHookInterface_Version)` to install hooks.

### `Kcdx` ergonomic wrapper (ships with v0.2)

The core ABI stays QueryInterface-shaped — that's the versioning surface kcdx commits to. But every C++ plugin author would otherwise re-invent the same boilerplate (fetch every sub-interface, store handles, build a logger). We ship that wrapper IN-BOX so authors get the ergonomics by default. Header-only, lives in `include/kcdx/Kcdx.h` alongside `Interfaces.h`. Pattern matches the existing `kcdxLogger` (already shipped).

```cpp
// include/kcdx/Kcdx.h — header-only convenience wrapper
struct Kcdx {
    const kcdxInterface*           api  = nullptr;
    kcdxPluginHandle               self = kcdxInvalidPluginHandle;
    kcdxLogger                     log;

    // Pre-fetched sub-interfaces. Null if the engine doesn't ship them
    // (graceful degradation across kcdx versions). Check before use.
    const kcdxHookInterface*       hook       = nullptr;
    const kcdxBytesInterface*      bytes      = nullptr;
    const kcdxTrampolineInterface* code       = nullptr;  // kcdx.code peer (v2)
    const kcdxMessagingInterface*  messaging  = nullptr;
    const kcdxTaskInterface*       task       = nullptr;
    const kcdxTrampolineInterface* trampoline = nullptr;
    const kcdxScriptingInterface*  scripting  = nullptr;
    const kcdxMemoryInterface*     memory     = nullptr;
    const kcdxConsoleInterface*    console    = nullptr;
    const kcdxSerializationInterface* serialization = nullptr;

    // Call from kcdxPlugin_Load. Returns true if all required interfaces
    // resolved; false (with a log line) if something the plugin needs is
    // missing. Plugins can then return false from kcdxPlugin_Load to
    // abort their own load gracefully.
    bool Init(const kcdxInterface* a, const char* my_stable_name);
};
```

Then every C++ plugin's `kcdxPlugin_Load` reads naturally:

```cpp
static Kcdx K;

bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!K.Init(api, "violetanvil.outfit-swap")) return false;
    K.log.Info("INIT", "loaded; kcdx=0x%08x runtime_game=0x%08x",
               api->kcdxVersion, api->runtimeGameVersion);

    kcdxHookOptions opts = {};
    opts.name = "outfit_gate";
    opts.address_name = "IsInCombat";   // Address Library NAME (mirrors the
                                        // Lua address_id="name" locator; or
                                        // opts.address_id = 1234, target_symbol,
                                        // pattern). function_name is dropped.
    opts.mode = kcdxHookMode_Replace;   // C++ uses a mode FIELD (idiomatic);
                                        // Lua uses mode-as-key. Same concept.
    opts.signature = "bool (ptr self)";
    opts.callback = (void*)&AlwaysFalseDetour;
    K.hook->Install(K.self, &opts, nullptr);

    return true;
}
```

The wrapper ships in Phase 3 alongside the new sub-interfaces. Lives in `include/kcdx/Kcdx.h` (separate file so plugins can choose `#include <kcdx/Interfaces.h>` only if they want the raw ABI).

### Critical files for the C++ wrapper

- New: [include/kcdx/Kcdx.h](../../../include/kcdx/Kcdx.h) — the wrapper struct + inline `Init` impl
- Modified: [README.md](../../../README.md) — "writing a C++ plugin" example uses `Kcdx` not raw QueryInterface
- Modified: every DLL test plugin under `test-plugins/` and the bugsplat-filename-fix DLL — adopt the wrapper rather than hand-rolled QueryInterface calls

## Plugin discovery + lifecycle

### One walker, one ordered list

`config::WalkForTomls` keeps its current two-root walk: `engine/builtin/` first (first-party fixes) then `plugins/` (user plugins). Directory renames per Phase 1. What changes: `LoadOneFile` no longer parses any behavior. It parses `[plugin]` + `[entrypoints]` + `[load_order]` and stops. All seven behavior table-arrays are gone.

After every manifest is parsed:

1. `load_order::Read` reads `engine/load_order.toml` (user overrides).
2. `load_order::Resolve` computes each plugin's effective `(zone, priority, enabled)`.
3. `plugin_loader::TopoSort` resolves dependencies + builds one global ordered list sorted by `(zone, priority, plugin_name)`. Lua plugins and DLL plugins interleave by priority. No fixed wave order.

### Lifecycle by phase

**Phase 1-10 (no VM overhaul yet):**

```
kcdx.dll DllMain (loader-lock)
├── paths::Init
├── deferred-log buffer ready
├── walk: engine/builtin/ + plugins/ → g_manifests
├── load_order::Read + Resolve
├── plugin_loader::TopoSort → unified ordered list
├── BEFORE_GAME PASS (loader-safe):
│   for each enabled plugin with zone=before_game, in order:
│     - if dll: LoadLibraryW + kcdxPlugin_Preload
│     - if lua: ERROR ("Lua-in-before_game requires phase 11+")
│   register LdrRegisterDllNotification for not-yet-loaded modules
├── spawn worker thread

worker thread
├── log::Init (flush deferred buffer)
├── kcdx hooks: hooks::Install + save_load_hooks::Install + serialization::Init
├── LoadLibraryW each enabled after_game DLL plugin (Preload only)
├── kcdxMessage_PostLoad + PostPostLoad fire

first update tick
├── lua_State captured; RegisterKcdxTable (kcdx.* surface alive)
├── kcdxMessage_LuaReady fires
├── AFTER_GAME REGISTRATION PASS (unified order):
│   for each enabled plugin with zone=after_game, in order:
│     - if dll: kcdxPlugin_Load (DLL calls api->Hook->Install etc. — these QUEUE registrations)
│     - if lua: lua_dofile each [entrypoints].lua file (kcdx.hook/.bytes/etc. QUEUE registrations)
│   each plugin's API calls validate immediately (locator format, signature, zone) but
│   defer the actual install to the apply pass below.
├── AFTER_GAME PRE-FLIGHT:
│   conflict_engine walks every queued registration once, classifies conflicts,
│   produces the unified apply plan. ALL conflicts surfaced in one report.
├── AFTER_GAME APPLY PASS:
│   walk the apply plan in unified load order; install each entry via the existing
│   engine paths (patch_engine::ApplyResolvedPatch, hook_engine::InstallRuntime,
│   trampoline pools). For each entry, mark its handle :applied(). Errors during
│   apply log under the OWNING plugin's name + engine log; subsequent entries
│   continue applying (one bad entry doesn't break the wave).
├── "ready" event fires for every after_game plugin (handle:applied() now reflects truth;
│   plugins listening via kcdx.on("ready", ...) get their callback. The engine
│   routes the event to each plugin AFTER its own zone's apply pass finishes —
│   same callback name for before_game and after_game plugins, engine handles
│   the per-plugin timing).
├── kcdxMessage_InputLoaded fires (existing — public lifecycle event for game input).
```

**Phase 11+ (force-load + FIX A shim lands):**

```
kcdx.dll DllMain (loader-lock)
├── paths::Init
├── deferred-log buffer ready
├── walk: engine/builtin/ + plugins/ → g_manifests
├── load_order::Read + Resolve
├── plugin_loader::TopoSort → unified ordered list
├── Install before_game patches/hooks against already-loaded modules (ntdll, kernel32, dinput8 if present, kcdx.dll itself)
├── Register LdrRegisterDllNotification for WHGame.dll mapping
├── LoadLibraryW(L"WHGame.dll") — synchronously maps WHGame + whatever its dependency chain pulls in
│   ├── LDR notification fires for each newly-mapped module
│   │   ├── before_game entries declaring WHGame.dll apply when WHGame itself maps
│   │   └── before_game entries declaring other modules apply when the chain maps that module (e.g. bugsplat-filename-fix → BugSplat64.dll)
│   └── WHGame.dll DllMain runs (BugSplat init sees patched colon-free string)
├── kcdx::lua_shim::Resolve() — GetProcAddress every lua_* and luaL_* symbol from WHGame.dll
├── luaL_newstate (via shim) → lua_State (WHGame's compiled Lua does the allocation; kcdx just called the function earlier than CryEngine would)
├── RegisterKcdxTable in the new state (kcdx.* surface available)
├── BEFORE_GAME REGISTRATION PASS (unified order):
│   for each enabled plugin with zone=before_game, in order:
│     - if dll: kcdxPlugin_Preload + kcdxPlugin_Load (QUEUE registrations)
│     - if lua: lua_dofile each [entrypoints].lua file (QUEUE registrations)
├── BEFORE_GAME PRE-FLIGHT + APPLY:
│   conflict_engine walks before_game-zone registrations, classifies, applies in unified order.
│   handle:applied() now true for before_game entries. Errors logged per-plugin + engine log.
├── "ready" event fires for every before_game plugin (their handles are applied;
│   their kcdx.on("ready", ...) callbacks fire now).
├── spawn worker thread

worker thread + first-update-tick
├── kcdx engine hooks: hooks::Install (update; the lua_pcall hook becomes unnecessary
│   in Phase 11 because we already own the lua_State — keep as no-op or remove)
├── save_load_hooks::Install + serialization::Init
├── AFTER_GAME REGISTRATION PASS + PRE-FLIGHT + APPLY (unified order):
│   same shape as Phases 1-10 lifecycle above.
├── kcdxMessage_LuaReady fires (now a formality — VM was always up; this signals
│   "after_game phase reached"). Kept for SKSE-parity message catalog.
├── "ready" event fires for every after_game plugin (its handles applied).
├── kcdxMessage_PostLoad / PostPostLoad / InputLoaded fire as today.
```

The capture-game-Lua-state hooking (`HookedLuaPcall` in src/hooks.cpp:50) becomes unnecessary — we know the lua_State* from luaL_newstate. The hook can be removed (or kept as a sanity check, since CryEngine's calls into lua_pcall still need to come through SOMETHING — but they call into the same compiled symbol kcdx already resolved, so they hit WHGame's lua_pcall directly with no need for our interception).

### Plugin coroutines — Lua plugin's plugin.lua runs inside its own coroutine

> **NOT BUILT — deferred design (kept as the record for if/when we revisit).**
> As built (sub-4), each `plugin.lua` runs as a plain `luaL_loadfile` +
> `lua_pcall` chunk under `crash_guard` (`src/lua_plugin_loader.cpp`) —
> NOT inside a per-plugin coroutine, and `handle:wait_applied()` is a
> stub. The "act on apply outcomes" need is met by the `kcdx.on("ready",
> ...)` event (also not yet built — `docs/outstanding-work/
> ready-event-and-handle-assert.md`), the simpler first step. The
> coroutine/`wait_applied` model below is a richer future option, not the
> current contract; revisit only if straight-line "wait for X" ergonomics
> are demanded beyond what `kcdx.on` provides.

Every Lua plugin's `plugin.lua` executes inside a dedicated Lua coroutine (`coroutine.create` over the loaded chunk). The engine drives each plugin's coroutine in turn during the registration pass, resumes it when its handles become applied, and treats `wait_applied()` / similar yields as natural sync points.

**Why this matters at the engine level:**

- The engine's "registration pass" walks every plugin's coroutine ONCE in unified-load-order, runs it until either it returns (plugin.lua finished) or it yields (plugin.lua is waiting on something).
- The apply pass installs all queued handles. After apply, the engine sweeps the coroutine list and resumes any coroutine waiting on a handle that just became applied.
- This generalizes: future "wait for X" APIs (`kcdx.wait_for_event(...)`, `kcdx.task(...)`) become coroutine yields with engine-side schedulers — no callback hell, no convention to remember.
- Per-plugin scheduling state is bounded (one coroutine + its stack per Lua plugin); negligible at TC scale.

**What this means for plugin authors:**

- Default is "write straight-line code; if you need to wait, you call `wait`-style functions and the engine handles the timing."
- The SKSE-style callback pattern (`kcdx.on("ready", ...)`) remains available for authors who prefer it (e.g. when registering event listeners that fire later, not when sequencing setup).
- Lua plugin authors don't deal with `coroutine.yield` themselves — they call `handle:wait_applied()` or similar, and kcdx's wrapper handles the yield internally.

**What this means for C++ DLL authors:**

C++ doesn't have language-level coroutines (until C++20 and we don't take that dep). For DLLs, the engine offers a different shape: `handle->WaitApplied()` blocks the calling thread until apply completes (the engine signals via a Win32 event). This is safe because `kcdxPlugin_Preload` / `kcdxPlugin_Load` already run on a kcdx-controlled thread, not the game's main thread; blocking briefly is fine. C++ authors who don't want to block use the `kcdx.on("ready", ...)` equivalent (`api->RegisterListener(...)` against the "ready" event).

### Manifest validation — HARD errors only

If `[entrypoints]` declares neither `lua` nor `dll`, the plugin is rejected with an ERROR log line ("must declare at least one entry point") and excluded from the load list. Same for a declared file that doesn't exist on disk. Same for any unparseable `kcdx.toml` syntax. Same for missing required `[plugin].name`. Plugins with broken manifests do not load, full stop — a fresh agent looking at the engine log sees exactly which plugins were rejected and why.

Per the workspace UX priority: silent partial behavior is worse than loud full rejection. A plugin missing an entrypoint is broken; the engine doesn't pretend it works.

A plugin can declare both `lua` and `dll`. Both run at the plugin's slot, with `dll` Preload first, then `dll` Load, then Lua files in declared order. This is the same "DLL Preload before DLL Load before Lua" micro-order WITHIN a plugin's slot; the macro-order across plugins is the unified sorted list.

**Multiple `lua = [...]` files share ONE coroutine.** When `[entrypoints].lua = ["plugin.lua", "scripts/extras.lua"]` declares multiple files, they all execute inside the SAME coroutine, in declared order. This means:
- Module-level locals (`local foo = ...` at file scope) in file 1 are visible from file 2's scope WITHIN the same coroutine's evaluation cycle (Lua chunks share an environment by default when run sequentially in a coroutine).
- A `yield` in file 1 (e.g. `handle:wait_applied()`) blocks file 2 from starting until file 1's coroutine resumes. This is the natural mental model — file 2 doesn't "run independently"; it's the continuation of file 1's setup.
- If an author wants concurrent registration (file 1 and file 2 register independently), they declare each as a separate plugin in its own folder. Within one plugin, the Lua files form one logical script that happens to span multiple physical files.

Document this in `docs/lua/` (the "plugin lifecycle" topic — see `docs/lua/index.md`).

### Critical files for discovery + lifecycle

- [src/config.cpp](../../../src/config.cpp) — collapse `LoadOneFile` to manifest-only. Delete `ParseOnePatch`, `ParseOneHook`, `ParseOneMidHook`, `ParseOneTrampoline`, `ParseOneScan` and their global-vector population (~600 LOC drop).
- [src/plugin_loader.cpp](../../../src/plugin_loader.cpp) — extend `DiscoverAndLoad` to dispatch Lua entrypoints + interleave with DLL Preload/Load by sorted list, not fixed wave.
- [src/hooks.cpp](../../../src/hooks.cpp) — `HookedUpdate` first-tick handler orchestrates the AFTER_GAME PASS (current first-tick apply loop is repurposed).

## The launcher exe (`kcdx.exe`)

A tiny no-UI Win32 exe that injects `engine/kcdx.dll` into the game. Four responsibilities:

1. Resolve paths: `kcdx.exe` lives next to `KingdomCome.exe`; `kcdx.dll` lives at `./engine/kcdx.dll`; watchdog at `./engine/kcdx-watchdog.exe`. All paths derived from `GetModuleFileNameW(NULL)` (the running launcher's own location).
2. `CreateProcessW(KingdomCome.exe, ..., CREATE_SUSPENDED, ...)` — spawn the game frozen.
3. `CreateRemoteThread(hProc, LoadLibraryW, "<full-path-to-engine/kcdx.dll>")` — inject. Wait + read exit code.
4. `ResumeThread(hMainThread)` — let the game run.

The watchdog is NOT spawned by the launcher; it's spawned by `engine/kcdx.dll`'s DllMain once injected (same flow as today). The launcher just owns process spawn + injection.

If injection fails (Defender flag, AV intercept), the launcher writes a clear log line to `engine/logs/kcdx-launcher_<ts>.log` and falls back to a `OpenProcess + WriteProcessMemory + CreateRemoteThread` form (slightly different AV signature). If THAT fails, it logs an actionable message ("AV may be blocking injection — see docs/troubleshooting.md") and exits nonzero so the user knows something went wrong.

Lives at `src/loader/main.cpp`. Built as `add_executable(kcdx-launcher ...)` in CMakeLists.txt with OUTPUT_NAME `kcdx` and SUFFIX `.exe` (so the on-disk binary is `kcdx.exe`). The engine DLL target stays as today, just renamed: OUTPUT_NAME `kcdx`, SUFFIX `.dll` → produces `kcdx.dll`. The .exe and .dll share the `kcdx` stem; Windows distinguishes by extension. Strips to <100 KB.

### Install layout (Phase 2+)

```
<game>/Bin/Win64MasterMasterSteamPGO/
├── KingdomCome.exe                  (vanilla)
├── WHGame.dll                       (vanilla)
├── kcdx.exe                         (LAUNCHER — the only kcdx file at game-bin root)
├── kcdx-README.txt                  (install + Steam launch options)
├── engine/                          (everything else kcdx ships)
│   ├── kcdx.dll                     (engine — injected by launcher)
│   ├── kcdx-watchdog.exe            (crash-bundle sidecar)
│   ├── engine.toml                  (engine config; dev_mode, dry_run, etc.)
│   ├── load_order.toml              (user load-order overrides)
│   ├── address-library/
│   │   └── database.csv
│   ├── logs/
│   │   ├── kcdx_<ts>.log
│   │   ├── kcdx-dev_<ts>.log
│   │   ├── kcdx-launcher_<ts>.log
│   │   ├── kcdx-watchdog_<ts>.log
│   │   └── crash/
│   │       └── crash_<ts>.zip
│   └── builtin/                     (first-party engine fixes; ship with kcdx)
│       └── bugsplat-filename-fix/
│           ├── kcdx.toml
│           └── bugsplat-fix.dll
└── plugins/                         (ONLY user/third-party plugins)
    └── <plugin-folder>/
        ├── kcdx.toml
        ├── plugin.lua               (or .dll, or both)
        └── logs/
            └── <name>_<ts>.log
```

**Key principles in the new layout:**

- `kcdx.exe` is the **only kcdx file at the game-bin root** — sibling of `KingdomCome.exe`. User clicks one binary to launch. Everything else (kcdx.dll, kcdx-watchdog.exe, engine.toml, logs, builtin fixes) lives one folder down under `engine/`. The user's mental model is simple: one launcher exe, two folders (engine + plugins).
- `engine/` and `plugins/` are **siblings**, both at the game-bin level. `engine/` is everything kcdx ships including the engine DLL itself. `plugins/` is **only** for user/third-party plugins — nothing kcdx-owned lives in here.
- The launcher (`kcdx.exe`) knows to inject `engine/kcdx.dll`. It also knows to spawn `engine/kcdx-watchdog.exe` as a sidecar. Path-resolution lives in `src/loader/main.cpp`.
- `kcdx-engine/` (today's name) is renamed to `engine/`. Shorter and clearer.
- **No more `.asi` extension anywhere.** kcdx.dll is loaded directly by kcdx.exe via CreateRemoteThread+LoadLibrary, not by Ultimate ASI Loader scanning for `*.asi` files.
- **`dinput8.dll` (Ultimate ASI Loader proxy) is no longer required or installed.** A user who has it from another mod can keep it, but kcdx neither installs nor depends on it. The dependency on a third-party loader is gone.

Steam launch options snippet (user-facing):

```
"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\kcdx.exe"
```

Or in Steam right-click → Properties → Launch Options: paste the path. Steam's overlay is preserved (kcdx.exe spawns the game via CreateProcess, which keeps Steam's hooks intact).

## kcdx replaces pak mods (asset replacement is in scope)

Per priorities, "general mechanism over special case" — and the existing pak-mod ecosystem is a special case that lives outside kcdx. For total conversion viability, **kcdx must absorb pak mods' capability set entirely**, then exceed it. Two coexisting asset-replacement systems is a UX foot-gun (which load order wins? do pak mods see kcdx-applied bytes?); the right answer is "kcdx handles everything."

This means kcdx ships an asset-replacement surface that can do everything pak mods can do today:

- **File replacement** — substitute a vanilla `.dds`, `.cgf`, `.cdf`, `.xml`, `.lua` (game-side script), `.ogg`, etc. with the plugin's version.
- **Pak overlay** — multiple plugins can stack their replacements; load order decides who wins for any specific file.
- **Compressed delivery** — plugins ship their assets in a structured archive (probably zip-without-compression like KCD2's pak format, or just a flat folder inside the plugin's install directory).

Plus things pak mods can't do today:

- **Asset-aware conflict detection** — two plugins replacing the same file get a clear "plugin A wins; plugin B's version of <file> is suppressed" log line, same shape as today's hook conflict reporting.
- **Conditional asset replacement** — Lua-side decision logic (`if kcdx.world.region() == "talmberg" then use this DDS else that one`).
- **Dynamic asset injection** — plugins can register virtual paths at runtime that the game's file-load functions resolve into plugin-supplied bytes (no on-disk file needed).

### How it integrates with the new schema

A plugin declares asset entrypoints alongside Lua/DLL ones:

```toml
[entrypoints]
lua    = "plugin.lua"           # optional
dll    = "bin/my-plugin.dll"    # optional
assets = "assets/"              # NEW: folder of files to overlay on game's vanilla paks
```

The `assets/` folder mirrors the game's pak structure. A file at `<plugin>/assets/Libs/UI/MainMenu.gfx` overlays the vanilla `Libs/UI/MainMenu.gfx`. The kcdx engine hooks the game's file-load path (probably `CrySystem`'s `OpenFile` / pak resolver) so when the game asks for `Libs/UI/MainMenu.gfx`, kcdx checks every loaded plugin's `assets/` folders in load-order priority and returns the highest-priority hit.

Lua side, dynamic injection looks like:

```lua
kcdx.assets.replace("Libs/UI/MainMenu.gfx", function()
    -- return the raw bytes the game should see
    return my_dynamically_generated_gfx_data
end)

kcdx.assets.replace_static("Libs/UI/HUD.gfx", kcdx.plugin_path .. "/assets/my-hud.gfx")
```

### Why this is in scope, not deferred

Without asset replacement, kcdx can't support TCs — Fallout: London ships ~30 GB of replacement assets and the existing pak mod path can't be load-order-managed cleanly. Shipping "kcdx for code + pak mods for assets" forces TC authors to maintain two parallel ecosystems with two load orders that don't talk. That's the "Y does 80%" anti-pattern: shipping code-only kcdx is the easier path that doesn't fully solve TC.

### Scoping for this plan

The asset replacement system is a meaningful chunk of engine work. It sits as **Phase 8.5** between Phase 8 (ASI cleanup) and Phase 9 (high-level Lua surface). Sub-phases:

- **Phase 8.5a**: hook the game's pak resolver. Identify `CrySystem::ICryPak::OpenResource` (or equivalent) via existing Address Library + Ghidra. Add to address-library/database.csv.
- **Phase 8.5b**: parse `[entrypoints].assets` directories at plugin discovery. Build an in-memory overlay map keyed by virtual path → (plugin, file_path).
- **Phase 8.5c**: in the pak resolver hook, check the overlay map first. On hit, return plugin file. On miss, fall through to vanilla.
- **Phase 8.5d**: ship the Lua surface (`kcdx.assets.replace`, `kcdx.assets.replace_static`, conflict reporting).
- **Phase 8.5e**: test plugin (`cap-XX-asset-replace`) that replaces a known-safe game file (probably a UI string in a menu) + verifies the replacement is visible in-game. Regression test ships alongside.

After Phase 8.5, the pak-mods.md workspace rule is rewritten to "pak mods are deprecated; use [entrypoints].assets — see docs/asset-replacement.md." Existing pak mods keep working (we don't break vanilla KCD2 modding), but kcdx is the path forward for new TC work.

### Critical files for asset replacement

- New: `src/asset_overlay.cpp` / `src/asset_overlay.h` — overlay map + pak resolver hook.
- New: `src/lua_bind_assets.cpp` — `kcdx.assets.*` Lua surface.
- New: `docs/asset-replacement.md` — author-facing guide for asset overlay.
- Modified: [src/config.cpp](../../../src/config.cpp) — parse `[entrypoints].assets`.
- Modified: `.claude/rules/pak-mods.md` — annotate as deprecated; point at asset-replacement.md.
- Modified: `_research/phase8-fix-a/` (or sibling) — RE work to identify the pak resolver vtable.

## Engine internals that stay intact

These are NOT rewritten by the restructure. The change is only WHO calls them: the TOML parser used to populate global vectors that the apply pass walked; now the Lua/C++ APIs append to the same vectors imperatively.

| Engine | File | Notes |
|---|---|---|
| `patch_engine::Resolve` (locator pipeline) | [src/patch_engine.cpp](../../../src/patch_engine.cpp) | Used by `kcdx.bytes` + `kcdxBytesInterface::Write` |
| `conflict_engine` (pre-flight + apply order) | [src/conflict_engine.cpp](../../../src/conflict_engine.cpp) | Runs incrementally; each API call slots in + verifies |
| `hook_engine::InstallRuntime` (MinHook + first-wins) | [src/hook_engine.cpp](../../../src/hook_engine.cpp) | Already the runtime path; promoted to primary |
| Trampoline pools | [src/trampoline.cpp](../../../src/trampoline.cpp) | Backs `kcdx.code` + `kcdxTrampolineInterface` v2 (`Allocate`/`Export`) |
| `ldr_notify` (LDR notification path) | [src/ldr_notify.cpp](../../../src/ldr_notify.cpp) | Extended for arbitrary kcdx-API-registered entries from DLL Preload |
| `load_order` (zones, priorities, overrides) | [src/load_order.cpp](../../../src/load_order.cpp) | `DeriveMinZone` becomes a manifest-field read instead of a vector scan |
| Messaging (kcdxMessage_*) | [src/messaging.cpp](../../../src/messaging.cpp) | Extended with `<sender>:<event>` pub/sub naming for kcdx.publish |
| Serialization (cosave) | [src/serialization.cpp](../../../src/serialization.cpp), [src/save_load_hooks.cpp](../../../src/save_load_hooks.cpp) | Lua wrapper added (`kcdx.cosave.*`) |
| crash_guard + watchdog | [src/crash_guard.cpp](../../../src/crash_guard.cpp), [src/watchdog/main.cpp](../../../src/watchdog/main.cpp) | Unchanged |
| log + deferred-log buffer | [src/log.cpp](../../../src/log.cpp) | Existing infrastructure (already shipped). The restructure ADDS responsibility for per-plugin error attribution: when an apply-pass failure occurs, the engine's log call must include the plugin name + registration call-site so the same error lands in both the engine log AND the offending plugin's log. log.cpp's existing per-plugin stream support handles this; the restructure just wires the apply-pass dispatcher to call `EmitPlugin(...)` alongside `EmitEngine(...)` for any error that has an owning plugin. |
| Symbols (cross-plugin) | [src/symbols.cpp](../../../src/symbols.cpp) | `kcdx.code(... export=...)` populates |
| Address Library | [src/address_library.cpp](../../../src/address_library.cpp) | Unchanged |
| Console | [src/console.cpp](../../../src/console.cpp) | `kcdx.command` routes here |

The net engine code change is small. The vast majority of source under `src/` is untouched. The shape changes; the engines don't.

## Migration of existing assets

### Engine builtin: `engine/builtin/bugsplat-filename-fix/`

The current `[[patch]]`-based LEA-rewrite was empirically disproven (PROBE R/S/T, 2026-05-21 — see [docs/known-issues/BugSplat dmp files don't reach disk for AV crashes.md](../../../docs/known-issues/BugSplat dmp files don't reach disk for AV crashes.md)). The real fix intercepts BugSplat64.dll's `MiniDmpSender` constructor.

New shape (DLL only, since pre-Phase-11 Lua can't run before_game):

```toml
# engine/builtin/bugsplat-filename-fix/kcdx.toml
[plugin]
author      = "kcdx_builtin"
name        = "bugsplat_filename_fix"
display_name = "BugSplat Filename Fix"
description = "Sanitizes BugSplat's szApp at ctor time so dmp filenames don't contain ':'."
version     = "1.0.0"
supports    = ["1.5*"]

[load_order]
zone     = "before_game"
priority = 30

[entrypoints]
dll = "bugsplat-fix.dll"
```

The DLL's `kcdxPlugin_Preload` calls `api->QueryInterface(kcdxInterface_Hook, ...)->Install(...)` with `mode=before`, a C callback that does the ":→ -" substitution on `szApp`, and a locator for the `MiniDmpSender` ctor. **Locator note (function_name dropped):** the ctor is a mangled C++ export with no readable name — so the builtin resolves it by `pattern` (an AOB on the ctor prologue) or by adding an Address Library entry for it (a named row, resolved via RE), then `address_id = "bugsplat_minidmpsender_ctor"`. NOT by pasting the mangled `??0MiniDmpSender@@...` string (dropped — unusable UX). PROBE T verified the before-mutate-szApp mechanism works; Phase 4 picks the locator form.

### 21 test-suite plugins

| Plugin | Today | After restructure |
|---|---|---|
| cap-01-patch | TOML `[[patch]]` + DLL verifier | `plugin.lua` (`kcdx.bytes`) + same DLL verifier |
| cap-03-hook-lua-callback | TOML `[[hook]]` + pak Lua | `plugin.lua` (`kcdx.hook` mode=before) |
| cap-04-midhook | TOML `[[mid_hook]]` (4 entries: a/b/c/d testing call_original true/false/auto-run/auto-skip) + pak Lua | `plugin.lua` with 4 explicit `kcdx.hook` calls, one per sub-test: cap-04-a (mode=mid, original="run"), cap-04-b (mode=mid, original="skip"), cap-04-c (mode=mid, original="auto", callback returns args._skip=true), cap-04-d (mode=mid, original="auto", callback returns nothing → original runs). Every sub-test surfaces its pass/fail independently via ReportTestResult. |
| cap-05-paklua-runtime | Pak Lua `dynamic_hook` | `plugin.lua` (`kcdx.hook`) |
| cap-07-trampoline-pools | DLL | DLL (renamed APIs) |
| cap-08-messaging | DLL | DLL same OR `plugin.lua` (`kcdx.on`) |
| cap-09-task-interface | DLL | DLL unchanged (Task is C++-only) |
| cap-10-scripting-interface | DLL | DLL unchanged |
| cap-12-serialization | DLL | DLL same + new Lua variant |
| cap-13-console-command | DLL | DLL same OR `plugin.lua` (`kcdx.command`) |
| cap-16-A/B (dependencies) | Two DLLs | Two DLLs unchanged |
| cap-17-enumerate-plugins | DLL | DLL unchanged |
| comp-02-hook-on-patch | Two TOMLs | Two `plugin.lua`s (one bytes, one hook) |
| comp-03-A/B (hook-on-hook) | Two TOMLs | Two `plugin.lua`s with same locator + different priorities |
| engine-self-test | DLL | DLL unchanged |
| probe-comp-crash | DLL | DLL unchanged |
| probe-crash-trigger | DLL | DLL unchanged |
| scan-demo | TOML `[[scan]]` | RETIRED (not migrated — cap-32 already covers the `kcdx.scan` surface end-to-end, reusing scan-demo's exact proven pattern; a migrated scan-demo would duplicate cap-32. Retired with the legacy `[[scan]]` path it showcased — last `[[scan]]` consumer gone, unblocking Phase 5's `[[scan]]` parser deletion. Decision 2026-05-26.) |

About two-thirds become Lua-only; one-third stays DLL. The migration happens in batches of ~5 per commit so a regression bisects cleanly.

## Probe disposition

| Probe | Status |
|---|---|
| PROBE Q (frealloc canary, [src/hooks.cpp](../../../src/hooks.cpp)) | **Keep**. Permanent FIX C regression guard per [.claude/rules/lua-bridge.md](../../../.claude/rules/lua-bridge.md). |
| PROBE H (lua_State variance log) | **Keep**. Refactor from inline block in HookedLuaPcall to named helper. |
| Phase5gReadback | **Removed (Phase 6, 2026-05-26)**. Dead code from a closed investigation. |
| PROBE R (CreateFileW probe, `src/probes/createfilew_probe.cpp`) | **Removed (Phase 6, 2026-05-26)**. Question answered (the broken-dmp CreateFileW caller is BugSplat64.dll); file deleted. |
| PROBE S+T (MiniDmpSender ctor probe, [src/probes/bugsplat_ctor_probe.cpp](../../../src/probes/bugsplat_ctor_probe.cpp)) | **KEEP — carried to Phase 11**. The diagnostic became the proven before_game-hook install machinery ([before-game-hooks.md](../before-game-hooks.md) §5); it relocates/generalizes into the real builtin at Phase 11, not deleted at Phase 6. |

After Phase 6, `src/probes/` retains `bugsplat_ctor_probe.{h,cpp}` (the Phase-11 install-machinery prototype); `createfilew_probe.*` is gone.

## Phasing — sequenced commits

All phases preserve a buildable, runnable engine + the 21/21 test suite green at every step. Step 0 is the user-requested snapshot ("commit the full repo first").

**Verification at every phase gate is mandatory, not optional.** No phase is "done" until its verification check passes. The Per-phase manual checks block below specifies what verification means for each phase. A phase that lands its code but skips verification is not landed — the next phase doesn't begin until the current phase's verification is green. This is a hard discipline; the alternative is "we'll catch regressions in the next batch" which is exactly the timing trap that produces hard-to-bisect bugs.

For phases that involve multiple commits (Phase 4 batch migration, Phase 11 sub-phases), every commit verifies independently before merging. A failed sub-step is reverted, not papered over.

### Phase 0 — manifest plan + snapshot commit

Two steps, both before any restructure code touches a file:

1. **Manifest this plan into the workspace.** Copy `C:\Users\Michael\.claude\plans\i-want-you-to-cheeky-journal.md` → `docs/outstanding-work/restructure-plan.md` so the plan is version-controlled with the code it describes. The `.claude/plans/` location is outside the repo and won't survive `git clean` or a fresh checkout — the doc must live in-tree. Link it from `docs/outstanding-work/README.md` so a fresh agent reading the outstanding-work folder discovers it.

2. **Snapshot commit.** Stage everything (`git add -A` including the new restructure-plan.md), then `git commit -m "Snapshot before manifest-only/Lua-first restructure"` + `git tag v0.1-final`. This is the recovery point — any phase can be rolled back via `git reset --hard v0.1-final` if the restructure goes sideways.

**Ongoing maintenance of the plan**: after Phase 0, the workspace copy at `docs/outstanding-work/restructure-plan.md` becomes the authoritative source. Edits during execution land there (committed alongside the code changes that produced them). The `.claude/plans/` copy is the bootstrap; once mirrored, it's stale by definition. Every phase that lands an edit to the plan ALSO commits the updated `docs/outstanding-work/restructure-plan.md` so a fresh agent reading the repo sees the current state.

**Mark `docs/design.md` as superseded.** Today's `docs/design.md` is the spec; after the restructure, most of it is wrong (the seven TOML entry types it describes don't exist anymore). A fresh agent reading the repo would find conflicting documents. Phase 0 prepends a clear banner to `docs/design.md`:

```markdown
> **SUPERSEDED.** This document describes the v0.1 design (seven TOML entry types,
> ASI-loader-based installation, immediate-apply hook model). It is preserved as
> historical reference for the engine internals that survive the restructure
> (patch_engine, conflict_engine, ldr_notify, etc.) but the schema, lifecycle, and
> author surface have all been replaced. **The current authoritative design is
> [`docs/outstanding-work/restructure-plan.md`](00-original-plan.md).**
> Each phase of the restructure that touches an engine surface should update both
> this doc (to mark the relevant section superseded) AND the restructure plan
> (to record what changed).
```

Phase-by-phase work then optionally trims out sections of design.md as the restructure phases land their corresponding changes. At the end of Phase 11, `docs/design.md` is either fully replaced by the restructure plan as the live spec, OR retained as the engine-internals reference (patch_engine algorithm, conflict_engine pre-flight matrix, etc.) with the schema/author-surface sections gone.

### Phase 1 — launcher exe + drop the .asi extension

This phase is the install-layout migration. The user-visible change is biggest here: the launch model flips from "Ultimate ASI Loader + kcdx.asi" to "kcdx.exe injects kcdx.dll."

**New binaries:**
- New `src/loader/main.cpp` — launcher logic (CreateProcessW SUSPENDED + CreateRemoteThread(LoadLibraryW) + ResumeThread + fallback chain).
- New `add_executable(kcdx-launcher ...)` CMake target (OUTPUT_NAME `kcdx`, SUFFIX `.exe` → `kcdx.exe`).
- Existing kcdx target: keep OUTPUT_NAME `kcdx`, change SUFFIX from `.asi` to `.dll` → produces `kcdx.dll`. Same target, just a SUFFIX-line change.

**Filesystem moves:**
- `kcdx-engine/` → `engine/` (rename; shorter and clearer).
- `plugins/` keeps its name (already SKSE-ecosystem convention) BUT loses anything kcdx-owned.
- Engine binaries (kcdx.dll, kcdx-watchdog.exe) move INTO `engine/`. They no longer live in `plugins/` (today) or at game-bin root.
- Only `kcdx.exe` (launcher) sits at the game-bin root. Everything else kcdx-owned is one folder down under `engine/`.
- The builtin fixes move with the engine folder: `kcdx-engine/builtin/` → `engine/builtin/`.

**Code changes:**
- [src/paths.cpp](../../../src/paths.cpp): find self by `kcdx.dll` (was `kcdx.asi`). kcdx.dll lives at `<game-bin>/engine/kcdx.dll`. Engine-data root is `<game-bin>/engine/` (sibling of kcdx.dll, same folder as the engine binary itself). Plugin scan root is `<game-bin>/plugins/`. Builtin discovery walks `<game-bin>/engine/builtin/`.
- [package-release.ps1](../../../package-release.ps1): release zip now includes `kcdx.exe` at the game-bin root + `engine/` (containing kcdx.dll, kcdx-watchdog.exe, engine.toml template, builtin/, empty logs/) + empty `plugins/`. NO dinput8.dll. NO ASI extension anywhere.
- Docs sweep: [README.md](../../../README.md), [docs/loader-architecture.md](../../../docs/loader-architecture.md) — install instructions point at running `kcdx.exe`; Ultimate ASI Loader is removed from prerequisites entirely.

**Existing installs migration (one-time, user-side):** the release notes describe a clean reinstall — uninstall the old kcdx.asi + dinput8.dll, install the new layout. The repo is unshipped so no real users have to do this; the only existing install is ours.

**`docs/migration.md` ships in this phase**, not Phase 5. Phase 1 IS a user-visible breaking change (install layout flips); the migration guide lands when the change lands, not three phases later. Phase 5's deletion of the old TOML behavior parsers updates the same doc with the schema-level migration steps.

**`kcdx --init-plugin <name>` scaffolder (lands in this same Phase 1 commit).** The launcher exe gains a CLI flag that scaffolds a new plugin folder under `<game-bin>/plugins/<name>/` with a minimal complete `kcdx.toml` (the `[plugin].author` defaults to a placeholder; `authored_against_game_version` pre-populated as a single-element array `["1.5.1164953"]` from the currently-installed KCD2 build via the existing `paths::Init` machinery) plus a stub `plugin.lua`. Removes the "blank-page TOML" friction for first-time authors — they run `kcdx --init-plugin my-mod` and get a complete manifest to edit. Implementation in `src/loader/main.cpp` parsing the flag + a sibling `src/loader/init_plugin.cpp` that writes the scaffolded files. ~50 LOC. Reads the running KCD2 version once (same code path the launcher already uses for its log). Equivalent to `npm init` / `cargo new` / `git init` — the standard "remove empty-folder friction" tool.

**End state:** game launches via `kcdx.exe` with zero plugins; `kcdx --init-plugin my-mod` produces a working scaffold the author can edit; existing test suite (still using old TOML behavior parser; the parser change is Phase 4-5) loads and remains 21/21 green.

### Phase 2 — Lua API skeleton (additive, no removals)

Executed as a sequence of independently-verified **subs** (each ships its `test-plugins/` regression per `.claude/rules/test-suite.md`; each is a working unit). The original "one monolithic skeleton" sketch was refined into these in flight:

**Done (live-verified) — AUTHORITATIVE LEDGER.** Each row: what shipped, the landing commit, and the matrix row that proves it live (`test-plugins/README.md`). Add a row here in the SAME unit of work that lands the sub — this list is the plan-of-record for "what's built," not a periodically-reconciled afterthought. (Update trigger: any commit whose subject is `Phase 2b sub-N …` or `Phase 2b … LIVE` adds/flips a row here.)

| Sub | What shipped | Commit | Matrix row(s) |
|---|---|---|---|
| **sub-1** | `kcdx.addr.*`: Address Library entries as Lua pointer userdata, by name. | `34cd870` | (exercised via the `address_id`-by-name path; CAP-20-addrname) |
| **sub-2** | hook signature parser (`src/hook_signature.{h,cpp}`, full grammar). | `3c6c38f` | (exercised via sub-4) |
| **sub-3** | `kcdx.hook` registration + validation surface (`src/hook_payload.{h,cpp}`, `src/lua_bind_hook.cpp`): parse → validate → queue into `lua_registry`; apply was a deferred stub. | `002f6a0` | (exercised via sub-4) |
| **sub-4** | the live `kcdx.hook` engine: plugin.lua EXECUTION (`src/lua_plugin_loader.cpp` runs `[entrypoints].lua` at first-tick under crash_guard, before `ApplyZone`) + script-path→plugin attribution; the `src/hook_chain.{h,cpp}` dispatcher (4 modes before/after/around/replace, mode-as-key + mutate-by-return, load-order chaining, `call_original` via a `dynamic_call`-thunk over pOriginal); wstr/cstr marshaling with pinning; `kcdx.log.*`. Fixed a latent `LUA_NUMBER=float` marshal bug in the shared `JitTrampoline` along the way (`docs/known-issues/cap-20-around-wraps-original-wrong-result.md`). | `27ee126` | CAP-20-hook-modes (4 modes) |
| **sub-4b** | `address_id` resolves by readable Address Library NAME or numeric id (the `function_name`/mangled-export locator is DROPPED); `ResolveAddressByName` added to the C++ plugin interface (parity — the early C++ side of Phase 3). | `533ac79` | CAP-20-addrname |
| **AP12 batch** | the disassembler-test cornerstone made law: #1 the name carries the ABI — `kcdx.hook{ target = "<name>" }` supplies address AND verified signature (`088eafa`); #2 locator cleanup — `target=` is the common path, dead `function_name` deleted (`a303200`); #3 plugin.lua errors carry file:line + traceback to the author's own log (`438d34a`). | `088eafa`, `a303200`, `438d34a` | CAP-20-target, CAP-20-target-nosig, cap-23-lua-error-lineinfo |
| **sub-5** | `kcdx.hook` **mode=mid** — mid-function capture (read/write registers+memory, return-based run/skip); built fresh on `hook_chain`, does NOT inherit the legacy cap-04-c auto-skip bug (CAP-21-skip proves it). Succeeds `[[mid_hook]]`. | `91de228` | CAP-21-read/write/skip/run |
| **sub-6** | `kcdx.hook` **mode=callsite** — single call-site E8 rel32 redirect; per-call-site, not per-callee (isolation proven). Design-gap #1 closed. | `f04edbe` | CAP-22-before/after/around/replace + 2 isolation rows |
| **sub-7** | `kcdx.on(event, fn)` + the **`ready`** event that fires after a zone's apply pass (unblocks asserting `handle:applied()`). | `92c0725` | CAP-20-ready, CAP-20-addrname-miss, CAP-20-conflict-rejected (⏳ pending the conflict-launch) |
| **sub-8** | `kcdx.on` lifecycle-event bridge — one engine-internal hook in `messaging::FireEngineMessage` fans the 9 `kcdxMessage_*` out to their `kcdx.on` Lua subscribers (the 3 save/load events pass the basename). | `73e9101` | CAP-24-input-loaded (✅); save-game/post-load-game (⏳ [manual]) |
| **sub-9** | `kcdx.publish` cross-plugin pub/sub — arbitrary Lua payloads by reference, `<publisher>:event` namespacing, shares the sub-8 subscriber registry (Lua-native, not the C++ messaging wire). | `725d8f4` | COMP-09-pubsub |
| **multi-file** | plugin-scoped `require` searcher in Lua 5.1 `package.loaders` + COMPLETE source attribution (the require'd helper's source lands in `g_scriptOwners` at the searcher's compile point) — closes the kcdx.publish/on/hook publisher-identity gap at its source. (Probe `7640fde`, searcher `3371de0`, regression `8ced8fd`.) Not a numbered sub — fell out of sub-9's identity gap. | `3371de0` | CAP-25-multifile-attribution |
| **`kcdx.command`** | the Lua console-command verb (`src/lua_bind_command.cpp`) over the proven C++ console path: `kcdx.command{name,description,callback}`, args as an array + `args.raw`; plus `kcdx.console.execute(line)` (the Lua mirror of the C++ `ExecuteString`). Register-immediately. | `d41cb0b`, `4dec94d` | CAP-26-command-roundtrip |
| **per-entry-zone** | the both-phase execution model (one plugin does before_game AND after_game work): console **deferred** command registration (queue + flush, closes the CAP-26 timing race); lifecycle/publish callbacks + entrypoints fire in **load-order priority** (both phases); the optional **`lua_after`** entrypoint + **`kcdxPlugin_PostGameLoad`** C++ export; deleted the silent before→after downgrade (AP13); **kcdx-owned `require`** (per-chunk fenv + `<owner>:<modname>` cache, bypass `_LOADED`) closing the cross-plugin require-cache collision. | `1a01516`, `f3b9337`, `54d761e`, `47cb804`, `7905f1c`, `82893e9`, `1cad2b5`, `d6df59c`, `7de07bb` | CAP-27-immediate/coexist, COMP-10-require-isolation-a/b, COMP-11-both-phase-order, CAP-29-both-phase-dll |
| **`kcdx.code`** | the Lua code/trampoline-allocation verb (`src/lua_bind_code.cpp`) over the existing trampoline pool + symbol table: `kcdx.code{name,bytes,size,pool,export}` allocates executable memory immediately, NOP-pads to size, optionally publishes a `target_symbol`-resolvable export, returns a live `kcdx.memory.pointer`. Brings Lua to parity with the C++ `kcdxTrampolineInterface`. Doc entry moved with it (docs-discipline.md). | `0f17e68`, `a9180ba` | CAP-30-alloc, CAP-30-export |
| **`kcdx.cosave.*`** | the Lua save/load-persistence domain (`src/lua_bind_cosave.cpp`) + a standalone tagged Lua-value codec (`src/lua_cosave_serial.cpp`) over the existing `kcdxSerializationInterface`: `on_save`/`on_load`/`write(tag,version,value)`/`records()`/`set_uid`. Headline UX — the cosave **UID auto-derives from the plugin name** (FNV-1a, single-sourced with the C++ `HashTag`); the author hand-packs no FourCC (the disassembler-test win). Backed by **string-tag records** added to the interface (`OpenRecordNamed`/`GetRecordTagName`, append-only Version 1→2) — fixes the FourCC-collision foot-gun at source on BOTH surfaces; CAP-12 (C++) migrated to the named path. Doc entry + glossary + C++ mirror (with the name-derived-UID Phase-3 parity debt noted) moved with it (docs-discipline.md). | `64567d9`, `1424fab`, `b75e43d`, `9672c57`, `5446e09`, `14c57eb`, `b8602e6` | CAP-31-outside-window/roundtrip/reject (✅), CAP-12 named records (✅) |
| **`kcdx.scan`** | the Lua diagnostic-AOB-scan verb (`src/lua_bind_scan.cpp`) over `scan_engine::ResolveScan` (refactored in sub-1 to return a structured, per-match module-attributed `ScanResult`; `[[scan]]` TOML log byte-for-byte preserved). The dev-time AOB-validation / address-discovery workbench: resolve a hand-written pattern, log the diagnostic, return `{ count, matches={ {addr,module,offset},… }, addr }` — always a table (count=0 on no-match), `(nil,err)` only on bad input. The `pattern` is the labeled expert hatch (kcdx.scan IS the AOB tool; common path is `kcdx.hook{ target=<name> }`). Doc + glossary + C++ NYI mirror moved with it. Cross-plugin opt-in scanning deferred to its own feature (`docs/outstanding-work/cross-plugin-scan.md`; the per-match attribution already generalizes). | `6cb1e98`, `0cb2295`, `330321e`, `76c7eda` | CAP-32-resolve/nomatch/badinput (✅) |
| **`zone_gate`** | per-API `requireZone` capability gating (the last Phase 2 item) — `src/zone_gate.{h,cpp}` engine + `kcdx.plugin.is_rejected` accessor. `load_order::Effective` split into `userEnabled` + `engineAccepted`; `IsPluginEnabled` returns the AND so a single gate at 5 plugin-init sites (Lua RunAll/RunAfterEntrypoints + C++ Preload/Load/PostGameLoad) honors both user-disable and engine-reject. zone_gate runs ONCE in `LoadAllConfigs` after `load_order::Resolve()`, BEFORE any plugin.lua/DLL Load — rejection prevents any registration. Every shipped API is `requireZone=Either` today (the deferred-registration pattern handles "called early but work later"); one synthetic test entry `kcdx.zone_gate_test_after_only=After` exercises the rejection path. PLUGIN_REJECTED log per `restructure-plan.md:165-168` shape names the API (and the plugin's 2-dot `<author>.<plugin>` identity — same shape `kcdx.plugin.is_rejected` queries with) + the kcdx.toml fix; init-site skip-logs enriched to distinguish engine-reject vs user-disabled. zone_gate's `g_rejected` keys on the 2-dot form (matches the author-facing accessor's cross-plugin reference per `naming-namespaces.md`); load_order's `g_effective` stays bare-keyed (different module, internal state). Doc + glossary + C++ NYI mirror (`kcdxPluginInfoInterface`) moved with it. | `448d9ff`, `5828373`, `a3bd3df`, `b1718ed`, `836f568`, `9960f13` | COMP-13-zone-reject (✅) |

**Remaining Phase 2 — empty. Phase 2 is complete.** (`src/zone_gate.{h,cpp}` landed across the 4 zone_gate subs above; the orphaned-`minZone` cleanup landed in commit `1506d8d` earlier in the session.)

(`docs/lua/` — per-call files fronted by `docs/lua/index.md` — is now DONE; the complete author reference first landed as the `docs/lua-api.md` monolith in commit `dd52f08`, since restructured into the per-call `docs/lua/` folder, encoding the `.claude/rules/lua-api-surface.md` conventions — struck from this Remaining list; see the End state below.)

**End state:** plugins ship `plugin.lua` calling the full `kcdx.*` surface; existing TOML-behavior plugins still work; suite green; **`docs/lua/` (per-call files fronted by `docs/lua/index.md`) covers every Lua surface** (the realized deliverable — first landed as the `docs/lua-api.md` monolith in `dd52f08`, since restructured into the folder).

### Phase 3 — C++ DLL API parity (additive) + ergonomic wrapper

- New: `src/hook_interface.cpp`, `src/bytes_interface.cpp`, `src/code_interface.cpp`.
- Extend [include/kcdx/Interfaces.h](../../../include/kcdx/Interfaces.h) with `kcdxHookInterface`, `kcdxBytesInterface`, and the v2 extension to the existing `kcdxTrampolineInterface` (the `kcdx.code` C++ mirror appends `Allocate`+`Export` to the raw pool floor rather than spawning a parallel `kcdxCodeInterface` — see Phase 3 sub-3 ledger).
- Wire into `QueryInterface` in [src/interfaces.cpp](../../../src/interfaces.cpp).
- **New ergonomic wrapper**: `include/kcdx/Kcdx.h` ships in this phase. Header-only struct that pre-fetches every sub-interface + builds a logger from one `Init(api, "my.name")` call. Pattern mirrors existing `kcdxLogger`. README's "writing a C++ plugin" example uses the wrapper.
- **`docs/cpp/`** (per-interface files fronted by `docs/cpp/index.md`) written alongside the new interfaces. Every method on every interface documented. Worked example for a typical C++ plugin (using `Kcdx.h`). Phase 3 ships incomplete if the doc is missing.
- **FULL PARITY with the as-built Lua surface is the bar** (`.claude/rules/lua-api-surface.md`): `kcdxHookInterface` must express every `kcdx.hook` capability — all modes (before/after/around/replace + mid/callsite when built), every locator (address / address_id by name|number / target_symbol / pattern), chaining, the `call_original` callable for around — idiomatic in C++ (an options-struct + `mode` field rather than mode-as-key; typed params rather than positional Lua values; whatever C++ form mirrors mutate-by-return). NO Lua-only or C++-only capability. Each capability gets a C++ test plugin (parity is tested, not assumed). Already landed early as the C++ side of sub-4b: `ResolveAddressByName` on `kcdxInterface` — APPEND-ONLY (AP11; new interface members go at the struct END or pre-built plugins crash on load).
- **C++ author-target precedence — DONE/CLOSED (landed via the symbols-fix cycle; from the author-targets feature, `naming-namespaces.md`).** The Lua `kcdx.hook{ target = "<name>" }` path resolves names with self > engine > other-plugin precedence because the binder threads the calling plugin's identity (`OwningPluginForCurrentCall`). The anonymous C++ `kcdxInterface::ResolveAddressByName(const char* name)` thunk has **no per-call plugin identity** — `g_api` is one shared struct handed by-pointer to every plugin — so it resolves **engine-seed + explicit-prefix only** (the `""`-owner path: safe, not wrong, no self tier). **Closed: the APPEND-ONLY `ResolveAddressByNameAs(kcdxPluginHandle owner, const char* name)` overload now ships on `kcdxInterface`** (placed after the `// --- APPEND-ONLY BELOW ---` marker, sibling to the symbol-resolution `ResolveSymbolAs(owner, name)`). The caller passes its own handle; the thunk threads it as the owner, reaching the same self > engine > other precedence as Lua. Documented as a built entry in [`docs/cpp/addr.md`](../../cpp/addr.md) (and marked single-surface there — no Lua `*As` mirror is owed, since Lua threads identity natively). No longer outstanding.

**End state:** DLLs reach every Lua capability via the wrapper (or raw QueryInterface). Existing DLL plugins (CAP-07, CAP-09, CAP-13, CAP-16-A/B) unaffected. Test suite green. **`docs/cpp/` exists (per-interface files fronted by `docs/cpp/index.md`) and covers every C++ surface; Lua↔C++ parity holds.**

#### Phase 3 sub-1 (extended) — `kcdxHookInterface` + Kcdx.h wrapper + Uninstall

Executed as ordered sub-steps. Each ships its `test-plugins/` regression per `.claude/rules/test-suite.md`. Per-step handoff doc: [`phase-3-sub-1-extended.md`](../phase-3-sub-1-extended.md).

**Done (live-verified) — AUTHORITATIVE LEDGER.** Same contract as Phase 2b's ledger: add a row in the SAME unit of work that lands the step.

| Step | What shipped | Commit | Matrix row(s) / verification |
|---|---|---|---|
| **1** | Engine: `hook_chain::Uninstall(handleId)` + `Status::Removed` + `Entry::handleId` + `lua_registry::SetStatus` writer + `Add()` gains handleId. Mark-removed-only semantics (Option A): the MinHook detour + JIT trampoline stay session-lifetime; `chain.entries.empty()` guards at DispatchPre/DispatchPost + MidDispatch's NOREF guard make a drained chain a no-op shim. Avoids use-after-free against in-flight dispatchers (which release `g_chainsMu` before `lua_pcall`). | `abbb4c6` | (exercised via step 3) |
| **2** | Lua: `H_uninstall` metatable method on the hook handle + `docs/lua/hook.md` row. `Kind::Hook` routes through chain Uninstall + SetStatus(Removed); non-Hook kinds (today only `kcdx.bytes`) raise a teaching `luaL_error` — the patch engine has no revert path, so silently flipping status would be AP13. | `0c0756a` | (exercised via step 3) |
| **3** | `test-plugins/cap-35-uninstall/` pure-Lua regression — 5 falsifiable rows: basic / idempotent / tostring / chain-survives / bytes-error. Matrix flipped to LIVE in `5c88f2b` after the live run. | `d03ffb1`, `5c88f2b` | CAP-35-uninstall-basic / idempotent / tostring / chain-survives / bytes-error (5/5 PASS) |
| **4** | C++ ABI: `kcdxHookInterface` v1 in `include/kcdx/Interfaces.h` with **six sub-verb method pointers** (Before / After / Around / Replace / Mid / Callsite) + 4 query methods (IsApplied / GetReason / GetName / Uninstall). `kcdxHookOptions` stripped of mode + callback + addressName + callsiteScope (variant IS the method name; target + callback positional; rule-4a-compliant). Header doc-comment hedges `Kcdx.h` as not-yet-built; `docs/cpp/hook.md` got a WIP banner pointing readers at the v1 shape. | `e8fe6ee` | (header-only; verified at step 7's C++ DLL plugin) |
| **5-pre** | **AP10 probe.** Three `LOG_DEBUG_KV("HOOK_THREAD", "dispatch", …)` sites in `hook_chain.cpp` (DispatchPre / DispatchPost / MidDispatch) + a `log::IsMainThread()` accessor over the existing `g_mainThreadId`. Theory-independent: every fire logs raw tid + is_main, no filter, no early return. **Reason:** before designing C dispatch / offThread routing / Marshal arg-snapshot machinery, observe whether any live hook site actually fires off-thread today. | `363227c` | (probe IS the test — re-run after step 5-pre-fix is the verification gate) |
| **5-pre-fix** | **Probe accessor fix.** Original `g_mainThreadId` captured at `log::Init` time = the kcdx.exe injector's `CreateRemoteThread`, NOT the game's main-Lua-VM thread; every dispatch read `is_main=0` falsely. Fix: split the variable. `g_engineInitThreadId` keeps `log::Init`-time capture (correct for the dev-log `tid=N` suffix consumer); peer `g_gameMainThreadId` captured at `hook_chain::SetLuaState`'s first non-null L (game main thread by construction, runs inside `hooks.cpp`'s first-update-tick handler). Three probe sites swap `IsMainThread()` → `IsGameMainThread()`. `.claude/rules/lua-callback-threading.md` rewritten to name both variables. | `ebc7367` | Probe re-run with menu reach: **HOOK_THREAD 16/16 `is_main=1`** (all dispatches on game main thread), single `tid=39380`. Suite roll-up `suite: 77/85 passing as of update tick` matches the pre-probe baseline — **zero regression** on the existing matrix from the probe instrumentation. |
| **5-main ch.1** | `ChainEntry` tagged union `{Lua: callbackRef \| C: cFn + Signature}`; DispatchPre/Post/DispatchExclusive/MidDispatch branch on `entry.kind`. Lua path byte-for-byte unchanged; C-kind branch a defensive warn-and-skip stub (no C-kind entry reachable until ch.3+4). Foundation refactor. | `b629e14` | Existing matrix 77/85 preserved; 16/16 HOOK_THREAD `is_main=1`; zero C-kind warns (no constructor yet). |
| **5-main ch.2** | `dynamic_call_jit::BuildNativeCallThunk` — native pass-through call-thunk JIT primitive (host x64 ABI end-to-end, no Lua stack) for C Around's `call_original`. Per locked D8 (native typed positional, superseding "symmetric with D1"). Dead code until ch.3+4. | `51374bc` | Existing matrix 77/85 preserved; dead-code-silent (no caller of BuildNativeCallThunk). |
| **5-main ch.3+4** | `kcdxHookInterface` end-to-end + off_thread routing. AP11-safe header appends (kcdxHookCallsiteBehavior + kcdxHookCaptureValue + callsiteBehavior field); HookPayload offThread + cFn; `hook_chain::AddC`/`AddCMid`/`AddCCallsite` + `callOriginalCThunk` + per-entry `cDispatchThunk`; `BuildCDispatchThunk` per-mode codegen + `MidShimEntry`; `src/hook_interface.cpp` 10 thunks; QueryInterface wire-up; NameForHandle/AuthorForHandle → plugin_loader; Lua `off_thread` parser; off-thread dispatch branch (Marshal/Skip warn-once, Error per-fire); HOOK_THREAD probe sites removed. Locked decisions D1/D2/D5/D6/D7/D8 + D-c-fn-abi-1/2/3. | `2f0381b` | cap-35 grew 5→7 rows (off-thread-skip + off-thread-bogus PASS); suite 79/87; zero HOOK_THREAD lines (probe removed); zero D5/C-kind warns. |
| **5-main ch.5** | `test-plugins/cap-36-cpp-hook-interface/` (C++ DLL) + `cap-36-cpp-hook-interface-lua/` (sibling Lua) — end-to-end verification. 7 rows: before/after/around/replace + uninstall + raw-floor + cross-language chaining (Lua + C++ on one target, load-order-ordered). | `f325d57` | Initial launch FAILED all 7 (surfaced the ordering bug below). After the fix: **7/7 PASS**, crosslang=122 (proves the ch.1 ChainEntry tagged union: Lua + C++ on one chain in load order). |
| **5-main fix** | **C++ hook installs failed at Load time.** cap-36's kcdxHookInterface thunks queue `Kind::Hook` entries during `kcdxPlugin_Load` (DllMain-phase), but the apply handler was registered only by `lua_bind_hook::bind()` at first-update-tick — too late; `lua_registry::Append` rejected with "no handler for Kind=1". Fix (Option 1a): split handler registration into per-binder `RegisterHandlers()` called at engine init (`dllmain.cpp`, before `DiscoverAndLoad`), for both `Kind::Hook` and `Kind::Bytes`. Found via `/debug` (no probe — root cause from logs); `docs/known-issues/cap-36 …md`. | `cdd5e7a` | **Live-verified 2026-05-25: suite 86/94; CAP-36 7/7 PASS; cap-35 7/7 PASS; zero `no handler for Kind` / `append_failed` / `D5` / `C-kind` lines.** Sole remaining FAIL CAP-20-target-nosig is the pre-existing parallel-chat-unrelated failure (not a regression). |

| **6** | `include/kcdx/Kcdx.h` (header-only `struct Kcdx` + `kcdx::hook` empowered helpers `Before/After/Around/Replace<Sig,&fn>` + `Try*` variants; per-mode adapter codegen so the author writes a NATURAL callback, hiding the mangled cFn ABI — the AP12 win at C++). Callback is a template non-type param (forced in C++17 — no per-hook context slot in the engine ABI). `docs/cpp/wrapper.md` (single-surface) + `docs/cpp/index.md` flips. | `b5e548a` | (verified at the cap-37 wrapper rows) |
| **6 AP7-fix** | **Test-coverage correction (grow-not-migrate).** Step 6 first MIGRATED cap-36's 4 sub-verb rows onto the wrapper (suite 7→7, deleted raw-floor After/Around/Replace coverage, left the wrapper's own trait/adapter machinery untested). Corrected: cap-36 reverted byte-exact to its raw-floor state (`f325d57`); NEW `test-plugins/cap-37-kcdx-wrapper/` is the wrapper's permanent net (6 rows incl. a `typemap` row exercising i32/f32/ptr DSL-token derivation). Both surfaces permanently tested; suite grows. Lesson saved to memory + `test-suite.md` "be proactive" clause. | `d5c3314` | **Live-verified 2026-05-25: cap-36 7/7 + cap-37 6/6, suite 92/100.** |
| **7** | C++ DLL test plugin — **folded into 5-main ch.5** (`cap-36-cpp-hook-interface` + sibling Lua already cover the 7-row breakdown incl. the cross-language chaining row). No separate `cap-NN-cpp-hook-interface` plugin; step 7's scope is satisfied by cap-36 (raw floor) + cap-37 (wrapper). | (folded into `f325d57` / `d5c3314`) | CAP-36 7/7 + CAP-37 6/6 |
| **sig-gate** | **Sig-mismatch gate (named-target + explicit-`signature` cross-check).** When a hook names a target carrying a verified ABI AND the author also passes an explicit signature, the engine DETECTs the conflict, emits a teaching WARN (`HOOK_SIG_GATE` / `explicit_overrides_verified`) on `!SignaturesCompatible`, then PROCEEDS with the explicit sig (behavior-c — override stays authoritative; silent-trust footgun closed). Both surfaces (`hook_interface.cpp` + `lua_bind_hook.cpp`); `SignaturesCompatible` promoted to `hook_signature.h`. `cap-38` (C++ + Lua): 2 auto `gate-proceeds` (install-proceeded) + 2 `[manual]` `gate-warn` rows. PROBE A established firing-on-a-named-game-target is not plugin-observable (dual-Lua boundary, `lua-bridge.md`) → cpp row asserts install-proceeded; cap-36 owns the C-dispatch firing proof. Cross-cutting doc entry added (both surfaces). | `18f5e6a`, `d8e4ac3`, `f161280` | **Live-verified 2026-05-25: suite 94/102; CAP-38 4/4 PASS** (both WARN lines fired). |
| **8** | **Docs sweep + ledger close.** `docs/cpp/hook.md` WIP banner stripped + rewritten to 6 sub-verb sections (`85e4919`); `docs/cpp/wrapper.md` + `index.md` LIVE flips (step 6); this ledger gains the 6 / 6-AP7-fix / 7 / sig-gate / 8 rows. Aggregator-prefix debt resolved (`57ef446`, direction B). Verification-checkpoint run. | `85e4919`, this commit | (ledger + docs only) |

**Phase 3 sub-1 step 5-main is COMPLETE (live-verified `cdd5e7a`, 2026-05-25).** `kcdxHookInterface` v1 is live + verified end-to-end for real C++ DLL authors: all 6 sub-verbs (Before/After/Around/Replace/Mid/Callsite) + 4 query methods, native C dispatch through the shared `hook_chain` (one chain per target, load-order-decides spans Lua + C++ — proven by the cap-36 cross-language chaining row), off_thread routing parity, AP11-safe ABI.

**Phase 3 sub-1 (extended) is now COMPLETE (closed 2026-05-25).** All steps landed + live-verified: 1–3 (Uninstall, Lua + cap-35), 4 (`kcdxHookInterface` v1 ABI), 5-pre/5-pre-fix (the off-thread probe + thread-id split), 5-main ch.1–5 + fix (`kcdxHookInterface` end-to-end, native C dispatch, cap-36), 6 + 6-AP7-fix (`Kcdx.h` wrapper + cap-37), 7 (folded into cap-36), the sig-mismatch gate (cap-38), and 8 (this docs/ledger close). Suite 94/102 (sole FAIL `CAP-20-target-nosig` is pre-existing, unrelated). **Next in Phase 3: sub-2 `kcdxBytesInterface` (C++ `kcdx.bytes` mirror) + sub-3 `kcdxTrampolineInterface` v2 — `Allocate`+`Export` appended onto the existing trampoline interface as the C++ `kcdx.code` mirror — neither built yet (no `src/bytes_interface.cpp`, no v2 extension to `src/trampoline.cpp`, no header decls).**

**Probe outcome (post-`ebc7367` re-run, 2026-05-24): OUTCOME P.** Pre-committed map per `.claude/rules/results-driven.md` was P / Q / R; P landed:

- **P** (confirmed): all 16 HOOK_THREAD fires across cap-20 (8 sig-hook fires) + cap-21 (4 mid fires) + cap-22 (4 callsite fires) read `is_main=1`. Single `tid=39380` consistent across the full corpus.
- **Q** (not observed): zero `is_main=0` fires; no off-thread dispatch in v1's test surface.
- **R** (false alarm — closed): the "matrix line missing" of the first run was a grep error on the orchestrator's part (`Test suite: …` vs the actual `suite: …`), not an aggregator bug. The aggregator emits at every lifecycle message (kPostLoad / kPostPostLoad / kLuaReady / kInputLoaded / update tick) — verified across all five in the re-run log. No separate cycle needed.

**What Outcome P means for step 5-main's design:**

- **`offThread = "marshal"` ships as `Skip-with-warn-once-when-it-ever-happens` for v1.** Zero hooks in the v1 corpus need an arg-snapshot machinery; building it now would be speculative work against a use case that does not exist. The warn-once line is the regression net — the first time any hook ever fires off-thread, the warn fires loud, and the arg-snapshot work lands in response to real data (its own future cycle).
- **`offThread = "skip"` and `offThread = "error"` ship working** — immediately useful for authors who hook a site they know might fire off-thread.
- **The substantive engine work of step 5-main is the C dispatch path** (ChainEntry tagged union + AddC + the 10 thunks + QueryInterface wire-up + the `HookPayload::offThread` field + the Lua `off_thread` parser). Off-thread routing is the small surface; ABI wiring is the bulk.

**Phase 3 sub-1 (extended) — ALL STEPS DONE (closed 2026-05-25; see the ledger above + the completion note).** The step-by-step plan below is retained as the as-built record. Remaining Phase 3 work is sub-2 (`kcdxBytesInterface`) + sub-3 (`kcdxTrampolineInterface` v2 — `Allocate`+`Export` appended for the `kcdx.code` C++ mirror).

#### Phase 3 sub-2 — `kcdxBytesInterface` (C++ `kcdx.bytes` mirror)

The C++ DLL author's parity surface for the byte-rewrite verb. ONE `Register`
operation (a byte rewrite has a single operation — write `replacement` at a
located site — so unlike `kcdxHookInterface`'s six sub-verbs there is one
install method) + the same query quartet. Same `PatchEntry` payload / exactly-
one-locator rule / `target = "<name>"` name-resolution / `Kind::Bytes`
`lua_registry` deferred apply as the Lua `kcdx.bytes` binder, but taking raw C
inputs from a DLL via the vtable instead of a Lua table.

**Done (live-verified `0ae9e53`, 2026-05-25, suite 96/104 CAP-39 2/2 PASS) — AUTHORITATIVE LEDGER.** Same
contract as sub-1: add a row in the SAME unit of work that lands the step.

| Step | What shipped | Commit | Matrix row(s) / verification |
|---|---|---|---|
| **1** | `kcdxBytesInterface` ABI v1 in `include/kcdx/Interfaces.h` (header-only): `kcdxBytesInterface_Version`, `kcdxInterface_Bytes = 9`, `kcdxBytesHandle`, `kcdxBytesOptions` (name/description/target + the `[advanced]` pattern/addressId/targetSymbol locators, replacement-required, original, module, offset, idempotent, context, anchorString, owningPlugin — NO raw `address` locator: bytes locates a SITE by name/pattern/symbol, mirroring Lua `kcdx.bytes`), the `Register` + `IsApplied`/`GetReason`/`GetName`/`Uninstall` vtable. APPEND-ONLY markers in both struct + vtable (AP11). | `90fd1cf` | (header-only; verified at step 4's C++ DLL plugin) |
| **2** | `src/bytes_interface.cpp` engine impl (`Thunk_Register` + the 4 query thunks; `OwnerFromHandle` + `ResolveTargetName` copied in structure from `lua_bind_bytes.cpp`; teaching-error auto-log to engine + plugin logs) + `src/bytes_interface.h` + `QueryInterface(kcdxInterface_Bytes)` wire in `src/interfaces.cpp` + CMakeLists. Uninstall is the one divergence from `hook_interface`: bytes has NO revert path, so it returns false + logs a teaching line (sub-1 step-2 decision; flipping status while the rewrite stays live would be AP13). | `14a0333` | (engine-side; verified at step 4) |
| **3** | `K.bytes` accessor on the `Kcdx.h` wrapper (`kcdxBytesInterface* bytes` field, fetched in `Init`) + `docs/cpp/wrapper.md` line. | `16f0c98` | (wrapper sugar; verified at step 4) |
| **4** | `test-plugins/cap-39-cpp-bytes/` (C++ DLL) — the AP7 + docs-discipline verification + close-out. `K.bytes->Register` a deferred byte rewrite by NAMED target (`outfit_swap_callsite_aob`, id 1004 — the SAME verified-safe `44 8A F0`→`45 31 F6` cap-01 proves on the Lua side); assert at `kcdxPlugin_PostGameLoad` (handle != 0 && `IsApplied` && `K.memory->ReadBytes(site)==45 31 F6`) + the Uninstall-returns-false no-revert row. `docs/cpp/bytes.md` flipped NYI→LIVE; `docs/cpp/index.md` bytes map row flipped to Built; this ledger entry. | `8436612`, offset-fix `2b2e6f5` | **Live-verified 2026-05-25: suite 96/104; CAP-39-cpp-bytes-register + CAP-39-cpp-bytes-uninstall-rejected 2/2 PASS.** First launch caught a test-offset bug (the named target resolves the AOB start; the rewrite is +13 — `2b2e6f5` set offset=13, matching cap-01; the interface had correctly REJECTED the mismatched-original patch, not a defect). Sole FAIL CAP-20-target-nosig pre-existing/unrelated. |

**Test design note (cap-39).** A self-host marker via `pattern =` over the
plugin's OWN module is NOT supported by the engine: both `scan_engine.cpp`
`ScanAll` and `patch_engine.cpp` `Resolve` scan `pe::ExecutableSections` only,
and `kcdxBytesOptions` has no raw `address` locator — a marker in writable
`.data` is unscannable. The robust observable is therefore the named-target
common path against cap-01's verified-safe SITE (cap-01 is NOT edited). The
same-replacement coexistence with cap-01's `[[patch]]` is conflict_engine
`WriteOnWriteFull` — NOT a rejection (both apply, the second idempotent-skips
to `Status::Applied`), so the observable (`site == 45 31 F6` && `IsApplied`)
holds in every apply interleaving with no fragile ordering dependency.

**Coexist decision (kcdxBytesInterface vs kcdxMemoryInterface::WriteBytes).**
Both ship and are documented as peers (`docs/cpp/bytes.md`):
`kcdxBytesInterface` = the DEFERRED, locator-based, conflict-resolved
registration surface (parity mirror of Lua `kcdx.bytes`); `WriteBytes` = the
IMMEDIATE raw-write floor at an address you already hold. Reach for the
registration for "patch this named site"; for `WriteBytes` when you hold an
address and want the write now.

**Suite count expectation.** sub-2 adds **+2** permanent rows (CAP-39's two);
post-sub-1 baseline was 94/102 (sole pre-existing FAIL `CAP-20-target-nosig`),
so the target was **96/104** with CAP-39 2/2 PASS — **achieved + live-verified
2026-05-25 (suite 96/104, CAP-39 2/2 PASS).**

**Phase 3 sub-2 is DONE (live-verified `2b2e6f5`, 2026-05-25: suite 96/104,
CAP-39 2/2 PASS).** `kcdxBytesInterface` v1 is live for real C++ DLL authors: the single
`Register` deferred byte-rewrite + the 4 query methods, the named-target common
path, the no-revert Uninstall, conflict-engine participation via the shared
`lua_registry` `Kind::Bytes` apply pass. Both surfaces of the byte-rewrite
capability now ship a regression (parity-is-tested). **Next in Phase 3: sub-3
`kcdxTrampolineInterface` v2 (`Allocate`+`Export`, the C++ `kcdx.code` mirror) — shipped, see the sub-3 ledger
below.**

#### Phase 3 sub-3 — `kcdx.code` C++ mirror (`kcdxTrampolineInterface` v2)

The C++ DLL author's parity surface for the code-allocation verb. **Decision:
EXTEND the existing `kcdxTrampolineInterface`, not a new parallel interface.**
The interface already carried the raw `AllocateFromBranchPool`/`LocalPool` floor
(Phase 4); the all-in-one `Allocate(kcdxCodeOptions*)` + standalone `Export`
are the high-level peers of that floor — the same surface, one tier up — so they
append onto `kcdxTrampolineInterface` (version bumped v1→v2, AP11 append-only)
rather than spawning a parallel interface. The Lua peer is the single
`kcdx.code{...}` verb (`src/lua_bind_code.cpp`); the C++ `Allocate` mirrors its
validate → alloc → memcpy/NOP-pad → export-register sequence reading a
`kcdxCodeOptions` struct instead of a Lua table.

**Done (live-verified `38f9dd5`, 2026-05-25, suite 99/107 CAP-40 3/3 PASS) — AUTHORITATIVE LEDGER.** Same
contract as sub-1/sub-2: add a row in the SAME unit of work that lands the step.

| Step | What shipped | Commit | Matrix row(s) / verification |
|---|---|---|---|
| **1** | `kcdxTrampolineInterface` ABI **v2** in `include/kcdx/Interfaces.h` (header-only): `kcdxTrampolineInterface_Version` bumped to `2u`, `kcdxCodePool` enum (`_Branch=0`/`_Local=1`), `kcdxCodeOptions` (owningPlugin/name/bytes/bytesSize/size/pool/exportName, with APPEND-ONLY marker), and `Allocate(const kcdxCodeOptions*) -> void*` + `Export(kcdxPluginHandle, const char*, uintptr_t) -> bool` appended after `AllocateFromBranchPool`/`LocalPool` (AP11 append-only — a v1 plugin finds the raw pool methods at the same offsets). | `519dbb2` | (header-only; verified at step 4's C++ DLL plugin) |
| **2** | `src/trampoline.cpp` engine impl: `Thunk_Allocate` (validate name + bytes/size rule + size≥bytesSize, allocate per `opts->pool` via the existing `AllocateBranch`/`AllocateLocal`, memcpy bytes to the head, NOP-pad the tail to size, register `exportName` via `symbols::Register` — dotted-name + collision are hard failures mirroring `lua_bind_code.cpp` exactly) + `Thunk_Export` (standalone `symbols::Register` for a held address); both appended to `g_iface` in vtable order. Owner identity via `AuthorForHandle`/`NameForHandle` (same as bytes/hook). | `2ce90ee` | (engine-side; verified at step 4) |
| **3** | (folded — no code change owed.) `K.code` on `Kcdx.h` already points at `kcdxTrampolineInterface` and `Init` already fetches it at `kcdxTrampolineInterface_Version` (now `2u`), so the bump is transparent to the wrapper — `K.code->Allocate`/`Export` are reachable with no wrapper edit. Verified by step 4 exercising both through `K.code`. | — | (verified at step 4) |
| **4** | `test-plugins/cap-40-cpp-code/` (C++ DLL) — the AP7 + docs-discipline verification + close-out. `K.code->Allocate` (plain region: read-back + NOP-pad + cast-to-`int(*)()`-and-call==42 executable proof) + `K.code->Allocate(exportName=)` (resolves via `K.api->ResolveSymbolAs(K.self, "cap40_region")`) + `K.code->Export` standalone (resolves via `ResolveSymbolAs`). `docs/cpp/code.md` flipped NYI→LIVE; `docs/cpp/index.md` code map row updated to full-surface Built; `docs/cpp/wrapper.md` K.code line confirmed accurate (no edit owed); this ledger entry. | `38f9dd5` | **Live-verified 2026-05-25: suite 99/107; CAP-40-cpp-code-allocate / -export / -export-standalone 3/3 PASS** (region executed → 42; both exports resolved via ResolveSymbolAs(self,…)). Sole FAIL CAP-20-target-nosig pre-existing/unrelated. |

**Test design note (cap-40).** Self-hosting + deterministic: code allocation is
PLUGIN-OWNED pool memory (no game site), so there is NO cap-01-style conflict
entanglement and NO dual-Lua boundary issue — every observable is verified
in-process. The allocate row does the **call-it-and-assert-42 executable proof**
(the bytes `B8 2A 00 00 00 C3` = `mov eax,42; ret` are valid self-contained
`int()` machine code — no relocations, no external calls — in a
`PAGE_EXECUTE_READWRITE` branch-pool region, so cast-and-call is safe and is the
strongest falsifiable proof the region is genuinely executable). The export rows
resolve via `ResolveSymbolAs(K.self, "<bare>")` — NOT bare `ResolveSymbol`:
`symbols::Register` stores the export under the `<author>.<plugin>.<bare>`
namespace key, so an anonymous bare lookup resolves other-only and misses the
plugin's OWN export; threading `K.self` as owner lets the self-tier resolve it
(the exact call the Lua side uses for a self-export).

**Extend-not-new-interface decision (kcdxTrampolineInterface v2).** `Allocate` +
`Export` are the high-level peers of the raw `AllocateFromBranchPool`/`LocalPool`
floor that already lived on `kcdxTrampolineInterface` — same surface, one tier
up — so they append (version bump v1→v2, AP11 append-only) rather than spawning
a parallel interface. The raw pool methods coexist (`Allocate` is built
on them); documented as peers in `docs/cpp/code.md`.

**Suite count expectation.** sub-3 adds **+3** permanent rows (CAP-40's three);
post-sub-2 baseline was 96/104 (sole pre-existing FAIL `CAP-20-target-nosig`),
so the target is **99/107** with CAP-40 3/3 PASS. (If only the allocate + one
export row were kept, +2 → 98/106; all three shipped.)

**Phase 3 sub-3 is DONE (live-verified `38f9dd5`, 2026-05-25: suite 99/107, CAP-40 3/3 PASS).** `kcdxTrampolineInterface`
v2 is live for real C++ DLL authors: the all-in-one `Allocate` (alloc + fill +
NOP-pad + export), the standalone `Export`, and the raw pool floor they peer,
with bare-name export publish + self-tier `ResolveSymbolAs` consume.
**Phase 3 (C++ DLL API parity) is now COMPLETE** — `kcdx.hook` (sub-1,
`kcdxHookInterface` + `Kcdx.h`), `kcdx.bytes` (sub-2, `kcdxBytesInterface`), and
`kcdx.code` (sub-3, `kcdxTrampolineInterface` v2) all shipped, each with both
surfaces under regression (parity-is-tested). The remaining core authoring verbs
(`kcdx.on`/`kcdx.command`/`kcdx.publish`) already had their C++ peers built in
earlier phases (the messaging + console interfaces); `kcdx.scan` + author-declared
targets + `kcdx.alias` remain NYI on both surfaces and belong to later restructure
phases (per `docs/cpp/index.md`'s map), not Phase 3 C++-parity scope.

**Step 5-main — `src/hook_interface.cpp` thunks + native C dispatch in hook_chain.** Locked decisions from the pre-dispatch round (D1 / D2 / D3 / D4 / D5 / D6 / D7 / D8):

1. **`ChainEntry` tagged union** in `src/hook_chain.cpp` §2: `enum class Kind { Lua, C };` discriminating `{ int callbackRef }` (today's path, byte-for-byte unchanged behavior) vs `{ void* cFn, hook_signature::Signature cSig }` (the new C path). Trivially movable; stable in `std::vector<ChainEntry>`. (D5.)
2. **`DispatchPre` / `DispatchPost` / `MidDispatch` branch on `entry.kind`.** Lua entries: existing `lua_rawgeti` + `lua_pcall` + push/write-slot path, UNCHANGED. C entries: call the C function pointer directly off the asmjit thunk's `parameters_t` slot array, using `entry.cSig` for widths. **No Lua crossing for C-only chains** — rejected the Lua-closure-wrap path on `lua-precision.md` (pointer args round at 16 MB through Lua stack — correctness bug, not perf).
3. **C `before` / `replace` callback ABI: out-parameter array.** `void cFn(uintptr_t args[], int* outCount, /* typed args... */)` — author fills `args[]` + sets `*outCount` to the number of replaced slots (0 = no mutation). Engine `WriteSlot`s per `entry.cSig`. (D1.) **C `after` callback ABI:** `<ReturnType> cFn(<ReturnType> origReturn, /* typed args... */)` — return-only mutation. Parity-preserving — Lua `after` today only writes the return slot. (D2.) **C `around` `call_original` primitive:** symmetric with D1 — `<ReturnType>(*)(uintptr_t args[], int* outCount, /* typed args... */)`. Implemented via a new `dynamic_call_jit::BuildCCallThunk` (parallel to the existing `BuildLuaCallThunk`). (D8.)
4. **`hook_chain::AddC`** — new public function parallel to `Add` taking `{void* cFn, Signature cSig, plugin, priority, name, handleId}`. Branches on `payload.callsiteScope` for AddCallsite-equivalent. Reuses `ResolveLocator`. Same `g_chainsMu` discipline.
5. **`HookPayload::offThread`** field (engine-internal, no AP11 binding). Default 0 (Marshal). Plumbed through both Lua and C call paths.
6. **`HookPayload::cFn`** field (one `void*`, defaults nullptr). `ApplyHookEntry` branches: `if (p->cFn) AddC(...) else Add(...)`. ONE `Kind::Hook` register; mutex enforced by which surface populated the payload + a defensive `assert(!cFn || callbackRef == LUA_NOREF)`. (D5.)
7. **`src/hook_interface.cpp` (NEW)** — 10 thunks. Six sub-verb installers (`Thunk_Hook_Before/After/Around/Replace/Mid/Callsite`) each build a `HookPayload` with `mode` set to the matching enum + threads ALL `opts->*` fields onto the payload, synthesizes a `lua_registry::Entry` with `kind=Hook` + the C `cFn` set, calls `Append` → handleId. Resolves signature via `opts->signature` parse OR `address_library::ResolveSignatureByName(target, author, plugin)`. Four query thunks (`IsApplied/GetReason/GetName/Uninstall`) mirror the Lua-side semantics already landed in steps 1–3.
8. **`src/interfaces.cpp::Thunk_QueryInterface`** — wire `case kcdxInterface_Hook` returning `kcdx::hook_interface::GetInterface()`.
9. **`src/lua_bind_hook.cpp`** parses `off_thread = "marshal" | "skip" | "error"` into `HookPayload::offThread` (Lua parity per `lua-api-surface.md`).
10. **Dispatch off-thread branch:** at the top of `DispatchPre` / `DispatchPost`'s per-entry block / `MidDispatch`, when `!log::IsGameMainThread()`: for `offThread == Marshal` OR `offThread == Skip`, warn-once-per-hook (dedup key: `entry.handleId` for chain entries, `chain.targetVa` for mid chains — D7) + skip the callback; for `offThread == Error`, log error + skip. The HOOK_THREAD probe sites + the duplicate-`ResolveChainForDispatch` of step 5-pre are removed in this step (the real dispatch branch supersedes the probe).
11. **`NameForHandle` / `AuthorForHandle`** move from `src/interfaces.cpp` (anon-namespace, today TU-local) to `src/plugin_loader.{h,cpp}` (they read `g_plugins` which lives there). Two existing callers in `interfaces.cpp` migrate; new caller in `hook_interface.cpp` includes `plugin_loader.h`. (D4.)
12. **C Mid + C Around wire end-to-end** (D6 = wire everything). The new `BuildCCallThunk` JIT path supports both; C Mid's capture-handle dispatch is parallel to MidDispatch's Lua path (slot base + per-slot type → C array form, NOT the Lua userdata form).
13. **CMakeLists.txt** appends `src/hook_interface.cpp`.
14. **`docs/outstanding-work/phase-3-sub-1-extended.md` step-5 prose rewrite** lands in the SAME commit as step 5-main, so the handoff doc names the engine work that actually shipped (rather than the elided original prose) — including the cross-language chaining row added to step 7's matrix (Lua `kcdx.hook.before(target, …)` + C++ sibling-plugin `K.hook->Before(target, fn, opts)` on the same target, load-order-ordered, both fire in order on one invocation — the test the dispatcher generalization is FOR).

**Step 6 — `include/kcdx/Kcdx.h` wrapper + empowered helpers. DONE (`b5e548a`, live-verified 2026-05-25: suite 86/94 held; wrapper test-coverage corrected post-verify — see the AP7 note below).** Header-only `struct Kcdx`. `Init(api, author, plugin)` fetches every shipped sub-interface; stamps author + plugin for identity threading. Templated helpers in `kcdx::hook` namespace, one per sub-verb (`Before<Sig>` / `After<Sig>` / `Around<Sig>` / `Replace<Sig>`), void + auto-logging on failure; `TryBefore<Sig>` etc. return handle. **Floor-3 dropped** — floor 4 (raw `K.hook->Before(target, void* callback, opts)`) IS the unchecked form by construction. `Mid` + `Callsite` NOT in the empowered helpers — per AP12's expert-form framing they take captures / sub-locators that don't templatize cleanly.

As-built deviations from the planned shape above (decided during the cycle):

- **Callback is a TEMPLATE non-type parameter** — `Before<Sig, &fn>(K, "target")`, NOT the planned `Before<Sig>(K, "target", &fn)`. FORCED in C++17: the engine's hook callback ABI carries no per-hook context slot (only Mid does), so each author fn must bake into a compile-time-distinct `static` adapter whose address is the engine's `void*`; a runtime-arg form would clobber on two same-signature hooks (a correctness defect). Same idiom as SKSE trampolines. The wrapper GENERATES the per-mode adapter (decision A2) so the author writes a NATURAL callback typed in the original target's signature (Before by-reference: `void cb(int& seed){ seed+=1; }`) — the mangled cFn ABI never appears in author code (the AP12 win at C++).
- **`opts.signature` derived only on the no-name path (B2)** — null on the named-target common path (the engine carries the verified ABI; the author never re-authors it). cap-36's rows use raw `opts.address`, so they exercise the derive-the-DSL-string branch.
- **`K.addr` / `K.test` are NOT distinct interface-pointer fields** — no `kcdxAddrInterface` / `kcdxTestInterface` exists; Address Library + test reporting are root-`kcdxInterface` accessors (`K.api->ResolveAddress*` / `K.api->ReportTestResult`), documented in `docs/cpp/wrapper.md`. `K.code` = `kcdxTrampolineInterface*`.
- **`docs/cpp/wrapper.md` marked single-surface** (D1) — C++ template sugar over `kcdxHookInterface`; Lua's `kcdx.hook.*` is the native peer (Lua's dynamic marshaling means no mangled ABI for the Lua author to hide), so no Lua mirror is owed.

**Step 6 AP7 correction (`d5c3314`, live-verified 2026-05-25: cap-36 7/7 + cap-37 6/6, suite 92/100).** Step 6 first satisfied its test bar by MIGRATING cap-36's 4 sub-verb rows onto the wrapper — which did NOT grow the suite, DELETED cap-36's raw-`kcdxHookInterface` coverage of After/Around/Replace, and left the wrapper's own machinery (the type→DSL `sig_traits`/`dsl_token` trait, the by-ref Before write-back, the void/non-void After split, the typed Around `call_original`, the Try* handle path) with no falsifiable row (AP7 + the test-suite.md grow-not-migrate mandate). Corrected: **cap-36 reverted to the PURE raw floor** (byte-exact to its chunk-5 state `f325d57`; it does NOT use `Kcdx.h`) — the permanent raw-`kcdxHookInterface` regression net for all 6 sub-verbs. **NEW `test-plugins/cap-37-kcdx-wrapper/`** is the wrapper's permanent net: 6 rows (4 sub-verbs via the empowered helpers + the Try* handle row + a `typemap` row over `int(int, float, void*)` that exercises i32/f32/ptr token derivation — the catch for a future trait regression). Both surfaces permanently tested (parity-is-tested); suite grows by 6.

**Step 7 — C++ DLL test plugin `cap-NN-cpp-hook-interface`.** Row breakdown (option 2 + the cross-language chaining row added per step 5-main scope item 14): 4 sub-verb rows (Before / After / Around / Replace) + 1 Uninstall row (peer of cap-35-uninstall-basic) + 1 raw-floor row (raw `api->QueryInterface` bypass of `Kcdx.h`) + **1 cross-language chaining row** (Lua plugin + C++ sibling-plugin on the same target, load-order-ordered, BOTH fire). 7 rows total. Plugin shape: `test-plugins/cap-NN-cpp-hook-interface/{CMakeLists.txt, cap-NN.cpp, kcdx.toml}` (next free cap ID verified at step-7 dispatch).

**Step 8 — docs sweep + restructure-plan ledger close + verification-checkpoint.** `docs/cpp/hook.md` strips step 4's WIP banner; rewrites as 6 sub-verb sections; cites rule 4a; shows empowered + raw-opts + raw-interface examples for the common Before/After/Around/Replace path. `docs/cpp/wrapper.md` (new) — `Kcdx.h` reference (3 floors). `docs/cpp/index.md` — `hook.md` flipped NYI→LIVE; `wrapper.md` added. This ledger gets its step-5-main / 6 / 7 / 8 rows added (one row per commit, parallel to steps 1–5-pre-fix). One game launch confirms cap-NN-cpp-hook-interface rows PASS, cap-35 still 5/5, no regression on existing hook rows.

**Out-of-scope debt surfaced this sub (not blocking; tracked):**

- **Aggregator log line mismatch** (cosmetic, blocks no work) — **RESOLVED.** The test aggregator emits `suite: X/Y passing as of <msg>` (e.g. `suite: 77/85 passing as of kInputLoaded`); a spread of docs/comments/rules/skills had referenced `Test suite: X/Y passing`. Fix direction (B) was taken: the docs/comments/rules/skills were updated to match the emitted `suite:` prefix — NO code change (the `src/test.cpp` emit sites were already internally consistent and stay as-is, including the gated-off line `N test_suite_only plugin(s) gated off (dev mode disabled; ...)`).
- **Real off-thread `Marshal` (arg-snapshot machinery)** is deferred per Outcome P. When the warn-once-per-hook line ever fires in the wild (cap-NN matrix row or a user-shipped plugin), a follow-on cycle designs the arg-snapshot path against real data. The deferral is user-blessed-with-a-real-next-step (the warn line is the trigger), satisfying AP13.
- **Step 5-pre cleanup**: the three HOOK_THREAD probe sites + the duplicate `ResolveChainForDispatch` in DispatchPre's probe block + the indexed-loop rewrite of DispatchPost are scheduled for removal in step 5-main (the real dispatch branch supersedes them). The `log::IsGameMainThread()` accessor + the `g_gameMainThreadId` capture site stay — step 5-main consumes them.
- **Step-review L-finding (5-pre-fix)**: `log::IsMainThread()` body now reads `g_engineInitThreadId` — the name now lies about the body. Zero remaining callers in `src/` today. Either rename to `IsEngineInitThread()` (no callers to update) or drop the accessor entirely and have `FormatDevLine` read `g_engineInitThreadId` directly. Trivial follow-up; fold into step 8 or its own `/commit`-direct.
- **Step-review L-finding (5-pre-fix)**: `s_gameMainThreadCaptured` static-bool sentinel in `SetLuaState` is defensive — `SetGameMainThread` is already idempotent. Comment justifies it on intent-clarity grounds. Either keep as-is or drop the sentinel + shrink the comment. Cosmetic.
- **Sig-mismatch gate — RESOLVED (landed `18f5e6a` + cap-38 row finalization `d8e4ac3`).** ~~On the named-target + explicit-`opts.signature`-both-present combination, `hook_interface.cpp` ResolveSignature (`:252-258`) trusts the explicit signature outright and never cross-checks it against the verified Address-Library ABI it has in hand — so a named target + a WRONG explicit signature is silently accepted (an author footgun on the exact surface AP12 protects).~~ Fixed with the user-decided **behavior-c: WARN + keep the explicit sig** (2026-05-25). When a hook names a target carrying a verified ABI AND the author also passes an explicit signature, the engine consults the verified ABI to DETECT the conflict, emits a teaching WARN (`HOOK_SIG_GATE` / `explicit_overrides_verified`) when they are NOT `SignaturesCompatible`, then PROCEEDS with the explicit sig (the deliberate-override case stays authoritative). Both surfaces gated: `src/hook_interface.cpp` `ResolveSignature` + `src/lua_bind_hook.cpp` signature resolution; `SignaturesCompatible` promoted from a `hook_chain.cpp` file-local to a `hook_signature.h`-declared surface (body still in `hook_chain.cpp`). Regression: **cap-38** (`cap-38-sig-mismatch-gate/` C++ + `cap-38-sig-mismatch-gate-lua/` Lua, both naming `kcdx.lua_settable` + the wrong `void (ptr L)` sig) — an auto `gate-proceeds` row per surface (handle non-zero / applied / fires) + a `[manual]` `gate-warn` log-assert row per surface (orchestrator greps the WARN KV line; pre-fix NO line → FAIL). Kcdx.h's named common path leaves `opts.signature` null so the wrapper never trips this; a raw-floor caller still benefits from the WARN.

### Phase 4 — migrate test suite + engine builtin — **DONE**

**Status (2026-05-28 audit):** every shipping plugin is on the new code surface (`plugin.lua` / DLL using `kcdx.*` Lua + `kcdx*Interface` C++). Audit: `grep -rE "^\[\[(patch|hook|mid_hook|trampoline|scan)\]\]" test-plugins/**/kcdx.toml kcdx-engine/builtin/**/kcdx.toml` returns ONE match — `test-plugins/cap-49-fix-stray-table/kcdx.toml` — which is a Phase-5 reject FIXTURE (the `[[patch]]` IS the stray top-level table whose silent-unparse-then-load-reject is the row's contract; see cap-49-observer's `cap-49-reject-stray-table` row). Phase 4a's pilot finding stays as the historical anchor; Phase 4b/4c shipped without a doc-recorded landing commit pair (the migration was incremental across the Phase 5 narrow cut and after).

**Phase 4a — pilot plugin first. DONE (live-verified `1d0faf1`, 2026-05-25: suite 99/107, CAP-01 PASS).** Migrated `cap-01-patch` end-to-end from `[[patch]]` TOML + verifier DLL → a pure-Lua `kcdx.bytes` plugin (same site/bytes, self-verifies at `kcdx.on("ready")` via `h:applied()` + a `kcdx.scan` read-back). Suite green with cap-01 on the new path, the other plugins still on the legacy TOML path. **Pilot finding (informs 4b):** the migration must move a `[[patch]]` `pattern=` locator that spans the mutated bytes to a `target=`/`address_id` NAME locator — the first launch FAILED because cap-01's pattern AOB ended in `44 8A F0` and cap-39 (same site, by name) rewrote it first, so the pattern scanned 0 matches; switching cap-01 to `target="outfit_swap_callsite_aob"` + `offset=13` (the disassembler-test name locator) fixed it (cap-39-first → idempotent-skip → applied). Original plan flow below retained as the as-built record.

Before the batch migration, migrate ONE test plugin (recommended: `cap-01-patch` — simplest case, exercises `kcdx.bytes`) end-to-end to the new API. Verify the suite stays green with this one plugin on the new path and the others still on the legacy TOML path. This validates the new API actually works at the plugin author level, not just internally. If something breaks, the diagnostic surface is tiny (one plugin to look at).

**Phase 4b — batch migration. DONE.** Every test plugin in `test-plugins/` ships behavior in `plugin.lua` / DLL using `kcdx.*` / `kcdx*Interface`. The corpus migration completed without an explicit landing-commit pair recorded here; the doc lagged the work. Audit confirms 0 legacy behavior tables in production manifests (the one `[[patch]]` survivor is `cap-49-fix-stray-table`'s reject fixture, by design).

**Phase 4c — engine builtin migration (bugsplat-filename-fix). MANIFEST-ONLY STUB ships; the DLL fix is BLOCKED on Phase 11.** `kcdx-engine/builtin/bugsplat-filename-fix/kcdx.toml` exists as a manifest-only stub (no `plugin.lua`, no DLL, `enabled = false` per the doc's earlier note). The actual filename-fix DLL was deferred because the surface the fix needs — Lua-in-before_game / DllMain-timed installs on a foreign-module export — lands at Phase 11 (per the bugsplat probe carried over from Phase 6 + the `before-game-hooks.md` outstanding-work spec). The `f03ca83` debug-agent commit that closed the unrelated step-4 boot AV is NOT this builtin's landing.

**End state (achieved for plugins; pending for bugsplat builtin):** every shipping test plugin uses the new APIs; the old TOML behavior parsers were genuinely unused once the migration completed (Phase 5 then deleted them, narrow cut). Bugsplat-filename-fix lands at Phase 11.

### Phase 5 — delete old TOML behavior parsers — **DONE (narrow cut, live-verified `95854fe`, 2026-05-26: suite 102/109, only the pre-existing CAP-20-target-nosig FAIL; 55/55 manifests valid).**

- Dropped ~642 LOC from [src/config.cpp](../../../src/config.cpp): `ParseOnePatch`, `ParseOneHook`, `ParseOneMidHook`, `ParseOneTrampoline`, `ParseOneScan` + their array-walking blocks in `LoadOneFile` + the now-unused `#include "scan_engine.h"`. `LoadOneFile` is manifest-only ([kcdx]/[plugin]/[entrypoints]).
- The bugsplat builtin's `[[patch]]` (the last legacy behavior table) was retired with it — the folder is now a metadata-only Phase-11 stub (`enabled=false`), pointing at [before-game-hooks.md](../before-game-hooks.md) for the real fix.
- **NARROW cut (user-decided):** the legacy globals (`g_patches`/`g_hooks`/`g_mid_hooks`/`g_trampolines`) + their consumers (`conflict_engine`/`hook_engine`/`ldr_notify`/`interfaces.cpp` GetConflictReport legacy loops) + the config.cpp-local sort lambdas over them are NOT deleted — they stay declared, are never populated (always empty), iterate inert. Removing that dead consumer machinery is a separate later pass.
- **No migration WARN:** kcdx is prerelease with no external plugins, so a stray legacy `[[patch]]` is silently unparsed (boots fine, no behavior) — fix-forward, nothing to be compatible with. (Supersedes the original "a WARN line tells the user" bullet — that was for a shipped-product compatibility scenario that can't occur prerelease.)
- **Follow-up (tracked):** `toml-schema.md` + any rule doc still describing `[[patch]]`/`[[hook]]`/etc. as live schema is now stale and needs a rule-doc rewrite (governance-adjacent; deliberately not folded into the code-deletion commit).

**End state:** TOML is genuinely manifest-only. Migrated suite still green from the new path (102/109, sole FAIL pre-existing + unrelated) — confirming the deleted parsers were genuinely unused.

### Phase 6 — probe code cleanup (narrow subset DONE + live-verified `3f66c47`, 2026-05-26: suite 102/109, PROBE Q silent; bugsplat probe + issue carried to Phase 11)

- **DONE** — deleted `src/probes/createfilew_probe.*` (PROBE R — answered: the broken-dmp CreateFileW caller is BugSplat64.dll; that finding reframed the whole investigation, question closed) + its `Install()` call site and `#include` in `hooks.cpp` (and the PROBE R comment), plus its `CMakeLists.txt` source row.
- **DONE** — deleted Phase5gReadback dead code (the function + its call site in `HookedUpdate`; the Phase5gReadback-only locals `t`/`L_now` and the `tick_count` static went with it — `done` is shared with surviving registration logic and stays).
- **KEEP** `src/probes/bugsplat_ctor_probe.*` (PROBE S/T) + the `ArmLdrInstall()` call site in `dllmain.cpp` — it is the Phase-11 before_game-hook install machinery (the proven prototype, live-confirmed 2026-05-26; [before-game-hooks.md](../before-game-hooks.md) §5). It relocates/graduates into the real builtin at Phase 11, NOT deleted here. The "remove after fix" lifecycle comments were relabeled to KEEP-for-Phase-11 this commit.
- Keep PROBE Q + PROBE H (permanent canaries — untouched).
- **DEFERRED to Phase 11** — [docs/known-issues/BugSplat dmp files don't reach disk for AV crashes.md](../../../docs/known-issues/BugSplat dmp files don't reach disk for AV crashes.md) stays OPEN until the before_game-hook fix lands (designed + de-risked, not built). Do NOT graduate to `closed/` yet.

### Phase 7 — capability gating rework + per-version survival manifest fields

**Zone-rework subset — DONE + live-verified (`54d7d4d`, 2026-05-26: suite 102/109; COMP-13-zone-reject, COMP-03, COMP-11 all PASS — the zone-via-new-key gate chain + the priority orderings survived the rename; 55/55 manifests valid).** The per-plugin manifest hints were renamed from `[plugin].default_position` / `[plugin].default_priority` to a new per-plugin `[load_order]` table (`zone` + `priority`), a HARD rename: `ParsePluginManifest` (`src/config.cpp`) reads ONLY the new `[load_order]` keys; the legacy `[plugin]` keys are no longer read (silently ignored). The internal `Manifest.defaultPosition` / `Manifest.defaultPriority` fields and their `load_order.cpp` consumers were left unchanged (only the TOML keys read FROM changed). The static per-API `requireZone` gate (zone_gate) was already live and is unaffected — it reads the internal zone field that `ParsePluginManifest` still populates. All plugins carrying the old keys (11: the 3 named here plus cap-36/-36-lua/-37/-38/-38-lua, comp-03-A/-B, comp-11-a/-b) were migrated to `[load_order]`.

**Survival manifest fields — SUPERSEDED 2026-05-27 by the §11.8 STREAMLINE three-track model. The `authored_against_game_version` / `on_changed_function` fields below WILL NOT BE BUILT as specified.** The original mechanism — *"the engine pre-checks every hooked function's hash against the SQLite reference DB and refuses-to-install on drift"* — rests on auto-tracking all ~321K binary functions across versions, which feasibility arithmetic killed (see `parallel-ghidra-research.md` §11.8 "STREAMLINE"). The streamlined model has THREE tracks (curated ~139 / author-declared via `kcdx.declare(module, name, versions)` / bulk DEV-DB discovery only). The pre-check primitive applies ONLY to the curated track (and is now `refdb::ResolveByName` itself — the engine carries per-version address + signature + content_hash + length per curated entity, see Phase 9.1). For author-declared (Track 2) the safety model becomes (a) author-supplied per-version `kcdx.declare` resolution + (b) **recovery + rollback at install/runtime** for any Track-2 plugin running on a version it didn't declare for (see the new outstanding-work bullet "Recovery + rollback for Track-2 plugins…"), not pre-check.

**What SURVIVES of the original below + what SUPERSEDES:**
- ✅ The `warn_and_try` default *posture* (warn, attempt anyway) SURVIVES — it aligns with the new default-ON UX. The user keeps mods running across patches by default; warnings/badges surface what may break.
- ✅ The `refuse_entry` opt-in for safety-critical entries (save-game serialization) SURVIVES.
- ❌ The mechanism description below — "engine knows the hashes of every function the plugin touched" — is SUPERSEDED. For curated targets, the maintainer carries the cross-version mapping (~139 by hand, per-patch — feasible); for author-declared targets, the AUTHOR provides per-version patterns via `kcdx.declare` and the engine resolves at launch.
- ❌ "Engine derives 'uses hash-checked primitive' from runtime registrations" is SUPERSEDED — the new derivation is "the plugin uses Track-2 (`kcdx.declare`) declarations, and the running game version isn't in their declared set."
- ❌ The per-call `on_changed = "refuse_entry"` override syntax is SUPERSEDED in part — the per-call override still makes sense for safety-critical entries, but the trigger (a hash check vs the new "pattern doesn't resolve / sanity check fails") differs.

**The current direction is `kcdx.declare(module, name, [versions_kv])` (§11.8.1 of parallel-ghidra-research.md) — not built yet (no `lua_bind_declare.cpp`); its landing surface is Phase 9.2's re-spec, not this Phase-7 block.** The prose below is a HISTORICAL anchor of the original design, kept to document what was REPLACED, not what to build.

- [src/load_order.cpp](../../../src/load_order.cpp): `DeriveMinZone` becomes a read of `[load_order].zone` from the manifest. No more vector scans.
- Per-API `requireZone` check live (Lua + C++).
- Verify: a `kcdx.toml` with `[load_order].zone = "before_game"` whose `plugin.lua` calls `kcdx.hook.before(...)` (an after_game-only API in this engine version) is REJECTED at manifest validation with the clear engine-log error message documented in "Capability gating."

**`authored_against_game_version` + `on_changed_function` manifest enforcement (lands in this same phase).** Two new manifest fields under `[plugin]`:

- `authored_against_game_version = ["1.5.1164953"]` — **array** of KCD2 builds the author wrote and tested the plugin against. Required when the plugin calls any hash-checked primitive (`kcdx.hook.*`, `kcdx.statement.*`, `kcdx.bytes` with a named locator, `kcdx.code{target_symbol}`, OR a `kcdx.behavior.*` whose underlying call references a function — though behavior-only plugins are exempt because the engine resolves underlying calls + knows their hashes itself). Pattern-only plugins (`kcdx.bytes{ pattern = "..." }` only) exempt — pattern resolution is its own survival check. Engine derives "uses hash-checked primitive" from runtime registrations during the registration pass (Option A from the design discussion; no static manifest analysis). The array shape lets an author retest on a new game version and add it to the list without removing the prior — `["1.5.1164953", "1.6.x"]` means "I tested on both."
- `on_changed_function = "warn_and_try"` (default) `| "refuse_entry"` — per-plugin posture for what happens when a function the plugin touched has a different hash on the running game version than ANY of the versions the plugin was authored against. **Default is `warn_and_try`** — the entry attempts to apply, the engine warns about the hash drift, the entry runs anyway. This prevents the failure mode "user's mods all silently break when KCD2 updates"; most patches are tolerant to small underlying function changes, and a warning is loud enough that author + savvy users know there's a risk. **`refuse_entry` is opt-in** for safety-critical entries (typically save-game serialization patches where wrong bytes corrupt user saves) — author explicitly declares "if the function changed at all, skip my entry rather than risk it."
- Per-call override: `kcdx.hook.before(module, target, callback, { on_changed = "refuse_entry" })` overrides the per-plugin default for that one entry.

**Plugins do NOT auto-drop on game updates.** The default `warn_and_try` posture means a user upgrading KCD2 keeps every plugin's entries running — entries targeting unchanged functions silently pass; entries targeting changed functions log a warning and proceed. Authors who want auto-skip behavior opt INTO `refuse_entry` per-plugin or per-entry. This is the cornerstones answer: the engine doesn't take options away from users, it surfaces information.

**Hard rejection on missing field (when required).** A plugin that calls a hash-checked primitive but omits `authored_against_game_version` is rejected at manifest validation. The error message includes the literal TOML line pre-filled to the current `runtimeGameVersion` AND explains when this requirement appeared:

```
[ERROR] plugin 'redmoon.outfit-fix' missing required field 'authored_against_game_version'.
This is required because your plugin calls kcdx.hook.before at plugin.lua:42 (a hash-checked
primitive). Currently running KCD2 1.5.1164953. If you tested your plugin on this version, add:
    authored_against_game_version = ["1.5.1164953"]
to the [plugin] section of your kcdx.toml. Behavior-only plugins (only kcdx.behavior.set calls)
are exempt from this requirement; this rejection appeared because you added a function-level
primitive call that requires the baseline anchor for the per-version survival check.
```

Author's first interaction with the field is one paste from the error message. New authors using `kcdx --init-plugin` (Phase 1) get the field pre-populated automatically and never see this error.

**Console command `kcdx_show_version`** prints the current KCD2 build number in the exact format the manifest accepts. Author updating to a new game version runs the command, copies the value into their `authored_against_game_version` array, retests.

**Verification gate:** test plugin authored against `["1.5.x"]` with a hook on a function whose simulated 1.6 hash differs — with `on_changed = "warn_and_try"` (default) the entry proceeds + warns; with `on_changed = "refuse_entry"` the entry skips + warns; other entries in the same plugin continue regardless. Behavior-only plugin (only `kcdx.behavior.set` calls) without `authored_against_game_version` still loads (exempt). Plugin updated to `["1.5.x", "1.6.x"]` on 1.6 simulated hash → no warning (1.6 is in the tested-against list).

### Phase 8 — ASI-loader cleanup (docs)

- Word-search the repo for "kcdx.asi", "Ultimate ASI Loader", "dinput8". Each hit removed or annotated as legacy. **DONE (2026-05-26):** prescriptive-stale references in PRESCRIPTIVE docs prose + `src/` code-comments reframed `.asi`/Ultimate-ASI/dinput8 → `kcdx.dll` / own-launcher injection. Comments + prose only — zero code/logic changes. Files touched: `docs/dev-mode.md`, `docs/load-order.md`, `docs/logging.md`, `docs/design-gaps.md`, `docs/phase5c7b-plan.md`; `src/dllmain.cpp`, `src/ldr_notify.{cpp,h}`, `src/log.cpp`, `src/hooks.cpp`, `src/config.cpp`, `src/address_library.h`, `src/watchdog/main.cpp`, `src/watchdog_spawn.h`, `src/probes/bugsplat_ctor_probe.{cpp,h}`. Already-correct legacy framing CONFIRMED (not touched): `.claude/rules/loader-architecture.md`, `docs/loader-architecture.md`, `README.md`, `CLAUDE.md`. Survivor sweep: every remaining `kcdx.asi`/`dinput8`/`Ultimate ASI Loader` reference in `docs/` + `.claude/` + `CLAUDE.md` is exempt/historical (migration.md, VERIFY_PHASE*, design.md SUPERSEDED, restructure-plan.md as-built record, known-issues/closed/) or explicit legacy framing — no prescriptive survivor. THREE runtime STRING LITERALS left unchanged (logic, out of scope for a comment/prose sweep, SURFACED): `src/config.cpp:676` (`L"kcdx.asi"` filename-exclusion compare in plugin discovery), `src/log.cpp:643` (`SetConsoleTitleA("kcdx.asi")` console-title cosmetic), `src/hooks.cpp:163` + `src/probes/bugsplat_ctor_probe.cpp:253` (PROBE log-line text). These are runtime behavior, not prose — flagged for a separate logic-touching change if desired.
- Final README + docs sweep. **README confirmed already current** (own-launcher framing). In-game verification gate (Phase-8 §"verification" line below) still pending the next launch.

### Phase 8.5 — Asset replacement (kcdx absorbs pak mods) — **NOT STARTED (8.5a partial: pak resolver IDENTIFIED but not detoured for asset overlay)**

**Status (2026-05-28 audit):** 8.5a is PARTIAL — `CCryPak_FOpen` is named in refdb (kcdx_id 131) + the observe-only FOPEN probe (`src/probes/fopen_override_probe.cpp`) detours the body in dev mode for path classification, NOT for asset replacement. 8.5b/c/d/e are NOT BUILT — no `[entrypoints].assets` parse, no overlay map, no `kcdx.assets.*` Lua surface (grep `src/lua_bind*.cpp` for `assets` returns nothing), no `kcdxAssetInterface`, no `cap-XX-asset-replace` plugin. The pak-mod ABSORB path (mod-loader takeover landing pak mods into MOUNT verbatim) is a SEPARATE feature already complete — that is NOT this phase. This phase is OVERLAY: a kcdx plugin shipping a single loose file that OVERRIDES a pak-resident asset by virtual path.

The "kcdx replaces pak mods" section above is this phase's scope. Sub-phases:

- **Phase 8.5a**: hook the game's pak resolver. **PARTIAL** — `CCryPak_FOpen` is named in refdb (`src/mod_absorb/...` uses the resolved address via `refdb::ResolveAddrByName("CCryPak_FOpen")`; FOPEN probe at `src/probes/fopen_override_probe.cpp` detours observe-only in dev mode). The PRODUCTION asset-overlay hook is not installed.
- **Phase 8.5b**: parse `[entrypoints].assets` directories at plugin discovery. **NOT BUILT** — no parse, no in-memory overlay map.
- **Phase 8.5c**: in the pak resolver hook, check the overlay map first. **NOT BUILT**.
- **Phase 8.5d**: ship the Lua surface (`kcdx.assets.replace`, `kcdx.assets.replace_static`, conflict reporting). **NOT BUILT**.
- **Phase 8.5e**: test plugin (`cap-XX-asset-replace`) that replaces a known-safe game file + verifies the replacement is visible in-game. **NOT BUILT**.

After Phase 8.5, the `pak-mods.md` workspace rule is rewritten to "pak mods are deprecated; use `[entrypoints].assets` — see `docs/asset-replacement.md`." Existing pak mods keep working; kcdx is the path forward for new TC work.

**Verification gate**: a TOML-only plugin with `[entrypoints].assets = "assets/"` containing a known-safe replacement (e.g. a UI string in a menu) loads, the in-game UI shows the replacement, the engine log emits the overlay-hit line, and a second plugin replacing the same file gets a "lost to plugin X" log line per existing conflict-report shape.

### Phase 9 — High-level Lua surface: 3 real capabilities + namespace structure for the rest — **NOT STARTED**

**Status (2026-05-28 audit):** zero `kcdx.player.*` / `kcdx.inventory.*` / `kcdx.world.*` / `kcdx.dialogue.*` / `kcdx.quest.*` binder in `src/lua_bind*.cpp`. None of the three "ships real" items (player.health, player.position, inventory.add) are built; the namespace stub tables are not registered. No `cap-XX-player-*` / `cap-XX-inventory-*` test plugin.

Addresses [docs/design-gaps.md](../../../docs/design-gaps.md) gap #16 (high-level Lua surface for gameplay).

Per workspace priority #2 (capability) and the rule "we don't ship 80% solutions" — Phase 9 commits to THREE real, working capabilities that prove the namespace design end-to-end, plus the namespace skeleton for everything else that will land incrementally.

**Ships real (Phase 9):**

- `kcdx.player.health` — `:get()`, `:set(n)`, `:add(n)`. Real, working, tested.
- `kcdx.player.position` — `:get()` returns Vec3 (x, y, z) read from the player struct. `:set()` may or may not be safe (teleport-like); ship `:get()` for sure, `:set()` only if RE confirms a safe write path.
- `kcdx.inventory.add(item_id, count)` — real, working, tested. Probably the most-requested TC primitive.

Each ships with its corresponding Address Library entries (added in this phase if not already present), a unit test plugin in test-suite, and full docs in `docs/lua/`.

**Ships namespace stubs (Phase 9):**

- `kcdx.player.*` (other accessors), `kcdx.world.*`, `kcdx.dialogue.*`, `kcdx.quest.*` — namespace tables exist with documented expected functions. Functions log "not yet implemented; tracking in design-gap #16" and return nil. Stubs let plugin authors write code referencing the future API and have it fail loudly + meaningfully when it tries to call something that isn't done yet.

**Why this scoping is right:** the stubs alone would be the "Y does 80%" anti-pattern — they ship the namespace but no actual capability. The 3 real items prove the architecture works end-to-end (Address Library lookup → pointer arithmetic → typed Lua read/write) and unblock TC authors on the most common use case (player state mutation). Subsequent PRs fill in the namespace by lifting more entries into "real" — each PR adds one or two capabilities + their address library entries + tests.

**Verification:** the 3 real items ship with regression tests. A test plugin calls `kcdx.player.health:set(50); assert(kcdx.player.health:get() == 50)` — failing the assert fails the test. Same for position and inventory.add.

### Phase 9.1 — SQLite reference DB + lookup primitive + per-plugin verification cache — **DB + ENGINE CONSUMER DONE; survival-cache PLUMBED-BUT-NOT-FED**

**Status (2026-05-28 audit):**
- **DB ships** at `data/reference.sqlite`, USER schema = `address_names` + `address_versions` + the dictionary/registry tables (see `data/reference.md` + `parallel-ghidra-research.md` §11.9 for the actual schema).
- **Lookup primitive is `refdb::ResolveByName(name)` / `ResolveById(kcdx_id)`** — refdb owns the bulk-built in-memory cache (commit `498934c`, the refdb-owns-the-cache refactor). The originally-planned standalone `hash_at(name, version)` C++ helper was never built as a separate symbol — the cached `address_versions.content_hash` + `length` are fields on `refdb::NameResolution` / `CachedEntity` instead, accessed via the existing resolve. Same effective primitive, different shape.
- **Per-plugin verification cache (`version_check.bin`) exists** at `<kcdx-engine>/cache/version_check.bin` and `survival_pass::RunPass` writes it — but no PRODUCTION binder calls `RecordTouchedRef`, so the cache contains only the self-test's synthetic data (see `src/version_check_selftest.cpp`). The cache PLUMBING ships; the production FEED awaits Phase 9.2's binder wiring against the §11.8 STREAMLINE model.
- **`behaviors` / `applicable_ops` DB tables** — never built; deferred indefinitely. No current consumer.
- **`statements` / `referenced_vars` / `call_edges`** — DEV-only tables (the bulk function table for `kcdx.find` discovery); not consumed by production (Phase 9.4 is not built).

**SCHEMA SUPERSEDED 2026-05-28** — read `parallel-ghidra-research.md` §11.9 for the shipped schema. The original `functions` / `statements` / `applicable_ops` / `behaviors` / `meta` flat shape sketched below was the pre-RE-evidence design; reading the section below is HISTORICAL anchoring, not specification.

The infrastructure phase for cross-version survival. Engine ships function/statement metadata as a single SQLite file; every cross-version consumer (Phase 9.2's enforcement, Phase 9.4's discovery) routes through the refdb cache lookup.

**Scope:**

- Vendor SQLite amalgamation under `vendor/sqlite/sqlite3.{c,h}` (~150 KB single-file vendor drop, public domain). Add `kcdx_sqlite` static-lib target to CMake.
- Ship `data/reference.sqlite` build artifact. Schema:
  - `functions`: `id`, `function_name`, `auto_name`, `game_version`, `module`, `rva`, `content_hash`, `signature`, `decompile_quality`, `subsystem`, `inferred_purpose`, `status`. `subsystem` is the caller-graph-clustering bucket (`inventory`, `combat`, …) consumed by `kcdx.find{ callee_in_subsystem = "..." }`; `inferred_purpose` is the provenance-tagged, evidence-anchored, NON-load-bearing one-sentence description carried for naming uncategorized functions + `kcdx_dev_inspect` display (it never participates in ID assignment, hash check, or auto-naming — those stay structural; see `parallel-ghidra-research.md` §2). Unique key `(function_name, game_version, module)`. Indexes on `(function_name, game_version, module)`, `(module, rva)`, `(auto_name, game_version, module)`, `(id, game_version)`, `(subsystem, module)`.
  - `statements`: `id`, `function_id`, `idx`, `kind`, `pseudo_text`, `byte_range_start`, `byte_range_len`, `content_hash`, `callee`, `condition_text`, `cvar_ref`, `string_ref`. Indexes on `(function_id, idx)`, `(callee, function_id)`, `(cvar_ref)`, `(string_ref)`.
  - `applicable_ops`: `statement_id`, `op_name`. Index on `(statement_id)`.
  - `behaviors`: `name`, `description`, `implementation_json`, `source` (`"engine"` for kcdx-shipped, `"plugin"` for plugin-declared at runtime — runtime plugin behaviors aren't in the shipped SQLite, but `kcdx.behavior.list` queries unify both sources). Index on `(name)`.
  - `meta`: `schema_version`, `oldest_supported_game_version`, `newest_shipped_game_version`, `built_at`.

**IDs are append-only and never change across game versions.** The `functions.id` integer is kcdx-assigned the first time a function appears in the SQLite (any game version) and never recycled. On future game-version refreshes, the maintainer matches new-dump functions to existing IDs by canonical name + signature + caller-graph fingerprint. A function that gets renamed or refactored in a new game version gets a NEW row with the SAME id (plugins still find it). A function that's removed gets a `status = "removed"` row for that version under its original id. **An ID resolves to the same conceptual function forever** — this is what makes `kcdx.hook.before(module, 1042, ...)` survive both function renames and game updates. Same append-only discipline as `data/seeds/policy.md`'s ID assignment policy, applied to the broader function reference. Per AP11 reasoning extended to data: never insert mid-range, never renumber, never recycle.

**Function lookup accepts three forms, all routing through the same SQLite path:**
- Canonical name (the common path; matches `functions.function_name`): `kcdx.hook.before(module, "IsInCombat", ...)`
- Stable kcdx ID (the most-stable-across-versions form; matches `functions.id`): `kcdx.hook.before(module, 1042, ...)`
- Ghidra auto-name (for uncategorized functions found via `kcdx.find`; matches `functions.auto_name` for the current game version): `kcdx.hook.before(module, "FUN_180A2B100", ...)`

The engine dispatches by argument type/shape (string vs integer; string-matching-canonical-name vs string-matching-auto-name). Author uses whichever they have; engine handles the rest.
- New `src/hashref.{cpp,h}` — single lookup primitive `hash_at(function_name, game_version) → hash | NOT_PRESENT`. All callsites in Phases 9.2+ route through this.
- New `src/version_check_cache.{cpp,h}` — per-plugin verification cache at `engine/cache/version_check.bin`.
  - Header carries a `cache_schema_version` constant baked into kcdx.dll, bumped only when check-logic changes (NOT on every kcdx release — most kcdx updates don't ship new check logic, so the cache survives them).
  - Per-plugin record: `(plugin_name, game_ver, sqlite_sha, toml_mtime, entrypoints_mtime, result_per_function)`.
  - **Always-on for all users** (not dev-mode-gated). The mtime fields catch the rare end-user-edits-a-plugin-file case at near-zero cost (<1 ms typical for ~100 plugins).
  - Mode-switch invalidation: dev-mode-on writes record extra diagnostic detail; switching dev-mode-on/off between launches invalidates the affected records once.

**Engine internals (not author-facing).** Authors never see hashes, IDs, or this cache. The author-facing contract is in `docs/lua/` — "your plugin keeps working when KCD2 updates as long as the function you targeted is byte-identical to a previously-verified version." Mechanism details live in `data/statement-library/policy.md` + `data/behavior-catalog/policy.md` (maintainer-facing, Phase 9.1 + 9.5 write these).

**Reference data is built once per game-version refresh, not at every kcdx build.** The parallel Ghidra research (`docs/outstanding-work/parallel-ghidra-research.md`) produces the per-function and per-statement rows; maintainer-side import populates the SQLite. Phase 9.1 engine work can proceed using a hand-built `reference.sqlite` containing a few dozen synthetic rows for testing while real data populates in parallel.

**Verification gate:** a synthetic test against a hand-built `reference.sqlite` with 100 rows calls `hash_at("test_function", "1.5.x")` and gets the expected hash; cache invalidation tested by modifying `toml_mtime` → cache miss → recheck runs.

### Phase 9.2 — Unified named-target surface: `kcdx.declare` + smart-resolver sub-verb shape over the unified table — **DECLARE STORE + SMART RESOLVER + SUB-VERB SURFACE + C++ MIRROR + `kcdx.scan{...}` LUA DIAGNOSTIC DONE; `kcdx_scan` CONSOLE COMMAND NOT BUILT**

**This phase MERGES the previously-separate Phase 9.2 (`kcdx.declare`) + Phase 9.7 (sub-verb resolver).** They were drafted as sequential phases against the original "engine hashes every plugin's touched function" framing; the §11.8 STREAMLINE replaced that framing, and the audit on 2026-05-28 surfaced that the two phases are **one surface with two population sources** (curated refdb rows + author declarations), accessed through one resolver. Landing them sequentially would have shipped a transitional UX (declare against the old flat `kcdx.hook{target=...}` shape for one release, then rewriting authors' code at 9.7). Unified, both tracks reach the same `kcdx.<verb>.<name>.<mode>` shape in the same cycle.

**Status (2026-05-29 audit):**
- **Curated substrate DONE.** Commit `498934c` (the refdb-owns-the-cache refactor) put the curated entity cache in process memory at boot. `refdb::Open()` bulk-resolves every curated entity for the running game version (closest-match version row + supersession walk + verification-state derivation); the `CachedEntity` row carries `kcdx_id`, post-supersession `name`, `rva`, `verified_signature`, `kind`, kind-specific fields (`offset` / `vtable_slot` / `value` / `length` / `content_hash`), and `verification_state`. The Lua-side smart resolver reads this cache; never touches SQLite at runtime.
- **`survival_pass` machinery built but scoped to the CURATED track only.** `src/survival_pass.{cpp,h}` + `src/version_check_cache.{h}` + `src/survival.{cpp,h}` + the `version_check.bin` write path are wired (self-test in `src/version_check_selftest.cpp` exercises them with synthetic data each boot). Per §11.8.3, survival_pass is the *"only 'we can pre-check' path"* — it ships per-version content hashes the maintainer maintains for ~139 curated entries. Track-2 declared entries do NOT feed survival_pass; their safety story is the badge + recovery/rollback path (separate outstanding-work item, default-ON shipping waits on it).
- **Author-declare store DONE.** `src/declared_targets.{cpp,h}` ships the runtime registry (`std::deque<DeclaredEntry>` per the cycle-2 storage swap commit `2dac79b`); `src/lua_bind_declare.cpp` ships `kcdx.declare(module, name, [versions_kv])` per the §11.8.1 spec; `src/declare_interface.{cpp,h}` + `kcdxDeclareInterface` in `include/kcdx/Interfaces.h` ship the C++ peer. cap-62 (`test-plugins/cap-62-cpp-declare-interface/`) is the C++ regression net.
- **Smart-resolver sub-verb surface DONE.** `lua_bind_hook` + `lua_bind_bytes` carry the `__index`-driven `kcdx.<verb>.<name>.<mode>` shape over the unified curated-cache + declare-store table. Per-verb "valid modes for this kind" filtering ships (cap-59 `CAP-59-invalid-mode-nil` row exercises the kind-aware filter on a `kind="data_slot"` declared entry). cap-28 (Lua bytes smart-resolver) + cap-59 (Lua hook smart-resolver) are the Lua regression nets.
- **C++ mirror DONE.** `kcdxHookInterface` + `kcdxBytesInterface` + `kcdxDeclareInterface` ship the per-verb sub-method surface; `include/kcdx/Kcdx.h` ships the empowered floor (`kcdx::hook::Before<Sig, &fn>(K, name)`, `kcdx::bytes::Write(K, name, replacement)`, `kcdx::declare::*` namespace). cap-63 (`test-plugins/cap-63-cpp-bytes-wrapper/`) + cap-64 (`test-plugins/cap-64-cpp-lua-pcall-fires/`) + cap-65 (`test-plugins/cap-65-classifier-bootstrap/`) are the C++ regression nets.
- **Engine-direct AP4 carve-out DONE for the canonical first site `engine.lua_pcall`.** Commit `1c01c9d` migrated the engine's own `lua_pcall` hook off raw `MH_CreateHook` onto `hook_chain::AddCEngine` (Kind::Engine identity; engine-first comparator front-sort; three-gate off-thread carve-out for engine-stamped C entries to break the dead-classifier chicken-and-egg discovered via PROBE α). This unblocked cap-59-fires (Lua hook on `lua_pcall` now actually fires) + cap-64 (C++ peer of cap-59) + cap-65 (load-bearing classifier-bootstrap regression). 5 sibling engine-direct sites (`frealloc` canary, `ModManager_ctor`, `MiniDmpSender` ctor, `SaveGame`, `LoadGame`) remain on raw-MinHook; tracked at [`../../tech-debt/TD-0003-engine-direct-hook-migration.md`](../../tech-debt/TD-0003-engine-direct-hook-migration.md). `update` is the SOLE documented bootstrap-pump exception (self-referential dispatch).
- **KI-0001 sentinel-mirror fix landed** during cycle-1 verification — `vendor/lua/ltable.c` now guards against kcdx's vendored Lua GC freeing WHGame's `.rdata` sentinel objects (the FIX-C inverse: kcdx-Lua → WHGame-sentinel free direction, vs the original FIX-C's WHGame-Lua → kcdx-sentinel free direction). `kcdx_node_freeable` + `kcdx_array_freeable` guards; cap-66 regression row.
- **`kcdx.scan{...}` Lua diagnostic DONE.** `src/lua_bind_scan.cpp` ships the address-discovery workbench shape (pattern + module + offset + context + anchor opts → log diagnostic with matches + nearby strings). This is the LUA surface, distinct from the `kcdx_scan` console verb below.
- **`kcdx.declared(name)` value accessor DONE.** Ships in `src/lua_bind_declare.cpp` for declared non-address entries (`["1.5.1164953"] = 0x0F` value-kind shape). cap-62's matrix exercises it.
- **NOT built:** `kcdx_scan` **console command** (in-game iterative AOB discovery — the explicit `_scan` console verb gated behind console RegisterCommand; this is distinct from the `kcdx.scan{...}` Lua diagnostic above). The discover-then-declare loop is unusable end-to-end without it; tracked as the final Phase 9.2 deliverable.
- **NOT built (separate phase):** the per-verb "valid modes for this kind" tables on `kcdx.statement.<name>.<op>` — slots into the smart-resolver shape when Phase 9.3 lands its binder. `kcdx.statement` shape NOT BUILT today.

#### What "unified named-target surface" means

ONE in-memory table of resolvable names. ONE smart resolver over it. TWO population sources:

| Population source | Owner | Identity | Per-version mechanism | Safety |
|---|---|---|---|---|
| **Curated refdb cache** (built today) | kcdx maintainer | `<name>` lives at the engine-reserved `kcdx.<name>` triple per `naming-namespaces.md` (1-dot explicit; bare-resolves at `engine` tier) | per-version row in `address_versions` (`rva` + `signature` + `content_hash` + `length` + `kind`); resolved at `refdb::Open()` against the running game version | `survival_pass`: per-version `content_hash` pre-checked at install (curated-only) |
| **Author-declared store** (this phase) | plugin author, in their own `plugin.lua` | `<name>` lives at the declaring plugin's `<author>.<plugin>.<bare>` triple per `naming-namespaces.md` (3-dot explicit; bare-resolves at `self` tier from inside the declaring plugin) | per-version `versions_kv` table the author supplied (`["1.5.1164953"] = {pattern=...}` etc.); the engine resolves ONCE at launch against the running game version using kcdx's canonical version string (already verified — sourced from `<game>/whdlversions.json` MasterMasterPGO config) | no pre-check; badge fires "Author certified through ≤ X; you're on Y" on undeclared versions; install-time / runtime failure routes through recovery/rollback (separate outstanding-work item) |

The author can't tell which source backed a name — that's the point. `kcdx.hook.IsInCombat.before(fn)` against a curated entity and `kcdx.hook.combatResolver.before(fn)` against the author's own declared name read identically, route through identical `__index` resolution, and install identically. The bare-name precedence walk (`self > engine > other` per `naming-namespaces.md`) is the disambiguation when names overlap: an author who declares `combatResolver` and is the calling plugin gets their own declaration first; a curated `combatResolver` resolves via `engine` for any other caller.

#### Two surface shapes for declare + one for resolve

**`kcdx.declare(module, name, [versions_kv])`** — populates the author-declared store. Per §11.8.1:

```lua
-- per-version table: explicit keys + wildcard, no range objects
kcdx.declare("WHGame.dll", "combatResolver", {
  ["1.5.1164953"] = { pattern = "48 8B 05 ?? ?? ?? ?? 8B" },
  ["1.6.*"]       = { pattern = "48 8B 0D ?? ?? ?? ?? 8B" },
})

-- a version-independent constant: per-version values
kcdx.declare("WHGame.dll", "combatStateMask", {
  ["1.5.1164953"] = 0x0F,
  ["1.6.*"]       = 0x1F,
})

-- table omitted: attempt on ALL versions (the simpler "this works everywhere" path)
kcdx.declare("WHGame.dll", "combatResolver", { pattern = "48 8B 05 ?? ?? ??" })
```

Rules (locked in the §11.8 spec):
- **`module` is REQUIRED** (positional first arg). No default — kcdx exists to enable cross-module plugins eventually, and a defaulted module silently misroutes when secondaries get involved.
- **Version keys:** explicit (`"1.5.1164953"`) and wildcard (`"1.5.*"`) only. NO range objects.
- **Table omitted** = attempt on all versions (the low-ceremony common case).
- **Plugin-Lua-side + an engine resolver** — does NOT write to the prod refs DBs. The declaration store is in process memory, owned by the declaring plugin's `<author>.<plugin>` namespace.

**`kcdx.<verb>.<name>.<mode>` sub-verb resolver** — the canonical access path for ANY name in the unified table, regardless of which source populated it:

```lua
kcdx.hook.IsInCombat.before(callback)          -- curated entity
kcdx.hook.combatResolver.before(callback)      -- the author's own declared name
kcdx.hook.IsInCombat.after(callback)
kcdx.hook.IsInCombat.replace(callback)
kcdx.hook.IsInCombat.around(callback)

kcdx.bytes.IsInCombat{replacement = "..."}
```

(`kcdx.statement.<name>.<op>` is Phase 9.3 scope and slots into the same smart-resolver shape when that phase lands; this phase wires only hook + bytes. `kcdx.scan` and `kcdx.code` do NOT get the sub-verb shape — both PRODUCE inputs to the unified table (scan returns addresses the author wraps in `kcdx.declare`; code allocates fresh executable regions the author publishes via `export` as new named symbols). The smart-resolver shape exists to dispatch a verb against an already-resolved named site; the producer-side verbs have no resolved site to dispatch against. See "Scan workflow" + "Code workflow" below.)

**`kcdx.declared(name)` value accessor** — for declared NON-address entries (the `["1.5.1164953"] = 0x0F` shape):

```lua
if (ret & kcdx.declared("combatStateMask")) ~= 0 then ... end
```

The §11.8.1 example's bare-string-in-arithmetic (`& "combatStateMask"`) is illustrative pseudocode; Lua doesn't coerce arbitrary strings to numbers in `&`. The accessor is the honest shape: the value form needs an explicit read, the address form (pattern declarations) routes through the same `kcdx.<verb>.<name>.<mode>` shape the curated track uses.

**Fallback path is preserved.** The Phase 2 flat-table form (`kcdx.hook{ target = "combatResolver", before = fn }`) and the Phase 9.3 explicit-positional form (`kcdx.hook.before("WHGame.dll", "Name", ...)`) keep working for the dynamic / pattern-locator / raw-address paths. The sub-verb form is the documented common path for any name in the unified table; the flat/positional forms are documented as the dynamic-dispatch alternative.

#### Implementation pattern — smart resolver, NOT pre-generated tables

The Lua binder does NOT materialize per-name-per-mode closures at boot. Verb tables `kcdx.hook` / `kcdx.bytes` carry `__index` metamethods that resolve on demand. (`kcdx.statement` extends to the same shape when Phase 9.3 lands its binder; `kcdx.scan` + `kcdx.code` are excluded by design — see "Scan workflow" and "Code workflow" below.)

1. `kcdx.hook` is a Lua table with `__index = c_function`.
2. Lookup of any name (`kcdx.hook.combatResolver`) invokes the metamethod. The metamethod consults the **unified named-target resolver**: it walks self > engine > other per `naming-namespaces.md` (`self` = the calling plugin's author-declared entries via the calling-plugin context the Lua VM already carries; `engine` = the refdb curated cache; `other` = other plugins' declared entries). Miss → return nil. Hit → push a small verb-bound userdata with two upvalues: a unified `ResolvedTarget` and the verb tag (`"hook"`).
3. The userdata's own `__index` resolves the mode (`.before` / `.after` / `.replace` / `.around` / `.mid`) against a per-verb static "valid modes for this kind" table. The kind drives validity — e.g., for `hook` + `function` kind: before/after/replace/around/mid all valid; for `hook` + `vtable_index` kind: only replace + before/after via vtable thunk are valid. Invalid mode → return nil. Valid → push a closure with `(target, verb, mode)` baked in.
4. The closure is what the author calls with `(callback)` or `{captures=..., callback=...}` — invoking the verb's existing install logic with the target's address / signature / kind pre-resolved from the unified `ResolvedTarget`.

Cost at boot: ~5 C functions + ~5 metatables total (one per verb's `__index`). No per-name materialization. The unified table (~140 curated + however many a plugin declares) is the only substantive data the surface touches.

**Typo fails fast at the right place.** `kcdx.hook.IsInCombatt.before(fn)` returns nil at step 2 (the name lookup); the next `.before` access throws a Lua "attempt to index a nil value" error that names the slot the author typoed. No install-time silent skip. No "the engine accepted my code but my hook isn't firing" mystery.

**Kind-aware filtering is structural.** A mode that doesn't apply to an entity's kind returns nil at the mode-resolution step, so `kcdx.hook.SomeVTableIndex.before(fn)` errors at `.before` if `before` isn't valid for `vtable_index` kind — at author-time, not at install-time. No engine-side after-the-fact "I refused to install this" log line for the author to dig out.

#### Engine architecture — one resolver, two backing stores

**The unified lookup entry point is the EXISTING `address_library::ResolveByName`** (owner-aware: takes `name` + `owningAuthor` + `owningPlugin`). It already owns the `self > engine > other` walk via `ResolveBareWinner`. The phase extends it with one new source — declared entries register as an additional `AuthorTarget` provider — and the existing precedence machinery handles the rest. The original spec sketched a separate `named_target_resolver.{cpp,h}` module; the audit confirmed the existing resolver IS that module under a different name, so spawning a new file would duplicate the precedence walk that already exists (the flat-table form `kcdx.hook{target=...}` routes through this same resolver today).

- **Engine tier (curated):** `address_library::ResolveByName` already consults `SeedHasName` / `SeedResolveAddr` at the engine tier, which read the refdb cache built by `refdb::Open()`. The smart resolver projects the resulting `NameResolution` (or the `AuthorTarget` for a self/other winner) into a `ResolvedTarget` carrying the fields the smart-resolver pattern needs (rva, signature, kind, etc.).
- **Self / other tier (declared):** a new `declared_targets.{cpp,h}` module — the in-memory store the `kcdx.declare` binder populates. Stores `(declaring_author, declaring_plugin, name, module, versions_kv)`. Declared entries register into the existing `AuthorTarget` machinery as a new source kind; `FindAuthorTarget` / `FindOtherAuthorTarget` consult declared entries in addition to the existing patch/symbol sources. On resolve, `ResolveAuthorTargetAddr` routes a declared-source winner through `declared_targets::LookupForCaller`, which picks the matching version-key entry (exact > longest-wildcard) and — for a `pattern` entry — runs `scan_engine::ResolveScan` once and memoizes the resolved VA; for a value entry stores it directly. The Q4 no-match-version case fires the §11.8.2 badge-equivalent log line from here.

The smart resolver's `__index` metamethod calls `address_library::ResolveByName` directly. The existing flat-table form `kcdx.hook{target=...}` already calls the same resolver today, so the two surface shapes coexist and route through one resolution path — over time the documented common path is the smart resolver, the flat-table form is documented as the dynamic-dispatch alternative for raw addresses / patterns without a declared name / etc.

#### C++ mirror — full parity per `lua-api-surface.md`

`kcdxHookInterface::IsInCombat::Before(callback)` (or the equivalent template specialization shape — settled at this phase's design step) mirrors the Lua surface. `kcdxDeclareInterface::Declare(module, name, versions)` mirrors `kcdx.declare`. The full-parity invariant from `.claude/rules/lua-api-surface.md` is preserved: every Lua surface ships with its C++ peer in this same multi-commit feature; both surfaces of one capability get test rows (Lua plugin + C++ DLL plugin, both exercising the same declared name).

**Empowered C++ wrapper.** The raw `kcdxDeclareInterface::Declare(entries, count, owningPlugin)` is the always-available floor; this phase also ships its empowered wrapper in `include/kcdx/Kcdx.h`, peer to the existing `kcdx::hook::Before<Sig, &fn>` helpers (Phase 3 sub-1 step 6). Two forms split by entry shape (the two intended use cases — pattern entries vs value entries — each get their own helper so the per-row `kcdxDeclareEntry` sentinel-init dance vanishes):

- `kcdx::declare::Function(K, "module", "name", { {versionKey, AOB, sig}, … })` — pattern entries. Wraps the raw call with auto-threaded `owningPlugin = K.self` and designated-initializer-style per-row args (no `kcdxDeclareEntry e = {}; e.versionKey = …; e.patternStr = …; e.signatureStr = …; …` boilerplate per row).
- `kcdx::declare::Value(K, "name", { {versionKey, intOrStr}, … })` — value entries. The Value form's `int64_t` vs `const char*` overload picks `valueIsString` for you (the discriminator vanishes).

The raw `K.declare->Declare(entries, count, K.self)` array-of-POD form remains documented in `docs/cpp/declare.md` as the labeled raw-floor drop-down per the 3-floor model (`docs/cpp/wrapper.md`); `docs/cpp/declare.md`'s common-path lead is the empowered form. Both forms ship a test row per `test-suite.md`. Direction was user-locked in the 2026-05-28 `/senior-architect-reply` thread for the hook docs flip (Option C — the empowered wrapper is the canonical Lua-mirror peer; the raw form is the labeled drop-down).

#### Scan workflow — `kcdx_scan` console command + the discover-then-declare loop

`kcdx.scan` is the AOB-discovery workbench: author hands it a hex pattern, scan_engine returns matches. It PRODUCES inputs to the unified named-target table (the resolved address becomes the value an author wraps in `kcdx.declare`); it does NOT consume the table. So the smart-resolver `__index` shape — which exists to consume a named entry — does not apply to it. A `kcdx.scan.<name>()` proxy would be a no-op over an address the table has already resolved; ceremony confusing what `kcdx.scan` is for. `kcdx.scan{pattern=...}` Lua form stays unchanged in this phase for runtime conditional scans (rare but legitimate — a plugin that needs to discover an address at boot rather than declare it ahead of time).

What this phase DOES ship for the scan side: a `kcdx_scan` **console command** that lets the author iterate AOB patterns in-game in seconds instead of the multi-minute compile-launch-grep cycle the Lua-only `kcdx.scan` currently forces. The console workflow:

1. Launch the game (one launch, normal).
2. Open the in-game console (`~`).
3. `kcdx_scan WHGame.dll "48 8B 88 ?? ?? ?? ?? 48"` → console prints `[scan] 0 matches` / `[scan] 3 matches: WHGame.dll+0x...`.
4. Iterate the pattern until it resolves uniquely (seconds per iteration).
5. Paste the working pattern into `kcdx.declare("WHGame.dll", "myTarget", {["1.5.1164953"] = {pattern="...", signature="..."}})` in the author's `plugin.lua`.
6. Reference by name everywhere: `kcdx.hook.myTarget.before(fn)`.

The console command shares the existing `scan_engine::ResolveScan` path with `kcdx.scan` (one new shared helper, two callers); both surfaces emit the same log lines so the dev-log workflow keeps working. Without this, declare's pattern-based expert hatch is gated behind the same multi-minute round trip the discovery problem already had — declare-with-discovery is a complete workflow only when both ship together. Matches the disassembler-test cornerstone: the engine does the heavy iteration; the author declares intent.

#### Code workflow — `kcdx.code` is producer-side and excluded by the same rule

`kcdx.code` is similarly excluded from the smart-resolver shape: it PRODUCES new executable code sites (which other verbs then consume by name via the symbol table). The smart-resolver shape exists to dispatch a verb against a resolved named site; allocating a fresh code region is producer-side work, not consumer-side. `kcdx.code{name=, bytes=, size=, pool=, export=}` allocates a region of executable memory and returns a live `kcdx.memory.pointer`; the optional `export = "..."` publishes the allocated region as a symbol under the calling plugin's `<author>.<plugin>.<bare>` namespace. The consumer side is `kcdx.hook{ target_symbol = "..." }` — that's where the named binding happens, against a resolved site. A `kcdx.code.<name>{...}` shape has no meaningful binding because the name isn't an existing site the verb operates against; the verb's whole job is to allocate something fresh. Same producer-vs-consumer asymmetry that excludes `kcdx.scan` above. `kcdx.code{...}` Lua form stays unchanged in this phase.

#### What the unified table MUST provide (per-`ResolvedTarget`)

Same shape as the existing curated `CachedEntity`, with provenance:

| Field | Source: curated | Source: declared | Use |
|---|---|---|---|
| `name` (post-supersession for curated) | `address_names.name` | `kcdx.declare` arg 2 | identity for verb-bound proxy |
| `triple` (`author.plugin.bare`) | `kcdx.<name>` (engine tier) | `<declaring-author>.<declaring-plugin>.<bare>` | precedence walk + log attribution |
| `module` | `modules.name` for the entity's `module_id` | `kcdx.declare` arg 1 | hook install / scan scope |
| `rva` + `WhgameBase()` | `address_versions.rva` for the running version | resolved by `scan_engine::ResolveScan` on the declared `pattern` (memoized) | absolute VA |
| `signature` | `address_versions.signature` (verified) | from a sibling `signature = ...` field in the declared per-version entry, when the author supplies one; otherwise hook-mode is rejected at install with a teaching error | hook-mode ABI binding |
| `kind` | `address_versions.kind` (`function` / `vtable_index` / etc.) | derived: `function` if the declared entry is `{pattern=...}`, `value` if the declared entry is a literal | drives per-verb "valid modes" |
| `vtable_slot` / `offset` / `value` | kind-specific fields from `address_versions` | kind-specific fields from the declared per-version entry | kind-specific install paths |
| `length` / `content_hash` | `address_versions.length` / `content_hash` (curated track only) | NOT present for declared (Track-2 has no pre-checked hash) | `kcdx.bytes.<name>` survival check (curated-only path) |
| `verification_state` | derived from supersession + deprecation + verified-at version | derived from "declared for running version" / "declared with wildcard match" / "declared for other versions only (badge fires)" | resolver chooses to warn or filter |

#### Out of scope for this phase (tracked elsewhere; do NOT fold in)

- **Apply-time enforcement of an `on_changed` posture for the CURATED track.** `survival_pass::RunPass` produces per-(plugin, function) results today (the self-test exercises this); the apply pass walks entries unconditionally. Wiring the apply pass to skip-or-warn based on a result is its own follow-up cycle. It is an engine-internal change on the curated track; this phase ships the unified author surface.
- **Recovery + rollback machinery for Track-2 plugins on undeclared versions.** §11.8.3 + the outstanding-work bullet "Recovery + rollback for Track-2 plugins on undeclared versions" specify what this is. Load-bearing for default-ON shipping. Spec to live in `docs/outstanding-work/track2-recovery-rollback.md` (not yet written). Until it lands, default-ON behavior runs in "fail-loud-but-no-clean-rollback" mode — a Track-2 declared entry resolving against an undeclared version succeeds-with-WARN or fails-loud at install; the recovery/rollback guarantee follows.
- **UI badge surfacing in `kcdx.exe`.** Pre-UI: the launch-log line per §11.8.2. The phase's verification gate asserts the log line; the UI badge is a `kcdx.exe` (interface project) deliverable.
- **The Phase 9.3 surface split** (`kcdx.hook.before` / `kcdx.hook.after` etc. as separate sub-verbs from `kcdx.hook{}`). The smart resolver here uses sub-verb `.mode` access at the `ResolvedTarget` level; the *flat-table fallback* still uses `kcdx.hook{mode=fn}` until 9.3 lands the sub-verb split everywhere.

#### Files (sketch — design step within the feature settles exact shapes)

- New `src/declared_targets.{cpp,h}` — the in-memory store + version-key matcher (exact > longest-wildcard); the resolve entry point that runs `scan_engine::ResolveScan` for `pattern` entries and memoizes; the metadata accessor for `value` entries; the Q4 no-match-version badge log line.
- Extend `src/address_library.{cpp,h}` — declared entries register as a new `AuthorTarget` source kind so `FindAuthorTarget` / `FindOtherAuthorTarget` / `ResolveAuthorTargetAddr` consult `declared_targets` alongside the existing patch/symbol sources. No new module; the existing `ResolveByName` IS the unified entry point per Q2.
- New `src/lua_bind_declare.{cpp,h}` — `kcdx.declare(...)` + `kcdx.declared(name)` binders; registers declared entries under the calling plugin's `<author>.<plugin>` namespace; enforces the Q1 hard-reject for `{pattern=...}` entries without a sibling signature.
- New `src/declare_interface.cpp` + new entries on `kcdxDeclareInterface` in `include/kcdx/Interfaces.h` (APPEND-ONLY per AP11) + `kcdxInterface_Declare` enum + `interfaces.cpp` `QueryInterface` wire-up + `include/kcdx/Kcdx.h` wrapper member append.
- `src/lua_bind_hook.cpp` — extend with the `__index` metamethod for the smart-resolver path; the existing flat-table / explicit-positional forms keep working as the dynamic / non-named-target path.
- `src/lua_bind_bytes.cpp` — same `__index` extension for `kcdx.bytes.<name>(...)`.
- `src/lua_bind_code.cpp` — UNCHANGED. `kcdx.code` is producer-side and excluded from the smart-resolver shape (see "Code workflow" above).
- `src/lua_bind_scan.cpp` — extract the arg-parse + scan-engine-call body of `Lua_Scan` into a shared `RunScan(ScanEntry) -> ScanResult` helper so the `kcdx_scan` console command can call the same code path. The Lua-binding entry shape is unchanged.
- New `kcdx_scan` console command registration — file TBD at the step's start (where the existing `kcdx_*` console commands are registered); argv parses into a `ScanEntry`, calls the shared `RunScan` helper, prints to console + emits the existing log lines.
- `src/hook_interface.cpp` + `src/bytes_interface.cpp` — C++ sub-verb mirror for each. `src/code_interface.cpp` is NOT extended with a sub-verb mirror (the Lua side has no sub-verb shape on `kcdx.code` by design — see "Code workflow" above).
- The per-verb "valid modes for this kind" tables: one small static array per verb in the corresponding `src/lua_bind_*.cpp`, plus one in the C++ binding layer.

(Phase 9.3 will introduce `kcdx.statement.*` + `kcdx.locator.*` + `kcdx.op.*`; this phase does NOT pre-extend the smart resolver into those — they slot in when 9.3 lands their binders, the resolver entry points are designed to accept new verbs without churn. `kcdx.scan` and `kcdx.code` are excluded from the smart-resolver shape by design — see "Scan workflow" + "Code workflow" above.)

#### Documentation deliverable (`docs/lua/` + `docs/cpp/` per `docs-discipline.md`)

- Per-call entries for each verb in `docs/lua/` (`hook.md`, `bytes.md`) get a new section **before** the existing flat-table / explicit-positional form, documenting the named-target sub-verb form as the common path for any name in the unified table. The fallback forms stay, documented as dynamic / pattern-locator / raw-address paths.
- `docs/lua/code.md` does NOT get a sub-verb section — `kcdx.code` is producer-side (it allocates fresh executable memory and optionally publishes the region as a symbol via `export`; the consumer side is `kcdx.hook{ target_symbol = "..." }`). The existing `kcdx.code{...}` doc stays as the Lua surface and gets a one-line clarifier near the top stating plainly that `kcdx.code.<name>{...}` is not a valid shape and pointing at `kcdx.hook{ target_symbol = ... }` as the consumer-side counterpart.
- `docs/lua/scan.md` does NOT get a sub-verb section — the existing `kcdx.scan{pattern=...}` doc stays as the Lua surface, EXTENDED with a new section documenting the `kcdx_scan` console command (the in-game iterative AOB discovery workflow; argv shape; the discover-then-declare loop ending in `kcdx.declare`). The console command becomes the documented common path for AOB discovery; `kcdx.scan{...}` Lua form is documented as the runtime-conditional-scan alternative.
- New `docs/lua/declare.md` — full reference for `kcdx.declare(module, name, versions_kv)` + `kcdx.declared(name)`. Lead with the "what does this do" framing (extends the unified named-target table with your own per-version names, accessible by the same `kcdx.<verb>.<name>` shape curated entities use); document version-key forms (explicit + wildcard); document the value vs pattern entry shapes; document the `<author>.<plugin>.<bare>` namespacing; document the badge behaviour on undeclared versions.
- New `docs/cpp/declare.md` — C++ mirror, marked NYI until the C++ side ships in this same phase (then NYI marker is removed in the same commit per `docs-discipline.md` §3).
- `docs/lua/index.md` + `docs/cpp/index.md` — front-door framing gets the unified shape woven in: `kcdx.<verb>.<name>` is the one-liner an author types for a curated target OR a name they declared; mention `kcdx.declare` as the way to add author-owned names to the same surface.
- Glossary terms in `docs/lua/index.md` glossary (and the C++ mirror in `docs/cpp/index.md` glossary):
  - **Named target** — an entry in the unified named-target table; sourced from curated refdb (engine-shipped) or from `kcdx.declare` (author-supplied). Accessible by name in the verb path.
  - **Smart resolver** — the `__index`-driven Lua / templated-accessor C++ shape that resolves a named target to its install function on demand, without pre-materializing per-name-per-mode closures.
  - **Curated target** — a named target from the kcdx-shipped refdb. Maintained by the kcdx maintainer; pre-checked for byte-survival across game versions on the curated track.
  - **Declared target** — a named target supplied by a plugin via `kcdx.declare`. Owned by the declaring plugin's `<author>.<plugin>` namespace; per-version mapping owned by the author.

#### Verification gate

- **Curated path:** a test plugin exercises one cache-resident name across every valid mode for that name's kind via the smart resolver (`kcdx.hook.<curated>.before(fn)` etc.) and asserts PASS.
- **Declared path — pattern entry:** a test plugin declares `kcdx.declare("WHGame.dll", "my_test_target", { ["1.5.1164953"] = { pattern = "..." } })` then hooks `kcdx.hook.my_test_target.before(fn)`; the hook fires; plugin reports PASS.
- **Declared path — value entry:** a test plugin declares `kcdx.declare("WHGame.dll", "my_test_value", { ["1.5.1164953"] = 0x7F })` then reads `kcdx.declared("my_test_value") == 0x7F` and reports PASS.
- **Cross-plugin reference:** plugin A declares `combatThing`; plugin B hooks `kcdx.hook["a.realism.combatThing"].before(fn)` via the prefixed form; hook fires; B reports PASS.
- **Precedence — author-declared shadows curated of the same name (from inside the declaring plugin only):** plugin A declares `IsInCombat`; from inside A, `kcdx.hook.IsInCombat.before(fn)` resolves to A's declaration (self > engine); from inside any OTHER plugin, the same call resolves to the curated entry. Single test plugin with two binding sites proves both directions.
- **Negative #1 — typo at the name slot:** `kcdx.hook.IsInCombatt.before(fn)` produces a Lua error naming the typoed slot, NOT a silent skip.
- **Negative #2 — invalid mode for kind:** pick a name whose kind doesn't support `mid`; `.mid` access returns nil; the call raises "attempt to call a nil value"; PASS.
- **Negative #3 — declared pattern doesn't resolve on the running version:** test plugin declares for a version that doesn't match the running one, OR a pattern that returns no matches; install fails LOUD; the engine log emits the "Pattern did not resolve on your version" badge-equivalent line per §11.8.2; plugin reports PASS by asserting the log line.
- **Negative #4 — declared without a hook-usable signature on the running version:** test plugin declares only `{pattern = "..."}` (no signature) then tries `kcdx.hook.<name>.before(fn)`; install fails with the teaching error documented above (a callback hook needs a signature).
- **Deprecated curated entity:** pick a name whose `verification_state` is DEPRECATED in the running version; `kcdx.hook.<name>.before(fn)`; resolver emits a one-shot WARN naming the replacement; install still succeeds against the deprecated row; PASS when WARN is in the log AND the hook fires.
- **C++ mirror parity:** a C++ DLL test plugin uses `K.hook->IsInCombat::Before(callback)` (or the settled template-specialization shape) against the same curated name; PASS. Same against a declared name via `K.declare->Declare(...)` from the same DLL.
- **`kcdx_scan` console command:** a test plugin auto-passes on "command registered" boot check (looked up via the existing IConsole query path used by the other `kcdx_*` commands). In-game test-mode = `console`, exact argv = `kcdx_scan WHGame.dll "<known-good pattern for a stable WHGame.dll byte sequence>"`, falsifiable observable = the expected match count + apply addr visible in console output AND mirrored to `kcdx-dev.log`. The existing `cap-32-scan` row for the Lua `kcdx.scan{...}` form stays green (no regression).
- **Suite stays X/Y green** (no regressions in any prior Phase 9 row; the flat-table forms continue to work for non-named-target paths; the existing `kcdx.scan{...}` Lua form is unchanged).

### Phase 9.3 — `kcdx.hook.*` + `kcdx.statement.*` split + `kcdx.locator.*` + `kcdx.op.*` value namespaces + multi-region trampoline pool — **NOT STARTED**

**Status (2026-05-28 audit):** zero `kcdx.statement.*` / `kcdx.locator.*` / `kcdx.op.*` / `kcdx.functions.*` / `kcdx.dll.declare` binders in `src/lua_bind*.cpp`. The `kcdx.hook` mode surface still ships as mode-as-key (the Phase 2 shape) — the sub-verb split (`kcdx.hook.before/after/around/replace`) is not built. No `kcdxStatementInterface` / `kcdxFunctionsInterface` in `include/`. Multi-region trampoline pool not extended (current `src/trampoline.cpp` is single-region).

The biggest surface phase. Lands the unified locator vocabulary + the two distinct site-modification namespaces + the value namespaces they consume + the trampoline-pool capacity work to support it.

**The two distinct namespaces:**

- **`kcdx.hook.*` — callback-based interception (industry-standard meaning of "hook").** Per-call cost (microseconds). Use when per-call Lua logic is needed.
  - `kcdx.hook.before(module, target, [locator], callback, [opts])`
  - `kcdx.hook.after(module, target, [locator], callback, [opts])`
  - `kcdx.hook.around(module, target, [locator], callback, [opts])`
  - `kcdx.hook.replace(module, target, [locator], callback, [opts])`
  - `kcdx.hook.insert_before(module, target, locator, callback, [opts])` — locator required (no meaningful default for "insert before what?")
  - `kcdx.hook.insert_after(module, target, locator, callback, [opts])` — same
- **`kcdx.statement.*` — static-bytes modification (kcdx-original concept, descriptively named).** Zero per-call cost; the game's bytes are modified and executed natively. Use when behavior is static and you want native-speed execution.
  - `kcdx.statement.replace_with(module, target, [locator], op, [opts])` — accepts only static ops
  - `kcdx.statement.insert_before(module, target, locator, callback, [opts])` — callback-only (no static-op form; insert-with-callback is the coherent use case)
  - `kcdx.statement.insert_after(module, target, locator, callback, [opts])` — callback-only (same)
  - No `before` / `after` / `around` variants — those describe callback-ordering relative to an original call, which has no static-bytes analog.

**`module` is a REQUIRED positional first arg on every hash-checked verb.** No default. The author types `kcdx.hook.before("WHGame.dll", "IsInCombat", ...)` explicitly every time. Verbose for the common case but honest about multi-DLL coverage; an author copying an example can't accidentally target the wrong module via a default. `target` accepts canonical name, stable kcdx ID, or Ghidra auto-name per Phase 9.1's resolution rules.

The split is honest about a real mechanism difference: callbacks pay per-call dispatch (trampoline + Lua marshal + `lua_pcall`); static-bytes execute natively forever after install. Authors pick by intent, not by guessing which namespace happens to have the verb they want.

**The `kcdx.functions.*` reference namespace + the game-DLL vs plugin-DLL distinction.**

Both `kcdx.hook.*` and `kcdx.statement.*` accept a function as EITHER a reference value (`kcdx.functions.<stem>.<name>`) OR a `(module_string, target_string)` pair. The reference form is the documented common path; the string pair is the dynamic-dispatch alternative. The engine dispatches by arg-1 type.

`kcdx.functions.*` carries TWO kinds of functions from TWO sources, and the stem makes the distinction visible at the call site:

- **Game-DLL functions — no-dot stem, sourced from the SQLite.** `kcdx.functions.WHGame.IsInCombat`. The stem is the DLL filename minus extension (`WHGame.dll` → `WHGame`). Populated EAGERLY at engine startup from `data/reference.sqlite` (~7 MB resident, ~200-300ms one-time, ~5ns/access). These are hash-tracked across game versions (the SQLite carries per-version hashes). This is the case the whole SQLite/Ghidra-dump apparatus exists for: WHGame.dll and friends are obfuscated, stripped, source-unavailable — the dump is how kcdx knows their names + signatures + statement metadata. `kcdx.functions.by_id[N]` is the stable-across-versions ID accessor, game-functions-only.
- **Plugin-DLL functions — dotted `<author>.<plugin>` stem, sourced from the plugin author.** `kcdx.functions["redmoon.outfit_mod"].SomeFn` (bracket-indexed because the stem has dots). The plugin DLL is AUTHOR-OWNED SOURCE — the author wrote it, knows every function's name and signature, controls when it rebuilds. It has NONE of the problems the SQLite solves, so it does NOT go through the SQLite. Plugin functions are NOT hash-tracked against the reference DB; cross-version coordination is the plugin's own semver + `[[plugin.dependencies]] min_version`.

The dotted-vs-undotted stem is structurally disjoint (game DLLs never have dotted stems; plugin stems are always `<author>.<plugin>`), so the two populations never collide. If a game DLL ever shipped with a dotted stem (extremely unusual), the engine handles it via the string-pair form and a startup note.

**How plugin-DLL functions get into the namespace (three sources, descending author cooperation):**

1. **`kcdx.dll.declare(plugin_namespace, function_map)`** — the plugin author declares their DLL's functions with signatures, COPIED FROM THEIR OWN SOURCE. No Ghidra, no disassembly — they already have the types. This is the primary path and the cornerstones-clean answer to "expose my internals for other mods to extend":
   ```lua
   -- redmoon-outfit-mod/plugin.lua
   kcdx.dll.declare("redmoon.outfit_mod", {
       CanSwapInCombat = { signature = "bool (ptr self)" },
       OnOutfitSwap    = { signature = "void (ptr self, i32 outfit_id)" },
   })
   ```
   Functions declared here populate `kcdx.functions["redmoon.outfit_mod"].*`. Another plugin hooks them by name with zero disassembly — the signature the consumer needs came free from the author's source.
2. **PDB auto-load** — if the plugin author ships `MyMod.pdb` next to the DLL (Visual Studio already produces it; shipping is "include the file"), kcdx parses it at plugin load via the Windows `DbgHelp` API (`SymLoadModuleEx` + `SymEnumSymbols`) and populates EVERY internal function's name + address — not just exports. This makes **static byte ops (`kcdx.statement.*`) on any internal zero-friction** (address comes from the PDB; static ops need no signature). Callback hooks still need a signature (PDBs don't reliably carry param types for release builds), but the address — the harder half — comes free. Graceful degradation: no PDB → exports-only; stale/mismatched PDB → `DbgHelp` detects the GUID/age mismatch, kcdx logs "PDB for plugin X doesn't match its DLL; internal functions unavailable" and falls back to exports-only. Purely additive — authors who don't ship a PDB lose nothing they had.
3. **C export table** — `extern "C"` exports are always readable (`GetProcAddress` for the address). Address only; still no signature, so callback hooks on a bare export still need the signature declared somewhere.

**The signature is the one irreducible thing.** A callback hook that marshals args needs the function's ABI (which register holds which arg, return type). Compiled C++ carries NO runtime-queryable signature — not for exports, not for internals. So a callback hook ALWAYS needs the signature from a non-binary source: the SQLite (game functions, via abi_walker during the dump), `kcdx.dll.declare` (plugin functions, from the author's source), or the consumer declaring it themselves (if they RE'd it). Static byte ops need only address + byte range and so work with no signature at all. This is a property of compiled C++, not a kcdx limitation — the engine cannot conjure ABI info the binary doesn't contain.

**Cross-plugin extension — friction is near-zero except one boundary case.** B extending A's plugin is disassembly-free in every case except A's stripped, undeclared, compiled internal that B wants to callback-hook:

| A's extension point | B extends via | Disassembly? |
|---|---|---|
| A Lua function | plain Lua wrapping (no engine involvement) | None |
| Event A publishes | `kcdx.on("a:event", ...)` | None |
| Behavior A declares | `kcdx.behavior.set("a.behavior", ...)` | None |
| C++ fn A declared (`kcdx.dll.declare`) | `kcdx.hook.*` by name | None (A declared from source) |
| C++ fn, A shipped a PDB, static op | `kcdx.statement.*` (address from PDB) | None |
| C++ fn, A shipped a PDB, callback | `kcdx.hook.*` (address free; B supplies signature) | Reads ABI off decompile, not full RE |
| C++ fn, stripped + undeclared, callback | B RE's it OR asks A to declare | The only full-RE case |

The last row is legitimately "B is doing something A never designed for" — where some effort by someone (B's RE, or A adding a one-line `kcdx.dll.declare`) is the correct cost. Every other path is disassembly-free. `docs/lua/extensibility.md` (Phase 9.6) makes these patterns idiomatic so the last row rarely triggers.

**`insert_before/insert_after` callback signature.** The callback receives captures as a named table:

```lua
kcdx.hook.insert_after("WHGame.dll", "CheckOutfitSwap", kcdx.locator.first_call_to("IsInCombat"),
    function(captures)
        kcdx.log.info("OUTFIT", "rax=%d rcx=%p", captures.rax, captures.rcx)
        -- return nothing → captures unchanged, original execution continues
        -- return a table with capture-name keys → those captures are written back to registers/memory
    end)
```

Captures available depend on the statement (the SQLite `statements.captures` metadata determines what's live at this point — visible via `kcdx_dev_inspect`). The "captures-by-name" lookup is a registration-time concept: the engine builds a thunk once at install that, per-call, copies the named registers/memory directly into a pre-allocated Lua table. No string lookup per-call; no SQLite query per-call. Per-call cost is native-speed register-to-table copies + the Lua callback dispatch.

If the callback returns a table, the engine writes the returned values back to the matching registers/memory after the callback returns (only writes back the keys present in the returned table; un-returned captures stay unchanged). Same shape as the existing `before` mode's return-flow.

**Engine pads-and-trampolines always.** When a `kcdx.statement.replace_with` op's bytes don't fit the statement's byte range, the engine trampolines automatically (lifts to ±2GB-adjacent allocation, redirects with rel32 jump). Author never sees a "doesn't fit" failure. Engine catches obvious type/kind mismatches at registration time with teaching errors ("`always_take_branch` requires a conditional jump statement; this statement is a `call`"), but does NOT gate on semantic-purpose correctness ("you're replacing the damage calculation with always-zero, is that what you intended?" — author's call, not engine's).

**`kcdx.locator.*` — locator values:**

- `function_entry()`, `function_exit()` — function-level common cases
- `first_call_to(fn)`, `last_call_to(fn)`, `call_to(fn)` (errors if multiple), `first_return()`, `last_return()`, `return_value(v)`, `first_read_of_cvar(name)` — statement-content shortcuts (documented common path)
- `matching{kind=, callee=, condition_contains=, reads_cvar=, references_string=}` — general statement-content matcher
- `matching_pattern("48 8B C1 ...")` — labeled expert hatch for raw-AOB cases

Locator defaults to `kcdx.locator.function_entry()` when omitted on verbs that accept the default. (Module is NOT defaulted — it is the required first positional arg per the rule above.)

**`kcdx.op.*` — static-bytes op values for `kcdx.statement.*`:** curated catalog with descriptive primary names + friendly aliases:

- `replace_with_return(value)` (alias: `return_const(v)`)
- `replace_with_noop` (alias: `noop`)
- `skip_call_void`
- `skip_call_return_value(value)`
- `replace_call_target(new_fn_name)`
- `always_take_branch`
- `never_take_branch`
- `invert_branch_condition`
- `replace_assignment_value(value)`
- `replace_compare_constant(value)`
- `replace_return_value(value)`

Each op carries a byte-emit function the engine invokes at apply time. The engine picks same-size byte rewrite vs trampoline based on op-bytes-vs-statement-range from the SQLite `statements.byte_range_len`. Authors never see assembly; the op names describe exactly what behavior the bytes will produce.

**Multi-region on-demand branch-pool expansion in `src/trampoline.cpp`:** when the current branch pool exceeds 80% used, the engine allocates an additional ±2GB-adjacent region (extending the existing `AllocateFromBranchPool` machinery). Up to N regions tried before genuine exhaustion. Required at TC scale where hundreds of plugins each install dozens of hooks. Strong specific error on genuine exhaustion naming the pool, percentage used, fallback attempted, actionable next step (per author-clear-error discipline).

**Files:**

- New `src/lua_bind_hook.cpp` (replaces existing, sub-verbs per mode)
- New `src/lua_bind_statement.cpp`
- New `src/lua_bind_locator.cpp`
- New `src/lua_bind_op.cpp`
- New `src/lua_bind_functions.cpp` — the `kcdx.functions.*` reference namespace (eager game-DLL population from SQLite at startup; per-plugin population at plugin load) + the `kcdx.dll.declare` verb
- New `src/plugin_pdb.{cpp,h}` — PDB auto-load via `DbgHelp` (`SymLoadModuleEx` + `SymEnumSymbols`); parses a plugin DLL's sidecar `.pdb` at plugin load, populates internal-function addresses into `kcdx.functions["<author>.<plugin>"]`; graceful fallback on missing/mismatched PDB. Links `dbghelp.lib`.
- `src/trampoline.cpp` extended for multi-region

**C++ side parity:** `kcdxHookInterface` gets `Before/After/Around/Replace/InsertBefore/InsertAfter` methods (sub-methods per mode, mirroring the Lua sub-verb shape). New `kcdxStatementInterface` for static-bytes work with the same shape. `kcdxFunctionsInterface` mirrors `kcdx.dll.declare` (a C++ plugin declares its own functions via `K.functions->Declare(...)`). The in-flight Phase 3 mode-as-field shape migrates to sub-method shape in this phase. C++ test plugins migrate alongside Lua test plugins.

**Verification gate:** every existing hook test plugin (`cap-03-hook-lua-callback`, `cap-04-midhook`, `cap-20-hook-modes`, `cap-21-mid`, `cap-22-callsite`, etc.) migrates to the new sub-verb shape; suite stays 21/21 green. New `cap-XX-statement-replace` test verifies `kcdx.statement.replace_with(kcdx.functions.WHGame.fn, kcdx.op.return_const(0))` produces the expected runtime cost shape (zero per-call Lua dispatch — verified by absence of dispatch log lines during a tight loop hitting the target). New `cap-XX-plugin-fn-declare` test: plugin A declares a function via `kcdx.dll.declare`; plugin B (depending on A) hooks `kcdx.functions["a.test"].DeclaredFn` and the hook fires — proves cross-plugin function access without disassembly. New `cap-XX-pdb-autoload` test: a test plugin ships a PDB; `kcdx.functions["test.pdbmod"].InternalNonExportedFn` resolves to a non-exported function's address; a static `replace_with_noop` on it applies — proves PDB-sourced addresses work without declaration.

### Phase 9.4 — `kcdx.find{...}` discovery surface + `kcdx_dev_inspect` console command — **NOT STARTED**

**Status (2026-05-28 audit):** no `src/lua_bind_find.cpp`; no `kcdx_find` / `kcdx_dev_inspect` console command registered. The bulk DEV DB tables `statements` / `referenced_vars` / `call_edges` exist (per Phase 9.1) but no consumer reads them.

Without discovery, statement-level modding requires Ghidra. With it, authors find what they need from what they already know about the game (a string they saw, a CVAR they read about, a function they suspect).

**Scope:**

- New `src/lua_bind_find.cpp` — `kcdx.find(criteria_table)` where the table is required (single-positional-arg form per the new rule 4 — table is the at-least-one-of-N case) and validated at parse-time to contain at least one criterion. Criteria: `string`, `cvar`, `callers_of`, `callee`, `name_contains`, `callee_in_subsystem`. Returns a Lua table of records: `{function, module, rva, decompile_quality, statements = [{idx, kind, pseudo_text, captures, applicable_ops}]}`.
  - **No matches:** returns `{}` (empty Lua table). Author's idiomatic check is `if #results == 0 then ... end`. No nil, no error.
  - **Result cap: 500.** Searches that would return more than 500 records return the first 500 plus a `_truncated = true` flag and a `_total_matches = N` count so the author knows to refine criteria. Loud truncation, not silent.
- Console command `kcdx_find` (takes module as first positional arg per the module-required rule, then criteria flags) — `kcdx_find WHGame.dll --string "test_marker"`. Invokes the same Lua path.
- New `kcdx_dev_inspect <module> <function>` console command — full statement enumeration for one function as a formatted table. Shows per-statement kind, pseudo-text, captures, applicable ops.
  - **Not-found UX:** teaching error with name-similarity suggestion + recommended next step:
    ```
    [ERROR] no function 'IsInCombatt' in WHGame.dll for KCD2 1.5.1164953.
    Did you mean: IsInCombat? Try:
        kcdx_dev_inspect WHGame.dll IsInCombat
    Or search by content:
        kcdx_find WHGame.dll --name_contains combat
    ```

Both resolve via the SQLite reference DB; no new author-facing concepts beyond the existing locator + op vocabularies.

**Verification gate:** test plugin uses `kcdx.find({string = "test_marker"})` against a reference DB containing a known function with that string → returns expected function name + statement list. Empty-criteria search returns `{}`. Synthetic 600-row search returns 500 records + `_truncated = true` + `_total_matches = 600`. Console: `kcdx_find` and `kcdx_dev_inspect` parse module + criteria correctly; not-found path produces the documented teaching error.

### Phase 9.5 — `kcdx.behavior.*` named-behavior catalog (two-tier: engine-shipped + plugin-declared) — **NOT STARTED**

**Status (2026-05-28 audit):** no `src/lua_bind_behavior.cpp`. No engine-shipped catalog at `data/behavior-catalog/`. The `behaviors` DB table referenced in the original Phase 9.1 sketch was never built (per the Phase 9.1 status note above).

The simple-modder surface. Author writes one line; never sees a function name, statement, or op. Two tiers of behaviors coexist: engine-shipped (reserved `kcdx.behavior.*` namespace) and plugin-declared (standard `<author>.<plugin>.<bare>` namespace), unified through the same `set`/`get`/`list` verbs.

**Scope:**

- New `src/lua_bind_behavior.cpp`:
  - `kcdx.behavior.declare(name, spec)` — a plugin declares a behavior it implements. Stamped as `<plugin.author>.<plugin.name>.<name>` per [naming-namespaces.md](../../../.claude/rules/naming-namespaces.md); appears in `list()` under the full prefixed name; callable from any plugin via the full name or via bare-name precedence (self > engine > other).
  - `kcdx.behavior.set(name, value)` — sets a behavior (engine's OR another plugin's). Resolves name with self > engine > other precedence; calls the resolved behavior's `implementation` function with `value`. `value` accepts any Lua type (bool, int, float, string, table, function); the behavior's implementation validates per its own logic. No engine type-gating.
  - `kcdx.behavior.get(name)` — returns the current set value (or the spec's `default` field if never set).
  - `kcdx.behavior.list([filter])` — returns all available behaviors, optionally filtered by name prefix (`kcdx.behavior.list("kcdx.")` for engine-shipped only; `kcdx.behavior.list("redmoon.")` for all of redmoon's; no filter for everything).
- Engine-shipped catalog at `data/behavior-catalog/behaviors.toml` (human-authored, build-step imports into the SQLite `behaviors` table with `source = "engine"`). Each entry: name, description, default value, implementation (a Lua function or reference to a kcdx.hook/kcdx.statement recipe). Reserved `kcdx.*` namespace.
- Plugin-declared behaviors register at plugin load time via `kcdx.behavior.declare`. Stored in an in-memory runtime registry alongside the SQLite-shipped behaviors. NOT in the SQLite (the SQLite is a static reference; runtime registrations are dynamic). `kcdx.behavior.list` queries unify both sources.
- **Ship with 5-10 engine-catalog entries at minimum.** All referencing functions whose data the parallel Ghidra research has produced. Canonical example: `kcdx.behavior.outfit_swap_in_combat` (the long-running case study of this design).
- Behavior-only plugins exempt from `authored_against_game_version` (engine resolves underlying calls itself + knows per-version hashes; teaching errors surface under the behavior name). Plugin-DECLARED behaviors that internally call `kcdx.hook.*` or `kcdx.statement.*` still need the field on the DECLARING plugin (since the declaring plugin uses hash-checked primitives in its implementation); consumer plugins that only call `kcdx.behavior.set` against another plugin's behaviors remain exempt.

**Cross-plugin example:**

A TC mod ships a `Realism` plugin declaring behaviors:

```lua
-- redmoon-realism/plugin.lua
kcdx.behavior.declare("hardcore_combat", {
    description = "Doubles damage taken and dealt",
    default = false,
    implementation = function(value)
        if value then
            kcdx.statement.replace_with("WHGame.dll", "DamageMultiplier",
                kcdx.locator.first_return(), kcdx.op.replace_with_return(2))
        end
    end,
})

kcdx.behavior.declare("npc_essential_list", {
    description = "List of NPCs that cannot be killed",
    default = {},
    implementation = function(value)
        -- value is a Lua table; behavior installs hooks per name in the list
        for _, name in ipairs(value) do ... end
    end,
})
```

A non-expert player installs Realism + writes a tiny tweak plugin:

```lua
-- player-tweak/plugin.lua
kcdx.behavior.set("redmoon.realism.hardcore_combat", true)
kcdx.behavior.set("redmoon.realism.npc_essential_list", {"Henry", "Hans", "Theresa"})
```

The tweak plugin has no statement-level knowledge; it consumes the behaviors Realism declared. This is the contribution surface that scales the simple-modder UX organically — each TC plugin grows the named-behavior pool for downstream consumers.

**Catalog grows two ways.** (1) Authors PR to the kcdx-shipped catalog when a behavior is genuinely common across multiple unrelated mods (gets promoted to `kcdx.behavior.*`). (2) Plugin-shipped behaviors stay under each plugin's namespace and serve their own ecosystem.

**Verification gate:** test plugin uses `kcdx.behavior.set("test_behavior", true)` against a `kcdx.behavior.*` catalog entry → underlying byte rewrite applies. Behavior-only plugin without `authored_against_game_version` still loads (exempt). `kcdx.behavior.list()` returns engine + plugin behaviors; `kcdx.behavior.list("redmoon.")` filters to redmoon's. Cross-plugin test: plugin A declares `a.test.foo`; plugin B calls `kcdx.behavior.set("a.test.foo", "value")`; implementation fires with the value.

### Phase 9.6 — `kcdx.bytes` narrowing + Lua API rule update + final migration — **NOT STARTED**

**Status (2026-05-28 audit):** `kcdx.bytes` still serves its Phase 2 remit (function-internal + non-function bytes both); the narrowing to "non-function only + labeled-expert `pattern` hatch" is not landed. Rule 4 / 4a in `.claude/rules/lua-api-surface.md` already documents the sub-verb model (struck during the 2026-05-28 doc updates that documented Phase 9.7's smart-resolver direction) — confirm against the rule file before landing. `docs/lua/extensibility.md` does not exist.

The cleanup phase. Rewrites the design rule that governs the surface; narrows `kcdx.bytes` to its post-9.3 remit; migrates any remaining test plugins.

**`kcdx.bytes` narrowing:**

- Remit narrowed to: raw byte rewrites OUTSIDE functions (data section, vtable slots, string tables); labeled-expert `pattern`-locator use for AOBs without function context.
- Function-internal byte work is documented as belonging in `kcdx.statement.replace_with` with a `kcdx.locator.*`.
- The narrowing removes engine-error space: `kcdx.bytes` does what `kcdx.statement.*` doesn't (touch non-function memory), `kcdx.statement.*` does what `kcdx.bytes` doesn't (function-internal with content locators + hash tracking). No overlap.

**`.claude/rules/lua-api-surface.md` update:**

- Rule 4 rewritten:
  > **4. Required args are positional; optional args live in a trailing table.** Required → positional means the author cannot forget — Lua errors immediately at the call site, not later at runtime. Optional → table means self-documenting, order-free, and additions don't break existing call sites.
- New rule 4a:
  > **4a. Discrete behavioral variants are sub-verbs, not table keys.** `kcdx.<verb>.<variant>(...)` makes the variant impossible to forget, lets each variant carry its accurate signature, and surfaces variants in autocomplete. Examples: `kcdx.hook.before/after/around/replace`, `kcdx.statement.replace_with/insert_before/insert_after`, `kcdx.log.info/warn/error/debug`. Mode-as-key reserved for cases where multiple modes legitimately compose on a single call (none currently exist).

**Final migration:**

- Any remaining test plugins / in-source call sites using the pre-9.3 `kcdx.hook` shape (mode-as-table-key) migrate to sub-verb shape.
- `kcdx.bytes` callsites that were function-internal byte work migrate to `kcdx.statement.replace_with` per the narrowed remit.
- `docs/lua/*.md` per-call files rewritten to reflect final shapes.

**`docs/lua/index.md` leads with the tiered author model.** The author landing on the docs sees in the first three sentences which surface to reach for based on what they want to do:

> kcdx gives you four ways to change how the game behaves. **`kcdx.behavior.set("name", value)`** if a named behavior exists for what you want (one line, no concepts to learn). **`kcdx.hook.before/after/around/replace`** when you need per-call Lua logic at a code site (per-call cost). **`kcdx.statement.replace_with`** when you want a static change at a code site (zero per-call cost; bytes execute natively). **`kcdx.bytes`** for raw byte rewrites outside functions (data section, vtable slots) or the labeled-expert `pattern` hatch.
>
> Browse `kcdx.behavior.list()` for what's named already (engine catalog + every loaded plugin's declared behaviors). Use `kcdx.find{...}` to discover a function from what you know (a string the game shows, a CVAR name). Use `kcdx_dev_inspect` for full statement detail on a function.

Without this front-door framing, the docs are a flat verb list; with it, the author picks the right tier in seconds.

**`docs/lua/extensibility.md` — the cross-plugin extension guide.** A first-class doc topic covering "make your plugin extensible" + "extend another plugin," leading with the disassembly-free paths. The highest-leverage deliverable for the friction-point-3 reduction (cultural, not engine): it makes the disassembly-free extension patterns idiomatic so plugin authors reach for them by default. Contents:

- **Make your plugin extensible (author A):** publish events at your extension points (`kcdx.publish` — the recommended primary surface, the SKSE mod-event pattern); declare behaviors for configurable rules (`kcdx.behavior.declare`); write extensible logic in Lua (other plugins wrap your Lua functions natively, zero friction); declare your DLL's public functions (`kcdx.dll.declare`, signatures from your own source); ship your `.pdb` if you want your internals statically modifiable by others.
- **Extend another plugin (author B):** subscribe to A's events (`kcdx.on("a:event", ...)`); reconfigure A's behaviors (`kcdx.behavior.set("a.behavior", ...)`); wrap A's Lua functions (plain Lua); hook A's declared C++ functions by name (`kcdx.hook.*`). The doc names the one boundary case — A's stripped, undeclared, compiled internal — as the rare expert fallback (RE it, or ask A to add a one-line `kcdx.dll.declare`).
- Leads with events/behaviors/Lua (zero friction for both sides); function-level hooking documented as the lower-level tool; the RE case explicitly the boundary of supported extension.

**Verification gate:** full test suite green; no plugin uses old surface forms; rule 4 + 4a documented; `kcdx.bytes` narrowed-remit doc landed; `docs/lua/index.md` leads with the tier model; `docs/lua/extensibility.md` exists and covers both directions; `kcdx.dll.declare` + `kcdx.functions.*` per-call docs landed; per-call `docs/lua/` and `docs/cpp/` entries cover every shipped capability per [docs-discipline.md](../../../.claude/rules/docs-discipline.md).

**Empowered C++ wrapper for `kcdx.bytes`.** Also ships in this phase since this is bytes' next unshipped phase. The raw `kcdxBytesInterface::Register(&opts)` is the always-available floor (Phase 3 sub-2, DONE); the empowered helper layers on top in `include/kcdx/Kcdx.h`, peer to the existing `kcdx::hook::*` helpers:

- `kcdx::bytes::Replace(K, "name", replacement, opts={})` — required args (target name + replacement bytes) positional; optional knobs (name / description / original / module / offset / idempotent / context / anchorString / `[advanced]` pattern / addressId / targetSymbol locators) in a designated-initializer-style trailing struct. Auto-threads `owningPlugin = K.self`. Wraps `K.bytes->Register(&opts)`. Header-only.

`docs/cpp/bytes.md`'s common-path lead flips to the empowered form; the raw `K.bytes->Register(&opts)` form is demoted to the labeled raw-floor drop-down per the 3-floor model (`docs/cpp/wrapper.md`). Test row added under `test-plugins/cap-NN-cpp-bytes-wrapper/` (both surfaces of the capability — raw + wrapper — under permanent regression, paralleling cap-36/cap-37 from Phase 3 sub-1). Direction was user-locked in the 2026-05-28 `/senior-architect-reply` thread for the hook docs flip; this is the bytes-side mirror.

### Phase 9.7 — MERGED into Phase 9.2 (2026-05-28)

The smart-resolver sub-verb shape `kcdx.<verb>.<name>.<mode>` was originally drafted as its own phase against the curated refdb cache only. The audit on 2026-05-28 (during the `/feature` brief for `kcdx.declare`) surfaced that Phase 9.7's resolver and Phase 9.2's `kcdx.declare` are **two halves of one surface** — a unified named-target table populated from two sources (curated refdb + author declarations), accessed through one smart resolver. Sequencing them would have shipped a transitional UX (declare against the old flat `kcdx.hook{target=...}` shape, then a second author-visible rewrite at 9.7).

**The full specification lives in Phase 9.2 above** ("Unified named-target surface"). It covers: the unified `ResolvedTarget` shape; the curated + declared population sources; the `self > engine > other` precedence walk; the `__index` smart resolver; the per-verb "valid modes for this kind" tables; the C++ mirror; the value-form `kcdx.declared(name)` accessor; the verification gate. Nothing from the original Phase 9.7 prose was dropped — the UX wins, files sketch, glossary terms, and verification cases are all carried forward in the unified phase.

The phase numbering preserves the 9.7 slot rather than renumbering 9.3–9.6 to avoid renumbering chains and to keep external references to "Phase 9.7" finding the redirect.

### Phase 10 — `[[event]]` → `kcdx.on(...)` event catalog — **LIFECYCLE EVENTS DONE; GAMEPLAY EVENT CATALOG NOT STARTED**

**Status (2026-05-28 audit):**
- **Done:** `kcdx.on(<lifecycle_event>, fn)` is a shipped Phase 2 surface; the lifecycle event catalog is wired through `src/messaging.cpp` and consumed widely (cap-24 lifecycle-events row PASSes; every save/load / post-load / input-loaded / new-game / pre-load-game / post-load-game / save-game / load-game-selected / delete-game / ready event is registered and tested).
- **Not built:** the 10–15 NEW gameplay events the phase committed to (damage_taken / damage_dealt / save_created / dialogue_line_spoken / item_picked_up / location_entered / combat_started / combat_ended / perk_unlocked / level_up / quest_stage_advanced / npc_interacted_with) — none of these are RE'd or hooked. No `kcdx.on("damage_taken", …)` consumer exists; no test plugin exercises a gameplay event subscription.

Addresses [docs/design-gaps.md](../../../docs/design-gaps.md) gap #15 (game-event API beyond lifecycle messages).

The today-unimplemented `[[event]]` table mentioned in design.md but never landed becomes `kcdx.on(...)`. Lifecycle messages get a documented catalog of named event strings (already wired via [src/messaging.cpp](../../../src/messaging.cpp) — DONE). Phase 10 ships the lifecycle catalog plus the FIRST 10-15 gameplay events from RE work (candidate list per gap #15: damage taken/dealt, save created, dialogue line spoken, item picked up, location entered, combat started/ended, perk unlocked, level-up, quest stage advanced, NPC interacted with). Each event is one kcdx-owned hook against the underlying CryEngine call path; plugins subscribe via `kcdx.on(...)` and get the event without each plugin re-hooking the same function.

**Event sites are hash-tracked the same as direct hooks.** Each gameplay event's underlying hook is a function-name reference in the SQLite reference DB; when KCD2 updates and changes the function backing an event, subscribers to that event get the same `on_changed` posture handling as a direct `kcdx.hook.*` call would. Authors subscribing via `kcdx.on(...)` to a broken event get a teaching error naming the event + the underlying function; other event subscriptions in the same plugin continue working regardless. Same survival contract as direct hooks, applied uniformly.

### Phase 11 — force-load WHGame.dll + use its compiled Lua

**Scope:** kcdx.dll's DllMain force-loads WHGame.dll, then uses the FIX A symbol shim to spin up the Lua VM via WHGame's compiled `luaL_newstate`. ONE compiled Lua body in the process (WHGame's); the dual-Lua sentinel hazard dies by construction.

This phase depends on FIX A (the symbol-harvest work currently in progress at `_research/phase8-fix-a/`). At the time of this plan, FIX A has ~38% of the ~110 Lua RVAs mapped. Phase 11 of this restructure begins when FIX A's shim is functional end-to-end; Phases 1-10 of the restructure can run in parallel with the remaining FIX A harvest work.

**Mechanism:**

1. kcdx.exe (launcher) does CreateProcessW(KingdomCome.exe, SUSPENDED) + CreateRemoteThread(LoadLibraryW, "kcdx.dll") + ResumeThread. Same as today's Phase 1 launcher.
2. kcdx.dll's DllMain begins under loader lock.
3. paths::Init + deferred-log buffer ready.
4. Walk manifests + load_order.
5. Install before_game patches/hooks against already-loaded modules (ntdll, kernel32, kcdx.dll itself, dinput8.dll if present).
6. Register `LdrRegisterDllNotification` for WHGame.dll mapping.
7. `LoadLibraryW(L"WHGame.dll")` — synchronously maps the DLL, plus whatever its dependency chain pulls in. The LDR notification fires for each newly-mapped module; for each, the before_game patches/hooks declaring that module as their target apply (bugsplat-filename-fix's target is BugSplat64.dll, which lands here when WHGame's chain maps it; before_game entries declaring WHGame.dll apply when WHGame itself maps). THEN WHGame.dll's own DllMain runs (BugSplat init now sees the patched colon-free string).
8. Initialize FIX A shim: `kcdx::lua_shim::Resolve()` populates the function-pointer table by `GetProcAddress` (or Address Library Resolve) against every `lua_*` and `luaL_*` symbol in WHGame.dll.
9. Call WHGame's `luaL_newstate` via the shim. The `lua_State*` is allocated by WHGame's compiled Lua, with WHGame's sentinels.
10. Register kcdx.* Lua tables in the new state.
11. Walk before_game-zone plugins in order; run each plugin's `plugin.lua` and/or call its DLL's `kcdxPlugin_Preload` / `kcdxPlugin_Load`. Lua plugins NOW work in before_game zone.
12. Spawn worker thread to handle after_game-zone work.
13. Worker thread continues normally — but every `lua_*` call routes through the FIX A shim, so there's no second compiled Lua body anywhere.

When CryEngine's startup code eventually calls `luaL_newstate` itself (somewhere inside its game-engine init), kcdx hooks the call and returns our pre-allocated state. CryEngine never creates its own — it operates on our state, using its own compiled Lua functions (which are the same Lua we resolved via FIX A — one body).

**Why this kills the hazard:**

The dual-Lua sentinel hazard exists because two compiled Lua bodies (kcdx's static-linked + WHGame's static-linked) operate on one `lua_State` and each compares against its own `.rdata` sentinel addresses. After FIX A, kcdx has NO compiled Lua of its own — it's all forwarded through the shim to WHGame's compiled functions. One body, one sentinel set, hazard impossible.

**What changes in the engine:**

- `vendor/lua/*.c` is dropped from the build. The `lua` static library disappears from CMakeLists.txt.
- `vendor/lua/*.h` stays (we need the headers for struct definitions: lua_State, global_State, Table, Node) used by PROBE Q and diagnostic helpers.
- `src/lua_shim.cpp` (already scaffolded at ~54 functions; expand to full 110+) defines every `LUA_API` / `LUALIB_API` symbol as a forwarder through `kcdx::lua_shim::g_api.<fn>(...)`.
- `src/address_library.cpp` gains ~110 new entries (IDs in the 1100-1199 range) for the Lua function RVAs in WHGame.dll.
- FIX C's `vendor/lua/ltable.c` setnodevector patch is reverted — no longer needed once there's no kcdx-side compiled Lua.
- PROBE Q stays as the permanent regression canary.
- kcdx::lua_shim::Resolve() runs in DllMain step 8 above, before any Lua call.

**Outcome:**

- Lua plugins can declare `zone = "before_game"` and their `plugin.lua` runs before CryEngine's startup code reaches its own `luaL_newstate` call (the LDR notification + WHGame DllMain happen synchronously inside our LoadLibraryW call; our Lua plugin code runs AFTER LoadLibraryW returns and AFTER our shim-driven `luaL_newstate` returns). Timing chain: before_game patches → WHGame DllMain → VM spun up via shim → before_game Lua plugins.
- The bugsplat-filename-fix DLL from Phase 4 could become a Lua plugin then (though the DLL form already works fine; no migration required).
- The `kcdxLuaApi` plugin-DLL surface becomes a direct forwarder to the same shim. C++ DLL plugins get the same one-body Lua.

**Dependencies:**

- FIX A symbol harvest at 100% (~110 RVAs verified, Address Library populated). Currently 38% (per user message); active work.
- Phases 1-10 of this restructure landed (so the new Lua API surface exists to migrate to the shim-backed VM).

**Phase 11 sub-phases (each with its own verification gate):**

### Phase 11a — FIX A shim integration

Depends on: FIX A symbol harvest at 100% (~110 RVAs verified, Address Library populated). FIX A is independent work; Phase 11a starts when FIX A reports done.

- Add `src/lua_shim.cpp` symbol forwarders (already scaffolded at ~54 functions; expand to 110+).
- Populate Address Library with all Lua RVAs (range 1100-1199).
- Add `kcdx::lua_shim::Resolve()` — runs after WHGame.dll is mapped; resolves every function pointer.
- **Verification gate**: a standalone test calls `kcdx::lua_shim::g_api.lua_pushinteger(L, 42)` through the shim and verifies the value lands on the stack correctly. PROBE Q canary stays silent. No 21/21 changes — the shim coexists with kcdx's compiled Lua at this point.

### Phase 11b — Force-load WHGame.dll from kcdx.dll DllMain

- Add `LoadLibraryW(L"WHGame.dll")` call to kcdx.dll DllMain BEFORE the before_game registration pass.
- Add `LdrRegisterDllNotification` for WHGame.dll mapping (already exists in `src/ldr_notify.cpp`; verify it fires synchronously inside the LoadLibraryW call).
- **Verification gate**: kcdx.dll DllMain log shows `LDR notification fired for WHGame.dll → before_game patches/hooks applied` BEFORE the next log line. 21/21 still green. Loader-lock budget measurement: log the wall-clock time from kcdx.dll DllMain start to "spawn worker thread" — must be <500ms.

### Phase 11c — Lua VM startup via shim, hook game's luaL_newstate

- Call `luaL_newstate` via FIX A shim AFTER LoadLibraryW(WHGame.dll) returns. Store the returned `lua_State*` as kcdx's primary game state.
- Hook the function CryEngine uses internally to create its own Lua VM. When CryEngine calls it, return kcdx's pre-allocated state instead of letting it create a new one.
- Register `kcdx.*` Lua tables in the state. Plugin Lua surface now alive in kcdx.dll DllMain.
- **Verification gate**: 21/21 still green. A test plugin's `plugin.lua` can call any `kcdx.*` API from `before_game` zone without errors. CryEngine's game scripts (System.LogAlways etc.) still work — they're executing in the kcdx-allocated state, but using WHGame's compiled Lua functions to do so, so nothing is different from their perspective.

### Phase 11d — Lift Lua-in-before_game restriction, drop static Lua

- Remove the capability-gate error for Lua plugins declaring `zone = "before_game"`.
- Drop the static `lua` CMake target — `vendor/lua/*.c` is no longer compiled.
- Revert FIX C's `vendor/lua/ltable.c::setnodevector` patch — no longer needed.
- **Verification gate**: 21/21 green. Engine binary shrinks (no compiled Lua code). PROBE Q canary stays silent across a full save-load cycle (the canonical dual-Lua hazard repro). A new test plugin (`cap-XX-lua-before-game`) declares `zone = "before_game"` in its kcdx.toml + has `plugin.lua` calling `kcdx.hook` — verifies the hook fires before CryEngine's init code runs.

Phase 11 is complete when 11d's verification is green.

### Phase 12 — C++ empowered-wrapper sweep (remaining surfaces) + correctness fix + UX polish — **NOT STARTED**

The closing C++ ergonomics phase. Phase 3 sub-1/sub-2/sub-3 shipped the raw `kcdxHookInterface` / `kcdxBytesInterface` / `kcdxTrampolineInterface` v2 + the empowered `kcdx::hook::*` wrapper layer on hook only (sub-1 step 6). Phase 9.2 ships the empowered `kcdx::declare::Function/Value` wrappers alongside `kcdxDeclareInterface`. Phase 9.6 ships the empowered `kcdx::bytes::Replace` wrapper alongside the `kcdx.bytes` narrowing. Phase 12 sweeps the remaining shipped raw surfaces that have no future-phase home of their own — code / task / cosave — and lands the wrapper-machinery correctness fix + UX polish.

**Direction (user-locked 2026-05-28, in the `/senior-architect-reply` thread for the hook docs flip — Option C):** the canonical Lua-mirror peer for any named surface is the empowered wrapper; the raw `K.<verb>->...(&opts)` form is the labeled raw-floor drop-down per [`docs/cpp/wrapper.md`](../../cpp/wrapper.md) §"The 3-floor model". Every wrapper that lands flips the matching `docs/cpp/<verb>.md` to lead with the empowered shape and demote the raw form.

**Naming convention (user-locked):** per-surface namespace, matching the shipped `kcdx::hook::*` exactly. Autocomplete after `kcdx::code::` shows only code verbs; mirrors Lua's `kcdx.code.` chain shape; predictable per [`.claude/rules/lua-api-surface.md`](../../../.claude/rules/lua-api-surface.md) rule 1.

**Each row ships its own `test-plugins/cap-NN-cpp-<surface>-wrapper/` regression per [`.claude/rules/test-suite.md`](../../../.claude/rules/test-suite.md) — both surfaces of the capability (raw + wrapper) under permanent regression, paralleling cap-36 (raw hook) + cap-37 (hook wrapper) from Phase 3 sub-1.**

#### Phase 12 sub-1 — empowered wrappers for code / task / cosave

| Step | What ships | Tier | Surface line saved (typing ROI rank, per the 2026-05-28 wrapper-improvements audit) |
|---|---|---|---|
| **1** | `kcdx::code::Allocate(K, "name", {.bytes=, .size=, .exportName=})` — required (name) positional, options-struct trailing (bytes / bytesSize-deduced / size / pool / exportName). Auto-threads `owningPlugin = K.self`. Wraps `K.code->Allocate(&opts)` (`kcdxTrampolineInterface_Version >= 2`). Header-only in `Kcdx.h`. | A | 9→4 lines (~56%), ranked #3 |
| **2** | `kcdx::task::Run(K, []{...})` — boxes a (capturing) lambda into a `kcdxTask` whose `Run()` invokes the lambda and whose `Dispose()` does `delete this`. Wraps `K.task->AddTask(new …)`. Header-only in `Kcdx.h`. Captures stored in the boxed task (storage owned by the wrapper). | B | 7→1 lines (~86%), ranked #6 |
| **3** | `kcdx::cosave::Save(K, 'UID', [](auto& w){ w.Record("tag", ver, [&](){ w.WriteU32(n); }); })` + `kcdx::cosave::Load(K, [](auto& r){ for (auto rec : r) { … } })`. Boxes the SetUniqueID + SetSaveCallback / SetLoadCallback registration; the `auto& w` / `auto& r` writer/reader objects fuse the OpenRecordNamed + WriteRecordData pairing (you can't forget the pair) and expose typed `WriteU32` / `WriteBlob` / `WriteString` / `ReadU32` / etc. helpers. Wraps `K.serialization->*`. Header-only in `Kcdx.h`. | B | 10→4 lines (~60%), ranked #7 |
| **4** | Docs sweep — flip [`docs/cpp/code.md`](../../cpp/code.md) common-path lead to the wrapper form; update [`docs/cpp/cosave.md`](../../cpp/cosave.md) common-path lead to the new wrapper; new [`docs/cpp/task.md`](../../cpp/task.md) (the existing surface is undocumented today) leading with `kcdx::task::Run`. Demote raw forms to the "raw floor / drop-down" section anchored at [`docs/cpp/wrapper.md`](../../cpp/wrapper.md) §3-floor model. Pull NYI markers on every newly-empowered surface. [`docs/cpp/index.md`](../../cpp/index.md) map updated. Per [`docs-discipline.md`](../../../.claude/rules/docs-discipline.md). | docs | docs-discipline coverage |

#### Phase 12 sub-2 — `sig_traits::dsl_token<T>` correctness fix

**The wrapper-machinery silent-miscompilation hazard (item E from the 2026-05-28 wrapper-improvements audit).** Today the primary template at [`include/kcdx/Kcdx.h:188-191`](../../../include/kcdx/Kcdx.h#L188-L191) defaults unrecognized types to `"ptr"`. Silent hazard — `Before<MyEnum(int)>` where `MyEnum` is `enum class : uint16_t` emits `ptr (i32)` and the engine reads the wrong width on the return. Fix: convert the primary template into a `static_assert(sizeof(T) == 0, "Unsupported type in hook signature DSL — specialize kcdx::detail::dsl_token<YourType> in your TU, or use a primitive the wrapper recognizes.")` so the miscompilation fires at compile-time. Existing specializations in `Kcdx.h:198-214` unchanged. Test row asserts the gate fires (compile-failure smoke test on a deliberately-unsupported type).

#### Phase 12 sub-3 — wrapper UX polish (A + B + F from the audit)

Header-only ergonomic improvements to the existing wrapper machinery. None changes the engine.

| Step | What ships | Audit rank | Per-plugin saving |
|---|---|---|---|
| **1** | **Auto-`Init()` (A).** Today `K.Init(api, "redmoon", "outfit")` requires the author to retype their `[plugin].author` + `[plugin].name` as string literals in their own DLL — duplicates `kcdx.toml`, and a typo (e.g. `"redomon"`) silently breaks identity threading (the resolved `K.self` becomes `kcdxInvalidPluginHandle` and every self-tier name resolution misses). Two implementation options; settled at the sub-step design step: **(a) CMake-generated header.** kcdx ships a CMake helper macro that parses the plugin's `kcdx.toml` at configure-time and emits `kcdx_plugin_id.h` with `inline constexpr const char* kcdxPluginAuthor = "…"; …Name = "…";`. `K.Init(api)` (no string args) reads them. **(b) Engine thunk.** Append `kcdxInterface::GetSelfPluginInfo(HMODULE dll)` to the root interface (AP11 append-only); engine maps the calling DLL's `HMODULE` → plugin record (the loader already knows). `K.Init(api)` calls it. (a) is the simpler ship; (b) makes copy-paste of plugin shells less brittle. | #2 | 1 line per plugin Init + closes the silent-typo footgun |
| **2** | **`KCDX_PLUGIN(...)` macro (B).** Synthesizes the `static Kcdx K;` global + the `kcdxPlugin_Load` entry point: `KCDX_PLUGIN("redmoon", "outfit") { K.log.Info(...); kcdx::hook::Before<...>(K, "IsInCombat"); return true; }`. Removes the boilerplate every plugin pays. Composes with step 1 — if (a) ships the macro reads identity from the generated header, no string args needed. | #3 | 4-5 lines per plugin shell |
| **3** | **Typed `kcdx::Handle` wrapper (F).** Today `auto h = TryBefore<...>(K, "name");` returns a raw `kcdxHookHandle` and the author calls `K.hook->IsApplied(h)` / `GetReason(h)` / `Uninstall(h)` against the raw handle. The wrapper would be `struct kcdx::Handle { kcdxHookHandle h; const kcdxHookInterface* iface; bool Applied() const { return iface->IsApplied(h); } const char* Reason() const; bool Uninstall(); };` so `auto h = TryBefore<...>(K, "name"); if (!h.Applied()) log.Warn("...", h.Reason());` reads as a typed handle, not a raw integer. Same shape for `kcdxBytesHandle`. The `Try*` forms in `kcdx::hook` change return type from `kcdxHookHandle` to `kcdx::Handle` (mild source-incompatibility for any caller that stored the raw type; sub-step ledger row notes the migration). | #4 | 1-3 lines per Try*-using plugin |
| **4** | Docs sweep — [`docs/cpp/wrapper.md`](../../cpp/wrapper.md) updated to lead with the auto-`Init()` shape (string-literal form demoted to "if you need to override"); add a `KCDX_PLUGIN` macro section; update the handle examples to use `kcdx::Handle`. Update [`docs/cpp/plugin-shell.md`](../../cpp/plugin-shell.md) to lead with the macro. | docs | docs-discipline coverage |

#### Phase 12 — outstanding tracked (capturing-lambda gap)

The per-hook context slot for capturing-lambda support in `kcdx::hook::*` is an engine ABI change (`void* userdata` on `kcdxHookOptions` + JIT thunk threading + the wrapper's capturing-lambda overload), not a wrapper-only edit. Strictly more risk and more scope than Phase 12's header-only sweep. Tracked as its own outstanding-work entry: [`hook-capturing-lambda-context-slot.md`](../hook-capturing-lambda-context-slot.md). Lands when its trigger fires.

**Verification gate (Phase 12):** every shipped raw C++ surface has its empowered-wrapper peer in `Kcdx.h`; every wrapper's surface line-saving matches the audit's ranked ROI; `docs/cpp/*.md` common paths lead with the wrapper, raw forms demoted; the `dsl_token` static_assert fires on a deliberately-unsupported type (compile-failure smoke test); auto-`Init()` reads identity from the manifest; `KCDX_PLUGIN` macro synthesizes the shell; `kcdx::Handle` wrapper carries the typed handle API; full test suite green; per-call `docs/cpp/` entries cover every shipped wrapper per [docs-discipline.md](../../../.claude/rules/docs-discipline.md).

## Test discipline

**Every shipped capability ships with a corresponding regression test.** This is a workspace policy, not a phase-specific rule. The 21-plugin test suite is the canonical aggregator; new capabilities extend it.

- Phase 2 (Lua API skeleton): every new `kcdx.*` function ships a test plugin that exercises it. `kcdx.hook` → test plugin verifies install + dispatch + arg mutation across all 5 modes. `kcdx.bytes` → test plugin verifies byte rewrite + idempotency. Etc.
- Phase 3 (C++ DLL API): every new sub-interface ships a test DLL using the `Kcdx` wrapper. Coverage parity with Lua.
- Phase 4 (engine builtin migration): the bugsplat-filename-fix DLL ships with a regression test (the AV-on-demand `kcdx_crash_now` console command verifies the dmp lands at the expected path).
- Phase 9-10 (high-level API stubs + events): each stub returning "not yet implemented" still ships a test that the stub returns the expected stub value + logs the expected message. When the stub is filled in later, the test extends to verify real behavior.
- Phase 11 (force-load + FIX A): a regression test verifies the Lua VM is up before any plugin runs, that PROBE Q canary stays silent, and that the dual-Lua hazard is gone.

The 21/21 suite count grows as capabilities ship. Every PR that adds a `kcdx.*` function adds a corresponding test plugin. PRs without tests are not merged.

## Performance discipline

The priority order is UX > Capability > Performance — but priority #3 is still a priority, and the user's clarification on these tradeoffs sets the rules:

- **One-time engine-startup cost is acceptable.** Allocating a few KB at plugin load, computing the unified ordered list, running conflict_engine pre-flight once — all of this happens during startup or after-game phase transitions, not during gameplay. Fine to spend cycles here.
- **Per-frame / per-invocation costs are NOT acceptable** without justification. Anything that runs every time a hooked function fires must be measured and optimized. The plan flags three hot-path concerns:

### Performance-critical paths and discipline

> **As-built note.** The original sketch here specced lazy per-slot
> `args.szApp:set()` wrappers. We chose the simpler **return-flow**
> surface instead (params in as positional Lua values, mutate by
> returning) for UX — see `.claude/rules/lua-api-surface.md`. The
> discipline below is updated to the as-built dispatch
> (`src/hook_chain.cpp`). Hot-path optimization (e.g. eliding the arg
> marshal/writeback for read-only `before` hooks) is deferred to the
> benchmark gate, not pre-built.

| Path | Concern | Discipline |
|---|---|---|
| `kcdx.hook` `mode=around` `orig()` call | The `orig` callable is a JIT'd `lua_CFunction` over MinHook's pOriginal (the `dynamic_call` thunk). Calling it crosses Lua→C→original→back. | Per-INSTALL JIT, not per-call: the thunk is built once at install (over the baked pOriginal VA) and reused. Per-call cost is the marshal + one extra Lua→C crossing — intrinsic to "let Lua decide whether to call the original", paid only by `around`. before/after/replace don't pay it. Zero per-call heap alloc in the dispatch path itself. |
| `kcdx.hook` arg/return marshaling (return-flow) | The dispatcher reads each arg slot into a bare Lua value before the callback, and writes back the callback's returned values to the slots after. Strings need their converted bytes to outlive the call. | Read/write is per-slot by the parsed `hook_signature::Type` (no per-call heap for primitives). Strings (`wstr`/`cstr`) are pinned in a per-dispatch arena (thread-local, depth-counted so re-entrancy is safe) and freed at the outermost dispatch exit. `LUA_NUMBER=float` is respected on both directions (`cvtsi2ss`/`cvttss2si`). The marshal touches every arg every call today; eliding it for read-only `before` hooks is a benchmark-gated optimization, not pre-built. |
| Hot-path hooks (every-frame functions) | An author can install `kcdx.hook` on a function called 10,000×/frame. Even O(1) per-call overhead matters at that scale. | Document in `docs/lua/hook.md` that hot-path hooks should prefer `replace` (return-only marshaling) or a read-only `before`; `around` and arg mutation are convenient but cost more — authors know what they choose. Optimize the hot path IF the benchmark gate shows it matters. |

### What we promise

- Plugin authors don't pay performance cost for capability they didn't use. A `mode=before` hook with no arg mutation doesn't pay for the writeback infrastructure that `mode=around` needs.
- The engine's own overhead at runtime is bounded: O(1) per hook fire, regardless of how many hooks are installed total. (The JIT thunk per hook is self-contained.)
- Performance regressions are caught by the test suite: each capability ships with a regression test (per workspace policy), and tests assert "before vs after" frame time when relevant.

### Benchmark gates — non-blocking but documented

A benchmark harness lands incrementally — NOT a blocking gate for any phase. The plan commits to measuring three things over the restructure's life, with results recorded in `docs/performance.md`:

- **kcdx.hook mode=before on a 10kHz call site** — measured after Phase 2 (or when the harness exists). Target: <0.5% frame time added. If the measurement misses the target, the engine optimizes the thunk before shipping the hook to TC authors who'll abuse it.
- **conflict_engine pre-flight at TC scale** (e.g. 1000 entries) — measured after Phase 7. Target: <50ms total. Sweep-line algorithm should handle this easily but the number gets recorded.
- **Loader-lock time budget for kcdx.dll DllMain** — measured after Phase 11. **This one IS more important.** kcdx.dll's DllMain runs the registration pass + the WHGame force-load + Lua VM startup + before_game apply pass, all under loader lock. If this exceeds ~500ms the user notices launcher → main menu lag. Target: <200ms total. If we blow the budget, we cut work (e.g. defer some registration validation to the worker thread), not ship the slow version.

These measurements are recorded in `docs/performance.md` as they're taken. Authors building plugins know what the engine costs them; users debugging a slow launch can compare against the documented baseline. The harness itself is part of the test-suite infrastructure (Phase 2 or later) but doesn't block any phase from landing.

### What we DON'T do

- Add layers of indirection that "would be nice to have for flexibility" but don't ship a use case. Every layer is justified by a real user-visible benefit.
- Optimize speculatively. Until profiling shows a path is slow, the simple version stays.
- Skip the optimization when it's hard. Per the workspace priority rule: "X is hard, Y does 80%" is rejected. If a hot path is slow, we make it fast — not ship the slow version and call it good.

## Risk register

| Risk | Mitigation |
|---|---|
| Launcher injection blocked by Windows Defender | Fallback chain: CreateRemoteThread → WriteProcessMemory variant → manual mapped LoadLibrary stub. Each step logged. Final fallback if all fail: clear error message + docs/troubleshooting.md instructions. Dev-build the launcher against multiple AV setups (Defender on/off, common third-party) before any release. |
| Deferred apply means failures attribute to the wrong place | A `kcdx.hook(opts)` call queues a registration; if conflict_engine or apply later rejects it, the failure happens far from the source line that registered it. Mitigation: every registration stamps the call-site (Lua: `debug.getinfo(2)`; C++: caller info via macros) into the registration record. Any later failure logs back to that origin: "[plugin 'author.modname'] kcdx.hook 'my_hook' (registered at plugin.lua:42) failed at apply: <reason>." Apply-pass errors land in BOTH the engine log AND the owning plugin's log. |
| Conflict_engine pre-flight is O(N²) over entries today | Today's pairwise comparison is fine at 21 plugins but the wrong shape for TC-scale (thousands of entries). The restructure adopts a sweep-line algorithm: sort all entries by their footprint's `begin` address (O(N log N)), then walk in order maintaining a window of "still-open" footprints (those whose `end` > cursor). Each new entry compares only against entries in the window. Net complexity: O(N log N) + O(N × K) where K = max overlap depth (~1-2 in practice). At 10,000 entries, that's 130K comparisons instead of 100M. |
| Cross-engine state ownership (handles can outlive their plugin) | Handles carry an index + generation counter into the global vector. Removal nulls the slot. ApplyAll skips nulls. Document the rule: "registration once, no removal until process exit, BUT individual handles can be invalidated" — same as SKSE. |
| Lua VM crash takes down all Lua plugins | crash_guard already wraps Lua callbacks; failures log + continue. Per-plugin sandbox via separate Lua environments would limit blast radius further — design-gap to track for v0.2+. |
| FIX A symbol harvest stalls (currently 38% complete) | Phases 1-10 don't block on this. If FIX A harvest never completes, Phase 11 doesn't land but the rest of kcdx ships fine. Authors lose Lua-in-before_game (acceptable, falls back to DLL-in-before_game). FIX A is an independent track tracked separately. |
| LoadLibraryW(WHGame.dll) from kcdx.dll DllMain triggers unexpected loader-lock side effects | dumpbin /imports WHGame.dll first to identify its dependency chain. If WHGame.dll triggers cascade-loads of delay-bound DLLs that we don't expect, the LDR notification fires for each — our before_game patches/hooks apply to them too (intended). The cascade itself is single-threaded under loader lock; the risk is one specific cascaded DLL having a deadlock-prone DllMain. Mitigation: dev-mode logging captures every module the LDR notification fires for; if a problem DLL surfaces, blacklist it (skip applying our before_game entries to it) or ship a workaround. |
| WHGame.dll's static-linked Lua has been customized in ways FIX A's shim doesn't expose | FIX A's shim covers the standard ~110 Lua 5.1 entry points. CryEngine adds its own (LUA_NUMBER=float, custom panic handler) but those are configuration changes, not new symbols. If CryEngine added net-new functions to its Lua, FIX A's symbol set needs to grow. Mitigation: PROBE Q already monitors for unexpected Lua behaviors; new symbol gaps surface as crash sites for triage. |
| Test plugin migration regressions | Batches of ~5 per commit. Each batch verifies 21/21 before merging. Bisects cleanly if a regression slips. |
| Plugin authors confused by "old TOML schema doesn't work anymore" | Phase 5's parser drops + clear WARN ("kcdx 0.2+ uses manifest-only TOML; behavior moved to plugin.lua / kcdxPlugin_Load — see docs/migration.md"). docs/migration.md ships with Phase 5. |
| SQLite reference DB ships large (~12-18 MB compressed) | Acceptable given total kcdx release size; uncompressed ~50-70 MB sits on disk after install. Re-baseline every ~12 months (or per major game content patch) drops oldest-version rows. If size becomes a real concern 5+ years out, revisit format then. |
| Statement-content locator misses on a game update (`kcdx.locator.first_call_to("X")` no longer matches in the updated function) | Engine emits teaching error naming the function + locator that didn't resolve + suggesting `kcdx_find` to locate the new site. Per-plugin `on_changed` posture controls whether the entry skips or attempts anyway. Other entries continue. Failure is bounded to one entry, not the whole plugin. |
| Ghidra dump misclassifies a statement's `applicable_ops` (says `return_zero` applies when actually the statement is too short to fit the bytes) | Apply-pass failure surfaces as "engine catalog said this op applies to this statement but it actually didn't fit at apply time; this is a kcdx data bug, please file an issue with the function name and op." Treated as maintainer-side data quality issue, not author-side primitive failure. CI verification on dump refresh attempts each `applicable_op` against a synthetic binary so misclassifications surface before ship. |
| Author targets a function not in the SQLite (unknown canonical name, no Ghidra coverage of that module) | Registration-time teaching error: "no function 'X' in MODULE for KCD2 V; try `kcdx.find{name_contains=\"X\"}` to search, or `kcdx_dev_inspect` to verify the exact name." Author who genuinely needs the function unblocked via `kcdx.bytes{ pattern = ... }` expert hatch + can PR an Address Library row + a research-brief partition addition for the missing module. Plugin's other entries continue applying. |

## Critical files

### Created

- `src/loader/main.cpp` — launcher exe (Phase 1)
- `src/loader/inject.cpp` — CreateRemoteThread + fallback helpers (Phase 1)
- `src/lua_bind_hook.cpp`, `_bytes.cpp`, `_code.cpp`, `_on.cpp`, `_command.cpp`, `_cosave.cpp`, `_address.cpp`, `_scan.cpp` (Phase 2)
- `src/zone_gate.{h,cpp}` (Phase 2)
- `src/hook_interface.cpp`, `bytes_interface.cpp`, `code_interface.cpp` (Phase 3)
- `include/kcdx/Kcdx.h` — header-only C++ ergonomic wrapper (Phase 3)
- `engine/builtin/bugsplat-filename-fix/bugsplat-fix.cpp` (Phase 4) — the in-repo source that ships pre-compiled in the release zip under the same path
- `src/lua_bind_player.cpp`, `_world.cpp`, `_dialogue.cpp`, `_quest.cpp` (Phase 9 stubs + 3 real implementations)
- `src/asset_overlay.{h,cpp}` — asset overlay map + pak resolver hook (Phase 8.5)
- `src/lua_bind_assets.cpp` — `kcdx.assets.*` Lua surface (Phase 8.5)
- `src/lua_shim.cpp` (expansion to ~110 functions) + Address Library entries 1100-1199 (Phase 11a)
- `docs/migration.md` — author-facing migration guide (lands in Phase 1 with install-layout change; updated in Phase 5 with TOML-schema change)
- `docs/asset-replacement.md` — TC-facing asset overlay guide (Phase 8.5)
- `docs/performance.md` — measured benchmark numbers as they're taken (Phase 2+)
- `docs/lua/` (per-call files fronted by `docs/lua/index.md`) — **REQUIRED** author-facing Lua API reference (Phase 2). Every function in the `kcdx.*` namespace documented: signature, semantics, error modes, performance notes (hot-path warnings per Performance Discipline section), example. The Lua API is the primary author surface; without complete docs we fail priority #1 (UX) by construction.
- `docs/cpp/` (per-interface files fronted by `docs/cpp/index.md`) — **REQUIRED** author-facing C++ API reference (Phase 3). Same coverage as `docs/lua/` but for the C++ plugin path: how to use `Kcdx.h`, how each sub-interface works, what to do in `kcdxPlugin_Preload` vs `kcdxPlugin_Load`, performance notes mirror `docs/lua/`.
- `vendor/sqlite/sqlite3.{c,h}` — vendored SQLite amalgamation (Phase 9.1)
- `data/reference.sqlite` — build artifact; per-function/per-statement/per-behavior reference data (Phase 9.1; populated by parallel Ghidra research per [`parallel-ghidra-research.md`](../parallel-ghidra-research.md))
- `data/behavior-catalog/behaviors.toml` — human-authored named-behavior catalog imported into reference.sqlite at build time (Phase 9.5)
- `src/hashref.{cpp,h}` — `hash_at(function_name, game_version)` lookup primitive over SQLite (Phase 9.1)
- `src/version_check_cache.{cpp,h}` — per-plugin verification cache at `engine/cache/version_check.bin` (Phase 9.1)
- `src/lua_bind_statement.cpp` — `kcdx.statement.replace_with/insert_before/insert_after` (Phase 9.3)
- `src/lua_bind_locator.cpp` — `kcdx.locator.*` value namespace (Phase 9.3)
- `src/lua_bind_op.cpp` — `kcdx.op.*` static-bytes op value namespace (Phase 9.3)
- `src/lua_bind_functions.cpp` — `kcdx.functions.*` reference namespace (game-DLL eager population from SQLite; per-plugin population at load) + `kcdx.dll.declare` verb (Phase 9.3)
- `src/plugin_pdb.{cpp,h}` — PDB auto-load for plugin DLLs via `DbgHelp` (`SymLoadModuleEx`/`SymEnumSymbols`); populates internal-function addresses; graceful fallback on missing/mismatched PDB; links `dbghelp.lib` (Phase 9.3)
- `src/lua_bind_find.cpp` — `kcdx.find{...}` discovery surface (Phase 9.4)
- `src/lua_bind_behavior.cpp` — `kcdx.behavior.set/get/list/declare` named-behavior surface (Phase 9.5)
- `src/loader/init_plugin.cpp` — `kcdx --init-plugin <name>` scaffolder helper (Phase 1)
- `docs/lua/extensibility.md` — cross-plugin extension guide ("make your plugin extensible" + "extend another plugin"); leads with the disassembly-free paths (Phase 9.6)
- `data/statement-library/policy.md` — maintainer-facing hash mechanism + per-version row + contribution model (Phase 9.1)
- `data/behavior-catalog/policy.md` — maintainer-facing behavior-catalog contribution model (Phase 9.5)
- `docs/outstanding-work/parallel-ghidra-research.md` — the reference-data sourcing plan that populates `reference.sqlite`: mechanical batch extraction (statements + content hashes + caller↔callee call graph + content anchors) plus a sparse curated name overlay. Backs the console-driven `kcdx.find` discovery UX. (Rewritten 2026-05-26 after enumeration retired the original subagent-partition model — see that file's §10.)

### Modified

- [src/config.cpp](../../../src/config.cpp) — manifest-only LoadOneFile (Phase 5 deletes ~600 LOC; Phase 2 adds `[entrypoints]` parsing)
- [src/plugin_loader.h](../../../src/plugin_loader.h) — add `entrypoints.luaFiles` vector + new wave orchestration helpers
- [src/plugin_loader.cpp](../../../src/plugin_loader.cpp) — unified ordered list dispatch
- [src/lua_bind.cpp](../../../src/lua_bind.cpp) — wire new sub-binders; drop legacy uppercase `KCDX` table
- [src/scripting.cpp](../../../src/scripting.cpp) — extend dispatchers for `around` mode + `after`-return-mutation
- [src/lua_memory.cpp](../../../src/lua_memory.cpp) — named-arg lookup for hook callbacks
- [src/messaging.cpp](../../../src/messaging.cpp) — `<sender>:<event>` pub/sub
- [src/hooks.cpp](../../../src/hooks.cpp) — first-update-tick handler orchestrates AFTER_GAME PASS (unified ordered list)
- [src/load_order.cpp](../../../src/load_order.cpp) — `DeriveMinZone` reads manifest field instead of vector scan
- [src/ldr_notify.cpp](../../../src/ldr_notify.cpp) — handle kcdx-API-registered before_game entries from DLL Preload
- [src/console.cpp](../../../src/console.cpp) — wire built-in `kcdx_list_plugins` command (always-on; not test-suite-gated)
- [.claude/rules/pak-mods.md](../../../.claude/rules/pak-mods.md) — annotated as deprecated (Phase 8.5); points at docs/asset-replacement.md
- [src/paths.cpp](../../../src/paths.cpp) — find self by `kcdx.dll` (was `kcdx.asi`); engine-data root is `<game-bin>/engine/` (was `kcdx-engine/`); plugin scan root stays `<game-bin>/plugins/`; builtin discovery walks `<game-bin>/engine/builtin/`
- [include/kcdx/Interfaces.h](../../../include/kcdx/Interfaces.h) — add 3 new sub-interface structs
- [src/interfaces.cpp](../../../src/interfaces.cpp) — wire new interfaces into QueryInterface
- [CMakeLists.txt](../../../CMakeLists.txt) — `kcdx.asi` → `kcdx.dll`; new `kcdx-launcher` target (→ `kcdx.exe`); new source files
- [package-release.ps1](../../../package-release.ps1) — bundle launcher + DLL
- [README.md](../../../README.md) — install instructions + author guide
- [docs/loader-architecture.md](../../../docs/loader-architecture.md) — promote v0.2 layout to current
- [.claude/rules/toml-schema.md](../../../.claude/rules/toml-schema.md), [.claude/rules/hook-engine.md](../../../.claude/rules/hook-engine.md) — rewrite for new shape
- [.claude/rules/lua-api-surface.md](../../../.claude/rules/lua-api-surface.md) — rule 4 rewritten (required→positional + optional→trailing table); new rule 4a (discrete behavioral variants are sub-verbs, not table keys) (Phase 9.6, but the rule change ships in the doc-cycle that produced this restructure plan)
- [src/trampoline.cpp](../../../src/trampoline.cpp) — multi-region on-demand branch-pool expansion at TC scale (Phase 9.3)
- [src/lua_registry.h](../../../src/lua_registry.h) + [src/lua_registry.cpp](../../../src/lua_registry.cpp) — `requires_hash_check` flag, touched-functions list, per-entry `on_changed` posture (Phase 9.2)
- [src/lua_bind_hook.cpp](../../../src/lua_bind_hook.cpp) — completely rewritten for the sub-verb shape per Phase 9.3 (`kcdx.hook.before/after/around/replace/insert_before/insert_after`)
- [src/lua_bind_bytes.cpp](../../../src/lua_bind_bytes.cpp) — narrowed remit per Phase 9.6 (non-function bytes + labeled-expert pattern hatch)
- [data/seeds/policy.md](../../../data/seeds/policy.md) — per-version row model with hash-based auto-verification (Phase 9.1)
- [docs/migration.md](../../../docs/migration.md) — section on "what happens when KCD2 updates" (Phase 9.2)
- 21 test plugin folders — each gets new `kcdx.toml` + `plugin.lua` or updated `.cpp`

### Deleted

- `src/probes/createfilew_probe.{h,cpp}` (Phase 6)
- `src/probes/bugsplat_ctor_probe.{h,cpp}` — NOT deleted at Phase 6; KEEP as the Phase-11 before_game-hook install machinery ([before-game-hooks.md](../before-game-hooks.md) §5). It relocates/generalizes at Phase 11.
- The uppercase `KCDX.*` Lua compat table at [src/lua_bind.cpp:184-196](../../../src/lua_bind.cpp#L184-L196) (Phase 2)

## Verification plan

The 21/21 test-suite aggregator (`kcdx::test::EmitSummaryIfChanged`) is the canonical green-everywhere benchmark. Every phase lands with the suite passing.

### Per-phase manual checks

**Phase 1 (Launcher):** Build `kcdx.exe` + `kcdx.dll`. Install to game folder with no plugins. Run launcher → game reaches main menu. Check `engine/logs/kcdx_<ts>.log` for "kcdx.dll loaded" line + `engine/logs/kcdx-launcher_<ts>.log` for successful injection trace.

**Phase 2 (Lua API skeleton):** Run 21-plugin suite (still using TOML behavior, additive). Add ONE pilot pure-Lua plugin (`test-plugins/pilot-lua-bytes/`) with `kcdx.bytes(...)` for the outfit-swap site. Confirm it applies.

**Phase 3 (C++ DLL API):** Add ONE pilot DLL plugin using `kcdxHookInterface::Install`. Confirm it installs + fires.

**Phase 4 (Migration):** After each batch of ~5 plugin migrations, confirm "suite: X/21 passing" matches X before the batch.

**Phase 4 (bugsplat-fix):** Trigger AV via `kcdx_crash_now`. Check `%LOCALAPPDATA%/Temp/Kingdom Come - Deliverance II*.dmp` — non-zero file, sanitized filename. This is the empirical close of the original investigation.

**Phase 5 (parser deletion):** Re-add a deliberately-broken `[[patch]]` to a test plugin's `kcdx.toml`. Boot. Confirm the section is ignored + WARN line emitted. Suite remains 21/21.

**Phase 6 (probe cleanup, narrow subset):** Confirm no `createfilew_probe.cpp` in `src/probes/` and engine log shows no `PROBE R` lines. `bugsplat_ctor_probe.cpp` STAYS (KEEP for Phase 11 — [before-game-hooks.md](../before-game-hooks.md) §5); its `PROBE S/T` install path remains live (dev-gated). Confirm Phase5gReadback / `5g-readback` lines are gone from the dev log.

**Phase 7 (capability gating):** Hand-write a `kcdx.toml` with `[load_order].zone = "before_game"` for a plugin whose `plugin.lua` calls `kcdx.hook(...)`. Confirm the plugin is REJECTED at manifest validation (does not load) with the documented engine-log error message.

**Phase 8 (docs):** Word-search `kcdx.asi`, `Ultimate ASI Loader`, `dinput8`. Each hit removed or annotated.

**Phase 9 (high-level stubs):** Call `kcdx.player.health:get()` from a test plugin. Confirm stub returns nil + logs "not implemented". No crash.

### End-to-end manual checks (developer launch checklist)

- **CAP-01**: enter combat, try to swap outfit, confirm patched behavior.
- **CAP-12**: load save, quicksave, quit, relaunch, load — counter persists.
- **CAP-13**: bring up `~` console, type `kcdx_test_cap13 hello world`, see callback fire line.
- **Bugsplat fix**: trigger AV via `kcdx_crash_now`, verify `.dmp` lands on disk with non-colon filename.

These stay on the developer launch checklist; the dev-mode banner prompts for them.

### Snapshot commit precedes everything

Phase 0 IS the snapshot commit. `git tag v0.1-final` so we have a recovery point. No restructure work begins until that tag exists.

## Parallel work (runs alongside this plan)

- **FIX A symbol harvest** ([docs/outstanding-work/fix-a-drop-static-lua.md](../../../docs/outstanding-work/fix-a-drop-static-lua.md)) is actively in progress at `_research/phase8-fix-a/` (38% RVAs mapped at plan finalization). FIX A is independently tracked and ships independently; this restructure plan's Phase 11 simply consumes FIX A once it's done. Phases 1-10 can land without FIX A; Phase 11 is the next item after FIX A and Phases 1-10 are both complete.

- **Ghidra dump for the SQLite reference DB** ([docs/outstanding-work/parallel-ghidra-research.md](../parallel-ghidra-research.md)) — the per-function and per-statement metadata that populates `engine/hashes/reference.sqlite` for Phases 9.1+ comes from a parallel research effort. The orchestration brief in `parallel-ghidra-research.md` describes the deliverable shape, parallelization model (per-DLL × per-subsystem subagent partitions), quality gates, and the maintainer-side post-processing flow. Authorization-gated: the user dispatches an orchestrator agent that reads the brief, proposes partitions for sign-off, then runs subagents in parallel. Phase 9.1+ engine work can proceed in parallel using a hand-built `reference.sqlite` with a few dozen synthetic rows for testing while real data accumulates. The SQLite ships populated by the time Phase 9.3's surface lands.

## Out of scope (explicitly deferred to later versions)

Each item below names a capability the plan acknowledges as real and useful, but is NOT scoped for this restructure. Revisit triggers are concrete events that should prompt re-evaluation.

- **Launcher UI** (drag-drop load-order editor, plugin enable toggles). v0.3 work. The no-UI launcher exe is the v0.2 deliverable. Revisit trigger: when there's a UI-worthy modding ecosystem (>20 third-party plugins in active use), the UI earns its keep.
- **High-level gameplay API expansion beyond Phase 9's three.** Phase 9 ships `kcdx.player.health/.position` and `kcdx.inventory.add` real. The rest of `kcdx.player.*`, `kcdx.world.*`, `kcdx.dialogue.*`, `kcdx.quest.*` is stubbed. Real implementations land incrementally as TC authors request them and the underlying RE work resolves the offsets. Revisit trigger: any TC author files a feature request naming a specific capability.
- **Game-event API expansion** (design-gap #15 in `docs/design-gaps.md`). Lifecycle events ship with `kcdx.on`; gameplay events (player_damaged, dialogue_begin, etc.) come later as their hook sites are identified. Revisit trigger: a TC contributor identifies a new event with a clear hook site + use case.
- **Hot reload / dev iteration.** Editing `plugin.lua` and seeing the change live without relaunching the game. Genuinely valuable for TC authors but a v0.3+ effort — the deferred-apply + coroutine model makes naive hot reload hard (an apply pass has already run; new registrations would need their own apply cycle, and removed registrations need an "unapply" path that doesn't exist today). Revisit trigger: TC authors actively asking for it.
- **Telemetry / `kcdx.profile`.** Per-hook fire-count + per-callback wall-time instrumentation, so TC authors can answer "which of my 100 plugins is the hot-path culprit." Reasonable v0.3+ addition. Revisit trigger: a TC author files a "kcdx is slow but I don't know why" report.
- **Cosave evolution to structured/versioned schemas.** Today's key/value record store is mempatch-equivalent; TC saves with hundreds of plugins each writing dozens of fields would benefit from a typed schema + versioning + migration. Revisit trigger: a TC contributor's cosave hits >1 MB or hits a version-migration scenario.
- **Plugin uninstall + clean state migration.** Today's model assumes plugin installs are write-only (drop in a folder); uninstall = remove the folder. User toggle state (`enabled = false` in load_order.toml) persists across uninstall+reinstall by default — which might or might not be desired. Revisit trigger: user reports confusion about "I uninstalled and reinstalled and my toggle came back / didn't come back."
- **Mempatch compatibility.** Dropped. kcdx's `[[patch]]` schema was identical to mempatch's; that compatibility promise is broken. Mempatch users migrate one-time by writing the equivalent `kcdx.bytes(...)` call in a `plugin.lua`.
- **Statement-level modding of `unanalyzable` functions.** Functions Ghidra can't decompile cleanly have no enumerable statements, so `kcdx.statement.*` can't address them. Authors hit this via the labeled expert hatch: `kcdx.hook.*` against the function entry (which only needs the function name) or `kcdx.bytes` with the `pattern` locator against a known AOB. Revisit trigger: a TC author identifies an important function in the `unanalyzable` set that the curated `applicable_ops` mechanism would unblock if extended.
- **Transactional multi-statement modification.** Today's design applies entries independently — if a plugin wants both statement 3 and statement 7 modified atomically (either both apply or neither does), no engine-level support exists. If a TC author hits a real "I need atomic multi-statement" case, that's a Phase 10+ trigger. Tracked as `docs/outstanding-work/transactional-statement-modification.md` (stub to land when the feature is genuinely demanded).
- **Unexpected-AOB-match handling for `kcdx.bytes{ pattern = "..." }`.** Today's pattern resolution catches "0 matches" and "N matches" cases cleanly, but not "matched at unexpected RVA because the binary was reorganized" — that produces a wrong-but-applies result. By-definition risk of the expert-hatch pattern locator; mitigated by author switching to a named target (`target = "..."`) via an Address Library row. Tracked as `docs/known-issues/pattern-locator-unexpected-match.md` (stub).
- **Single-plugin hot reload (`kcdx_reload_plugin <name>`).** A console command that re-runs ONE plugin's `plugin.lua` against the current Lua state without restarting the game — for the author iteration loop. Wouldn't fully unhook (safely unhooking is its own engineering surface) but would let authors iterate on simple changes without 10-30 second restart cycles. Tracked as `docs/outstanding-work/single-plugin-hot-reload.md` with revisit trigger: when authors actively ask for it or kcdx ships beyond ~50 active plugin authors where the iteration friction is felt across the community.
