# Phase 11 Lua VM — plan spec (shared spec + coverage map)

The shared spec every step in this tree leans on. The settled design is
[`lua-vm-design.md`](lua-vm-design.md) (committed `990fb0e`) — this spec does not
restate it; it carries the cross-step invariants, the settled decisions verbatim
with their source, and the design→step **coverage map** that proves every design
element is accounted for (`.claude/rules/spec-conformance.md`).

## Goal

kcdx builds the ONE Lua VM itself on its worker thread (after the game maps WHGame —
NO force-load, design §6.2) via the FIX A symbol shim; the engine's
`CScriptSystem::Init` VM-creation is intercepted so it ADOPTS kcdx's state and never
creates its own. One compiled Lua body, one sentinel set — the dual-Lua sentinel
hazard dies by construction. Delivers the
`before_game` Lua zone, boot-asset Lua swaps (KI-0005), and served-`.lua` execute
(KI-0006).

## Settled decisions (verbatim, with source)

| Decision | Settled to | Source |
|---|---|---|
| VM ownership | kcdx builds the one state; the engine adopts it, never creates its own | user 2026-06-05 · design §1 |
| First step | a keystone PROBE, not a build — observe `CScriptSystem::Init` + `lua_newstate` live before wiring the interception | user 2026-06-05 · design §4 |
| VM-build mechanism | NO force-load (impossible — CREATE_SUSPENDED + loader lock); the worker waits for the game to map WHGame (`WaitForGameDll`) then builds the VM; the existing LDR before_game apply is confirmed, not rebuilt | PROBE P3 2026-06-05 · design §6.2/§6.4 |
| Intercept point | probe-decides; **lean: hook `lua_newstate` callee `0x14492A8`**, the engine runs its own `storedebug=0`/`openlibs`/extension-libs on our state; fall back to hooking `CScriptSystem::Init` only if the probe shows Init reads a virgin-state field | user lean 2026-06-05 · design §4.1 |
| Boot-asset Lua swap (KI-0005) | a REQUIRED first-class probe outcome — an early-slot `kcdx.assets.replace` must win the boot-asset open | user constraint 2026-06-05 · design §4.3, §7.1 |
| Early Lua slot shape | probe-gated ("probe all to see") — reuse `plugin.lua`-early (candidate A, the lean) vs a new `lua_before` entrypoint (candidate B); decision criterion pre-committed | user 2026-06-05 · design §4.4, §5 |
| AP14 warn fate | build-time decision against observed early-slot serve; lean narrow-to-late-slot | user 2026-06-05 · design §7.1 |
| KI-0006 serve-execute | confirm execute via the kcdx slot (not the crashing mod-init overlay); root-cause a residual crash only if it reproduces post-FIX-A | user 2026-06-05 · design §7.2 |

## Cross-step invariants

- **kcdx writes/builds/deploys; the user only launches** (`.claude/rules/agent-builds-and-deploys.md`). Every probe + test in this tree is agent-built, agent-deployed, agent-log-read; the user runs the game launch only.
- **PROBE Q stays silent** is the canonical dual-Lua-hazard observable through every phase. A PROBE-Q fire = a kcdx-image sentinel still in play = the static-Lua drop is incomplete.
- **One state** — `[L->l_G + 0xB0] == L` (the mainthread self-pointer) holds against the single VM; no second `lua_newstate` allocation.
- **Resolve() bails loud** — kcdx never touches the VM if a REQUIRED shim symbol fails to resolve (a known-stripped function with a stub is not a required miss).
- **Internal-only Lua fns stay internal** — `lua_close`/`lua_newstate`/`lua_setallocf`/`lua_atpanic` are resolvable by the shim but NEVER exposed through `kcdxLuaApi`.
- **GC-barrier safety** — any shim stub writing a GC pointer calls `luaC_barrierf` (`0x3997070`); the harvest doc's "What NOT to do" list is a hard constraint.
- **Stub layout-const validation** — stubs reading WHGame struct fields validate a known invariant at init (the mainthread self-pointer) so a future game-update struct shift fails loud, not silent.
- **Each step ships its permanent `test-plugins/` regression row** (`.claude/rules/test-suite.md`); build-green is necessary, not sufficient — the matrix is confirmed by the user's launch + the agent's `kcdx-dev.log` read.
- **Worker-startup budget** — the worker path (WaitForGameDll → refdb::Open → shim Resolve → VM build → intercept install) + the DllMain registration targets <200ms, hard cap <500ms; over budget = cut work, never ship slow. (No force-load — PROBE P3.)

## RE evidence

