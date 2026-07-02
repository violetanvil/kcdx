# Batch-install cost — does a SAFE batch save anything in the VEH safetyhook build?

**Kind:** durable process-output (a finding a future build/design decision reuses).
**Trust:** primary evidence — every cost claim cites the vendored source read this session.
**Question (the user''s, before dropping the §4.5 batch):** the §4.5 premise (safetyhook''s
`enable()` does a stop-the-world thread-suspend per hook, amortized by batching) was
FALSIFIED — this build''s `trap_threads` is VEH-based (Vectored Exception Handler +
`VirtualProtect`), not `SuspendThread`. So: is there a REAL batch saving on the ACTUAL
per-`enable()` cost in this build, and can ANY of it be batched SAFELY?

This is a finding, NOT a decision. The user decides build-vs-drop.

---

## BOTTOM LINE

**No meaningful safe batch saving exists on the safetyhook path. Drop §4.5''s safetyhook batch.**

1. The §4.5 premise is dead: `enable()` does NO thread-suspend in this build. The thing batching
   was meant to amortize (N stop-the-world cycles) does not exist on the safetyhook path. The
   expensive VEH install is a process-global one-time cost already.
2. The residual per-`enable()` cost is **4 `VirtualProtect` syscalls + 3 `VirtualQuery` + a small
   map insert + a tiny prologue write**, all cheap. At N=hundreds it is single-digit milliseconds
   total, **once, at boot**. Batching saves a fraction of that — premature.
3. The ONE safe collapse the architect did not fully explore — **register-all-N-traps-first, then
   patch all N** — IS safe in principle (it closes the AV the naive one-trap construction hits),
   BUT it saves almost nothing: the dominant cost (`VirtualProtect` on N scattered game prologues)
   does not coalesce, because the N targets are scattered game VAs on different pages. The
   construction is correct but the saving is ~0 (and it widens the mid-prologue residual window).
4. **MinHook is the opposite** — its `enable()` DOES `SuspendThread` every thread, and
   `MH_ApplyQueued` collapses N freezes into ONE. That batch saving is REAL and large per hook.
   BUT MinHook''s N is tiny here (loader-lock/bootstrap paths — ~1-4 hooks), so even a real
   MinHook batch saving does not matter at that N (and these install under the loader lock where
   freezing is the very hazard MinHook is there to avoid).

Net: realistic-N x per-hook-cost is small on both backends. Safetyhook saving is near-zero AND
premature; MinHook saving is real-per-hook but applies to N~1-4. Sequential `enable()` (the §9.7
fallback "unbatched baseline") is correct and the right choice.

---

## 1. The exact per-`enable()` cost (SOURCE-cited)

`InlineHook::enable()` does exactly ONE wire op: one `trap_threads(...)` call with a closure that
writes the prologue jump. SOURCE: vendor/safetyhook/src/inline_hook.cpp:373-412.

Inside one `trap_threads(from,to,len,run_fn)` (SOURCE: vendor/safetyhook/src/os.windows.cpp:268-318),
per `enable()`:

| Operation | Count | SOURCE | Weight |
|---|---|---|---|
| VirtualQuery | 3 (find_me, from, to) | os.windows.cpp:273-275 | cheap |
| add_trap map insert | 1 (std::map insert_or_assign keyed by from) | os.windows.cpp:300,213-224 | cheap |
| VirtualProtect | 4 (from->RW, to->RW, to->restore, from->restore) | os.windows.cpp:309,310,316,317 | DOMINANT (4 syscalls) |
| run_fn prologue write | 1 (emit_jmp_e9/ff, <=14-byte store) | inline_hook.cpp:383-403,85-98 | tiny |
| VEH install | 0 per hook (installed ONCE, lazily, first trap) | os.windows.cpp:293-298,173 | one-time, ~0 |
| SuspendThread / freeze | 0 — NONE | (absent) | the §4.5 premise — does not exist |
| mutex acquires | 2 (TrapManager::mutex, virtual_protect_mutex) | os.windows.cpp:294,304 | cheap, uncontended at boot |

**Dominant at N=hundreds:** the 4 `VirtualProtect` syscalls per hook. Everything else is
sub-microsecond. Per `enable()` ~= low single-digit µs.

### 1a. VEH install is process-global + one-time (NOT per-hook)
The VEH (`AddVectoredExceptionHandler`) lives in the `TrapManager` ctor, and `TrapManager` is a
lazily-built singleton: the `instance == nullptr` guard means it is constructed only on the FIRST
`trap_threads`. SOURCE: os.windows.cpp:296-298 (guard), :173 (ctor calls AddVectoredExceptionHandler).
Hook #2..#N reuse it. There is no per-hook stop-the-world to amortize.

