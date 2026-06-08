# Hook-backend marriage — the settled design

**Status:** v1 (settled 2026-06-07; changelog `hook-backend-marriage-changelog.md`).
**Supersedes:** nothing — this is a new architectural layer beneath the existing
hook engine. It does NOT supersede the hook-chain conflict model, the Lua/ABI
marshaling, or the Address Library; those sit unchanged above it.
**Authoritative for:** the detour-backend layer build — the executor builds to
THIS doc, not to a step-doc summary of it (`.claude/rules/spec-conformance.md`).

This is the canonical spec for marrying **MinHook** and **safetyhook** behind one
uniform backend interface, so every hook-install path runs on the engine whose
strengths fit it and neither engine's weakness can break a path. The design is a
pure internal-quality change — no author-facing surface moves — so it is weighed
on correctness + maintenance, not UX (`cornerstones.md`: a swap touching no
author surface is not a UX fork).

**The core insight that shapes everything below:** there is no single best detour
engine for kcdx. safetyhook is strictly better for the bulk (thread-safe install,
far-target reach, a vetted mid-hook primitive, typed errors) but its sole install
primitive — unconditional thread-suspend in `enable()` — is **unsafe under the
Windows loader lock**, and kcdx has a path (`early_hook`) that installs under the
loader lock by necessity. MinHook patches bytes without suspending threads, which
is exactly what that path needs. The marriage routes each install to the engine
that fits it, behind one interface, with the chain unchanged on top.

---

## §1 Vision

**One uniform detour-backend interface at the install chokepoint
(`hook_engine::InstallRuntime`); MinHook and safetyhook are two interchangeable
implementations; each install path is routed to the backend whose strengths fit
it — automatically, by install context.**

The chain (`hook_chain`), the conflict model (`CanCoexist`, one detour per
target, ordered callbacks), and the Lua/ABI marshaling (`PushSlot`/`WriteSlot`,
the per-slot type JIT) are untouched: they sit above the backend layer and do not
know which engine patched the bytes underneath. The marriage is a substitution at
the byte-patcher layer only — plus two capabilities it unlocks (a vetted mid-hook
primitive, foreign-hook chaining) that the bespoke MinHook layer could not give.

The backend seam lands where the install actually bottoms out: `InstallRuntime`
is the single function every chain install calls to create + enable the one
detour per target (it calls raw `MH_CreateHook` today). `detour_hook` — the
former JIT call-original slot owner — **dissolves**: the backend owns its own
relocated-original slot (the `void**` the JIT bakes), so there is no separate
adapter layer between the chain and the patcher.

### v1 success criteria (measurable)

- **Function-entry parity.** Every cap-NN function-entry hook row passes live on
  the safetyhook backend; the suite `X/Y passing` line is unchanged from the
  MinHook baseline at the same commit.
- **Loader-lock safety preserved.** `early_hook` still installs under the loader
  lock with zero deadlock and zero hang (MinHook retained on that path; the
  MiniDmpSender ctor target still fires, as today).
- **Mid-hook parity via `ctx.rip`.** The cap-04 skip-original matrix (CAP-04a/b/c/d
  — True/False/Auto) passes with `safetyhook::MidHook`, after the spike proves the
  `ctx.rip`→mode mapping (§9, gated).
- **Far-target reach.** The cap-22 far-module callsite/hook rows pass with NO
  per-module-pool special-casing required (safetyhook's E9→FF fallback reaches any
  64-bit address) — the cap-21/cap-22 engine gap is closed by the backend, not by
  kcdx's branch-pool anchoring.
- **Foreign-hook coexistence (a core pillar, §6).** A two-mod fixture (kcdx + a
  synthetic foreign E9 on a shared target) shows BOTH detours fire — the foreign
  mod's hook runs in the chain, kcdx's runs too. (`comp-NN` row.) This is a v1
  pillar, not deferred polish — the extreme-mod (multiplayer) consumer lives or
  dies on chaining onto foreign detours.
- **Batch install at scale.** A fixture installing N function-entry hooks at boot
  (a TC/multiplayer-scale install set) completes through a single thread-suspend
  window rather than N — the safetyhook per-`enable()` suspend cost does not scale
  linearly with hook count. (`comp-NN` row; the batch mechanism is §4.5.)

### The top-level architecture decision

A **backend abstraction at `InstallRuntime`** (not a wholesale MinHook removal,
not a no-abstraction per-path hard-wire). MinHook stays permanently for the two
loader-lock/bootstrap paths; safetyhook serves everything else; one interface
makes them interchangeable and a future third backend a drop-in. The seam sits at
the install chokepoint every chain hook already funnels through, so `detour_hook`
(a redundant slot-owner adapter once the backend owns its slot) dissolves rather
than being reshaped.

---

## §2 Glossary

- **Detour backend** — the byte-patcher + trampoline + install/uninstall layer
  for a function-entry or mid-function hook. MinHook and safetyhook are the two
  implementations. The backend owns: writing the prologue jump, allocating +
  populating the trampoline (the relocated original bytes), and the
  install/uninstall mechanism. It does NOT own the chain, the conflict model, or
  the Lua marshaling.
- **`IDetourBackend`** — the uniform interface the marriage introduces (working
  name; the executor may rename). Methods: `create(target, detour) -> original`,
  `enable()`, `disable()`, `get_original() -> trampoline_ptr`. `InstallRuntime`
  selects a backend by install context and drives it; the backend owns its own
  relocated-original slot. (Already built in step 3 as `IDetourBackend` +
  `MinHookBackend` — the interface is correct; the marriage relocates where it
  attaches, from the now-dissolved `detour_hook` to `InstallRuntime`.)
- **`InstallRuntime`** — `hook_engine::InstallRuntime(name, target, detour)`: the
  single function the chain's first hook per target calls to create + enable that
  target's one detour. It calls raw `MH_CreateHook` today; the marriage makes it
  the backend-dispatching install. The real install chokepoint — `detour_hook`
  was not (its `enable()`/`disable()` are dead on the chain path).
