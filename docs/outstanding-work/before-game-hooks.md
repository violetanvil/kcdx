# before_game hooks — author-facing DllMain/LDR-timing hooks

> **Spec status: AUTHORITATIVE FUTURE-WORK SPEC, design-settled, build-deferred.**
> The design below was settled with the user via `/senior-architect-consult`
> (2026-05-26) and is the contract the Phase 11 implementation follows. The
> empirical findings (PROBE R/S/T, PROBE BG1) are live-confirmed, not
> theorized. Deviating from a "MUST" reopens a question already closed — stop
> and ask rather than guess.

## 1. Status

Designed + live-de-risked, **build-deferred to Phase 11**. The capability lets a
`before_game`-zoned plugin install a MinHook detour at DllMain/LDR-notification
timing — early enough to intercept a function the game's init code calls before
kcdx's worker thread (and thus `kcdxPlugin_Load`) runs. The forcing use case is
the BugSplat colon-filename fix (§6), which needs to hook
`BugSplat64.dll!MiniDmpSender::MiniDmpSender` at DllMain time.

**Why deferred (not a blocker we hit — a sequencing decision):** the restructure
plan (`restructure-plan.md:166,98`) designs `kcdx.hook` as `requireZone=After`
for phases 1-10 and `Either` from Phase 11, *because* before_game Lua hooks need
the Lua VM up in DllMain, which needs **FIX A** (`fix-a-drop-static-lua.md` —
drop static-linked vendored Lua, route `lua_*` through WHGame.dll's symbols so
one VM exists at DllMain). FIX A is Phase 11. The zone_gate synthetic row
`kcdx.zone_gate_test_after_only` (After-required) is the stand-in enforcing
"no before_game hooks yet." A C++-only before_game hook (raw native detour, no
Lua) is buildable *today* without FIX A (the self-registration design §3
sidesteps the VM) — but the user chose to keep the whole capability as one
coherent Phase 11 deliverable rather than ship a C++-only subset ahead of
sequence. The investigation work that de-risks it is captured here so Phase 11
builds from it, not from scratch.

## 2. Trigger to revisit

Phase 11 (FIX A lands — the Lua VM is available at DllMain). At that point both
tiers (§4) are buildable together: the C++/native before_game hook (no VM needed,
already de-risked by PROBE T) AND the Lua-callback before_game hook (needs the
Phase 11 VM). Build them as one capability so the gate flips `kcdx.hook` to
`Either` once, for both surfaces.

## 3. Settled design (the `/senior-architect-consult` decisions, verbatim)

1. **Timing is ZONE-DRIVEN, not a per-hook knob.** A hook installed by a
   `before_game`-zoned plugin (`[load_order].zone="before_game"`)
   installs at DllMain/LDR-notification timing — zone is the single source of
   truth for apply timing, mirroring how before_game `[[patch]]` entries already
   apply at DllMain (`ldr_notify.cpp`). The SAME `kcdx.hook` surface; the
   plugin's zone decides when. An after_game plugin's hooks install at the
   apply-pass as today (`hook_chain` at `hooks::Install`).

2. **FOREIGN-MODULE + EXPORT-SYMBOL locator.** `kcdx.hook.before("BugSplat64.dll",
   "MiniDmpSender::MiniDmpSender", ...)` — the first positional arg is the MODULE
   (the planned shape, `lua-api-surface.md` "no default module"). For a foreign
   module the target name is an EXPORTED symbol resolved via `GetProcAddress`
   (the mangled name for a C++ export). **NO new locator field is needed** — both
   `kcdx.hook` (Lua, `lua_bind_hook.cpp:463` reads `module`, default "WHGame.dll")
   and `kcdxHookOptions.module` (Interfaces.h, default "WHGame.dll") ALREADY carry
   `module`. The work is a new RESOLUTION TIER: when `module != "WHGame.dll"` and
   the target is an export, `GetProcAddress(GetModuleHandleW(module), exportName)`.
   The existing locator resolves a foreign module only via the RVA form
   (`"Module.dll @ rva 0xNNNN"`, `pe_helpers::OpenModule`, `hook_chain.cpp:1451`);
   the export-name tier is the addition.