This tree rests on game-binary facts verified in the FIX A harvest. The canonical
source is [`../fix-a-drop-static-lua.md`](../../fix-a-drop-static-lua.md); seed rows
`data/seeds/address_names_seed.csv` (ids 114, 116, 117, 121 + the Lua-API rows at
low ids ~1–130, resolved by NAME not a baked id range — "1100-range" was the
pre-2026-05-28 numbering, renumbered in the three-file split) +
`address_versions_seed.csv`. The design §3 tabulates the load-bearing facts
(Init `@ 0x1448F38` sole-caller of `lua_newstate @ 0x14492A8`; the post-newstate
sequence; the layout constants). A step turning on such a fact cites it from there,
never a bare RVA.

## Coverage map — every design element → its step

`.claude/rules/spec-conformance.md`: every element the design specifies resolves to a
step (or an explicit user-decided deferral). No deferrals in this plan — every
element ships.

| Design element | Source § | Covered by | Notes |
|---|---|---|---|
| Keystone probe — intercept-point safety | §4.1 | P1 step 1 | settles the lean vs fallback |
| Single-VM validation (one state, PROBE Q silent) | §4.2 | P1 step 1 (probe) + P3 step 3 (build assertion) | probe observes, build asserts |
| Boot-asset swap reachability (KI-0005, required) | §4.3 | P1 step 1 | required outcome — gates P4 |
| Early-slot shape (probe all to see) | §4.4 | P1 step 1 | settles candidate A vs B |
| Symbol shim — forward 93 resolved fns | §6.1 | P2 step 1 | resolve each by canonical NAME (`refdb::ResolveAddrByName`), not a baked id range |
| Shim — stub ~24 inlined/stripped fns | §6.1 | P2 step 2 | layout-const based |
| Stub GC-barrier safety (`luaC_barrierf`) | §6.1 | P2 step 2 | hard constraint, per-stub test |
| Stub layout-const validation at init (mainthread self-pointer) | §6.1 | P2 step 2 | future game-update struct shift fails loud |
| `Resolve()` bail-loud + internal-only gating | §6.1 | P2 step 1 | |
| WHGame mapping pipeline (`WaitForGameDll` + LDR before_game apply) — NO force-load | §6.2 | P3 step 2 | PROBE P3: force-load impossible/unnecessary; the pipeline ALREADY EXISTS — confirmed, not built |
| VM build (on the worker) + `Init` interception (engine adopts) | §1, §4.1, §6.4 | P3 step 2 | consumes P1's intercept verdict; folds the old force-load step |
| Mandatory happens-before event gate (worker signals → boot-open path waits-and-blocks; NEW edge, no existing one covers it) | §5 | P4 step 1 (FOUNDATION) | PROBE P11 v2 found the dependency cross-thread + ungated — the gate is the fix, not timing |
| `RegisterRuntimeOverlay` two-writer CAS (worker writer + game-main writer) | §5 | P4 step 1 (FOUNDATION) | race-safety infra reused by the P5 early surface |
| Order-inversion regression (FAILS if boot open resolves before the slot signaled) | §5 | P4 step 1 (cap-82) | the falsifiable proof the gate holds |
| Cross-thread VM-adoption publish (release/acquire; worker builds, game thread adopts) | §5, §6.4 | P3 step 2 | the adoption handoff is published, never timed (PROBE P3: worker-build → game-thread-adopt) |
| Early Lua slot + boot-asset Lua swap delivered (KI-0005) | §7.1 | **P5 (startup contract)** — see `phase-05-startup-sequence-contract/` coverage below | the early-slot SHAPE moved from P4 to the startup-contract phase |
| AP14 warn build-time decision | §7.1 | **P5** (startup contract) | narrow/remove per observed serve |
| Drop static Lua + FIX C revert + `kcdxLuaApi`→shim | §6.3 | P6 step 1 | hazard-killing step |
| PROBE Q stays as permanent canary | §6.3 | P6 step 1 | unchanged, carried |
| Served-`.lua` execute confirm (KI-0006) | §7.2 | P7 step 1 | via kcdx slot, not mod-init |
| KI-0006 residual-crash root-cause if reproduces | §7.2 | P7 step 1 | honest scope, not guaranteed fix |
| `early_hook.{h,cpp}` relocate/generalize | §8 | P3 step 1 | from `src/probes/bugsplat_ctor_probe` |
| `before_game`-zone Lua capability (lift zone gate) | §1, §6.3 | P6 step 1 | the zone-gate's synthetic restriction lifts |
| Per-step permanent test-plugins/ rows | §9 | every step's Test bar | suite grows per step |

## Phase 5 — the startup-sequence author contract: coverage map

Design source: [`phase-05-startup-sequence-contract/bring-forward-design.md`](phase-05-startup-sequence-contract/bring-forward-design.md) (v2, committed `ee9c744`, design-fidelity gated PROCEED). Steps live in [`phase-05-startup-sequence-contract/`](phase-05-startup-sequence-contract/README.md).

