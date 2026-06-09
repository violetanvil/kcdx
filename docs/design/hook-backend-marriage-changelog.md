# Hook-backend marriage design — changelog (newest first)

## 2026-06-08 — §4.5 batch install DROPPED; the false thread-suspend premise corrected

Phase 5 (batch install) opened with the §9.7 U7 probe, resolved by a static read of
the vendored safetyhook source (`results-driven.md` §4 — static evidence settling a
checkable unknown), confirmed by an independent cold architect-review and a read-only
cost investigation. The finding: the design's §4.5 batch premise is FALSE for this
safetyhook build, so the batch is DROPPED on measured evidence.

- **§4.5 — the batch mechanism is REMOVED, replaced with a measured "why no batch"
  note.** safetyhook's `enable()` does NOT suspend threads: `trap_threads`
  (`vendor/safetyhook/src/os.windows.cpp:268-318`) is VEH + `VirtualProtect`, not
  stop-the-world (zero `SuspendThread` in the vendored tree). So there is no
  thread-suspend cost to amortize. The measured per-`enable()` cost (≈4
  `VirtualProtect` + a trap-map insert + a once-shared VEH) is single-digit ms total
  at boot scale = premature optimization; the only safe multi-target window-collapse
  saves ~0 (scattered `VirtualProtect`s can't coalesce) and widens the mid-prologue
  safety window. MinHook's real batch (`MH_ApplyQueued`) is immaterial at its N≈1-4.
  The note is the institutional-memory record so the question isn't re-investigated.
- **§7 — the "safetyhook suspends all threads" evidence bullet corrected.** The
  earlier read confirmed only that `enable()` CALLS `trap_threads`; it never read
  what `trap_threads` DOES (an unbacked runtime-mechanism assertion). Corrected to
  the verified VEH+`VirtualProtect` mechanism. The loader-lock bullet corrected: the
  "suspend-deadlocks-under-the-loader-lock" reason is false; MinHook stays the
  conservative choice and safetyhook's actual VEH-under-loader-lock safety is a
  marked assumption-to-probe.
- **§4.2 — the loader-lock routing WHY corrected** (user-decided): remove the false
  suspend-deadlock reason; keep the MinHook OUTCOME (the conservative LDR-callback
  choice); mark safetyhook's loader-lock safety as an assumption-to-probe.
- **§1 batch success criterion, §8 batch-install unit, §2 "Batch install" glossary
  term — all DROPPED** (the mechanism doesn't exist). **§9.7 U7 — RESOLVED** (batch
  dropped, not pending a probe). **§11 batch row — corrected** to record the drop on
  evidence (a Performance add-on that delivers nothing; an honest interface stays
  per-hook). **§8 backend units** — the batch-API mentions on `MinHookBackend` /
  `SafetyhookBackend` removed.

**Integrated in:** §1, §2, §4.2, §4.5, §7, §8, §9.7, §11.
**Why:** the probe-first discipline killed a wrong design assumption (a falsified
runtime-mechanism claim) before it shipped — a finding, not a failure. The corrected
design speaks with one voice (no known-false clause) and carries the measured cost as
institutional memory. The marriage's delivered value (Phases 1–4: the InstallRuntime
seam, function-entry on safetyhook, the mid-hook retirement, foreign-hook coexistence
— all live-verified) is untouched; only the §4.5 Performance add-on is dropped.

## 2026-06-08 — slot-ownership + g_installed model corrected (step-4a build findings)

Building Phase 2 step 4a, the U8 caller-set probe (§9.8) ran against the real
install code and a cold architect-review confirmed two gaps where the §4
re-grounding's prose assumed a clean 1:1 relocation the live code does not have.
Both settled by the user:

- **JIT slot ownership: `runtime_func_t` owns the storage, the backend POPULATES
  the value.** §4.1/§8 said "the backend owns its slot" but §4.4 said "the backend
  populates it" — an internal contradiction. The callsite path (`AddCallsite`)
  falsifies the "backend owns" reading: it installs NO backend yet still needs the
  slot (it writes the callee VA directly). So the slot STORAGE moves onto
  `runtime_func_t` (a plain member the JIT bakes); each producer (a backend via
  `InstallRuntime`, the callsite path directly, `dynamic_hook`) writes the value
  in. Resolves the tension in §4.4's favor — what the live code already does.
- **`g_installed`: its redundant role retires, its unique cross-registry guard
  re-homes (NOT removed outright).** The U8 check resolved that `InstallRuntime`
  has TWO callers: the chain's `FindChain`-gated sites (where `g_installed` is
  redundant) AND `kcdx.memory.dynamic_hook` (a non-chain caller in a separate
  registry, for which `g_installed` is the ONLY cross-registry double-install
  refusal, with a loud owner-naming message `FindChain` cannot produce). Per
  §9.8's own "re-home before removing, never drop silently," the unique guard
  re-homes into `InstallRuntime` (a minimal per-target installed-set at the seam);
  dropping it would lose a load-bearing loud refusal (AP14).