- **Install context** — the runtime condition at the moment a hook is installed:
  whether the caller holds the loader lock, whether `MH_Initialize` has run,
  whether the install is on the per-frame bootstrap path. The routing predicate
  reads this to pick the backend.
- **Batch install** — installing N detours under ONE thread-suspend window
  instead of N. safetyhook's `enable()` suspends all threads per call; at
  TC/multiplayer scale (hundreds of boot installs) that is hundreds of
  stop-the-world cycles. The batch path creates all N hooks disabled, then patches
  them in a single frozen window. A kcdx-authored mechanism over safetyhook's
  primitives (§4.5), not a safetyhook feature.
- **Loader-lock path** — `early_hook`: installs a detour from inside an LDR
  notification callback, which fires under the Windows loader lock during DLL
  mapping. Thread suspension here deadlocks (a suspended thread may hold the
  loader lock the installer is inside). MinHook-only by construction.
- **Bootstrap pump** — `HookedUpdate` (the per-frame `CGame_Update` detour): the
  one engine hook that drives the chain dispatcher itself, so it cannot be a chain
  entry without self-deadlock, and cannot take a thread-suspending install in the
  frame loop. Direct MinHook, the documented bootstrap exception
  (`hook-engine.md`).
- **`Context64`** — safetyhook's mid-hook callback parameter: every GPR + all 16
  XMM registers + `rflags`, by value with writeback (a write to `ctx.rdx` lands in
  the real register when the original resumes), plus `rip` (points at a trampoline
  of the replaced instruction) and `trampoline_rsp` (the writable stack pointer;
  `rsp` itself is read-only). The mechanism that replaces `make_jit_midfunc`'s
  hand-rolled register-capture + skip codegen.
- **Foreign hook** — a detour installed by another mod (not kcdx) on a function
  kcdx also wants to hook. Detected by reading the target's prologue and finding a
  jump (E9 rel32 / FF25 absolute) that does not point into a kcdx trampoline.
- **Foreign chaining** — following the foreign jump to capture the foreign
  detour as kcdx's "original," so installing kcdx's hook preserves the foreign
  mod's hook in the call chain (both run).

---

## §3 User Stories & Acceptance Criteria

The "users" here are the kcdx engine maintainer and the mod author whose hooks
must keep working. No author-facing surface changes; these stories are about what
keeps working and what newly works.

### US-1 — A plugin author's existing hooks keep working unchanged

A mod author who wrote `kcdx.hook.before("WHGame.dll", "IsInCombat", fn)` (or any
function-entry / mid / around hook) sees identical behavior after the marriage —
same install, same fire, same coexistence. They never learn the backend changed.

**Acceptance:** every cap-NN hook row (function-entry + mid + around + callsite)
passes live at the post-marriage commit, matrix `X/Y passing` unchanged from the
pre-marriage baseline. No `kcdx.toml` / `plugin.lua` / C++ interface change is
required of any existing plugin.

### US-2 — A hook on a function in a far-away module installs reliably

