# Detour-backend layer

The byte-patcher beneath every hook. kcdx installs detours through **two**
interchangeable engines — **MinHook** and **safetyhook** — sitting behind one
uniform interface (`IDetourBackend`) at a single install chokepoint
(`hook_engine::InstallRuntime`). The engine picks the backend automatically from
the install context; a plugin author never names one. The hook chain, the
conflict model, and the Lua/ABI marshaling all sit UNCHANGED above this layer —
they do not know which engine patched the bytes underneath.

This doc is the HOW-IT-WORKS reference for that layer cover-to-cover: the
interface contract, the install seam, the two backends, the routing rule, the
mid-hook path, and foreign-hook chaining. Read it to understand the layer; read
the source for the exact bytes.

**Why two backends, in one sentence:** there is no single best detour engine for
kcdx — safetyhook is strictly better for the bulk (thread-safe install,
far-target reach, a vetted mid-hook primitive, typed errors), but a few install
paths run under the Windows loader lock where a plain byte-write is the
conservative choice and safetyhook's mechanism is unverified, so MinHook stays
permanent on those paths.

---

## The `IDetourBackend` contract

`IDetourBackend` (`src/detour_backend.h`) is the core-layer interface every
concrete backend implements. It depends on nothing kcdx-specific; both backends
depend on it; it depends on neither of them. Its one responsibility is the
install / uninstall + relocated-original surface a detour engine must expose.

Four methods:

| Method | What it does |
|---|---|
| `set_instance(name, target, detour)` | Configure the detour (name + target VA + replacement). Does NOT install. |
| `enable()` | Install — create the trampoline + write the prologue jump. Idempotent (a repeated call no-ops). |
| `disable()` | Uninstall — restore the prologue. |
| `get_original()` | Return `void**` — a pointer to the stable slot holding the relocated-original entry. |

`enable()` is `create + enable` collapsed into one call: it allocates the
trampoline (the relocated original bytes), then writes the prologue jump that
diverts the target to the detour. A repeated `enable()` no-ops; a failed install
leaves the original-slot value null, which is the failure signal the caller
checks.

### The `get_original()` guarantee — and who owns the slot

`get_original()` returns a stable, callable pointer to the relocated-original
entry point, valid for the hook's lifetime. This is the value the backend
PRODUCES: the address you call to run the original (un-hooked) function. MinHook
returns its `pOriginal`; safetyhook returns its `InlineHook`'s trampoline entry.
Both are directly callable with the original ABI from the JIT-generated
call-original thunk.

The load-bearing distinction: **the backend populates the value; it does NOT own
the slot storage.** The call-original slot is a `void**` that the JIT'd asm
dereferences to reach the original. That slot lives on `runtime_func_t` (it bakes
the slot's stable address into its generated code at the call-original deref, the
around path, and the mid-resume site). `InstallRuntime` reads `*get_original()`
out of the backend once, after `enable()`, and copies that value into the
`runtime_func_t`-owned slot. The backend object itself only holds the storage for
its OWN relocated-original pointer (the thing `get_original()` returns the address
of); it never owns the JIT slot the trampoline reads.

Why the slot can't live in a backend: a **callsite** hook installs no detour at
all — it rewrites an `E8` call displacement and writes the callee VA straight
into the slot. That path has no backend object, yet still needs a valid slot. So
the slot must live on `runtime_func_t` independent of any install, and each
producer (a backend via `InstallRuntime`, the callsite path directly, or a
dynamic hook via `InstallRuntime`) writes into it. A backend-owned slot would
leave the callsite path with nowhere to write.

The backend object is non-copyable, non-movable, and leaked for the session —
kcdx never unhooks (the "no FreeLibrary, no teardown" model), so the object must
stay put for the hook's whole lifetime to keep its relocated-original slot valid.

### What sits unchanged above the backend

