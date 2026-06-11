# Phase 9.5 P1 step 1 — startup-ordering recon (PROBE 9.5-S1)

Discharges behavior-design.md's marked assumption clauses (§5/§6/§7/§8/§9).
Artifact kind: durable process-output. Trust: agent-authored recon grounded in
code reads cited at the operative file:line (primary evidence = the cited
source); the live-clause answers are in §5 (run of 2026-06-11) — the outcome
map (§3) was written BEFORE the run per `.claude/rules/results-driven.md`.
Probe marker in source: `// === DIAGNOSTIC (PROBE 9.5-S1) ===`, log category
`PROBE_95S1`, every line carries `tid`. Probe is removed (capture-then-remove)
before the step commits.

Date: 2026-06-11. Game version context: release_1_5_1164953_841.
NOTE: file:line cites below are PRE-probe-insertion line numbers.

---

## 1. The startup timeline as built (static ground truth)

1. **ctx-A (DllMain, loader lock)** — `RunBeforeGameZoneInDllMain`
   (`src/dllmain.cpp:404-468`): `config::LoadAllConfigs` at
   `src/dllmain.cpp:424` → `load_order::Resolve()` inside it at
   `src/config.cpp:1254`. The unified order is final before any plugin code
   runs (confirms design §6's premise). Worker spawned at `src/dllmain.cpp:481`.
2. **ctx-B (worker)** — version detect → refdb::Open → hooks install →
   ModManager ctor bracket → `plugins::DiscoverAndLoad` at
   `src/dllmain.cpp:303` (body `src/plugin_loader.cpp:591-891`): Preload wave
   (dispatch `:770`), Load wave (dispatch `:830`), then
   `kcdxMessage_PostLoad` / `PostPostLoad` fire ON THE WORKER (`:887,:890`).
3. **Game-main first update tick** — `HookedUpdate` one-shot block
   (`src/hooks.cpp:355-601`): `RegisterKcdxTable` → `hook_chain::SetLuaState`
   (`:374-376`) → `lua_plugin_loader::RunAll(L)` (`:386`; sequential, priority
   asc — `src/lua_plugin_loader.cpp:323-414`, comparator `:179-192`) →
   console/cvar init → **ApplyZone(AfterGame) pass 1** (`src/hooks.cpp:546`) →
   `RunAfterEntrypoints` (`:558`) → **ApplyZone pass 2** (`:567`) →
   `plugins::RunPostGameLoad` (`:582`; two sequential passes vs Lua — §2e) →
   `kcdxMessage_InputLoaded` (`:589`).
4. **Per tick thereafter** — `ApplyZone(AfterGame)` steady-state drain
   (`src/hooks.cpp:614`) + `task::DrainQueue` (`:618`).

---

## 2. Static findings

### 2a. Marshal pump — reuse fit YES; boundedness: **UNBOUNDED** (design §5.4 / §8)

- The pump named by `lua-callback-threading.md` is `kcdx::task` —
  `src/task.cpp:16`: `std::vector<kcdxTask*> g_pending`, mutex-guarded;
  `AddTask` callable from any thread (`:18-25`); `DrainQueue` snapshot-swaps
  and runs on the game main thread once per tick (`src/hooks.cpp:618`),
  SEH-guarded per task (`src/task.cpp:49-57`).
- **Reuse fit for queued toggles: GOOD.** FIFO arrival order preserved (vector
  push order; snapshot preserves it), each task runs exactly once as its own
  unit (no coalescing), a re-entrant AddTask from inside Run defers to the
  next tick (`src/task.cpp:38-46`) — matches §5.4's queue contract verbatim.
- **Boundedness: NONE.** No cap, no high-water check, no rejection path — an
  off-thread flood grows the vector without bound. **Implication for §5.4:**
  the "is bounded" half of the marked assumption is DISPROVEN; per the
  design's own clause the overflow disposition is a user decision — surfaced,
  not defaulted.

### 2b. C++→Lua call machinery — reuse fit for `Invoke` is HALF (design §8)

- The existing C++→Lua invocation path is the hook-chain dispatch: resolve
  chain under lock, release before Lua (`src/hook_chain.cpp:968-973`), push
  args, `lua_pcall(L, nargs, LUA_MULTRET, 0)` at `src/hook_chain.cpp:1069`,
  error logged attributed to the owning plugin (`:1070-1073`), per-dispatch
  string-lifetime arena cleared at end of each top-level dispatch (`:628-636`),
  dispatch main-thread-only behind the off-thread marshal gate
  (`lua-callback-threading.md`).
- **Reusable half:** the pcall harness — ref-invoke, error capture +
  per-plugin attribution, string-lifetime arena, main-thread-only discipline.
- **NOT directly reusable:** the argument marshal. `PushParamsPositional`
  (`src/hook_chain.cpp:1066`) is typed by a hook's `runtime_func_t` parameter
  list (the verified ABI signature), not arbitrary Lua values. `Invoke` needs
  its own value-push path (scalars/strings/table-handles) around the reused
  harness.
- **Implication for §8:** "riding the existing callback-dispatch path" holds
  for the call/error/threading machinery; the arg-marshal layer is new code.

### 2c. Per-version verification call site (design §9) — CONFIRMED call-time

- refdb caches the RUNNING build's `game_versions` row once at `refdb::Open()`
  (`src/refdb.cpp:1880`, reading `kcdx::plugins::g_runtimeGameVersionString`;
  missing tag → loud `no_game_version_row`, `:1894`), and per-resolve serves
  only rows whose version interval covers the running build (interval logic
  region `src/refdb.cpp:984+`).
- The consuming resolution fires at the **call sites a hash-checked verb
  reaches**, never at manifest load:
  - Lua `kcdx.hook` `target = "<name>"`: verified-signature resolve at BIND
    time — when the calling script line executes (`src/lua_bind_hook.cpp:676`,
    declared-check `:731`); address resolve at APPLY time inside the chain
    install (`src/hook_chain.cpp:1619`).
  - C++ `kcdxHookInterface`: `ResolveSignatureByName` / `ResolveByName` at
    interface-call time (`src/hook_interface.cpp:292,307,378`).
- **Implication for §9:** CONFIRMED — a behavior declarer's implementation
  running at the apply boundary hits per-version verification exactly when its
  hash-checked calls execute (bind/apply call-time); no load-time manifest
  rejection point exists, matching §9's "load-time determination is
  structurally impossible and is not claimed."

### 2d. Builtin pin-ahead for the catalog pack (design §7) — zone/priority alone does NOT pin

- Today's machinery: discovery walks `kcdx-engine/builtin/` first
  (`src/config.cpp:1157-1169`); an engine builtin with no explicit
  `default_position` defaults to zone `before_game`
  (`src/load_order.cpp:206-230`); priority stays the author hint
  (`defaultPriority`, default 50 — `src/plugin_loader.h:133`).
- But **zone does not order the Lua main wave**: `RunAll` sorts by
  (priority asc, orderIndex, name) ONLY (`src/lua_plugin_loader.cpp:179-192,
  345-360`), and the `before_game` ApplyZone slice has NO call site today
  (P11-P5 `bring-forward-design.md` §7.5). A builtin at priority 50 ties with
  user plugins and falls to the name tiebreak.
- **Implication for §7's marked assumption:** "the builtin zone/priority
  machinery can pin the pack ahead of all user plugins" is DISPROVEN as
  stated. The ordering guarantee must come from the catalog-aware loader's
  CALL SITE: the engine runs the catalog files itself before
  `lua_plugin_loader::RunAll` (the first-tick block `src/hooks.cpp:374-386`
  offers that slot today; a worker-side post-VM-publish slot exists under
  P11). Consistent with §7's settled engine-identity loader route (a
  manifest-fronted pack is structurally blocked — `src/config.cpp:373-380`);
  the finding refines the clause: the guarantee is loader placement, not
  zone/priority.

