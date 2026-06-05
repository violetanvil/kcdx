# Phase 11 Lua VM — kcdx owns the one compiled Lua body

**Status:** v1 (settled 2026-06-05)
**Owns:** the Phase 11 Lua-VM mechanism for the kcdx restructure. Supersedes the
mechanism prose in [`00-original-plan.md`](../00-original-plan.md) §"Phase 11" and
the per-step stubs in this subdir — those become the build-grain decomposition of
THIS design (consumed by `/plan` → `/execute`).
**Consumes:** [`fix-a-drop-static-lua.md`](../../fix-a-drop-static-lua.md) (the FIX A
symbol harvest — the verified RE evidence this design rests on),
[`before-game-hooks.md`](../../before-game-hooks.md) §6b/§6c (the boot-asset serve +
serve-execute consumers), KI-0005 (closed) + KI-0006 (open) as the two asset
consumers bundled here.

---

## §1 Vision

**kcdx builds the ONE Lua VM itself on its worker thread (after the game maps
WHGame — NO force-load, §6.2) through the FIX A symbol shim, and makes the engine
ADOPT that state instead of creating its own.** One compiled Lua body in the process
(WHGame's, reached through the shim), one sentinel set — the dual-Lua sentinel hazard
dies by construction, in both directions (FIX C's kcdx→WHGame and KI-0001's
WHGame→kcdx).

**v1 success criteria (measurable):**

- The process runs with `vendor/lua/*.c` dropped from the build — zero kcdx-compiled
  Lua. PROBE Q canary reads **zero** `frealloc.kcdx_image_ptr` lines across a full
  save-load cycle (the canonical dual-Lua repro). The full test suite stays green.
- The engine's `CScriptSystem::Init` operates on **kcdx's** `lua_State` — a
  single-state assertion holds (no second VM allocated; the `[L->l_G + 0xB0] == L`
  mainthread self-pointer invariant holds against the one state).
- A `before_game`-zone Lua plugin's hook fires before CryEngine reaches its own VM
  creation.
- A `kcdx.assets.replace` registered in the early Lua slot **wins a boot-asset open**
  (KI-0005's gap) — the resolver serves the overlay with `rt=HIT` for a boot asset
  the engine opens once at `CSystem::Init`.
- A served `.lua` run by the early kcdx-owned slot **executes end-to-end** (KI-0006's
  open execute-leg) — without riding the engine's crashing mod-init loader.

**The top-level architecture decision (settled):** kcdx is the VM owner. The engine
does not create its own Lua VM; kcdx creates the single state via the shim and the
engine's VM-creation path is intercepted to adopt it. Rejected: letting the engine
create its state and kcdx merely post-hooking to capture it — that leaves the
"VM up before the boot asset open" goal unreachable (the engine's open happens
inside the same `CScriptSystem::Init` that creates the state), which is exactly the
KI-0005 boot-swap capability v1 must deliver.

---

## §2 Glossary

| Term | Meaning |
|---|---|
| **The shim** | `src/lua_shim.{h,cpp}` — a function-pointer table (`kcdx::lua_shim::g_api`) resolving every `lua_*`/`luaL_*` symbol against WHGame.dll's compiled Lua, plus kcdx-side stubs for the inlined/stripped functions. `kcdx::lua_shim::Resolve()` populates it. |
| **The one state** | the single `lua_State*` kcdx allocates via the shim's `lua_newstate`. The engine adopts it; there is never a second. |
| **`CScriptSystem::Init`** | WHGame `@ 0x1448F38` — the engine's Lua-boot anchor; **sole caller** of `lua_newstate` (`@ 0x14492A8`) and `luaL_openlibs` (`@ 0x1449600`). Verified evidence below. |
| **The early Lua slot** | the pre-boot-open execution window for a plugin's early Lua, on the DllMain VM. Its author-facing shape is a **probe-gated decision** (§5). The single primitive serving three consumers: before_game Lua hooks, the boot-asset serve, the serve-execute path. |
| **Boot asset** | an asset the engine opens once at `CSystem::Init` and caches (e.g. the menu logo). A runtime overlay can only win it if registered BEFORE that open — KI-0005's constraint. |
| **PROBE Q** | the permanent dual-Lua canary (`ArmFreallocProbe`/`HookedFrealloc`, `src/hooks.cpp`). Stays through and after FIX A. |

---

## §3 RE primary-sources / evidence (the facts this design rests on)

This design turns on game-binary facts. Each is stated with its verified value and
its evidence tier on the reuse-first ladder, per
[`.claude/rules/reverse-engineering.md`](../../../../.claude/rules/reverse-engineering.md).
The canonical consolidated source is
[`fix-a-drop-static-lua.md`](../../fix-a-drop-static-lua.md); the seed rows are
`data/seeds/address_names_seed.csv` (ids 114, 116, 117, 121 + the Lua-API rows at
low ids ~1–130 — resolved by NAME, not by a baked id range; the old "1100-range" was
the pre-2026-05-28 single-CSV numbering, renumbered in the three-file split) +
`address_versions_seed.csv`.

| Fact | Verified value | Evidence tier |
|---|---|---|
| `CScriptSystem::Init` RVA | `0x1448F38` | maintainer_ghidra (seed id 121, v1.5.1164953) |
| Init is the **sole caller** of `lua_newstate` + `luaL_openlibs` | `lua_newstate @ 0x14492A8`, `luaL_openlibs @ 0x1449600` | maintainer_ghidra (seed id 121) |
| Init's post-`lua_newstate` sequence | stores L on instance (+0x10) and global (.data `0x549A0E8`) → **sets storedebug=0** (`[g+0x22]=0`, overriding `lua_newstate`'s default =1) → `luaL_openlibs(L)` → 3 extension-lib registrars (`0x1449698`, `0x1449410`, `0x1449584`) | maintainer_ghidra (seed id 121) |
| `lua_newstate` body | allocates `0x268` (sizeof LG); sets `storedebug=1` @ g+0x22, gcpause/gcstepmul=200 @ g+0x90/0x94, totalbytes=0x268 @ g+0x78, mainthread=L @ g+0xB0, L->l_G @ L+0x20; final `luaD_rawrunprotected(L, f_luaopen, NULL)` @ `0x14493F0` | maintainer_ghidra (seed id 114) |
| `gEnv->pScriptSystem` write target | .data RVA `0x549A0E8` (NOT the stale muyuanjin `0x4092B828`) | maintainer_ghidra |
| Lua API harvest | 93/117 `LUA_API`/`LUALIB_API` resolved + 13 internal helpers + 4 CScriptSystem anchors; ~24 inlined/stripped catalogued with per-function stub strategies + GC-barrier hazards | maintainer_ghidra (seeded at low ids ~1–130, resolved by name; consolidated in the harvest doc) |
| Layout constants (sizeof LG, field offsets, `LUA_NUMBER=float`, `TValue=0x10`, …) | per the harvest doc's "Layout constants" table | maintainer_ghidra |

**The README "~38% / BLOCKED" note is STALE** (it predates the 2026-05-21 harvest
completion). The harvest is substantially complete; the remaining work is the
design + build below, not an external symbol-harvest blocker.

---

## §4 The keystone probe — step 1, before any mechanism is wired

The mechanism (kcdx owns the VM, the engine adopts it) is settled. **Which
interception point is safe is a checkable unknown** — resolved by a probe FIRST, not
a guess (`.claude/rules/results-driven.md`; the agent writes/builds/deploys the
probe, the user only launches, `.claude/rules/agent-builds-and-deploys.md`). The
probe instruments `CScriptSystem::Init` + `lua_newstate` live and observes ground
truth across four questions, each with a pre-committed, flat,
theory-independent outcome→meaning map.

> **PROBE STATUS — RAN + SETTLED (PROBE P11 v2, 2026-06-05; archive
> `_research/probe-archive/p11-keystone-init-vm-ordering.md`).** §4.1 = the narrow
> hook (`lua_newstate` callee) is SAFE (state virgin `storedebug=1` at creation,
> static evidence confirms Init overwrites with no read-branch). §4.2 = one VM,
> `[L->l_G+0xB0]==L` holds. §4.3 = **the VM-build (worker thread) → boot-open (game
> main thread) dependency is CROSS-THREAD and was UNGATED** — the §5 ordering guard
> was a timing defect, now corrected to a mandatory happens-before EVENT GATE (P3/P4
> builds a NEW edge; no existing event covers it). §4.4 = the slot-shape decision is
> unaffected, still P4-gated. P2–P6 build against this corrected design.

### 4.1 Intercept-point safety — where does the interception sit?

**Lean (user, 2026-06-05): hook `lua_newstate` (the callee, `0x14492A8`); let the
engine run its own `storedebug=0` / `luaL_openlibs` / 3-extension-registrar sequence
on kcdx's state.** Narrowest intercept, least drift, no replication of CryEngine's
boot sequence. The probe confirms it is safe.

**Observe:** when `CScriptSystem::Init` calls `lua_newstate` and receives kcdx's
pre-built state, does Init read any field assuming a **virgin** `lua_newstate`
state (notably `storedebug==1` @ g+0x22, or the freshly-initialized gc fields)
before it overwrites them?

| Outcome | Meaning | Next action |
|---|---|---|
| Init treats the return opaquely — only overwrites (`storedebug=0`, stores L) and calls `luaL_openlibs(L)` | the narrow hook is safe; the engine builds the rest on our state | **build the `lua_newstate`-callee hook** (the lean) |
| Init READS a virgin-state field (a branch on `storedebug==1`, a gc-field assumption) before overwriting | the narrow hook would feed Init a state it mis-reads | **fall back to hooking `CScriptSystem::Init` itself** — kcdx supplies the state + replicates the post-newstate sequence (storedebug=0, openlibs, 3 registrars); record the replication as a maintenance cost (re-verify per game patch) |
| Init does something else unexpected with the return | unknown shape | re-observe ground truth; do not hop to a fix theory; surface the finding |

### 4.2 Single-VM validation — is there exactly one state, hazard-free?

**Observe:** after feeding kcdx's state through Init's flow, is there exactly one VM
(no second `lua_newstate` allocation), and does PROBE Q stay silent?

| Outcome | Meaning | Next action |
|---|---|---|
| One state; `[L->l_G+0xB0]==L` holds; PROBE Q silent | the mechanism delivers the single-body invariant | proceed |
| A second state was allocated (the hook missed a call site) | the interception is incomplete | re-observe which call site created it; widen the hook |
| PROBE Q fires (a kcdx-image sentinel still in play) | a kcdx-compiled Lua body remains (the drop is incomplete) | the static-Lua drop (§6) is not done; trace `caller_ra`/`block` |

### 4.3 Boot-asset swap reachability — KI-0005's required capability

**The user's load-bearing constraint: this mechanism MUST enable Lua asset swaps on
game-load items.** A `kcdx.assets.replace` registered in the early Lua slot, before
the engine's boot asset open, must be HIT by the resolver. Re-instrument the
resolver per `_research/probe-archive/ki0005-resolver-dds-observer.md`.

The reachability is NOT a timing question — it is a GATING question (§5 "ordering
guard"). The early slot (worker thread) and the boot open (game-main thread) are two
threads; the boot open must be **gated** to wait on the slot's registration, never
merely happen-after it by wall-clock. So the outcome map below reads in terms of the
event gate, not a timing margin.

| Outcome | Meaning | Next action |
|---|---|---|
| The boot-open path WAITS on the early-slot's readiness gate and resolves the overlay only after it is signaled; the boot asset opens with `rt=HIT` | **KI-0005 boot-swap delivered** — gated, not raced | confirm + close the KI-0005 boot-serve deliverable; retire/narrow the AP14 warn (§7) |
| The boot open is UNGATED relative to the VM-build / early slot (the two run on different threads with no synchronization edge) | the cross-thread dependency is a race — a DEFECT (§5) | **fix in the design + P3/P4: add the mandatory happens-before event gate** (worker signals, boot-open path waits-and-blocks). NOT a timing fix ("run the slot earlier") — that is the forbidden race. (This is the outcome PROBE P11 v2 found: the boot-open path is currently ungated — §5 now mandates the gate.) |
| The boot open is gated and after the signal but the resolver MISSES | a store/key issue independent of the gate | re-observe the key fold; this is a store bug, not an ordering bug |

### 4.4 Early-slot shape — which author surface runs safely this early?

**Settled to: probe all to see** (user, 2026-06-05). The probe observes which slot
shapes run safely in the pre-boot-open window; §5 records the candidates and the
decision criterion. **Observe:** run a trial early-Lua body (a `System.LogAlways` +
a `kcdx.assets.replace`) via each candidate shape and record whether it executes
cleanly that early without touching post-init engine state.

| Outcome | Meaning | Next action |
|---|---|---|
| A `before_game`-zoned `plugin.lua` runs cleanly in the early window | reuse the existing entrypoint (§5 candidate A) is viable — fewest surfaces | settle the slot to **reuse `plugin.lua` early** |
| `plugin.lua` early-run touches post-init state and faults/misbehaves, but a minimal dedicated body is clean | the existing entrypoint is unsafe early; a separate slot is needed | settle the slot to a **new `lua_before` entrypoint** (§5 candidate B) |
| Neither runs cleanly this early | the VM-up point is later than the boot open | re-observe the VM-up timing vs the boot open; the slot window may not exist where assumed — surface |

---

## §5 The early Lua slot — probe-gated author surface

The early Lua slot is the one primitive three Phase-11 consumers share: before_game
Lua hooks (`before-game-hooks.md` §1–§5), the boot-asset runtime serve (§6b), and
the served-`.lua` execute path (§6c). Its **author-facing shape is settled by §4.4's
probe**, against these two candidates:

- **Candidate A — reuse `plugin.lua`, run early for `before_game`-zoned plugins.** No
  new entrypoint. A plugin declaring `zone = "before_game"` gets its existing
  `plugin.lua` run in the early window on the DllMain VM; `after_game` plugins keep
  the late slot. One file, one mental model; timing follows the `zone` the author
  already sets.
- **Candidate B — a new `lua_before` entrypoint.** A separate early-only entrypoint
  distinct from `plugin.lua`; the author opts a file into the early window
  explicitly, and `plugin.lua` keeps its current post-init timing untouched.

**Decision criterion (pre-committed):** if a `before_game`-zoned `plugin.lua` runs
cleanly in the pre-boot-open window (§4.4 outcome 1), settle to **candidate A** (the
fewest surfaces wins, no new author-facing entrypoint to document/test/teach).
Only if early-run `plugin.lua` is unsafe (§4.4 outcome 2) settle to **candidate B**.
The design does not pre-commit the answer — the probe's observed behavior settles it,
and the settled shape is captured back into this §5 + the changelog when it lands.

**The ordering guard — a MANDATORY happens-before EVENT GATE, never a timing margin (hard invariant).** The early Lua slot runs on the **kcdx worker thread**; the engine's boot-asset open runs on the **game main thread** (verified by PROBE P11 v2, 2026-06-05: `dllmain_vm_point`/VM-build on the worker, `boot_open.first` + the engine's `lua_newstate` on game-main `tid`). These are two threads with a cross-thread data dependency (the boot open must see the early slot's runtime-overlay registration). **A cross-thread dependency is permitted ONLY when an explicit synchronization edge GATES it** — multiple threads are allowed for non-conflicting work, but a dependent thread MUST be stopped by a gate until the thread it depends on has finished. Therefore:

- **The gate is mandatory and explicit.** The early Lua slot (worker), after its asset-declaration calls complete, **signals a readiness event** (a new manual-reset event — call it `g_kcdxLuaSlotReadyEvent` pending P3/P4 naming). The boot-asset-open path (`asset_overlay.cpp` HOOK 1 `AdjustFileNameResolver` + HOOK 2 `FOpenLooseOverlay`, game-main thread) **waits on that event (`WaitForSingleObject`, the bounded-timeout form) and BLOCKS until it is signaled, before resolving the overlay** for a boot asset. An unsignaled boot open **blocks**; it does NOT proceed on a timing assumption and it does NOT race.
- **No existing edge covers this** (verified 2026-06-05): the boot-open path is currently UNGATED relative to the VM-build (`asset_overlay.cpp` calls `RecordBootOpen` and proceeds with "the Lua VM not yet up"; `g_kcdxReadyEvent` gates the `ModManager` ctor-bracket, NOT the boot-asset open; `g_whgameLoadedEvent` gates the worker's WHGame-mapped wait, NOT this). So P3/P4 **adds** this edge — it cannot reuse one. (P3/P4 confirms the exact event + the bounded-timeout fallback behavior under its own architect-review.)
- **Timing-based ordering is FORBIDDEN.** "The early slot completes ~Nms before the boot open" is NOT the guarantee and is never accepted as one (it is the cross-thread race this gate exists to kill). The happens-before EVENT EDGE is the guarantee; the wall-clock margin is irrelevant.
- **A boot open that proceeds without the gate signaled is a DEFECT, not a deferral** — fixed at source, never worked around with a sleep, a retry, or a "usually it's ready by then." The gate is the bar (`.claude/rules/concurrency.md` — a cross-boundary dependency is gated, not timed; `.claude/rules/polling.md` — no sample-and-hope).
- **Regression:** a permanent test row that FAILS if the boot open observes the overlay store BEFORE the slot signaled (order inversion) — the falsifiable proof the gate holds, not the timing.

---

## §6 The shim + the static-Lua drop

### 6.1 The symbol shim (`src/lua_shim.{h,cpp}`)

- A function-pointer struct (`kcdx::lua_shim::LuaApi`) sized for all 117
  `LUA_API`/`LUALIB_API` symbols. `kcdx::lua_shim::Resolve()` populates it after
  WHGame.dll is mapped and the Address Library is up, before any Lua VM touch.
- **The 93 resolved functions** forward by resolving each function's canonical NAME
  through `refdb::ResolveAddrByName("<lua_fn>")` (the by-name resolution the keystone
  probe used; robust against id renumbering — NOT a baked id range).
- **The ~24 inlined/stripped functions** get kcdx-side stubs per the harvest doc's
  verified per-function strategies (e.g. `lua_gettop` → `(int)(L->top - L->base)`;
  `lua_pushnil/boolean/number/...` → direct TValue write + `L->top++`;
  `luaL_register` reimplements `luaI_openlib`'s inlined body using resolved
  primitives). These stubs rely on the layout constants in §3, validated at init
  (the mainthread self-pointer invariant) so a future game-update struct shift fails
  loud, not silent.
- **Resolve bails loud on any REQUIRED symbol that fails** — kcdx does not touch the
  VM if the shim is incomplete (distinguishing a required miss from a known-stripped
  function that has a stub).

**Stub safety — the GC-barrier hazard (non-negotiable).** A stub that writes a GC
pointer (`lua_pushthread`, `lua_replace`, and kin) MUST call `luaC_barrierf`
(`0x3997070`) — without the barrier the incremental GC can free live objects. The
harvest doc's "What NOT to do" enumerates the unsafe stubs; the design adopts that
list as a hard constraint on the shim build, with a test row per GC-touching stub.

**Internal-only functions stay internal.** `lua_close` / `lua_newstate` /
`lua_setallocf` / `lua_atpanic` are resolvable by the shim but NEVER exposed through
the plugin-facing `kcdxLuaApi` — exposing them would let a mod author destroy or
replace the game's VM.

### 6.2 WHGame mapping + the before_game apply pass — NO force-load (corrected by PROBE P3, 2026-06-05)

**A kcdx DllMain force-load of WHGame is IMPOSSIBLE and is NOT done.** PROBE P3
(archive `_research/probe-archive/p3-vm-build-timing.md`) verified against the live
boot: kcdx is injected into a `CREATE_SUSPENDED` game, so at kcdx's DllMain WHGame is
not mapped (`GetModuleHandleW` null) and `LoadLibraryW` is loader-lock-forbidden; the
probe further found WHGame is not even mapped when kcdx's WORKER thread starts
(`whgame_already_mapped=0` at worker entry) — the game maps WHGame itself, on its own
thread, after `ResumeThread`. The earlier design wording ("`LoadLibraryW` in kcdx
DllMain") was a defect (it predated the probe); it is dropped.

- **The game maps WHGame; kcdx waits.** kcdx's worker thread blocks on
  `ldr_notify::WaitForGameDll` (an LDR-notification event signaled when WHGame maps)
  — already built (`src/dllmain.cpp`, `src/ldr_notify.cpp`). No kcdx force-load.
- **The before_game apply pass ALREADY EXISTS and works** (not new Phase-11 work):
  `ldr_notify.cpp`'s LDR callback runs `ApplyEntriesForModule` per newly-mapped
  module (the bugsplat fix's `BugSplat64.dll` target applies when WHGame's chain maps
  it) AND `SetEvent`s the WHGame-loaded gate. Phase 11 CONFIRMS this pass, it does not
  rebuild it.
- **The VM is built on the worker** at the post-`WaitForGameDll` / post-`refdb::Open`
  point (WHGame mapped, `lua_newstate` resolvable — PROBE P3 `vm_buildable
  whgame_mapped=1 lua_newstate_resolved=1`). This worker point precedes the
  game-main-thread engine Init by ~2 s (cross-thread — the worker builds, the game
  thread adopts via the §5 gate). See §6.4.
- A bad mapping/boot AVs at startup — boot is the falsifiable observable.

### 6.4 VM build on the worker + the cross-thread adoption (PROBE P3-settled)

kcdx builds the one `lua_State` on the WORKER thread, at the point above. The engine's
`CScriptSystem::Init` (game-main thread, ~2 s later) adopts it via the
`lua_newstate`-callee intercept (§4.1, the narrow hook P1 settled). The worker-builds
→ game-adopts handoff is a cross-thread dependency, ordered by the §5 mandatory
event gate (worker publishes the built VM + `kcdx.*` tables with a release edge; the
game thread's intercept observes them after the acquire) — NEVER a wall-clock margin.

### 6.3 Drop static Lua (the hazard-killing step)

- `vendor/lua/*.c` leaves the build; the `lua` static target leaves CMakeLists.txt.
  `vendor/lua/*.h` STAYS (struct defs for the stubs + PROBE Q).
- `src/lua_shim.cpp` defines every `LUA_API`/`LUALIB_API` symbol as a forwarder.
- **FIX C's `vendor/lua/ltable.c::setnodevector` patch is reverted** — unneeded once
  there is no kcdx-side compiled Lua.
- **PROBE Q stays** as the permanent regression canary.
- The `kcdxLuaApi` plugin-DLL surface becomes a direct forwarder to the same shim —
  C++ DLL plugins get the same one-body Lua.

---

## §7 The asset consumers (KI-0005 boot-swap, KI-0006 serve-execute)

### 7.1 Boot-asset Lua swap — KI-0005 (the user-required capability)

The mechanism delivers this by construction when §4.3's probe passes: kcdx registers
`kcdx.*` tables + runs the early Lua slot BEFORE the engine's `CScriptSystem::Init`
flow reaches the boot asset open, so an early-slot `kcdx.assets.replace` keys the
runtime store before the open and the resolver HITS. KI-0005 is closed-by-design
today (the declarative sidecar wins boot assets pre-VM); this phase delivers the
**Lua-runtime** boot serve it deferred.

**The AP14 warn — build-time decision (settled to: decide against observed
behavior; lean narrow).** Until now, a Lua runtime `register`/`replace` on a
boot-opened vpath emits a teaching warn ("use the declarative sidecar; runtime boot
replace is Phase 11"). Once the early slot serves boot assets, the warn's fate is
decided when the early slot is built and its boot-serve is live-confirmed:

- **Lean — narrow it:** a boot-vpath replace from the EARLY slot is valid (no warn);
  a boot-vpath replace from the LATE `plugin.lua` still cannot win the boot open →
  keep the warn there, pointing at the early slot or the sidecar.
- **Remove it** only if the early slot fully subsumes the late path AND a late-slot
  boot target is structurally rejected (never a silent no-op — the exact regression
  KI-0005's warn prevents).

The precise narrowing depends on whether the early slot subsumes the late path,
observable only once built. Recorded as a build-time decision; the lean is narrow.

### 7.2 Served-`.lua` execute — KI-0006 (open)

KI-0006's execute-leg is unconfirmed and its mod-init-overlay path crashes
(heap corruption, 4 probes, not root-caused pre-FIX-A; the cross-CRT `FILE*` free is
confirmed-real but NOT the trigger). **Phase 11's deliverable (settled scope):**

1. Confirm serve-AND-execute via the **early kcdx-owned Lua slot** — NOT a
   `scripts/mods/*.lua` mod-init overlay (the crashing path). A served `.lua` the
   kcdx slot runs proves execute end-to-end on an instrumentable path.
2. **If a crash still reproduces post-FIX-A**, THEN root-cause it with the cross-CRT
   variable eliminated (FIX A collapses the dual-runtime that created the hazard
   class). The surviving evidence (the `WHGame+0xB2DBA0` victim site, the
   cap-78-`overlay_entry`-keyed correlation) is the starting point; the falsified
   theories (record-synth, re-entrancy, mod-init-serve) stay falsified.
3. **If no crash reproduces** (likely — the hazard class is structurally collapsed),
   KI-0006 closes on the confirmed execute.

**Honest scope:** NOT a guaranteed crash-fix. KI-0006's corrupting write is
unidentified and not provably in Phase 11's path; the bundle ensures the re-attempt
runs against the shipping architecture with the confirmed hazard structurally
addressed and a far more instrumentable execution path.

---

## §8 Structure (units this design introduces / touches)

| Unit | Responsibility | New / changed |
|---|---|---|
| `src/lua_shim.{h,cpp}` | resolve + forward every `lua_*`/`luaL_*` through WHGame; stub the inlined/stripped set | NEW |
| `src/dllmain.cpp` | on the WORKER thread (post-`WaitForGameDll`), build the VM + install the Init interception; drive the early Lua slot; order the early slot vs the boot open. NO force-load (§6.2). | CHANGED |
| `src/ldr_notify.cpp` | the LDR-notification before_game apply pass + the WHGame-loaded gate (ALREADY built + working; Phase 11 confirms, does not rebuild) | reused |
| `src/address_library.*` (DB seeds) | the Lua RVAs (already seeded, low ids ~1–130, resolved by name) | reused |
| `src/early_hook.{h,cpp}` (from `src/probes/bugsplat_ctor_probe`) | the generalized author-parameterized early-install primitive (`before-game-hooks.md` §5) | relocated/generalized |
| `vendor/lua/` | `*.c` dropped from build; `*.h` kept; FIX C patch reverted | CHANGED |
| `src/hooks.cpp` PROBE Q | the permanent dual-Lua canary | reused, unchanged |

---

## §9 Phases / roadmap (the build order — for `/plan` to decompose)

Dependency-ordered; each step independently verifiable when it lands
(`.claude/rules/incremental-delivery.md`). The keystone probe is step 1 — it
resolves the intercept point + the boot-swap reachability + the early-slot shape
that every later step rests on.

1. **Keystone probe (§4)** — observe `CScriptSystem::Init` + `lua_newstate` live;
   settle the intercept point, the boot-swap reachability, the early-slot shape.
   Output: the decided mechanism details. (Probe; its captured finding + wiring
   archive to `_research/probe-archive/`, no residue in live source.) DONE.
2. **Shim build (§6.1)** — `src/lua_shim.{h,cpp}`: forward the 90 (by name), stub the
   31 catalogued (GC-barrier-safe), `Resolve()` bails loud. Coexists with static Lua
   at this point. Test: a shim round-trip lands on the stack; PROBE Q silent. DONE.
3. **VM build + Init interception on the worker (§6.2, §6.4, §4.1 result)** — NO
   force-load: kcdx's worker waits for WHGame (`WaitForGameDll`, exists), then at the
   post-`refdb::Open` point builds the one state via the shim's `lua_newstate` and
   installs the narrow `lua_newstate`-callee intercept; the game-main `CScriptSystem::Init`
   adopts it via the §5 cross-thread gate. Confirms (does NOT rebuild) the existing LDR
   before_game apply pass; loader/worker-startup budget measured. Test: boots; single-state
   assertion; `kcdx.*` tables present; CryEngine scripts still run on our state. (This
   folds the prior "force-load" step — there is no force-load to build.)
4. **Early Lua slot + boot-asset serve (§5, §7.1)** — the probe-decided slot shape +
   the mandatory event gate; an early-slot `kcdx.assets.replace` wins a boot asset
   (`rt=HIT` via the gate); the AP14 warn narrowed/removed per the build-time decision.
5. **Drop static Lua (§6.3)** — `vendor/lua/*.c` out, FIX C reverted, `kcdxLuaApi` →
   shim forwarder. Test: suite green with static Lua dropped; PROBE Q silent across
   save-load; a `before_game`-zone Lua plugin's hook fires before CryEngine init.
6. **Serve-execute confirmation (§7.2)** — a served `.lua` executes via the kcdx slot;
   KI-0006 execute-leg confirmed (re-attempt crash root-cause only if it reproduces).

Each step ships its permanent `test-plugins/` regression row
(`.claude/rules/test-suite.md`).

---

## §10 Out of scope / deferrals

- **The bugsplat builtin filename fix** is a CONSUMER that rides this phase
  (`before-game-hooks.md` §6), not part of the VM design. It lands once before_game
  hooks work; its KI stays open until then.
- **Full plugin migration to Lua-`before_game`** beyond the test vehicle — authors
  adopt the zone as they choose; v1 ships the capability + one regression vehicle.
- **Phase 12** (the C++ empowered-wrapper sweep) is independent and unblocked by this.