A TC author hooks a function in a module loaded >2GB from WHGame.dll (e.g.
BugSplat64.dll, or another mod's DLL). The hook installs every launch, regardless
of ASLR placement.

**Acceptance:** the cap-22 far-module rows pass with zero "not rel32-reachable"
failures across repeated launches, and WITHOUT the per-module branch-pool anchor
special-case being the thing that saves it — safetyhook's absolute-jump fallback
reaches the target directly. (The branch-pool may remain for the trampoline's own
allocation; the point is far-target reach is no longer a kcdx-engineered edge.)

### US-3 — A mid-hook that skips the original instruction works in all three modes

An author's mid-hook with `call_original = true | false | "auto"` behaves exactly
as specified — true runs the captured instruction, false never runs it, auto
decides at runtime from callback state — now backed by `safetyhook::MidHook`'s
`ctx.rip` instead of hand-rolled asmjit codegen.

**Acceptance:** CAP-04a (true → 110), CAP-04b (false → 10), CAP-04c (auto+skip →
10), CAP-04d (auto+no-skip → 110) all pass. Named captures read AND write back
(an author mutating a captured register sees the mutation take effect). This
acceptance is gated on the §9 spike proving `ctx.rip` carries all three modes.

### US-4 — kcdx coexists with another mod that hooked the same function

A mod author runs kcdx alongside another mod (e.g. a TPV camera mod) that hooks a
function kcdx also hooks. Both mods' hooks fire; neither silently loses; the
prologue is not corrupted.

**Acceptance:** a `comp-NN` fixture installs a synthetic foreign E9 on a target,
then kcdx hooks the same target; a live run shows BOTH the foreign detour and
kcdx's chain fire, in a defined order. kcdx's install detects the foreign hook,
follows it, and preserves it as the chain's original.

### US-5 — Engine bootstrap still installs under the loader lock without hanging

The `early_hook` LDR-time install (the before_game / BugSplat-ctor path) installs
its detour from inside the loader lock, as today, with no deadlock and no
boot hang.

**Acceptance:** US-5 boots cleanly across repeated launches incl. under
multitasking load (the KI-0003 contention scenario); the early_hook MiniDmpSender
ctor target still fires and logs. The path uses MinHook (no thread suspension),
verified by the routing predicate selecting MinHook for a loader-lock install.

---

## §4 The model — one interface, two backends, context-routed

### §4.1 The backend interface (`IDetourBackend`) lands at `InstallRuntime`

The real install chokepoint is `hook_engine::InstallRuntime(name, target, detour)`
(`src/hook_engine.cpp`) — the single function the chain's first hook per target
calls to create + enable that target's one detour. It calls raw `MH_CreateHook`
today (`src/hook_engine.cpp:66`), then writes MinHook's `pOriginal` into the slot
the JIT baked. `detour_hook` was NOT the chokepoint — it is only the JIT
call-original slot owner; its `enable()`/`disable()` are dead on the chain path
(`src/rom_borrowed/runtime_func_t.h:80-86` — the install path bypasses `m_detour`
because `InstallRuntime` calls `MH_CreateHook` directly). The marriage therefore
lands the backend at `InstallRuntime` and **dissolves `detour_hook`**: the backend
owns its own relocated-original slot, so no separate slot-owner adapter remains.

```
IDetourBackend (interface):
  create(target, detour) -> original_ptr | error   // allocate trampoline, prepare patch
  enable() -> ok | error                            // write the prologue jump
  disable() -> ok | error                           // restore the prologue
  get_original() -> trampoline_ptr                  // the relocated-original entry point

MinHookBackend     implements IDetourBackend  // wraps MH_CreateHook/Enable/Disable/Remove
SafetyhookBackend  implements IDetourBackend  // wraps safetyhook::InlineHook
```

`IDetourBackend` + `MinHookBackend` already exist (step 3, commit 64fba7d) —
built behind `detour_hook`, but the interface itself is correct and unchanged;
the marriage relocates its attachment point to `InstallRuntime` and reuses
`MinHookBackend`'s create/enable/get_original bodies verbatim.

`InstallRuntime` becomes the backend-dispatching install: pick the backend by
install context (§4.2), `create(target, detour)`, `enable()`, return the backend's
relocated-original pointer for the JIT slot. Above it, nothing changes:
`runtime_func_t` bakes the slot the backend populates into its JIT'd call-original
code (the call-original deref, the around path, the mid-resume slot); the chain
reads it for `BuildNativeCallThunk`. Below it, each backend owns its own
trampoline + patch mechanism.

**The one contract the backend must honor:** `get_original()` returns a stable,
callable pointer to the relocated-original entry point, valid for the hook's
lifetime, written into the JIT slot. MinHook returns its `pOriginal`; safetyhook
returns its `InlineHook`'s `.original()` / trampoline address. Both satisfy the
existing JIT contract (the asmjit thunks deref a pointer slot — the backend writes
the right value into that slot, §4.4).

The redundant first-wins map retires with this seam (§4.6): `g_installed`
(`src/hook_engine.cpp:51`) was a v0.1 second conflict model layered over the
chain's own. The chain's `FindChain` + `CanCoexist` already prevents a
double-install per target; `g_installed` duplicated that decision in a second
place. It is removed, not ported.

### §4.2 Backend routing — install-context-driven, automatic

The backend is chosen by the engine from the **install context**, with NO author
knob (cornerstone #1 — the engine does the heavy lifting; a plugin author always
goes through `hook_chain` and never names a backend):

| Install path | Backend | Why |
|---|---|---|
| `early_hook` (loader-lock, during DllMain / LDR callback) | **MinHook** | safetyhook's `enable()` suspends all threads unconditionally — a deadlock under the loader lock. MinHook patches without suspending. Hard correctness constraint. |
| `HookedUpdate` bootstrap pump (per-frame, drives the chain dispatcher) | **MinHook** | Can't be a chain entry (self-deadlock); a thread-suspending install in the frame loop is wrong. Direct MinHook, the documented bootstrap exception. |
| `hook_chain` function-entry (all plugin + engine-stamped hooks) | **safetyhook** | Thread-safe install, far-target reach, typed errors. The bulk. |
| `hook_chain` mid-function (`make_jit_midfunc` replacement) | **safetyhook** | `safetyhook::MidHook` — `ctx.rip` + register writeback. §9. |

**The routing predicate is the one safety-critical mechanism in this design.** A
loader-lock install misrouted to safetyhook is a deadlock; a path that should be
safetyhook misrouted to MinHook silently forgoes the thread-safety gain. The
predicate is: *is the caller on a loader-lock / pre-`MH_Initialize` / bootstrap-pump
context?* → MinHook; else → safetyhook. (The build resolves HOW the predicate
reads the context — a flag threaded through the install call, an explicit
backend argument at the engine-internal install site, or a context probe. This
is a §9 build-gated unknown, not a design fork — the OUTCOME is the table above;
the mechanism is the executor's, settled against the constraint that misrouting a
loader-lock install must be impossible, not merely unlikely.)

### §4.3 The chain is untouched — safetyhook is "just the patcher" for it

The `hook_chain` conflict model stays exactly as it is: one detour per target, an
ordered `std::vector<ChainEntry>`, `CanCoexist` as the sole predicate, the
engine-first stamp, the off-thread marshaling. safetyhook replaces ONLY the
byte-patcher under the one detour the chain installs per target. safetyhook has no
concept of the chain and is not asked to — the chain owns "who reacts, in what
order"; the backend owns "patch the bytes safely." (Foreign-hook detection, §6,
is the one addition ABOVE the patcher — it extends what the chain does at install
time, not the patcher.)

### §4.4 The call-original contract — bridging two trampoline models

This is the load-bearing integration detail. `runtime_func_t` bakes the
trampoline pointer into JIT'd asm by dereferencing a raw `original_` storage slot
(the `void**` the JIT bakes the address of, at three asmjit sites). Today
`InstallRuntime` writes MinHook's `pOriginal` into that slot after `MH_CreateHook`;
the slot's storage is the backend's to own once `detour_hook` dissolves. MinHook
hands back a `pOriginal` that fits this directly. safetyhook owns its trampoline
inside the `InlineHook` object and exposes `.original()` / `.call()`.

The backend resolves the mismatch by populating the slot: `MinHookBackend` writes
its `pOriginal`, `SafetyhookBackend` writes the address of safetyhook's trampoline
(the relocated-original entry) — both into the same slot the JIT thunks already
deref. The JIT codegen does NOT change — it still reads a pointer slot; the
backend (via `InstallRuntime`) writes the right pointer into it. (This is why the
abstraction lands at `InstallRuntime`/`get_original` and not deeper: the JIT
contract is "deref this slot for the original," and both backends satisfy it by
populating the slot the backend now owns. **Marked assumption-to-probe:** that
safetyhook's trampoline entry is directly callable with the original ABI from the
JIT thunk the same way MinHook's `pOriginal` is — verify in the spike alongside
the mid-hook proof, §9.)