### 2e. P11-P5 early-stop interleaving (design §6 second assumes-half) — **PROVISIONAL**

- The P5 tree does NOT settle C++-vs-`lua_before` interleaving at the early
  stop: `bring-forward-design.md` §7.1 names the two early entries (both run
  by the worker before-game runner — step-5 + step-8 docs), but neither §7 nor
  the step docs states one merged unified-order walk vs two sequential passes.
- The analogous SHIPPED stop is explicitly **two sequential passes, not
  interleaved**: all `lua_after` by priority, then all
  `kcdxPlugin_PostGameLoad` by priority — same-priority Lua/C++ pairs do not
  interleave (`src/hooks.cpp:574-578` comment; independent sort in
  `RunPostGameLoad`, `src/plugin_loader.cpp:938-966`).
- **Provisional finding:** §6's interleaved-early-stop assumption is NOT
  confirmed by the P5 tree; the precedent points to two passes. PROVISIONAL
  until P11-P5's `lua_before` lands (user-decided shape, 2026-06-11) — the
  live confirmation rides that trigger with the deferred Lua fixture leg.
  Until then §6's window-law reasoning should not rest on intra-stop
  interleaving.

### 2f. Static pre-reads informing the live clauses

- **Apply-boundary drains (clauses 1/2):** a registration queued immediately
  after `RunAll` returns (`src/hooks.cpp:386`) has TWO pre-InputLoaded drains
  ahead (`:546`, `:567`). A registration queued after `RunPostGameLoad`
  returns (`:583`) and before InputLoaded (`:589`) has NO drain until the
  per-tick ApplyZone at `:614` — the END of the SAME HookedUpdate call, AFTER
  InputLoaded fired. Statically: boundary-queued registrations always land in
  a drain eventually (ApplyZone is idempotent + per-tick), but pre-ready
  coverage depends on which side of `:567` the boundary sits.