### 1b. How VEH replaces suspend (why no batch is needed for safety)
`trap_threads` registers a trap `[from,from+len)` + a `[to,to+len)` page record (os.windows.cpp:213-224),
protects both to RW, writes the prologue, restores. A thread executing in the prologue page during
the window faults; `trap_handler` (os.windows.cpp:230-256) catches the AV and `fix_ip`
(os.windows.cpp:320-338) rewrites that thread''s RIP old-byte -> trampoline-byte. Suspend-free.

---

## 2. SAFE partial OR all-traps-first batch? (the crux)

### 2a. VirtualProtect side — coalesce? (the only real saving candidate)
The 4 protects hit two ranges: the target prologue (`from` — a scattered game VA) and the
trampoline (`to` — kcdx-allocated).
- **Target prologues do NOT coalesce.** N independent game entry points on different 4KiB pages;
  VirtualProtect is per `[addr,len)` run; N scattered single-prologue runs cannot merge. SOURCE:
  N `m_target` values, one per InlineHook (inline_hook.cpp:178); trap_threads protects exactly
  `from,len` (os.windows.cpp:309). **This dominant cost is irreducibly per-hook.**
- **Trampolines near-contiguous but it does not help.** The allocator carves N trampolines off
  shared Memory blocks via a freelist (SOURCE: vendor/safetyhook/src/allocator.cpp:85-134), so
  their `to`-side protects could coalesce — but that is only 2 of 4 syscalls, a page already RWX
  needs no re-protect, and coalescing the cheap side while the scattered-target side stays per-hook
  saves a negligible fraction.

So the side that dominates (scattered game prologues) is exactly the side that cannot coalesce.
No meaningful partial batch.

### 2b. All-traps-first construction — IS it safe? (what the architect didn''t fully explore)
The architect ruled out the NAIVE collapse (one outer trap_threads over N, write all N inside) as
an AV: only target-0''s trap is registered, so a thread faulting in target-K>0''s now-RW page hits
`find_trap == nullptr` -> `find_trap_page` null too -> EXCEPTION_CONTINUE_SEARCH -> crash
(os.windows.cpp:239-247). Correct: the naive construction is unsafe.

**The construction the architect did not fully explore:** `add_trap` ALL N targets FIRST (every
trap in the shared map), then protect all N pages, write all N, restore all N. `find_trap` does
`std::find_if` over the ENTIRE m_traps map (os.windows.cpp:181-191) — not scoped to one hook. So
with all N registered first, a thread faulting in ANY of the N pages finds ITS trap and `fix_ip`
(os.windows.cpp:251-253) relocates it. **This closes the exact AV the architect found** — its
defect was incomplete registration, not a fundamental flaw.

**Residual-window caveat (real but narrow):** `fix_ip` rewrites only a thread whose RIP == old_ip
EXACTLY (os.windows.cpp:329). A thread already MID-prologue (RIP partway through a multi-byte
original instruction being overwritten) is NOT handled. **This hazard exists for the single-hook
`enable()` too** — it is the standing residual risk of any in-place prologue patch, not introduced
by batching. Batching N WIDENS the window (open across N writes vs one), raising the probability a
thread is caught mid-prologue on SOME target, but adds no new hazard class.

**Crux conclusion:** an all-traps-first batch IS achievable and DOES close the architect''s AV —
not "fundamentally unsafe." But it (i) saves ~0 (scattered-prologue protects stay per-hook) and
(ii) widens the mid-prologue window across N. It trades a small real safety-window increase for a
near-zero throughput gain. Not worth it.

---

## 3. Realistic N — and does it matter?

**Install path:** `ApplyZone` (SOURCE: src/lua_registry.cpp:435-565) walks the sorted pending
Bytes/Hook entries for a zone and calls the per-kind apply handler per entry; the Hook handler
funnels (per target''s first hook) through hook_chain -> hook_engine::InstallRuntime (SOURCE:
src/hook_chain.cpp:2355,2784; src/hook_engine.cpp:54) -> one backend `enable()`. So N = distinct
function-entry hook TARGETS at a zone (additional hooks on one target join the chain, NO new detour).

**Realistic N:** the suite is 89 cap-*/comp-* plugins (test-plugins/, counted this session). Only a
SUBSET install a function-entry detour (many are bytes-patches, Lua-callback/event/console/cosave/
asset/scan tests, or mid-hooks). **Mid-hooks do NOT route through InstallRuntime/enable() at all**
(SOURCE: src/hook_engine.h:210-214; src/hook_chain.cpp:2484 — the safetyhook::MidHook adapter is a
dedicated path). A heavy real load order (TC + several mods) is the design''s stated "hundreds"
worst case (design §4.5 / glossary). Take it at face value: **N ~= a few hundred at boot, worst case.**