3. **The author SUPPLIES the signature for a foreign export — this is the
   LEGITIMATE expert-hatch, NOT an AP12 violation.** The engine has no Address
   Library entry for a third-party DLL's exports and cannot name them, so
   author-supplied ABI (`opts.signature` / `signature=`) is correct here
   (`cornerstones.md`: the expert hatch for what the engine cannot yet name).
   WHGame named targets still carry their verified ABI as today; only foreign
   exports need author signatures.

4. **CONFLICT MODEL = FIRST-APPLIED-WINS at before_game timing.** A before_game
   hook installs under loader lock BEFORE the apply-pass runs, so it CANNOT
   defer-and-arbitrate through `hook_chain`'s conflict pre-flight. It inherits the
   existing before_game `[[patch]]` model: first-applied-wins by the load-order
   sort key (`ldr_notify.cpp:108-134` already does this for patches, deliberately
   bypassing conflict-engine pre-flight). This is a DOCUMENTED AP4 carve-out — the
   same one before_game patches already have (`load-order.md:137`). Do NOT try to
   run `hook_chain` arbitration at DllMain. A before_game hook is therefore NOT in
   `GetConflictReport` (it's not in the chain) — acceptable + logged, matching the
   before_game-patch precedent.

5. **OPEN TO AUTHORS, UNRESTRICTED, rules DOCUMENTED not ENFORCED.** Both Lua
   (Phase 11) and C++ can install before_game hooks. The loader-lock hazards (a
   before_game callback that calls `LoadLibrary` / heavy heap / the Lua VM can
   SILENTLY deadlock the loader lock — frozen launch, no crash/dump, victim is the
   mod USER) are DOCUMENTED LOUDLY in the author docs, NOT engine-enforced. The
   cornerstone is enable-not-forbid (the user's explicit decision): a deadlock is
   the author's own footgun, taught via docs. Do NOT build a restricted-API gate
   or a callback sandbox. (Rationale: of the three failure modes, only loader-lock
   deadlock is silent+catastrophic; a plain crash and a won't-install are loud +
   diagnosable, i.e. normal author bugs, not grounds to forbid the attempt.)

6. **MECHANISM = SELF-REGISTRATION (the plugin's OWN DllMain installs).** A
   before_game plugin DLL installs its hook from its own `DllMain` via a
   kcdx-provided LDR-notification install primitive (§5). **The engine MUST NOT
   `LoadLibrary` a plugin under the loader lock** — that is the
   `dllmain.cpp:143` invariant ("MinHook init / CreateThread / LoadLibrary are NOT
   done at DllMain — those stay in the worker thread") and reintroducing it is the
   #1 silent-deadlock risk in kcdx's own startup. The earlier "engine early-loads
   the plugin" option was REJECTED for exactly this. The capability's one accepted
   lacuna: a before_game hook requires the plugin to SHIP A NATIVE DLL (the detour
   body is native; there is no safe Lua-at-DllMain even with FIX A's VM for an
   arbitrary author callback — the detour must be raw/native). A pure-Lua plugin
   cannot install a before_game hook; that is intrinsic to "before_game =
   load-time," not a design shortfall.

7. **BUGSPLAT = the first consumer, a before_game-zoned BUILTIN PLUGIN** on this
   public surface (`kcdx-engine/builtin/bugsplat-filename-fix/`), user-disableable
   via `load_order.toml` as today. A builtin is just a first-party plugin; it
   dogfoods the capability. The disproven `[[patch]]` (LEA `0x1824599e7`) is
   DELETED — empirically wrong (PROBE R/S).

## 4. The two tiers (why one capability, built together at Phase 11)

| Tier | Callback | VM needed? | Buildable | Gate today |
|---|---|---|---|---|
| **C++/native** | raw `__fastcall` detour, no Lua | NO | de-risked NOW (PROBE T) | blocked by synthetic After-row |
| **Lua-callback** | a Lua function fired at DllMain | YES (FIX A) | Phase 11 only | blocked by design (correct) |

Build both at Phase 11 so `kcdx.hook` flips to `Either` once for both surfaces.
The C++ tier needs no VM but was deferred with the Lua tier by the user's
sequencing choice (one coherent capability, not a C++-only subset early).

## 5. The proven install machinery (KEEP — it is the engine half of the fix)

`src/probes/bugsplat_ctor_probe.{h,cpp}` (currently dev-gated, wired into
`dllmain.cpp` `RunBeforeGameZoneInDllMain` via `ArmLdrInstall()`, committed
`f42c7bd`) IS the prototype of the §5 primitive — KEEP it, relocate out of
`src/probes/` into a permanent engine home (e.g. `src/early_hook.{h,cpp}` or an
extension of `ldr_notify`) when Phase 11 builds. It provides:

- `ArmLdrInstall()` — if the target's module is already mapped at kcdx DllMain,
  install immediately; else register an `LdrRegisterDllNotification` callback to
  install the instant the module maps (pre-its-own-DllMain).
- `HookedCtor` — a raw `__fastcall` MinHook detour. `MH_Initialize` is idempotent
  under loader lock; `GetProcAddress` on the mangled export; `MH_CreateHook` +
  `MH_EnableHook`. **PROBE T (live 2026-05-26 09:01) CONFIRMED all of this works
  under loader lock** — the LDR notification armed, fired when BugSplat64.dll
  mapped, installed the hook, and the ctor then fired with the colon string.

The Phase 11 work generalizes this from one baked target into an
author-parameterized install (module + export + signature + the plugin's named
detour) driven by the before_game-zoned plugin's own DllMain.

## 6. The bugsplat fix specifics (the first consumer)

- Target: `BugSplat64.dll!MiniDmpSender::MiniDmpSender`, mangled export
  `??0MiniDmpSender@@QEAA@PEB_W000K@Z` (export ordinal 3 / RVA 0xC914).
- ABI (verified PROBE R/S): `MiniDmpSender::MiniDmpSender(wchar_t const*
  szDatabase, wchar_t const* szApp, wchar_t const* szVersion, wchar_t const*
  szUser, unsigned long flags)`. Win64 fastcall: RCX=this, RDX=szDatabase,
  **R8=szApp (the colon string to fix)**, R9=szVersion, [rsp+0x28]=szUser,
  [rsp+0x30]=flags.
- The fix: in the detour, if `szApp` contains `':'`, substitute a colon-free copy
  ("Kingdom Come Deliverance II" — a static buffer the detour owns, simpler than
  depending on WHGame.dll's no-colon sibling string at `0x183e18290`) before
  calling the original. Colon→space; preserve the rest. The probe's `HookedCtor`
  is LOG-ONLY today; the fix changes it to rewrite szApp.
- Full investigation trail: `docs/known-issues/BugSplat dmp files don't reach
  disk for AV crashes.md` (PROBE R/S/T + the 2026-05-26 reframe).

## 7. The zone_gate interaction (MUST resolve at Phase 11)

The zone_gate is NOT buggy — it is correctly enforcing "no before_game hooks
yet" via the synthetic `kcdx.zone_gate_test_after_only` (After-required) row,
the only non-`Either` row (`zone_gate.cpp:56`). The gate is STATIC by design
(`zone_gate.h:30-34`): it does not inspect what a plugin calls; it rejects a
before_game plugin if ANY table row requires after_game. So the synthetic row
rejects EVERY before_game plugin — fine while no real before_game plugin exists
(only comp-13, which is DESIGNED to be rejected), but it blocks the first real
one.

**At Phase 11, when `kcdx.hook` becomes `Either` (FIX A lands):**
- The synthetic row's purpose (stand-in for `kcdx.hook`'s After-requirement) ends.
- Per `zone_gate.cpp:38`: convert the synthetic to `Either` OR replace it with a
  REAL zone-requiring capability if before_game-hook introduces one. (Likely it
  does NOT — before_game-hook is a TIMING of `kcdx.hook`, which becomes `Either`;
  the zone drives timing, it doesn't gate the API. So the synthetic likely just
  retires to `Either`.)
- **comp-13-zone-gate-observer RELIES on the synthetic After-row** to exercise
  the rejection path (it asserts a before_game subject is rejected). Reworking
  comp-13 is part of the Phase 11 change: either retarget it onto a real
  zone-requiring capability if one exists, or convert it to test the gate via a
  different mechanism. Do NOT silently break comp-13 (it is the zone_gate
  regression).

**Also (PROBE BG1 finding):** `kcdx::plugins::g_manifests` is NOT populated when
`RunBeforeGameZoneInDllMain`'s early code runs — the manifest list / zone
resolution lands later in `LoadAllConfigs`. A Phase 11 DllMain-time enumeration
of before_game plugins must confirm WHERE in the init sequence the manifest +
zone data is available (the self-registration model sidesteps this — each plugin
acts from its own DllMain, not from a kcdx enumeration — but any engine-side
before_game-plugin sweep must account for it).

## 8. Files that need to change (Phase 11 change set)

- `src/probes/bugsplat_ctor_probe.{h,cpp}` → relocate to a permanent engine home
  (`src/early_hook.{h,cpp}` or extend `src/ldr_notify.{h,cpp}`); generalize
  `ArmLdrInstall` + `HookedCtor` into a parameterized install primitive.
- `src/ldr_notify.{h,cpp}` — the natural home for the before_game-hook install
  path (it already owns the LDR notification + the already-loaded sweep for
  patches; extend to also install before_game HOOKS). Confirm `MH_Initialize` +
  `MH_CreateHook` loader-safety (PROBE T already proved it for the bugsplat case).
- `src/hook_chain.cpp` / `src/lua_bind_hook.cpp` / `src/hook_interface.cpp` — the
  `GetProcAddress` export-name resolution tier (foreign module + export).
- `src/zone_gate.{h,cpp}` — flip `kcdx.hook` (synthetic row) to `Either`; rework
  the synthetic / comp-13 per §7.
- `include/kcdx/Interfaces.h` — likely NO change (module field exists; zone-driven
  timing needs no new field). Append-only + version-bump ONLY if a field proves
  needed.
- `kcdx-engine/builtin/bugsplat-filename-fix/` — rewrite from `[[patch]]` to a
  before_game builtin DLL installing the szApp-rewrite; delete the disproven
  `[[patch]]`.
- `test-plugins/cap-NN-before-game-hook/` — the regression (a before_game DLL
  plugin that installs a before_game hook on a known export, records "fired" into
  a flag read at ready/InputLoaded for a boot-only auto-pass; the cap-43 skeleton
  built during this investigation was removed when the work deferred — rebuild it).
- `test-plugins/comp-13-zone-gate-*` — rework per §7.
- `docs/lua/hook.md` + `docs/cpp/hook.md` — before_game timing, foreign-module/
  export locator, the LOUD loader-lock-rules section (§5 decision 5).
- `docs/load-order.md:137-139` — update the hook rows (before_game hooks now
  supported via the LDR-notification path).

## Pointers

- `docs/known-issues/BugSplat dmp files don't reach disk for AV crashes.md` — the
  full PROBE R/S/T/BG1 trail + verified ABI + the 2026-05-26 reframe.
- `fix-a-drop-static-lua.md` — the Phase 11 dependency the Lua tier needs.
- `restructure-plan.md:159-176` (Capability gating) + `:96-98` (zone declaration,
  Lua-before_game Phase 11 constraint) + `:166` (`kcdx.hook` After→Either).
- `src/probes/bugsplat_ctor_probe.cpp` — the proven install machinery to generalize.