- **Anonymous registrations** (the probe's, or any engine-issued one) default
  to after_game / priority 50 (`src/lua_registry.cpp:450-469`); Append logs a
  `queued kind=... name=...` Info line (`src/lua_registry.cpp:342`); an apply
  failure logs `entry '<name>' ... failed at apply: <reason>` at Error
  (`:558`); ApplyZone logs a transition summary only when count > 0 (`:572`).
- **C++ main-stop positions (clause 3):** statically `kcdxPlugin_PostGameLoad`
  dispatches inside the first-tick block after the Lua waves and both
  ApplyZone passes, before InputLoaded (`src/hooks.cpp:582`); PostLoad /
  PostPostLoad subscribers fire much earlier, ON THE WORKER, at
  `DiscoverAndLoad` end (`src/plugin_loader.cpp:887,890`). Tids confirm live.
- **VM-adoption point (clause 4):** kcdx builds the ONE state on the worker
  via the shim's `lua_newstate` (`src/lua_vm_build.cpp:148`), publishes it
  (`:187`), installs the engine-stamped `lua_newstate` Replace intercept
  (`:212-243`); the engine ADOPTS at `CScriptSystem::Init`
  (`Intercept_lua_newstate`, `src/lua_vm_build.cpp:65-97`) on the game's init
  thread, racing the worker's `DiscoverAndLoad` (~2 s on a populated tree —
  `src/dllmain.cpp:262-271`). The adoption-vs-Load-wave ordering is a genuine
  race statically (the ctor bracket waits on `g_kcdxReadyEvent`; the
  lua_newstate call is not shown to be gated by it) — exactly why §8's
  exclusive-VM-access clause needs the live tid/order observation.

---

## 3. Outcome→meaning map (written BEFORE the run — flat, no expected outcome)

Read recipe after the user's launch: newest
`<game-bin>/kcdx-engine/logs/kcdx-dev_<ts>.log`, grep `PROBE_95S1` +
`kcdx_probe_95s1` + `lua_registry:`; order-of-appearance + the `tid` KVs are
the data.

### Clause 1+2 — apply-boundary point + registrations queued there

Probe: registration A (`kcdx_probe_95s1_a` — `kcdx.bytes` flat form on engine
seed `kcdx.lua_newstate`, `original = replacement = "CC CC CC CC"`) queued
immediately after `RunAll` returns; registration B (`kcdx_probe_95s1_b`, same
shape) queued immediately after `RunPostGameLoad` returns, before InputLoaded.
Both are built to FAIL byte-verify at apply (a live function entry under an
engine Replace detour cannot read CC*4); nothing installs and no byte is
written in any outcome.

- A's `failed at apply` line appears BEFORE the `post_game_load_returned`
  probe line → the `:546`/`:567` pre-InputLoaded drains pick up post-Lua-wave
  registrations → a §5 boundary placed after the Lua wave but before
  RunPostGameLoad gets in-block ApplyZone coverage for registrations its
  implementations make → §5's second assumes-half holds at that placement.
- A's `failed at apply` line appears only AFTER the `input_loaded_firing`
  probe line → even post-RunAll registrations miss the in-block drains →
  a boundary implementation's registrations land only post-InputLoaded →
  §5's assumes-half DISPROVEN for in-block coverage; the design needs an
  explicit drain after the boundary.
- B's `failed at apply` line appears BEFORE `input_loaded_firing` → an
  un-read drain exists between `:583` and `:589` (contradicts the static
  read) → re-read the block; the static drain map is wrong somewhere.
- B's `failed at apply` line appears AFTER `input_loaded_firing` (same tick
  or later) → matches the static read: a boundary at the strict
  post-RunPostGameLoad/pre-InputLoaded point gets NO pre-ready drain → if the
  §5 boundary sits there, an added ApplyZone call (or moving the boundary
  before `:567`) is required for boundary-queued registrations to land
  pre-ready.
- Either entry shows NO `queued` line → its bind was rejected (the engine-seed
  name did not resolve from the anonymous context) → probe inconclusive for
  that entry; rework with a different resolvable target. No conclusion drawn.