### §4.5 Batch install — N detours, one thread-suspend window

The cost that does not scale: `safetyhook::InlineHook::enable()` calls
`trap_threads(...)` — suspend ALL threads, patch, resume — once **per hook**
(`vendor/safetyhook/src/inline_hook.cpp:383`). One hook: one stop-the-world
cycle, fine. A TC's hundreds of boot installs, or a multiplayer mod's
cross-cutting hook set: hundreds of stop-the-world cycles back-to-back, each
suspending and resuming every game thread. This is a real scale wall for the
extreme-mod consumer, not a micro-optimization.

safetyhook exposes **no batch primitive** — no `enable_all`, no multi-hook install
(verified: read of `vendor/safetyhook/` this session). It DOES expose the two
building blocks a kcdx-authored batch path needs:

- **`StartDisabled`** (`vendor/safetyhook/include/safetyhook/inline_hook.hpp:118`,
  a `Flags` bit) — `create` the hook with its trampoline allocated + relocated but
  the prologue jump NOT yet written. Defers the patch (and its thread-suspend) to
  a later `enable()`.
- **`trap_threads(from, to, len, run_fn)`** (`vendor/safetyhook/include/safetyhook/os.hpp:70`)
  — the exposed suspend-all-threads-and-run-`run_fn` primitive `enable()` itself
  uses. It runs an arbitrary closure inside the frozen window.

The batch mechanism kcdx builds over these: at a known batch boundary (boot, a
plugin's install set), `create` all N hooks with `StartDisabled`, then run ONE
`trap_threads`-equivalent whose `run_fn` writes all N prologue jumps inside a
single frozen window. N installs, one stop-the-world cycle.

**Marked assumption-to-probe (§9):** that a single `trap_threads` window can patch
N independent targets safely — the primitive is documented for one
`[from, to)` range, and the multi-target frozen-window patch (iterating N
prologue writes inside one `run_fn`) is feasible-from-the-primitive but NOT yet
observed end-to-end. The batch path is provisional on a probe that installs N>1
hooks through one window and confirms all N fire (a `comp-NN` fixture);
falsification falls back to per-hook `enable()` (the unbatched baseline, correct
but unscaled). This is a backend-layer mechanism — `MinHookBackend` has its own
batch story (MinHook exposes `MH_QueueEnableHook` + `MH_ApplyQueued`, verified
present in `vendor/minhook/include/MinHook.h`, to batch-apply queued hooks in one
pass; **marked assumption-to-probe:** that MinHook's apply imposes no per-hook
thread-suspend the batch would still pay — verify against MinHook's source at
batch-implementation time), so the batch API is expressed on `IDetourBackend` and
each backend implements it natively.

### §4.6 The single conflict model — `g_installed` retires

The chain already owns conflict resolution: `FindChain(target)` returns the one
existing chain for a target (or none), `CanCoexist` is the sole predicate for
whether a new hook joins it, and the first hook per target installs the one detour
while the rest append a `ChainEntry`. `g_installed` (`src/hook_engine.cpp:51`) was
a SECOND model for the same concern — a first-wins map `InstallRuntime` consulted
to refuse a double-install per target. With the chain's `FindChain` gate already
preventing exactly that, `g_installed` is redundant: two conflict models for one
concern, the precise drift this re-grounding exposed (the design had layered the
backend behind a slot-owner while the real install — and a duplicate conflict
check — lived elsewhere).

`g_installed` is **removed, not ported** onto the backend seam. `InstallRuntime`'s
single caller is the chain's first-hook-per-target path, which `FindChain` already
gates; the map guarded a double-install the chain structurally cannot reach. (If a
non-chain caller of `InstallRuntime` exists, it surfaces during the removal — a
checkable fact, resolved by reading the call sites, not assumed here.)

---

## §5 The mid-hook replacement — `make_jit_midfunc` retires

The single highest-value piece. The ~370-line hand-rolled asmjit mid-hook codegen
(`make_jit_midfunc` in `src/rom_borrowed/runtime_func_t.cpp`) is fully replaced by
`safetyhook::MidHook`. This is where the project's most fragile, most-bled-on code
lives — the cap-04 history (the 5-byte-MinHook-minimum off-by-one, the
RIP-relative-truncation `xchg` workaround) shows these bugs only surfaced through
live in-game iteration. safetyhook carries the equivalent, vetted.

### §5.1 The three call-original modes map onto `ctx.rip`

`make_jit_midfunc`'s True/False/Auto codegen becomes a `Context64` manipulation in
the mid callback:

| Mode | Today (`make_jit_midfunc`) | safetyhook `MidHook` |
|---|---|---|
| **True** (run original) | push MinHook trampoline_ptr as ret target | default — let safetyhook's trampoline run the replaced instruction (leave `ctx.rip` alone) |
| **False** (skip original) | push `resume_addr` as ret target (codegen-time) | set `ctx.rip = resume_addr` (past the captured instruction) |
| **Auto** (runtime decide) | push trampoline; post-callback read skip-flag, conditionally overwrite slot | callback conditionally sets `ctx.rip = resume_addr` from callback state |

This is strictly simpler than the asmjit version because safetyhook owns the
trampoline + resume machinery — kcdx no longer hand-computes `resume_addr` (the
5-byte-min accumulation), no longer fights asmjit's RIP-relative truncation, no
longer manages the skip-flag byte. The `resume_addr = target + stack_restore_offset`
computation (hde64 accumulate-to-≥5) MAY still be needed to know where "past the
instruction" is for False/Auto — **build-gated unknown:** whether safetyhook
exposes the resume address directly or kcdx still computes it (§9).

### §5.2 Named captures map onto `Context64` register writeback

`make_jit_midfunc`'s named register/stack captures (the `captures = {...}` table
the author declares) map onto `Context64`'s by-value-writeback fields: a capture
named `rdx` reads `ctx.rdx`; a `:set(v)` writes `ctx.rdx`, which safetyhook writes
back to the real register when the original resumes. The Lua-facing capture-handle
surface (the `CaptureHandle` userdata with `:get()`/`:set()`, the 16-byte slot
stride, the per-type marshaling in `PushCaptureValue`/`WriteCaptureValue`) is
**rewired onto `Context64` fields** instead of the JIT stack payload — the author
surface (`args.rdx:get()`) is unchanged; only what it reads/writes underneath
moves from a JIT slot to a `Context64` field.

