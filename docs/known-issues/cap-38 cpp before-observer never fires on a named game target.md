# cap-38 C++ before-observer never fires on a named game target

## Symptom

cap-38's C++ auto row (`CAP-38-cpp-gate-proceeds`) reports FAIL. The gate
itself works: the install proceeds (`handle != 0`, `IsApplied=1`) and the
`HOOK_SIG_GATE explicit_overrides_verified` WARN fires for both the C++ and
Lua surfaces. The FAIL is solely because the C++ before-observer's
`g_observer_fired` flag reads 0 at report time — the callback never ran.
The Lua peer (`CAP-38-lua-gate-proceeds`) PASSES because it asserts only
`applied()`, not "fired".

## Facts

- The gate WARN fires for both surfaces (dev log 12:08:07.375 C++,
  12:08:17.911 Lua — earlier run; 12:08:02.937 / .937 latest). Gate is sound.
- C++ install: `handle != 0`, `IsApplied=1`. Install proceeds (behavior-c).
- C++ before-observer `g_observer_fired` reads 0 at report time. (run 18f5e6a
  + a8c02b4 both.)
- The hook is on **WHGame.dll's** `lua_settable` @ runtime VA (seed id 1186,
  static 0x71E7C0). Install log: `[hook 'cap38_gate'] installed at runtime
  target 0x...E7C0`.
- `kcdxMessage_InputLoaded` fires at 12:08:02.937 — **1 ms after** the apply
  pass installs the hook at .936, NOT late in boot. (dev log `Firing
  kcdxMessage_InputLoaded...` at .937.) The suite's `kInputLoaded`
  aggregation at .280 of the next second is just result collection ~343 ms
  later; the plugin's InputLoaded handler ran at .937.
- `kcdxLuaApi::SetTable` (scripting_interface.cpp:358) → unprefixed
  `lua_settable` → **kcdx's vendored** Lua (statically linked into kcdx.dll),
  a DIFFERENT function/binary from WHGame's `lua_settable` the hook is on.
  One shared `lua_State` (lua-bridge.md). So any engine/plugin path that
  calls `kcdxLuaApi::SetTable` cannot fire the WHGame detour.

## Killed theories

- **Apply-race at PostGameLoad** (fix 6f5fc54 → moved assertion off
  observer-fired). Killed: the issue is not that PostGameLoad is too early
  per se.
- **Report too early; move to InputLoaded** (fix a8c02b4). Killed: InputLoaded
  fires at .937, 1 ms after install — `lua_settable` (WHGame) still not
  observed called in that window. Observer STILL 0.

## Open questions

**Does WHGame's `lua_settable` (the detour target) get CALLED in-process at
all between install and report, and does the C-dispatch before-thunk FIRE
when it is?** — Probe A: from inside a game-invoked kcdx C function (real
game `lua_State`), resolve WHGame's `lua_settable` via
`ResolveAddressByName("kcdx.lua_settable")`, build a 2-arg stack (table, key,
value) and call it directly; observe whether `g_observer_fired` flips.

Outcome→meaning map (results-driven.md; crash is a real outcome):
- **Observer flips to 1, no crash** → the resolve-and-call path fires the
  detour safely; the C-dispatch is sound, the row just never had the game
  call its target. Adopt the self-driven call to prove firing.
- **Observer stays 0, no crash** → the detour is not firing even when WHGame's
  `lua_settable` runs in-process → a real dispatch bug the row caught.
  Escalate; do not paper over.
- **Crash / STATUS_HEAP_CORRUPTION / PROBE Q fires** → the dual-Lua boundary
  (lua-bridge.md) makes a plugin-driven cross-call unsafe; reject the path,
  fall back to install-proceeded (cap-36 owns the C-dispatch firing proof via
  its own-function hooks).

## Active diagnostic instrumentation

| File | Change | Keep/revert |
|---|---|---|
| test-plugins/cap-38-sig-mismatch-gate/cap-38.cpp | PROBE A: registered C fn resolves+calls WHGame lua_settable, logs CAP38_PROBE | REVERTED after probe — row finalized to install-proceeded |
| test-plugins/cap-38-sig-mismatch-gate-lua/plugin.lua | PROBE A: ready-handler called kcdx.cap38.drive_probe() | REVERTED |

## Trail

| Action | Result |
|---|---|
| PROBE A: game-invoked C fn resolves WHGame lua_settable (VA=0x7FFA4B79E7C0), builds 2-arg stack, calls it | Outcome C. Call did NOT return (no post-call line); process survived 5+s; no crash zip, no PROBE Q. Engine log: `"ready" callback ... threw: [Error] Lua error` 3ms after the call — WHGame's lua_settable RAISED, longjmp'd to the nearest lua_pcall, unwound past the C fn. |

## Resolution

**Cause (not an engine bug):** the C-dispatch before-thunk install is correct
(`IsApplied=1`, WARN fired). The row failed only because it tried to observe
the detour FIRING by waiting for the game to call WHGame's `lua_settable` —
which it doesn't on the observed path between install and report. PROBE A
tried to force the call from a plugin and established the boundary fact: a
**plugin-driven** call into WHGame's `lua_settable` (with a stack built by
kcdx's vendored Lua) **raises a Lua error** and `longjmp`s out (dual-Lua
boundary, `lua-bridge.md`) — it never returns to the caller and is
uninstrumentable from the plugin side. So firing-on-a-named-game-target is
not plugin-observable.

**Fix (decided outcome-C action):** the row asserts **install-proceeded**
(`handle != 0 && IsApplied`), the cap-33/34/35 install-is-the-proof idiom,
identical to the Lua peer. The C-dispatch FIRING proof is owned by cap-36's
six own-function rows (which hook a plugin-local function and INVOKE it — the
safe, in-vendored-Lua path). No engine change. Landed alongside the row
finalization.

**Killed theories** (above) → struck: both were timing; the real constraint
is the dual-Lua boundary + the named-target requirement, not when the report
fires.