The chain (`hook_chain`), the conflict model (`CanCoexist`, one detour per
target, ordered mode-tagged callbacks), and the Lua/ABI marshaling (the per-slot
type JIT, the capture handles) do not change and do not know which engine patched
the bytes. The backend is "just the patcher" to everything above it: the chain
owns who reacts and in what order; the backend owns patching the bytes safely.

---

## The `InstallRuntime` seam

`hook_engine::InstallRuntime` (`src/hook_engine.{h,cpp}`) is the single install
chokepoint where the backend is selected and driven. Every chain hook's
first-detour-per-target install funnels through it; so does the non-chain
`dynamic_hook` caller. Its signature:

```
RuntimeInstallResult InstallRuntime(name, target_addr, detour_addr, InstallKind kind);
```

The caller passes an `InstallKind` (what the install IS), never a backend.
`InstallRuntime` maps the kind to a backend, constructs that backend, calls
`set_instance` + `enable()`, reads `*get_original()`, and returns the
relocated-original pointer (in `RuntimeInstallResult`) for the JIT slot.

### Why the seam is here, not behind `detour_hook`

The install used to be framed as living behind `detour_hook` — the former JIT
call-original slot owner. That was the wrong layer. `detour_hook` was ONLY the
slot owner: its own `enable()`/`disable()` were dead on the chain path, because
the chain's install called the byte-patcher directly and never went through
`detour_hook`'s install. The real chokepoint every chain hook funnels through is
`InstallRuntime` — it is the function that actually creates + enables the one
detour per target.

So the backend lands at `InstallRuntime`, and `detour_hook` dissolves: its only
job (own the JIT slot) moved onto `runtime_func_t`, leaving no separate adapter
layer between the chain and the patcher. The slot's storage is now a
`runtime_func_t` member; the backend merely populates it.

### One conflict model — and the unique guard that re-homed here

There are NOT two conflict models. The chain already owns conflict resolution:
`FindChain(target)` returns the one existing chain for a target, `CanCoexist` is
the sole predicate for whether a new hook joins it, and the first hook per target
installs the one detour while the rest append an ordered chain entry. Two
overlapping conflict models was the drift this layer removed.

`InstallRuntime` historically also consulted a separate first-wins map
(`g_installed`) to refuse a double-install per target. For the chain's callers
that map was **redundant** — `FindChain` already prevents the double-install — so
the redundant first-wins role was retired.

But the map was not purely redundant. `InstallRuntime` has two caller families:

- The chain's first-hook-per-target sites, all gated by `FindChain` (the map
  added nothing here).
- `kcdx.memory.dynamic_hook` — a NON-chain caller that does not consult
  `FindChain` and registers in a SEPARATE registry. `FindChain` structurally
  cannot see a `dynamic_hook` install; the two registries are disjoint.

For the non-chain caller, the map was the ONLY thing refusing a cross-registry
double-install on a shared target VA (a `dynamic_hook` colliding with a chain
hook, or two `dynamic_hooks` on one VA) — and refusing it loudly, naming the
first owner. So that unique guard **re-homed INTO `InstallRuntime`**: a minimal
per-target installed-set owned at the seam, preserving the exact loud
owner-naming refusal the `dynamic_hook` caller depends on. The chain's
`FindChain` front-runs it for chain hooks (so it never fires redundantly there);
the seam guard only ever actually fires for the disjoint cross-registry collision
`FindChain` cannot see. The redundant duplication retired; the load-bearing
cross-registry refusal stayed, moved to the seam.

---

## The two backends

### `MinHookBackend` — the loader-lock + bootstrap paths

`MinHookBackend` (`src/minhook_backend.{h,cpp}`) wraps MinHook
(`MH_CreateHook` / `MH_EnableHook` / `MH_DisableHook` / `MH_RemoveHook`). It
holds the `original_` slot MinHook populates with the trampoline pointer;
`get_original()` returns `&original_`, and `InstallRuntime` copies the value into
the JIT slot.