- **Stack-expression captures** (`[rbp+0x10]`-style) map onto reading through the
  register fields + `ctx.trampoline_rsp` for stack-relative addresses.
  **Build-gated unknown:** whether every stack-expression capture form
  `make_jit_midfunc` supports has a `Context64` equivalent (§9) — most do via the
  register + offset fields; a form that doesn't is surfaced, not silently dropped.

### §5.3 The Lua dispatch + off-thread marshaling stays

`MidDispatch`, the off-thread filter, the engine-bootstrap carve-out, the
re-entrancy depth tracking, the pin-arena — all unchanged. safetyhook's `MidHook`
calls a `void(Context64&)` C callback; that callback IS kcdx's `MidDispatch`
adapter, which does the same Lua marshaling it does today, reading from
`Context64` instead of the JIT payload. The replacement is the codegen +
register-capture mechanism, NOT the dispatch/marshaling layer above it.

---

## §6 Foreign-hook detection + chaining — a core v1 pillar

The capability neither engine gives for free: when kcdx installs a hook on a
target another mod has already hooked, kcdx **detects the foreign hook, follows
it, and chains onto it** so both mods' hooks fire. This is the true cross-mod
coexistence answer — kcdx becomes a good citizen instead of silently winning the
prologue.

**This is a core pillar of the marriage, not deferred polish.** The extreme-mod
consumer — a multiplayer mod, a total conversion running alongside other mods —
is exactly the case that hooks functions OTHER mods also hook (camera, input,
networking, save, the update pump). For that consumer, "kcdx silently wins the
prologue and the other mod's hook vanishes" is a failure, not an edge case;
chaining onto foreign detours is the difference between kcdx coexisting in a heavy
load order and breaking it. The thread-safe, IP-fixing safetyhook install (§7) is
what makes patching a prologue another mod is actively in SAFE to do at all —
which is why foreign-hook coexistence is a first-class outcome the backend
marriage unlocks, surfaced in §1's success criteria and built as a v1 pillar
rather than a final-phase addition.

### §6.1 Detection — read the prologue before patching

Before installing, kcdx reads the target's first bytes and classifies the
prologue:

- **Clean prologue** (real game instructions) → install normally.
- **A jump to a kcdx trampoline** (the target is already in a kcdx chain) →
  the existing chain path (`CanCoexist` / append a `ChainEntry`), unchanged.
- **A foreign jump** (E9 rel32 / FF25 absolute pointing OUTSIDE any kcdx
  trampoline) → foreign hook detected; go to §6.2.

The discriminator between "kcdx trampoline" and "foreign" is whether the jump
target falls inside kcdx's known trampoline allocations (the branch-pool ranges +
safetyhook's allocator ranges — kcdx owns both). A jump elsewhere is foreign.

### §6.2 Chaining — follow the jump, capture the foreign detour as "original"

kcdx installs ITS hook such that the call chain becomes
`game → kcdx hook → foreign hook → real original`. Mechanically: kcdx's backend
captures the CURRENT prologue (which jumps to the foreign detour) as kcdx's
trampoline-original, so calling kcdx's "original" runs the foreign mod's detour,
which in turn runs the real function. kcdx's chain dispatch fires first, then
delegates down to the foreign hook via the normal call-original path.

This is what safetyhook's thread-safe, IP-fixing install makes SAFE to do at all
(patching a prologue another mod is also actively in) — but the chaining logic
itself is kcdx's, above the patcher (§4.3). safetyhook does not document
jmp-following; kcdx builds the detection + capture.

### §6.3 The hard sub-problems (design content, not deferred)

- **kcdx-vs-foreign load order.** When BOTH mods chain, who runs first depends on
  who installed last (each new hooker prepends itself). kcdx running first
  (game → kcdx → foreign → original) is the v1 contract: kcdx installs over the
  foreign hook, so kcdx's chain fires, then delegates to the foreign detour. This
  is load-order-by-install-time, the same model the kcdx chain already uses
  internally — stated, not silently chosen.
- **The foreign mod unhooks later.** If the foreign mod removes its hook after
  kcdx chained onto it, kcdx's captured "original" now points at a restored-or-
  freed foreign trampoline. v1 contract: kcdx hooks live for the session (the
  backend installs are never disabled/removed — "no FreeLibrary, no teardown,"
  matching SKSE), and kcdx assumes the foreign hook does too (the common
  mod-loader model). A foreign mod that
  unhooks mid-session is OUT OF SCOPE for v1 coexistence — surfaced as a known
  limitation, not silently mishandled (a foreign unhook kcdx cannot observe is a
  documented risk, §11).