**Integrated in:** §4.1, §4.4, §4.6, §7 (the g_installed evidence bullet), §8
(the `MinHookBackend`/`InstallRuntime`/`detour_hook`/`runtime_func_t` units), §9.8
(U8 RESOLVED), §11 (the "Backend seam home" + "Conflict model" rows corrected, a
"JIT slot ownership" row added).
**Why:** the §4 re-grounding correctly relocated the seam to `InstallRuntime` but
its prose assumed a backend-owns-the-slot, remove-the-map-outright model the real
code does not support — the callsite path needs a backend-independent slot, and
`dynamic_hook` needs the cross-registry guard. Building step 4a on the
contradictory prose would have broken cap-21/cap-22 or silently dropped a refusal;
correcting the doc first keeps step 4a (and 4b/5) building to accurate authority.

## 2026-06-08 — §4 re-grounded against the real install architecture

The code revealed the v1 design had the install layering inverted: §4.1 named
`detour_hook` as "the single chokepoint for every chain hook," but `detour_hook`
is only the JIT call-original SLOT owner (its `enable()`/`disable()` are dead on
the chain path). The real install chokepoint every chain hook funnels through is
`hook_engine::InstallRuntime` → raw `MH_CreateHook` (`src/hook_engine.cpp:66`).
Four settled revisions:

- **Backend seam relocates to `InstallRuntime`; `detour_hook` dissolves.** The
  backend dispatches at the install chokepoint and owns its own relocated-original
  slot (the `void**` the JIT bakes) — no separate slot-owner adapter remains. The
  step-3 `IDetourBackend` + `MinHookBackend` (commit 64fba7d) are CORRECT as built;
  only their attachment point moves (out from behind `detour_hook`), and
  `MinHookBackend`'s bodies are reused verbatim.
- **`g_installed` retires — one conflict model.** The first-wins map
  (`src/hook_engine.cpp:51`) was a v0.1 second model; the chain's
  `FindChain`/`CanCoexist` already prevents the double-install it guarded. Removed,
  not ported (pending the §9.8 caller-set check).
- **Foreign-hook coexistence is now a core v1 pillar (§6), not final-phase polish.**
  The extreme-mod consumer (multiplayer, heavy TC load orders) hooks the same
  functions other mods hook, so chaining onto foreign detours is existential for
  it. Chain-always policy unchanged; only priority/placement elevated.
- **Batch install designed now (§4.5).** safetyhook's `enable()` suspends all
  threads per hook (`vendor/safetyhook/src/inline_hook.cpp:383`) — N stop-the-world
  cycles at TC/multiplayer scale. No safetyhook batch primitive exists (verified
  this session); a kcdx-authored batch path is constructible over `StartDisabled`
  + `trap_threads` (create-all-disabled → one frozen window patches N). The
  multi-target frozen-window patch is a marked assumption-to-probe (§9.7); the
  path is provisional on it.

**Integrated in:** §1, §2, §4.1, §4.4, §4.5 (new), §4.6 (new), §6, §7, §8, §9
(unknowns 7–8 added), §11.
**Why:** the v1 design named the wrong layer as the install seam — a backend
behind `detour_hook` would not sit on the install path at all. Re-grounding the
seam at `InstallRuntime`, retiring the redundant conflict model, and elevating the
foreign-hook + batch concerns the extreme-mod consumer depends on aligns the
design with the architecture the code actually has.

## 2026-06-07 — v1 settled (initial authoring)

The marriage of MinHook + safetyhook behind one backend interface, settled
through the design dialogue. The decisions:

- **Backend abstraction behind `detour_hook`.** A uniform `IDetourBackend`
  interface (create/enable/disable/get_original); MinHook + safetyhook as two
  implementations; the chain + conflict model + Lua/ABI marshaling unchanged
  above it.
- **Routing is install-context-driven, automatic.** Loader-lock (`early_hook`) +
  the `HookedUpdate` bootstrap pump → MinHook (safetyhook's unconditional
  thread-suspend deadlocks under the loader lock — a hard correctness constraint,
  not effort). Everything else → safetyhook. No author knob.
- **`make_jit_midfunc` fully replaced by `safetyhook::MidHook`.** The three
  call-original modes map onto `Context64.rip` (True = run trampoline, False =
  `ctx.rip = resume_addr`, Auto = callback conditionally sets it); named captures
  map onto register writeback. The fragile asmjit mid-hook codegen (the cap-04
  scar tissue) retires.
- **Foreign-hook detection + chaining added on top.** kcdx detects a pre-existing
  foreign E9/FF on a target, follows it, and chains so both mods' hooks fire —
  true cross-mod coexistence, built above the patcher. Chain-always policy; a
  configurable policy is reserved.
- **The mid-hook replacement is provisional, gated on a spike.** The
  `ctx.rip`→three-modes mapping is read-feasible from safetyhook's header but not
  yet observed at runtime — marked assumption-to-probe, proven by porting cap-04
  before the full retirement lands (`results-driven.md`).

**Integrated in:** §1–§11 (initial authoring — the whole doc).
**Why:** there is no single best detour engine for kcdx — safetyhook is strictly
better for the bulk but its thread-suspend install is unsafe under the loader
lock, which kcdx's `early_hook` path requires. Routing each path to the engine
whose strengths fit it, behind one interface, captures both engines' strengths
and neither's weakness.