**When + why:** MinHook serves the install paths that run under the Windows
loader lock (`early_hook`, the LDR-callback-time installs) plus the per-frame
bootstrap pump (`HookedUpdate`). MinHook patches a prologue with a plain byte
write — a path with no known loader-lock hazard — which is exactly the
conservative property those paths need (see the routing section for the full
loader-lock reasoning). These are a handful of bootstrap hooks (roughly one to
four) that never needed safetyhook's gains, so MinHook is correct on them by
construction.

### `SafetyhookBackend` — the bulk

`SafetyhookBackend` (`src/safetyhook_backend.{h,cpp}`) wraps
`safetyhook::InlineHook`. It owns the live `InlineHook` for the session (kcdx
never unhooks), creates it disabled then enables it (mirroring MinHook's
create-then-enable split), and exposes the relocated-original via
`get_original()`.

**When + why:** safetyhook serves everything else — every plugin and
engine-stamped function-entry hook, the overwhelming majority of installs. It
brings three things MinHook's bespoke layer could not:

- **Thread-safe install.** safetyhook patches without stopping the world (see
  below — its mechanism is a Vectored Exception Handler plus `VirtualProtect`, NOT
  a thread suspend).
- **E9 → FF far-target reach.** `InlineHook` tries a 5-byte `E9` rel32 jump first
  and falls back to a 14-byte `FF25` absolute jump when the target is out of rel32
  range, so it reaches ANY 64-bit target. This closes the far-module callsite/hook
  gap natively — no per-module branch-pool special-casing is required for reach.
- **The mid-hook primitive (`MidHook`).** A vetted register-capture +
  call-original mechanism that replaces the project's most fragile hand-rolled
  asm (see "The mid-hook path").

`SafetyhookBackend` surfaces safetyhook's typed `InlineHook::Error` set as
specific reason strings in the log, so a failed install names the precise cause
(allocation failure, an undecodable/unrelocatable prologue, an out-of-range
relocated instruction, an unprotectable page, no room for the jump). On a foreign
target, the undecodable/unrelocatable-prologue errors are the
unrelocatable-foreign-shape case: kcdx fails the install LOUD and leaves the
foreign mod's hook intact (its prologue restored), never a silent mis-install or
a corrupted prologue (see "Foreign-hook detection + chaining").

After a successful install, `SafetyhookBackend` records its trampoline's address
range in the kcdx trampoline registry, so a later hook landing on the same target
is classified as already-in-a-kcdx-chain rather than as a foreign detour.

---

## The routing predicate

The backend is chosen by the engine from the **install context** — there is no
author knob. A caller declares its `InstallKind` (what the install is); the
engine maps the kind to a backend.

### The routing table

| Install context | Backend | Why |
|---|---|---|
| `early_hook` — loader-lock, during DLL-mapping LDR callbacks | **MinHook** | The conservative loader-lock choice (see below). |
| The `HookedUpdate` bootstrap pump — per-frame, drives the chain dispatcher | **MinHook** | Can't be a chain entry (it installs before the dispatcher it would chain into exists). The documented bootstrap exception. |
| `hook_chain` function-entry — every plugin + engine-stamped hook | **safetyhook** | Thread-safe install, far-target reach, typed errors. The bulk. |
| `hook_chain` mid-function | **safetyhook** | `safetyhook::MidHook` + register writeback (see "The mid-hook path"). |

In code, the routing decision lives in exactly one place: a `constexpr`
`select_backend(InstallKind)` mapping `ChainFunctionEntry → safetyhook` and
`DynamicHook → MinHook`. The two loader-lock/bootstrap paths (`early_hook`, the
update pump, the frealloc canary) install via raw `MH_CreateHook` and never reach
this seam at all — they are the documented bootstrap exceptions, structurally
outside the backend layer, so they carry no `InstallKind`. Mid-function hooks
also do NOT route through `InstallRuntime` (they install directly via the
`MidHook` adapter — see below).

### The impossible-misroute mechanism

