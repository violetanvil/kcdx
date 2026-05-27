# Phase 3 sub-1 (extended) — kcdxHookInterface + Kcdx.h wrapper + Uninstall

**Status:** 4 of 8 steps committed and live-verified. Steps 5–8 paused
pending user approval of the revised step decomposition (§3 below).

A fresh agent picks this up by reading ONLY this file plus the cited
commits.

---

## 1. As-landed state (steps 1–4)

| Step | Commit | Scope |
|---|---|---|
| 1 | `abbb4c6` | Engine: `hook_chain::Uninstall(handleId)` + `Status::Removed` + `Entry::handleId` + `lua_registry::SetStatus` writer + `Add()` signature gains handleId. Mark-removed-only semantics (Option A): the MinHook detour + JIT trampoline stay session-lifetime; the existing `chain.entries.empty()` guards at DispatchPre:749 + DispatchPost:809 + MidDispatch's NOREF guard at :885 make a drained chain a no-op shim, so the original runs unaltered. Avoids a use-after-free against in-flight dispatchers (which release `g_chainsMu` before `lua_pcall`). |
| 2 | `0c0756a` | Lua: `H_uninstall` metatable method on the hook handle + `docs/lua/hook.md` row. `Kind::Hook` routes through chain Uninstall + SetStatus(Removed); non-Hook kinds (today only `kcdx.bytes`) raise a teaching `luaL_error` (`"not yet supported for kcdx.bytes handles"`) — the patch engine has no revert path, so silently flipping status would be AP13. |
| 3 | `d03ffb1` | `test-plugins/cap-35-uninstall/` pure-Lua regression — 5 falsifiable rows: basic / idempotent / tostring / chain-survives / bytes-error. Matrix flipped to LIVE in `5c88f2b` after the live run. |
| 4 | `e8fe6ee` | C++ ABI: `kcdxHookInterface` v1 in `include/kcdx/Interfaces.h` with **six sub-verb method pointers** (Before / After / Around / Replace / Mid / Callsite) + 4 query methods (IsApplied / GetReason / GetName / Uninstall). `kcdxHookOptions` stripped of mode + callback + addressName + callsiteScope (variant is the method name; target + callback positional; rule-4a-compliant). Header doc-comment hedges Kcdx.h as not-yet-built. `docs/cpp/hook.md` got a WIP banner pointing readers at the v1 shape. |

**Live verification:** suite at `77/85` on 2026-05-24 17:57 (cap-35 added 5
PASS rows to the prior 72/80 baseline, zero regression on existing hook
rows). Sole pre-existing FAIL: CAP-20-target-nosig (parallel-chat-related,
unrelated to this feature).

**Live engine surface today:**

- Lua `handle:uninstall()` works on `kcdx.hook` handles. Idempotent at
  every layer; returns self for chaining; `:applied()` reads false
  after; `tostring(h)` shows `status=removed`.
- `kcdx.bytes` handles RAISE a teaching `luaL_error` on `:uninstall()`.
- `kcdxHookInterface` v1 ABI present in the header **but no thunks
  wired yet** — `api->QueryInterface(kcdxInterface_Hook,
  kcdxHookInterface_Version)` returns nullptr today. Step 5 wires
  `src/interfaces.cpp::QueryInterface` + the 10 thunks.

---

## 2. Step-4 redesign trigger

Between step 3 and step 4, the parallel chat amended
`.claude/rules/lua-api-surface.md` with rule 4a:

> Discrete behavioral variants are sub-verbs, not table keys. No verb
> in the current or planned surface fits the [mode-as-key] exception.

The originally-planned step 4 (one `Install(opts)` method with
`kcdxHookMode_*` enum on opts) directly violated rule 4a. The shape
that landed in `e8fe6ee` — six sub-verb method pointers — is the
rule-4a-compliant rework. The user approved the sub-verb direction
before step 4 dispatched.