- **Detection completeness.** A foreign hook installed AFTER kcdx (the foreign mod
  loads later and patches over kcdx's prologue) is the reverse case — kcdx can't
  detect at its own install time what doesn't exist yet. v1 detects foreign hooks
  present at kcdx's install time; the foreign-installs-later case is a documented
  limitation (§11), not a v1 guarantee.

### §6.4 What foreign-chaining is NOT in v1

Per the settled scope: kcdx **chains onto** a detected foreign hook (the chosen
policy) — it does NOT offer an author-configurable policy (chain / warn-and-take /
refuse-and-yield). Chain-always is the v1 behavior. A configurable policy model is
reserved (§11) — if a target where chaining is unsafe surfaces, the policy
question reopens as its own design.

---

## §7 RE / library evidence — the verified facts this design rests on

This design turns on library + OS facts, not (primarily) game-binary facts. Each
load-bearing claim with its evidence tier (`reverse-engineering.md` ladder applies
to the kcdx-source + library-source reads done this session):

- **safetyhook `enable()` suspends all threads unconditionally — no opt-out.**
  Evidence: read of safetyhook `src/inline_hook.cpp` this session — `enable()`
  calls `trap_threads(...)` unconditionally; `StartDisabled` only defers `enable()`,
  it does not avoid the suspend; the `unsafe_*` variants concern calling the
  ORIGINAL without a mutex, not a no-freeze install. No `unsafe_enable` exists.
  **This is the fact that forces MinHook-permanent on the loader-lock path.**
- **safetyhook `Context64` layout** — full GPR + 16 XMM + rflags, by-value with
  writeback ("modifications affect the context of the hooked function"), `rip`
  points at a trampoline of the replaced instruction, `rsp` read-only with
  `trampoline_rsp` for stack edits. Evidence: read of `include/safetyhook/context.hpp`
  this session. **This is the fact that makes the mid-hook replacement feasible.**
- **safetyhook E9→FF jump fallback.** InlineHook tries a 5-byte E9 rel32 first,
  falls back to a 14-byte FF25 absolute jump (x86-64) when out of rel32 range.
  Evidence: read of `src/inline_hook.cpp` (`e9_hook` then `ff_hook` fallback) +
  the typed `Error` enum (`NOT_ENOUGH_SPACE`, `SHORT_JUMP_IN_TRAMPOLINE`,
  `IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE`). **This is the fact that closes the
  cap-21/cap-22 far-target gap natively.**
- **The Windows loader-lock deadlock constraint.** Suspending a thread that may
  hold (or be waiting on) the loader lock, from inside the loader lock, is the
  canonical Win32 DLL deadlock. Evidence: Win32 DLL-best-practices (a known OS
  invariant, not a kcdx finding) + kcdx's own `early_hook.cpp` comment that the
  LDR path "runs under the loader lock during kcdx.dll DllMain." Tier: established
  platform fact + in-repo confirmation of the loader-lock timing.
- **kcdx's install paths all bottom out at MinHook today — and the chain path's
  chokepoint is `InstallRuntime`, not `detour_hook`.** `hook_chain` → first hook
  per target calls `hook_engine::InstallRuntime` → raw `MH_CreateHook`
  (`src/hook_engine.cpp:66`), which then writes `pOriginal` into the JIT slot;
  `early_hook` → `MH_CreateHook` under the loader lock; `HookedUpdate` → direct
  `MH_CreateHook` (bootstrap exception); the frealloc canary → direct
  `MH_CreateHook`. `detour_hook` is only the JIT call-original slot owner — its
  `enable()`/`disable()` are DEAD on the chain path (`InstallRuntime` calls
  `MH_CreateHook` directly, bypassing `m_detour`; `src/rom_borrowed/runtime_func_t.h:80-86`).
  Evidence: grep + read of `src/hook_engine.cpp`, `src/hook_chain.cpp`,
  `src/rom_borrowed/runtime_func_t.{h,cpp}`, `src/early_hook.cpp`, `src/hooks.cpp`
  this session. **This is why the backend seam lands at `InstallRuntime` and
  `detour_hook` dissolves — `InstallRuntime` is the actual single chokepoint for
  the chain path; the original design named the wrong layer.**
- **`g_installed` is a redundant second conflict model.** `InstallRuntime`
  consults a first-wins `g_installed` map (`src/hook_engine.cpp:51`) to refuse a
  double-install — but the chain's `FindChain(target)` already returns the one
  existing chain per target before a second install is ever attempted, so the map
  guards a case the chain structurally prevents. Evidence: read of
  `src/hook_engine.cpp` + `src/hook_chain.cpp` (`FindChain` at `:468`/`:906`/`:1922`,
  the first-hook-installs-rest-append gate at `:1922-1995`) this session. **This is
  why `g_installed` is removed, not ported (§4.6).**
- **The cap-04 / cap-21-cap-22 scar tissue.** The mid-hook skip codegen and the
  far-target trampoline were each multi-launch live-debugged fixes. Evidence:
  `docs/known-issues/closed/cap-04 ...md`, `docs/known-issues/closed/cap-21-cap-22
  ...md`. **These prove the gaps the marriage closes are real, not hypothetical.**

The mid-hook `ctx.rip`→three-modes mapping (§5.1) and the trampoline-callable
contract (§4.4) are **read-feasible from the headers but NOT observed at runtime
this session** — they are MARKED assumptions-to-probe, gated on the §9 spike
before the rewrite lands (`.claude/rules/results-driven.md` — a runtime-mechanism
claim is provisional until probed; the header read is static evidence that the
mechanism EXISTS, not proof it carries kcdx's exact three modes end-to-end).

---

## §8 Structure — responsibility units

The marriage introduces / reshapes these units (`structure-by-responsibility.md`):

- **`IDetourBackend`** (new, a core contract — already built, step 3) — the
  backend interface (`create`/`enable`/`disable`/`get_original`, plus the batch
  API §4.5). Core-layer: depends on nothing kcdx-specific; both backends depend on
  it; `InstallRuntime` drives it. One responsibility: the byte-patcher/
  trampoline/install contract. Built correctly in step 3; the marriage only
  relocates its attachment point (from `detour_hook` to `InstallRuntime`).
- **`MinHookBackend`** (new leaf — already built, step 3) — implements
  `IDetourBackend` over MinHook (`MH_CreateHook`/`Enable`/`Disable`/`Remove`,
  + `MH_QueueEnableHook`/`MH_ApplyQueued` for batch). Holds the relocated-original
  slot the JIT bakes; `get_original()` returns it. Reused verbatim — the bodies
  do not change, only where they are called from. One responsibility: MinHook-
  backed detours.
- **`SafetyhookBackend`** (new leaf) — implements `IDetourBackend` over
  `safetyhook::InlineHook` (+ the `StartDisabled` + `trap_threads` batch path,
  §4.5). One responsibility: safetyhook-backed detours.
- **`hook_engine::InstallRuntime`** (reshaped, the backend dispatch site) — the
  single install chokepoint becomes backend-dispatching: read the install context,
  pick a backend, `create` → `enable`, write the backend's relocated-original into
  the JIT slot. The `g_installed` first-wins map is REMOVED (§4.6 — the chain's
  `FindChain`/`CanCoexist` is the sole conflict model). Owns no patching logic of
  its own (it delegates to a backend).
- **`detour_hook`** (DISSOLVED) — the former JIT call-original slot owner is
  removed: it was only the slot owner (its `enable()`/`disable()` were dead on the
  chain path), and the backend now owns its own slot. The slot the JIT bakes
  becomes the backend's; no separate adapter layer remains between the chain and
  the patcher. (Step 3 placed `IDetourBackend`/`MinHookBackend` BEHIND
  `detour_hook`; this step moves them out from behind it and removes the husk.)
- **`runtime_func_t`** (reshaped) — the function-entry path keeps its asmjit
  pre/post JIT (the call-original slot is now the backend's, populated via
  `InstallRuntime`); the mid-function path (`make_jit_midfunc`) is REMOVED and
  replaced by a `safetyhook::MidHook` adapter that calls the existing `MidDispatch`
  from a `Context64` callback. The dead `m_detour` member + its slot plumbing are
  removed with `detour_hook`.
- **A batch-install unit** (new) — owns the "install N detours under one
  thread-suspend window" mechanism (§4.5): create-all-disabled, then one frozen
  window patches N. Expressed on `IDetourBackend` (each backend implements it
  natively — safetyhook via `StartDisabled` + `trap_threads`, MinHook via the
  queue API). One responsibility: batched detour application at a known boundary.
- **A foreign-hook-detection unit** (new, a core pillar §6) — the prologue
  classifier + the follow-the-jump capture. One responsibility: detect + capture a
  foreign detour at install time. Used by the `hook_chain` install path, above the
  backend.
- **`early_hook`, `HookedUpdate`, the frealloc canary** (unchanged backend) —
  stay on MinHook; the routing predicate selects MinHook for them. `early_hook`'s
  body is unchanged (it already calls MinHook directly under the loader lock).

The reference doc for the backend layer is added in the same change as the code
(`structure-by-responsibility.md` §6 — a new responsibility unit gets its
subsystem doc).

---

## §9 Build-gated unknowns (recorded — NOT design forks; the build's probes)

These are checkable facts the build resolves with a probe, not decisions the user
makes. Each is gated BEFORE the dependent step (`.claude/rules/incremental-delivery.md`
+ `results-driven.md` — a phase resting on a runtime mechanism opens with the
probe that proves it).

1. **The mid-hook proof spike (gates the `make_jit_midfunc` retirement).** Port
   cap-04 onto `safetyhook::MidHook` behind the new backend seam; re-run the
   cap-04 matrix. Proves: `ctx.rip = resume_addr` skips the captured instruction
   (CAP-04b/c), leaving it alone runs it (CAP-04a/d), and register-capture
   writeback works. Outcome map: all four pass → the full retirement proceeds;
   any fail → the mid-hook replacement is reconsidered (the design's one
   deliberately-gated path — a fallback keeps `make_jit_midfunc` if `ctx.rip`
   can't carry Auto cleanly). **The whole §5 rewrite is provisional on this spike.**
2. **The trampoline-callable contract (§4.4).** Does safetyhook's `.original()` /
   trampoline entry deref-and-call cleanly from the existing asmjit call-original
   thunk, the same way MinHook's `pOriginal` does? Probed alongside spike #1
   (an around-mode hook through the safetyhook backend).
3. **`resume_addr` ownership (§5.1).** Does safetyhook expose the past-the-
   instruction resume address, or does kcdx still compute it (hde64 accumulate-to-≥5)?
   Read safetyhook's MidHook trampoline contract; if kcdx still computes it, the
   existing hde64 logic is retained as a small helper (not the whole codegen).
4. **Stack-expression capture coverage (§5.2).** Does every `[rbp+0x10]`-style
   capture form have a `Context64` equivalent? Enumerate the forms cap-21 + the
   mid-hook tests exercise; map each to `Context64` + `trampoline_rsp`; a form with
   no equivalent is surfaced as a gap, not dropped.
5. **The routing-predicate mechanism (§4.2).** HOW the install context is read
   (a threaded flag / an explicit engine-internal backend arg / a context probe).
   The build picks the mechanism that makes a loader-lock-install misroute
   IMPOSSIBLE (compile-time or assert-guarded), not merely unlikely. The OUTCOME
   (the §4.2 table) is settled; the mechanism is the executor's.
6. **safetyhook license + vendoring (`dependencies.md`).** Verify safetyhook's
   license against the repo allowlist BEFORE vendoring; record the manifest row in
   the same change. (safetyhook is permissive — confirm at vendor time, both
   registry + source.)
7. **The batch-install trap_threads-reuse mechanism (§4.5, gates the batch path).**
   Does a single `trap_threads` frozen window safely patch N independent targets
   (iterating N prologue writes inside one `run_fn`), the way `enable()` patches
   one? `trap_threads(from, to, len, run_fn)` is documented for one `[from, to)`
   range; the multi-target reuse is feasible-from-the-primitive but NOT yet
   observed end-to-end. Probe: a `comp-NN` fixture creates N>1 hooks with
   `StartDisabled`, patches all N in one window, confirms all N fire. Outcome map:
   all fire → the batch path proceeds; any miss / instability → fall back to
   per-hook `enable()` (unbatched but correct). **The §4.5 batch path is
   provisional on this probe.**
8. **`InstallRuntime`'s caller set (gates the `g_installed` removal, §4.6).** Is
   `InstallRuntime`'s ONLY caller the chain's first-hook-per-target path (which
   `FindChain` already gates against a double-install), or does a non-chain caller
   exist that `g_installed` was guarding? A checkable fact — grep the
   `InstallRuntime` call sites before removing the map. Outcome map: chain-only →
   remove `g_installed` (the chain's `FindChain` is the sole gate); a non-chain
   caller exists → that caller's double-install guard is surfaced and re-homed
   before the map is removed, never dropped silently.

---

## §10 Out of scope (deferred, reserved)

- **Removing MinHook entirely.** MinHook is PERMANENT for `early_hook` (loader
  lock) and the `HookedUpdate` bootstrap pump (and likely the frealloc canary).
  "Complete replacement" is impossible on the merits (§7 — safetyhook's
  unconditional thread-suspend is unsafe on those paths), not deferred for effort.
- **Author-configurable foreign-hook policy.** v1 chains onto a foreign hook
  always (§6.4). A policy model (chain / warn-and-take / refuse-and-yield, per
  target, author- or engine-set) is reserved — reopens only if a target where
  chaining is unsafe surfaces.
- **Foreign mod that unhooks mid-session, or installs AFTER kcdx (§6.3).** v1
  coexistence covers a foreign hook present at kcdx's install time, living for the
  session. The unhook-later and install-later cases are documented limitations,
  not v1 guarantees.
- **A third backend.** The `IDetourBackend` interface makes one possible
  (PolyHook2, a custom patcher); none is planned. The interface is the
  future-proofing, not a commitment to add one.

---

## §11 Decision record (what was settled, and what lost)

| Concern | Pick | Rejected — why |
|---|---|---|
| Core architecture | **Backend abstraction at `InstallRuntime` (the install chokepoint); `detour_hook` dissolves** | *Backend behind `detour_hook`* — the original pick, FALSIFIED this re-grounding: `detour_hook` is only the JIT slot owner (its `enable()`/`disable()` are dead on the chain path); the real install chokepoint every chain hook funnels through is `InstallRuntime` → `MH_CreateHook`. A seam behind `detour_hook` would not be on the install path at all. *safetyhook-primary-with-MinHook-fallback* — the auto-selection is the same predicate either way, but framing safetyhook as "primary" understates that MinHook is mandatory-not-fallback on two paths. *Per-capability hard-wire, no interface* — two hooking idioms with no unifying seam; a future change touches both. |
| Backend seam home | **`InstallRuntime` — the backend dispatches there; the backend owns its slot; `detour_hook` is removed** | *Keep `detour_hook` as the adapter* — keeps a husk whose only job (own the JIT slot) the backend now does, leaving two layers where one suffices and the slot owned in the wrong place. *Relocate the step-3 interface unchanged* is exactly the pick — `IDetourBackend`/`MinHookBackend` are correct as built, only their attachment point moves. |
| Conflict model | **One model — the chain's `FindChain`/`CanCoexist`; `g_installed` removed** | *Port `g_installed` onto the backend seam* — carries a redundant first-wins map forward; the chain's `FindChain` already prevents the double-install the map guarded, so two conflict models for one concern (the drift this re-grounding exposed). Removed, pending the caller-set check (§9.8). |
| Foreign-hook priority | **Core v1 pillar (built up front, §6), driven by the multiplayer/extreme-mod lens** | *Final-phase polish* — the original placement; understates that the extreme-mod consumer (multiplayer, heavy TC load orders) hooks the same functions other mods hook, so foreign-detour chaining is existential for that consumer, not an edge case. Policy unchanged (chain-always); only the priority/placement elevated. |
| Batch install | **kcdx-authored over safetyhook's primitives (`StartDisabled` + `trap_threads`), designed now** | *Per-hook `enable()` only* — N stop-the-world suspend cycles at TC/multiplayer scale; the unbatched baseline, kept as the §9.7 fallback if the batch probe fails, not as the v1 target. *Wait for a safetyhook batch primitive* — none exists (verified); building over the exposed primitives is the only path, and it is constructible. Provisional on the §9.7 multi-target-window probe. |
| Mid-hook scope | **Full `make_jit_midfunc` replacement with `safetyhook::MidHook`** | *Keep `make_jit_midfunc`, safetyhook function-entry only* — leaves the most fragile code (the cap-04 scar tissue) in place, forgoes the single biggest maintenance win. *Decide after a spike* — folded IN as the §9 gate rather than rejected: the replacement is the decision, the spike PROVES it before landing (not a separate decision). |
| Conflict / coexistence | **Add foreign-hook detection + chaining on top** | *Keep the chain as-is, safetyhook is just the patcher* — forgoes the cross-mod coexistence the thread-safe patch enables; tkhquang's actual concern goes unaddressed. (The chain IS kept as-is for kcdx-vs-kcdx; foreign-detection is added above it, not instead of it.) |
| Foreign-hook policy | **Chain onto it (follow the foreign jmp)** | *Detect + warn, install anyway* — doesn't solve coexistence, one mod still loses. *Detect + configurable policy* — most flexible but most surface; reserved (§10) for if an unsafe-to-chain target surfaces. |
| Backend routing | **Install-context-driven (automatic)** | *Explicit per-call-site* — folded into §9.5 as the likely MECHANISM the predicate uses (explicit engine-internal backend arg), but the routing DECISION is context-driven by table, not author-visible. No author knob (cornerstone #1). |
| Mid-hook proof | **Mark provisional, gate the rewrite on the §9 spike** | *Treat as settled, build directly* — ships the most fragile rewrite on an unobserved runtime-mechanism claim (the AP10 / `results-driven.md` disguise); if `ctx.rip` can't do Auto, it surfaces mid-rewrite instead of at a gate. |

---

## Pointer for the prior assessment

The senior-architect consult thread that developed this (the MinHook-vs-safetyhook
gap analysis, the loader-lock investigation, the cap-04/cap-21/cap-22 mapping) is
the reasoning behind these decisions; this doc is the settled output.