The routing predicate is the one safety-critical mechanism in the layer: a path
that should be safetyhook silently misrouted to MinHook would forgo the
thread-safety gain. The design makes a wrong-backend choice **unwriteable** rather
than merely unlikely. A caller passes an `InstallKind`, never a `Backend` literal,
and `select_backend` is a `constexpr` total mapping — so the kind→backend
selection is a compile-time fact, verified by static assertions in the install
translation unit at zero runtime cost. There is no call site where a caller could
name the wrong backend, because a caller cannot name a backend at all. The default
arm of the mapping fails closed to MinHook (the loader-lock-safe choice).

### Why MinHook is permanent on the loader-lock paths

MinHook stays on `early_hook` because patching a prologue with a plain byte write
under the loader lock has no known hazard — it is the CONSERVATIVE choice, and
`early_hook` is a handful of bootstrap hooks that never needed safetyhook's gains,
so MinHook is correct there by construction.

This is NOT because "safetyhook deadlocks under the loader lock." safetyhook does
not suspend threads (see the next section) — there is no thread-suspend deadlock.
Rather, whether safetyhook's actual install mechanism (registering a Vectored
Exception Handler, calling `VirtualProtect` during DLL load, and tolerating a
fault in the VEH window while the loader lock is held) is SAFE under the Windows
loader lock is an **open, unverified question** — not a known deadlock, an
unprobed assumption. Because it is unverified, the conservative byte-write path
(MinHook) is kept, and a loader-lock install is never routed to safetyhook unless
and until that assumption is probed.

---

## The mid-hook path

A mid-function hook lands at an arbitrary offset inside a function (typically a
specific instruction whose effect the author wants to observe or override),
captures named registers / stack expressions, lets a callback read and mutate
them, and optionally skips the captured instruction. This path uses
`safetyhook::MidHook` through a dedicated adapter
(`src/safetyhook_midhook.{h,cpp}`) — NOT `IDetourBackend`, NOT `InstallRuntime`.

### Why the mid path is a dedicated adapter, not a backend

`safetyhook::MidHook` does not fit `IDetourBackend`: it owns its own install,
takes no external detour pointer, returns no trampoline-original for a JIT slot,
and its callback is a bare `void(Context64&)` with no userdata channel. So the
mid hook installs DIRECTLY from the chain's mid-add path, never through the
function-entry seam.

### The `Context64` adapter and the slot table

`Context64` is safetyhook's mid-hook callback parameter: every general-purpose
register and all 16 XMM registers, passed by value with writeback (a write to a
register field lands in the real register when the original resumes), plus `rip`
(which points at a trampoline of the replaced instruction) and a writable stack
pointer (`rsp` itself is read-only).

Because the callback is a bare `void(Context64&)` with no userdata, and `ctx.rip`
points at a trampoline rather than the target VA, neither a closure nor `ctx.rip`
can recover WHICH target fired. Identity is recovered by a **fixed pool of
compile-time C trampolines**, each baking its own slot index: a slot is claimed
at install, the trampoline at that index dispatches to a single adapter function
with its index, and the index looks up the slot table's bound target VA, capture
grammar, and resume address. This is zero runtime codegen — each trampoline is an
ordinary compiled function, the array is built at compile time. The pool is fixed
size; exhaustion fails loud (never a silent drop). A fire reads its slot by
const-reference with no lock and no allocation (the slot is bound once before the
patch goes live and never mutated after), keeping the fire path allocation-free.

On each fire the adapter reads every capture out of `Context64` into the 16-byte
payload the existing `MidDispatch` expects, runs `MidDispatch` UNCHANGED (the
off-thread filter, the engine carve-out, the re-entrancy tracking, the Lua/C
marshaling all stay), then writes any mutated captures back to `Context64` so an
author's `:set()` lands in the real register/memory when the original resumes.

### The three call-original modes via `ctx.rip`

The captured instruction's run-vs-skip decision maps onto `ctx.rip`:

| Mode | Behavior | Mechanism |
|---|---|---|
| **True** (run the original instruction — default) | The captured instruction always runs after the callback | Leave `ctx.rip` alone — safetyhook's trampoline re-runs the replaced instruction. |
| **False** (skip it — decided at build time) | The captured instruction never runs | Set `ctx.rip` to the resume address (the first clean byte past safetyhook's relocated region). |
| **Auto** (decide at runtime) | The callback decides per fire from its own state | The callback conditionally sets `ctx.rip` to the resume address. |

The resume address is `targetVa + the size of safetyhook's relocated region` —
read from the hook itself after create, never recomputed. The relocated region is
the captured instruction PLUS any following instructions safetyhook had to swallow
to fit its jump, rounded up to an instruction boundary; resuming there lands on
the first clean byte past the patch. Resuming at "the captured instruction's
length" alone would land INSIDE the jump bytes when that instruction is shorter
than the patch — a known scar this construction avoids. The slot's resume address
is set after create but before enable, so a fire can never read it unset.

---

## Foreign-hook detection + chaining

When kcdx installs a hook on a function another mod has already hooked, kcdx
detects the foreign hook, follows it, and chains onto it so BOTH mods' hooks fire
(`game → kcdx hook → foreign hook → real original`). This is a core capability of
the layer, not deferred polish: a heavy load order (a multiplayer mod, a total
conversion running alongside other mods) hooks exactly the functions other mods
hook — camera, input, networking, save, the update pump — and "kcdx silently wins
the prologue and the other mod's hook vanishes" is a failure there, not an edge
case. safetyhook's thread-safe, IP-fixing install is what makes patching a
prologue another mod is actively in safe to do at all.

### The prologue classifier

Before installing a function-entry hook, the classifier
(`src/foreign_hook_detect.{h,cpp}`) reads the target's first bytes and returns
one of:

- **Clean** — real game instructions, no prologue jump. Install normally. This is
  the overwhelming common case and the fast path (the first byte is not a
  jump-family opcode, so the classifier returns immediately).
- **KcdxTrampoline** — an `E9`/`FF25` jump whose target falls inside a range kcdx
  itself allocated. The target is already in a kcdx chain; the existing
  coexistence path (`CanCoexist` / append a chain entry) handles it.
- **Foreign** — an `E9`/`FF25` jump pointing OUTSIDE every kcdx-owned trampoline
  range. Another mod hooked this target first; chain onto it.
- **Unknown** — a prologue that begins with a jump opcode byte but did not decode
  into a recognized 5-byte `E9` or 14-byte `FF25` form (a truncated read, an
  unrecognized `FF` modrm, an `FF25` with a non-zero displacement). Conservative:
  NOT treated as foreign, NEVER chained, but surfaced and logged so an unrecognized
  shape is never silently mishandled. The caller installs normally.

The decoder recognizes two jump forms: a 5-byte `E9` rel32 near jump (target =
instruction VA + 5 + signed disp) and a 14-byte `FF25` rip-relative absolute jump
(`FF 25 00 00 00 00` then an 8-byte absolute target) — the exact shape safetyhook
writes for a far target. The discriminator between "kcdx trampoline" and "foreign"
is kcdx's OWN trampoline registry (`src/kcdx_trampoline_registry.h`), an
append-only session-lifetime record of every range kcdx allocated (a safetyhook
inline trampoline, a mid-hook trampoline, a branch-pool reservation). kcdx tracks
its own ranges because the underlying allocator does not expose an
"is-this-yours?" query — so "is this address kcdx-owned?" is answered from kcdx's
records, and a jump elsewhere is foreign.

### Chaining — follow the jump via IP-fixed relocation

On a Foreign verdict, the install proceeds through the normal `SafetyhookBackend`
path: safetyhook RELOCATES the foreign prologue jump into kcdx's trampoline
(IP-fixed), so kcdx's captured "original" runs the foreign mod's detour, which in
turn runs the real function. kcdx does not follow the foreign jump by hand — the
decoded foreign-detour VA in the log is informational only; safetyhook's
relocation captures the foreign detour correctly regardless. The result:
`game → kcdx hook → foreign hook → real original`, both mods' hooks firing.

### v1 scope boundaries

The chaining behavior is fixed:

- **Chain-always.** v1 always chains onto a detected foreign hook. There is no
  author-configurable policy (chain / warn-and-take / refuse-and-yield); a
  configurable policy is reserved for if a target where chaining is unsafe ever
  surfaces.
- **Load order is by install time.** kcdx installs over the foreign hook, so kcdx
  fires first then delegates down — the same install-time ordering the kcdx chain
  uses internally.
- **Foreign unhook-later and foreign-install-later are out of scope.** v1 covers a
  foreign hook present at kcdx's install time, living for the session (kcdx assumes
  the foreign mod follows the same no-teardown model). A foreign mod that unhooks
  mid-session leaves kcdx's captured "original" pointing at a restored-or-freed
  trampoline — a documented limitation. A foreign hook installed AFTER kcdx (the
  reverse case) cannot be detected at kcdx's install time — also a documented
  limitation, not a v1 guarantee.