The ripple effects of that direction on steps 5–8 are captured in §3
below and need user approval before further dispatch.

---

## 3. Revised plan for steps 5–8 (pending user approval)

### Step 5 — `src/hook_interface.cpp` thunks + `HookPayload::offThread` plumbing

**New file** `src/hook_interface.cpp` containing 10 thunk functions
(one per `kcdxHookInterface` method pointer). Each sub-verb thunk
builds a `hook_payload::HookPayload` with `mode` set to the matching
enum value, threads opts → payload, calls `hook_chain::Add(... handleId)`,
returns the handleId as `kcdxHookHandle`. Query thunks: `IsApplied`
reads `lua_registry::Find(h)->status.load()`; `GetReason` / `GetName`
read stored strings; `Uninstall` calls `hook_chain::Uninstall(h)` +
`lua_registry::SetStatus(h, Removed)` (mirrors `H_uninstall`'s Hook
branch).

Wire `src/interfaces.cpp::QueryInterface` to return the
`kcdxHookInterface` instance when `interfaceID == kcdxInterface_Hook`
and version matches.

Plumb `HookPayload::offThread` — add the `offThread` field to
`src/hook_payload.h` (enum mirroring `kcdxHookOffThread`); thread from
the C++ thunks AND from `src/lua_bind_hook.cpp` (Lua surface already
parses `off_thread = "marshal"/"skip"/"error"`); consume in
`src/hook_chain.cpp`'s dispatch off-thread-routing path. Verify the
as-built dispatch code before designing the wire-up.

**Scope:** `src/hook_interface.cpp` (new), `src/interfaces.cpp`,
`src/hook_payload.h`, `src/lua_bind_hook.cpp`, `src/hook_chain.cpp`,
`CMakeLists.txt`. ~6 files.

**Test:** exercised at step 7.

### Step 6 — `include/kcdx/Kcdx.h` wrapper + empowered helpers

**New file** `include/kcdx/Kcdx.h`. Header-only `struct Kcdx`.
`Init(api, author, plugin)` fetches every shipped sub-interface
(logger, memory, addr, scripting, task, serialization, console, hook);
stamps author + plugin for identity threading; returns bool. Fields
are interface pointers: `K.hook` (`kcdxHookInterface*`), `K.memory`,
`K.addr`, `K.code`, `K.console`, `K.test`, `K.log`, `K.api` (raw
`kcdxInterface*` for floor-4 drop-down).

Templated helpers in `kcdx::hook` namespace, one per sub-verb:

```cpp
namespace kcdx::hook {
    template<typename Sig> void Before  (kcdxHookInterface*, const char* target, Sig* fn);
    template<typename Sig> void After   (...);
    template<typename Sig> void Around  (...);
    template<typename Sig> void Replace (...);
    // Mid + Callsite intentionally NOT in the empowered helpers —
    // they take captures / sub-locators per AP12 expert framing.

    template<typename Sig> kcdxHookHandle TryBefore (...);
    // ... matching Try* variants.
}
```

`Before / After / Around / Replace` return void and auto-log on
failure. `Try*` variants return handle for programmatic-branch cases.

**Open: floor-3 fate.** The locked plan had
`kcdx::hook::InstallRawUnchecked<Sig>(hook, opts, fn)` as the third
floor (bypass templated sig check, still use the wrapper). Under
sub-verbs the "one Install to bypass" doesn't exist. Three options:
1. Six unchecked variants (`Before<Sig>Unchecked` etc.) — symmetric,
   lots of typing.
2. **Drop floor 3 entirely.** The author who wants opt-out drops to
   floor 4 (`K.hook->Before(target, callback, opts)`) — `void*
   callback` is unchecked by construction. Simpler.
3. One generic `InstallRawUnchecked<Sig>(method_ptr, args...)` —
   clever but obscure.

Lean: option 2.

**Scope:** `include/kcdx/Kcdx.h` (new). 1 file.