- Either entry shows a `queued` line but NEVER a `failed`/Applied line in the
  session → the entry was never drained → DISPROVES "registrations queued at
  the boundary still land in an ApplyZone drain" outright.
- Either entry reports **Applied** → the byte-verify unexpectedly matched
  (memory unchanged by construction — original-identical bytes) → the
  mismatch premise was wrong; the drain-timing data is still valid (use the
  Applied line's position the same way); record the verify behavior for
  rework.

### Clause 3 — C++ main-stop positions vs the Lua wave

Probe lines: per-plugin `cpp_preload_dispatch` / `cpp_load_dispatch` /
`cpp_post_game_load_dispatch` (plugin + tid), `runall_begin` / `runall_end`,
`post_game_load_returned`, `input_loaded_firing`, all with tid.

- All `cpp_load_dispatch` lines appear before `runall_begin` AND carry the
  worker tid (≠ `runall_begin`'s tid) → C++ `kcdxPlugin_Load` is an EARLY
  stop relative to the Lua main wave → §6's window law must treat a C++
  Load-time `set` against a plugin-tier behavior as out-of-window (the wall
  is real in the shipped binary).
- Any `cpp_load_dispatch` appears AFTER `runall_begin` or on the Lua wave's
  tid → the waves overlap or share a thread → §6's early-vs-main wall as
  drawn is wrong; re-derive the stop set from the observed order.
- All `cpp_post_game_load_dispatch` lines appear after `runall_end` and
  before `input_loaded_firing` on the game-main tid → `PostGameLoad` is a
  MAIN-stop surface after the Lua wave, pre-ready → a PostGameLoad `set`
  against a plugin-tier behavior resolves, and the post-PostGameLoad boundary
  candidate sits after BOTH main-stop surfaces — §6's placement assumption
  holds.
- Any `cpp_post_game_load_dispatch` appears after `input_loaded_firing` or on
  a non-main tid → the §5 boundary-candidate ordering is wrong as designed →
  the boundary must move or the design's stop map revises.

### Clause 4 — VM-access window during the worker wave

Probe lines: `discover_and_load_begin` / `_end` (worker tid),
`vm_adoption_intercept` (tid of the engine's CScriptSystem::Init caller).

- `vm_adoption_intercept` appears AFTER `discover_and_load_end` → the engine
  adopts only after the whole C++ wave finished → the worker wave has
  exclusive VM access for its full duration → §8's load-wave-query clause is
  safe as designed.
- `vm_adoption_intercept` appears BETWEEN begin and end (different tid) → the
  engine adopts and may run Lua CONCURRENTLY with the tail of the Load wave →
  §8's exclusive-access assumption DISPROVEN; load-wave queries need a gate
  (the g_kcdxReadyEvent pattern) or the window narrows to pre-adoption — a
  design revision via its ceremony.
- `vm_adoption_intercept` appears BEFORE `discover_and_load_begin` → adoption
  precedes the wave entirely → every load-wave query runs against an
  already-engine-shared VM → same disproof, stronger: no exclusive window
  exists.
- `vm_adoption_intercept` never appears → the intercept did not fire this
  session → the P11-P3 adoption mechanism itself needs re-observation before
  any §8 conclusion; check the cap-81 rows.

(Confound logged: every line carries tid, so same-thread serialization is
distinguishable from true cross-thread overlap in every outcome.)

---

## 4. Probe wiring (the reusable recipe — capture survives probe removal)

- One-shot/per-plugin timeline lines, all `LOG_DEBUG_KV("PROBE_95S1", ...)`
  with `log::KV("tid", GetCurrentThreadId())`, each block marker-headed
  `// === DIAGNOSTIC (PROBE 9.5-S1) ===`:
  - `src/plugin_loader.cpp` — `discover_and_load_begin`/`_end` at
    `DiscoverAndLoad` entry/exit; per-plugin `cpp_preload_dispatch` /
    `cpp_load_dispatch` / `cpp_post_game_load_dispatch` immediately before
    each `guard::Call` dispatch.
  - `src/lua_plugin_loader.cpp` — `runall_begin` after the latch+null checks,
    `runall_end` before return (+ a marked probe-only `<windows.h>` include).
  - `src/hooks.cpp` — `runall_returned` + registration A after `RunAll(L)`;
    `post_game_load_returned` + registration B after `RunPostGameLoad`;
    `input_loaded_firing` before `FireEngineMessage(kcdxMessage_InputLoaded)`.
  - `src/lua_vm_build.cpp` — `vm_adoption_intercept` inside
    `Intercept_lua_newstate`'s success path (+ a marked probe-only
    `<windows.h>` include).
- Test registrations: `luaL_loadstring` + `lua_pcall` of a `kcdx.bytes{...}`
  flat-table call (anonymous context → after_game/priority 50), target
  `kcdx.lua_newstate`, `original == replacement == "CC CC CC CC"` — fails
  byte-verify at apply by construction; zero writes in every outcome.
- No line sits on a per-frame path (`DrainQueue` and the per-tick ApplyZone
  untouched); every added line is one-shot or per-plugin boot code.

---

## 5. Live results (2026-06-11 run)

Log: `kcdx-dev_2026-06-11_10-55-20.log`. Each result names the §3 outcome-map
branch that fired.

- **F1 — registration A drained pre-InputLoaded.** A's `failed at apply` line
  appeared BEFORE `post_game_load_returned` → §3 clause-1+2 branch 1 fired:
  the in-block `:546`/`:567` ApplyZone drains pick up post-Lua-wave
  registrations; a §5 boundary placed after the Lua wave but before
  RunPostGameLoad gets in-block coverage.
- **F2 — registration B drained only post-InputLoaded.** B's `failed at apply`
  line appeared only AFTER `input_loaded_firing` → §3 clause-1+2 branch 4
  fired (matches the static read): a registration queued at the design's
  boundary candidate (post-RunPostGameLoad, pre-InputLoaded) gets NO pre-ready
  drain — the boundary pass must trigger its OWN ApplyZone drain.
- **F3 — C++ Load is an EARLY stop, cross-thread.** All ~40
  `cpp_load_dispatch` lines carried tid=45196 (worker) and appeared strictly
  before `runall_begin` (tid=3412) → §3 clause-3 branch 1 fired: the C++ Load
  wave precedes the Lua main wave on a different thread; design §6's
  early-vs-main wall is real in the shipped binary.
- **F4 — PostGameLoad is a MAIN-stop surface, pre-ready.**
  `cpp_post_game_load_dispatch` lines on tid=3412 after `runall_end` and
  before `input_loaded_firing` → §3 clause-3 branch 3 fired: PostGameLoad runs
  on game-main, post-Lua-wave, pre-InputLoaded; the §5 boundary candidate sits
  after BOTH main-stop surfaces.
- **F5 — adoption followed the wave, by margin not by gate.**
  `vm_adoption_intercept` (tid=3412) at 10:55:29.564, AFTER
  `discover_and_load_end` (10:55:23.941) → §3 clause-4 branch 1 fired: the
  ordering HELD this run — but by a ~5.6 s wall-clock margin, UNGATED (static
  read confirms no gate exists). The exclusive window is observed, not
  guaranteed; disposition in §6.

---

## 6. Dispositions (user rulings 2026-06-11 + mechanical refinements)

- **R1 — the pump stays UNBOUNDED.** The engine never rations capable
  authors: no cap, no rejection, no coalescing. A runaway is the author's
  bug; the engine makes it DIAGNOSABLE — a high-water teaching warn at the
  SHARED pump (`kcdx::task` enqueue): one attributed log line when depth
  crosses a generous threshold, naming the producing plugin + depth (e.g.
  "check for a runaway loop"); one integer comparison at enqueue; covers ALL
  producers (behavior commands, hook marshaling, plugin AddTask). No TD
  entry — the warn is the disposition.
- **R2 — ADD THE GATE on VM adoption.** The loader signals C++-wave end
  (`DiscoverAndLoad` end); the engine's VM-adoption intercept
  (`Intercept_lua_newstate`) WAITS on that signal. One-shot, boot-only,
  never a hot path; observed margin ~5.6 s → typical wait zero. Load-wave
  queries stay legal under the gate's guarantee. An order-inversion
  regression is owed when the gate builds (P2 step 1).
- **Boundary drains itself (from F2).** The §5 boundary pass triggers its
  own ApplyZone drain; a registration queued there otherwise drains only
  post-InputLoaded.
- **§7 pin = loader call-site placement (from §2d).** Zone/priority is
  DISPROVEN as the catalog pin; the engine runs the catalog files before
  `lua_plugin_loader::RunAll`.
- **§8 Invoke = half-reuse (from §2b).** The pcall harness reuses; the
  argument-marshal layer is new code.
- **§9 call-time enforcement OBSERVED (from §2c).** Per-version verification
  fires at the bind/apply call sites; no load-time manifest rejection point.
- **P5 early-stop interleave stays PROVISIONAL (from §2e).** The P5 tree is
  silent; shipped precedent is two sequential passes. Until `lua_before`
  lands, §6 does not rest on intra-stop interleaving.