**Matter?** N=500 x ~4 VirtualProtect x ~1µs ~= ~2 ms protect time + bookkeeping = **single-digit
ms, ONCE, during a multi-second boot** that already loads gigabytes. No per-frame/repeated cost;
`enable()` runs at install only ("no FreeLibrary, no teardown" — design §6.3). A batch shaves a
fraction of those ms (and per §2 cannot shave the dominant part). **Textbook premature optimization**
at a one-time boot event — complexity + a widened mid-prologue window for a sub-perceptible saving.

---

## 4. MinHook side — real saving, tiny N

MinHook''s `enable()` is the OPPOSITE: it stops the world. `MH_EnableHook` -> `EnableHook` ->
`Freeze` -> EnumerateThreads + `SuspendThread` per thread (SOURCE: vendor/minhook/src/hook.c:722-769,
328-366, :348), patch, `Unfreeze` (ResumeThread, :369-390). One enable = one full freeze cycle.

`MH_ApplyQueued` (SOURCE: hook.c:834-881) does ONE Freeze(ALL_HOOKS)/Unfreeze around the whole
queued set — N hooks in ONE freeze window. So MinHook''s batch (MH_QueueEnableHook + MH_ApplyQueued,
MinHook.h:168,178) is **REAL and LARGE per hook** — the genuine version of what §4.5 thought
safetyhook did.

**But MinHook''s N is tiny here.** Per design §4.2, MinHook serves ONLY loader-lock + bootstrap:
`early_hook`, `HookedUpdate`, the frealloc canary — **~1-4 hooks** (SOURCE: design §4.2 table, §10).
At N~1-4 a large per-hook saving collapses to "save 1-3 freeze cycles, once" — and these install
under the loader lock where suspending threads is the very hazard MinHook is there to avoid. Real
per-hook, irrelevant at this N.

---

## 5. Evidence summary for the build-vs-drop decision

- safetyhook enable() suspends threads? **NO** — 0 SuspendThread/ResumeThread/Thread32First/OpenThread
  in the whole safetyhook tree (grep, 0 hits); VEH + VirtualProtect only (os.windows.cpp:268-318).
- per-enable cost: 4 VirtualProtect (dominant) + 3 VirtualQuery + 1 map insert + <=14-byte write +
  2 uncontended mutexes; VEH one-time process-global (inline_hook.cpp:373-412; os.windows.cpp:296-298).
- safe partial batch saves a meaningful fraction? **No** — dominant scattered-prologue protects do
  not coalesce; only the cheap trampoline side could (allocator.cpp:85-134; os.windows.cpp:309).
- all-traps-first collapse safe? **Yes, closes the architect''s AV** (find_trap scans the whole map;
  register all N first) — but saves ~0 and widens the mid-prologue window (os.windows.cpp:181-191,
  239-256, 329).
- realistic N (safetyhook): few hundred function-entry hooks, worst case; mids excluded
  (lua_registry.cpp:435-565; hook_engine.h:210-214; 89 plugins).
- matter at that N? **No** — single-digit ms once at boot; premature.
- MinHook batch real? **Yes, large per hook** (MH_ApplyQueued = 1 freeze for N; hook.c:834-881).
- MinHook N? **~1-4** (loader-lock/bootstrap only; design §4.2, §10) — saving immaterial.

**Recommendation (input, not decision):** drop §4.5''s safetyhook batch; use the §9.7 fallback
(sequential per-hook enable() — correct, and the architect''s only verified-safe multi-target
construction). The all-traps-first collapse is possible and safe but buys a sub-ms fraction of a
one-time boot cost at the price of a wider mid-prologue window — not worth it. MinHook batch is
genuinely beneficial per hook but its N (~1-4 loader-lock hooks) makes it immaterial, and the
loader-lock context argues against freezing anyway. Net: no meaningful safe batch saving on either
path at realistic N; sequential install is the right design. The user decides.

---

## Wiring (reproducibility)

- safetyhook trap mechanism: vendor/safetyhook/src/os.windows.cpp:157-338.
- safetyhook enable/setup/trampoline: vendor/safetyhook/src/inline_hook.cpp:117-440.
- safetyhook allocator (trampoline contiguity): vendor/safetyhook/src/allocator.cpp:70-266.
- MinHook freeze/queue/apply: vendor/minhook/src/hook.c:328-390, 393-477, 722-881.
- MinHook queue API: vendor/minhook/include/MinHook.h:163-178.
- kcdx install funnel: src/lua_registry.cpp:435-565; src/hook_chain.cpp:2355,2784;
  src/hook_engine.cpp:54; src/hook_engine.h:210-214 (mid excluded).
- Verification grep: SuspendThread|ResumeThread|Thread32First|OpenThread over vendor/safetyhook/
  -> 0 matches; same tokens present throughout vendor/minhook/src/hook.c.