| Design element | Source § | Covered by | Notes |
|---|---|---|---|
| Worker GC-safety of each early subsystem bind (probe) | §8.2 | P5 step 1 | PROBE INITORDER proved the ordering; this confirms the bind is GC-safe + PROBE-Q-silent |
| console::Init + cvar::Init move to the worker | §3, §9 | P5 step 2 | PROBE INITORDER: gEnv->pConsole non-null at the worker point |
| New ctx-B phase: kcdx-subsystems-ready | §3, §4, §9 | P5 step 2 | the ordered-init signal; precedes the boot open |
| Phase model promoted to author-facing (init_phase.h tokens) | §1, §4, §9 | P5 step 2 (phase added) + step 3 (events) + step 4 (query) | one timeline = control + visibility + docs |
| Lifecycle event per author-reachable phase (kcdx.on / RegisterListener, parity) | §5.1, §9 | P5 step 3 | fired on the existing bus at each AdvanceTo |
| Existing messages reconciled as timeline points | §5.1, §8.5 | P5 step 3 | PostLoad/PostPostLoad/InputLoaded/LuaReady; build-time read of the firing sites |
| Late-subscribe-fires-immediately | §5.1, US-1 | P5 step 3 | the on-ready discipline generalized |
| New phase `kcdxMessageType` values (append-only) | §9 | P5 step 3 | AP11-safe additions at the END |
| `kcdx.startup.phase()` + `.at_least()` (Lua) + C++ accessor (query API, parity) | §5.2, US-2, §9 | P5 step 4 | thin read over g_phase + Name() |
| ctx-A shown-not-subscribable; internal-plumbing phases internal | §4, §12 | OUT-OF-SCOPE (user-decided) | shown on the timeline doc as internal markers, no event |
| New ctx-B phase: before_game early-slot | §4, §7, §9 | P5 step 5 | where lua_before / the C++ entry runs |
| `lua_before` `[entrypoints]` key + `luaBeforeEntrypointsRel` | §7.1, §9 | P5 step 5 | mirrors `lua_after` |
| The worker before-game runner (runs lua_before + C++ entry; signals the gate) | §7.1, §7.4, §9 | P5 step 5 | the new coordinator unit |
| Declarative / needs-only-kcdx early-bind surface on the worker | §7.3, §7.4 | P5 step 5 | needs-only-kcdx vs needs-the-live-game |
| US-7 out-of-window call fails loud (teaching error) | §6 US-7, §10 | P5 step 5 | AP14 — never a silent no-op |
| The before_game apply-driver (`ApplyZone(BeforeGame)`) — queued hook/bytes ACTUALLY INSTALLS early | §7.5, §6 US-8, §11 | P5 step 6 | the before_game invocation of the ONE driver; closes init.md's STUBBED before_game apply path (migration step 3); the "full CONTROL" half |
| US-8 — a before_game hook declared in the slot installs + fires before the init call | §6 US-8 | P5 step 6 | FAILS if the queue was never drained (the apply-driver, not just the slot) |
| before_game apply-driver target-ordering (engine calls the target after the apply point) | §8.7 | P5 step 6 (probe at step head) | UNVERIFIED per-target; LDR-time targets → self-registration hatch §7.2 |
| Boot-asset serve via the early slot (KI-0005) + AP14 warn | §6 US-5, §11 | P5 step 7 | early-slot replace wins boot open; user sees the swap |
| The kcdx-driven C++ before-game entry export | §7.1, US-4, §9 | P5 step 8 | new export; name resolved at step head (§8.3 probe) |
| C++ export-name determination (new export vs reuse Preload) | §8.3 | P5 step 8 | build-time read of Preload's fire timing |
| The author startup-sequence doc (the timeline) | §5.3, §11 | P5 step 9 | + cross-link docs/init.md |
| Per-call doc entries + Lua/C++ parity + tests per surface | §10 | each P5 surface step's deliverable | docs-discipline: docs move with the surface |
| Self-registration expert hatch (US-6) | §7.2 | NOT a step — retained from before-game-hooks.md §5/§6 | BugSplat builtin rides as a consumer |
| Event gate + RegisterRuntimeOverlay CAS | §7.4, §9 | P4 foundation (reused) | not a P5 step — built in Phase 4 |
| Boot-cvar read-in-window; phase-token reconciliation; gate timeout | §8.4-6 | build-time determinations within P5 steps 3/5/7 | provisional per §8 |

## Consumers riding this phase (out of scope here — see design §10)

- The **bugsplat builtin filename fix** is a CONSUMER (`before-game-hooks.md` §6), lands once before_game hooks work; not a step in this tree.
- **Full plugin migration to Lua-`before_game`** beyond the test vehicle — author-driven adoption, not a plan step.