---

## Why install is per-hook (no batch)

The layer installs each detour with its own `enable()`. There is NO batch-install
path — `IDetourBackend` has no batch entry point — and that is correct, not a
gap. This is recorded here as institutional memory so the next person who asks
"should we batch installs?" finds the answer instead of re-running the
investigation.

**The original batch premise was FALSE.** An earlier design specified a batch path
to amortize "hundreds of stop-the-world thread-suspend cycles at boot," on the
belief that safetyhook's `enable()` suspends all threads per hook. It does not.

**safetyhook's `enable()` does NOT suspend threads.** Its install mechanism is a
Vectored Exception Handler plus `VirtualProtect`, not a stop-the-world suspend: it
registers a per-target trap in a global map, `VirtualProtect`s the target +
trampoline pages, runs the patch under a mutex, and restores protection; a thread
that faults into a trapped page gets its instruction pointer fixed on demand by
the exception handler. There is zero thread suspension on the safetyhook path.

So there is no stop-the-world cost to amortize — per-hook install is the
measured-correct path. The per-`enable()` cost is a few `VirtualProtect` syscalls
plus a cheap map insert plus a small prologue write; at a realistic install count
(hundreds at a heavy boot) that totals single-digit milliseconds, once, during a
multi-second boot loading gigabytes. The only safe multi-target collapse would
save essentially nothing (the dominant `VirtualProtect`s are on scattered game
prologues that cannot coalesce) while WIDENING the mid-prologue residual safety
window across N targets — a net negative.

(MinHook's `enable()` DOES suspend threads per hook, and MinHook does offer a real
queued batch — but MinHook serves only the one-to-four loader-lock/bootstrap
hooks, so the saving is immaterial at that tiny N. Hence no batch on either path.)

Adding a batch API whose safetyhook implementation is just sequential `enable()`
would be an interface that overstates what it does. The interface stays honest:
install is per-hook.

---

## How to add a third backend

The `IDetourBackend` interface is the future-proofing — a third backend
(PolyHook2, a custom patcher) is a drop-in. None is planned; the interface exists
so adding one touches a bounded surface:

1. **Implement `IDetourBackend`** — the four methods (`set_instance`, `enable`,
   `disable`, `get_original`). `enable()` allocates the trampoline + writes the
   prologue jump; `get_original()` returns a stable `void**` whose pointed-to value
   is the callable relocated-original entry (the value `InstallRuntime` copies into
   the `runtime_func_t` slot); a failed install leaves that value null.
2. **Add a routing-table row** — a new `InstallKind` (or reuse an existing one) and
   its arm in `select_backend`, so the engine selects the new backend for the
   install context it serves. No call site names a backend, so nothing else
   changes.

The chain, the conflict model, the JIT call-original contract, and the Lua
marshaling are all untouched by a new backend — they sit above the layer and only
ever see the `runtime_func_t`-owned slot the backend populates.
