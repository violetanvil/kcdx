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
| Early Lua slot + the ordering-guard EVENT GATE | §5 | P4 step 1 | consumes P1's slot-shape verdict; builds the slot |
| Mandatory happens-before event gate (worker signals → boot-open path waits-and-blocks; NEW edge, no existing one covers it) | §5 | P4 step 1 | PROBE P11 v2 found the dependency cross-thread + ungated — the gate is the fix, not timing |
| Order-inversion regression (FAILS if boot open resolves before the slot signaled) | §5 | P4 step 1 + step 2 | the falsifiable proof the gate holds |
| Cross-thread VM-adoption publish (release/acquire; worker builds, game thread adopts) | §5, §6.4 | P3 step 2 | the adoption handoff is published, never timed (PROBE P3: worker-build → game-thread-adopt) |
| Boot-asset Lua swap delivered (KI-0005) | §7.1 | P4 step 2 | early-slot replace wins boot open |
| AP14 warn build-time decision | §7.1 | P4 step 2 | narrow/remove per observed serve |
| Drop static Lua + FIX C revert + `kcdxLuaApi`→shim | §6.3 | P5 step 1 | hazard-killing step |
| PROBE Q stays as permanent canary | §6.3 | P5 step 1 | unchanged, carried |
| Served-`.lua` execute confirm (KI-0006) | §7.2 | P6 step 1 | via kcdx slot, not mod-init |
| KI-0006 residual-crash root-cause if reproduces | §7.2 | P6 step 1 | honest scope, not guaranteed fix |
| `early_hook.{h,cpp}` relocate/generalize | §8 | P3 step 1 | from `src/probes/bugsplat_ctor_probe` |
| `before_game`-zone Lua capability (lift zone gate) | §1, §6.3 | P5 step 1 | the zone-gate's synthetic restriction lifts |
| Per-step permanent test-plugins/ rows | §9 | every step's Test bar | suite grows per step |

## Consumers riding this phase (out of scope here — see design §10)

- The **bugsplat builtin filename fix** is a CONSUMER (`before-game-hooks.md` §6), lands once before_game hooks work; not a step in this tree.
- **Full plugin migration to Lua-`before_game`** beyond the test vehicle — author-driven adoption, not a plan step.