### Step 7 — C++ DLL test plugin `cap-NN-cpp-hook-interface`

C++ DLL plugin exercising kcdxHookInterface end-to-end.

**Open: row breakdown.** Three options:
1. 6 sub-verb rows — one per Before/After/Around/Replace/Mid/Callsite.
2. **4 sub-verb rows + 1 Uninstall row + 1 raw-floor row** — exercises
   the everyday verbs (Before/After/Around/Replace; Mid/Callsite are
   expert and covered Lua-side by cap-21/cap-22), one C++ Uninstall
   (peer of cap-35-uninstall-basic), one row demonstrating floor 4
   (raw `api->QueryInterface` bypass of Kcdx.h). 6 rows total.
3. 2 rows: smoke + comprehensive — compact, loses per-row
   diagnosability.

Lean: option 2.

Plugin shape: `test-plugins/cap-NN-cpp-hook-interface/` (next free cap
ID verified at step-7 dispatch — parallel chat may have landed more
cap rows). DLL with `kcdxPlugin_Load` export. Uses `Kcdx.h` for the
empowered floor; one row uses raw `api->QueryInterface` to prove
floor 4 works without the wrapper.

**Scope:** `test-plugins/cap-NN-cpp-hook-interface/{CMakeLists.txt,
cap-NN.cpp, kcdx.toml}` (new); `test-plugins/README.md`.

### Step 8 — Docs sweep + restructure-plan ledger close + verification-checkpoint

Per the per-feature doc gate (locked earlier this feature), the full
doc rewrite lands here.

**`docs/cpp/hook.md`** — strip step 4's WIP banner; rewrite as 6
sub-verb sections; cite rule 4a; show empowered + raw-opts + raw-
interface examples for the COMMON Before/After/Around/Replace path.

**`docs/cpp/wrapper.md`** (new) — `Kcdx.h` reference. The floor model
documented (3 floors if option 2 picked in step 6, 4 otherwise).

**`docs/cpp/index.md`** — `hook.md` flipped NYI→LIVE in the map;
`wrapper.md` added.

**`docs/outstanding-work/restructure-plan.md`** — Phase 3 sub-1 ledger
row + commits; cite this doc as the design-history source.

**Verification-checkpoint** — one game launch confirms cap-NN-cpp-hook-
interface rows PASS; cap-35 still 5/5; no regression on existing hook
rows.

**Scope:** `docs/cpp/hook.md`, `docs/cpp/wrapper.md` (new),
`docs/cpp/index.md`, `docs/outstanding-work/restructure-plan.md`,
`test-plugins/README.md`.

---

## 4. Decisions needed before step 5 dispatch

1. Approve or refine §3 (revised steps 5–8 above).
2. Floor-3 fate (step 6) — lean: drop it (floor 4 IS the unchecked form).
3. Test row breakdown (step 7) — lean: option 2 (4 sub-verb + 1
   Uninstall + 1 raw-floor).
4. Confirm the Lua `docs/lua/hook.md` rewrite under rule 4a is NOT this
   feature's scope (it belongs to a separate cycle — today's
   `docs/lua/hook.md` still describes the old `kcdx.hook{...}` table
   shape).

---

## 5. Parallel chat's working tree (do not stage)

The parallel chat has uncommitted edits to these paths. Stage by
specific path per `concurrency-git.md` rule 2 — never `-A`:

- `.claude/rules/lua-api-surface.md` (the rule 4 + 4a amendment)
- `.claude/rules/lua-callback-threading.md`
- `CLAUDE.md`
- `docs/design.md`
- `docs/outstanding-work/restructure-plan.md` (Phase 3 + Phase 8.5
  prose; line 1234 references the sub-verb migration as Phase 8.5
  work, predating step 4's redesign — that mention is now stale)
- `src/lua_lifecycle.h`
- `docs/outstanding-work/parallel-ghidra-research.md` (untracked)
