# outfit-swap test matrix

The "did we crack the vault" bulletproofing exercise. We take the
known-working outfit-swap-in-combat fix (a 3-byte rewrite at
`mov r14b, al`) and re-express it through **every kcdx primitive
that could plausibly deliver it**, then test each in isolation,
then test combinations, then stress-test the survivor(s).

Goal: a per-method capability + limit chart at the bottom of this
document, filled in from live runs. By the end we know exactly
what each primitive can and cannot do, and which one we'd
recommend for the "I want to write a code-injection mod for KCD2"
question.

## Site under test

| What | Value |
|---|---|
| Module | `WHGame.dll` |
| AOB (Tier 1) | `48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0` |
| Offset of bytes | 13 |
| Original bytes | `44 8A F0` (`mov r14b, al`) |
| Patched bytes | `45 31 F6` (`xor r14d, r14d`) |
| Game build | release_1_5_1164953_841 |
| Effect | `IsInCombat()` result is replaced with 0 in the gating register that decides whether the `next_outfit` action binding is enabled |
| In-game test | Enter combat (draw weapon, attack NPC, take a hit). Try to open inventory and change outfit. Vanilla = popup "You can't switch outfits in combat." Patched = swap succeeds, popup never appears. |

## Plugin folders

Each plugin lives at
`<game>/Bin/Win64MasterMasterSteamPGO/plugins/<folder>/` with a
`kcdx.toml` (or for the pak-Lua case, the pak goes to
`<game>/mods/outfit-swap-paklua-mod/Data/`). Enable / disable
individually by renaming `kcdx.toml` ↔ `kcdx.toml.disabled` (or
the pak folder ↔ `<folder>.disabled`).

| Folder | Method | Lives in |
|---|---|---|
| `outfit-swap-patch/` | `[[patch]]` byte rewrite | `plugins/` |
| `outfit-swap-hook-observe/` | `[[hook]]` + lua_callback (observability only) | `plugins/` |
| `outfit-swap-hook-write/` | `[[hook]]` + Lua does runtime byte-write | `plugins/` |
| `outfit-swap-midhook/` | `[[mid_hook]]` + lua_callback overrides r14 | `plugins/` |
| `outfit-swap-paklua-mod/` | Pak mod, pure Lua, uses `kcdx.memory.dynamic_hook` from script | `mods/` |

Dev mode stays on for all tests (the `dev-mode-enable/kcdx.toml`
opt-in plugin remains installed) so we capture full kcdx-dev.log
dispatch traces.

## Result columns

For each test we record:

- **Build** — kcdx commit SHA the test ran against
- **Loaded?** — did kcdx find + parse + register the plugin (kcdx.log apply-summary line)
- **Installed?** — for hooks, did MinHook accept the install (kcdx-dev.log DYNAMIC_HOOK/install-ok)
- **Callback fired?** — for hooks, did the Lua callback dispatch (kcdx-dev.log SHIM/enter or DISPATCH/pre)
- **In-game effect?** — Yes / No / Crash / Other (with notes)
- **Notes** — any surprise in the logs, the failure mode, the exact log lines that pinned the outcome

---

## Individual method tests

### Test 1 — `[[patch]]` byte rewrite (kcdx native)

**Method:** Use kcdx's `[[patch]]` engine to do the same 3-byte
rewrite as the original mempatch fix. Phase 1 verification — the
patch surface must be mempatch-compatible.

**Expected:** ✅ outfit swap works in combat. Baseline.

| Field | Value |
|---|---|
| Build | _TBD_ |
| Loaded? | _TBD_ |
| Installed? | _N/A (patch, not hook)_ |
| Callback fired? | _N/A_ |
| In-game effect? | _TBD_ |
| Notes | _TBD_ |

---

### Test 2 — `[[hook]]` + lua_callback (observability only)

**Method:** Hook the registrar function's entry. Lua callback
just `System.LogAlways("[OUTFIT_HOOK_OBSERVE] registrar entered")`
and returns true. Does NOT modify behavior.

**Expected:** ❌ outfit swap STILL BLOCKED. The registrar runs
once at startup, our callback observes it, but we don't change
what it does — so the byte still loads `IsInCombat()` into r14.

This test exists to prove the hook chain works on this specific
target, and to confirm that hook-without-effect is a real failure
mode plugin authors should expect.

| Field | Value |
|---|---|
| Build | _TBD_ |
| Loaded? | _TBD_ |
| Installed? | _TBD_ |
| Callback fired? | _TBD_ (should fire once at game-Lua-init time) |
| In-game effect? | _TBD_ (should be ❌ no change) |
| Notes | _TBD_ |

---

### Test 3 — `[[hook]]` + runtime byte-write from Lua

**Method:** Same hook target as Test 2, but the Lua callback now
runs `kcdx.memory.scan_pattern(AOB):add(13):set_bytes("45 31 F6")`
to do the byte rewrite at hook-fire time. Hook target must fire
*before* the patched bytes get executed.

**Expected:** ✅ outfit swap works. Proves `kcdx.memory.*` from a
TOML-hook Lua callback can do runtime code modification.

| Field | Value |
|---|---|
| Build | _TBD_ |
| Loaded? | _TBD_ |
| Installed? | _TBD_ |
| Callback fired? | _TBD_ |
| In-game effect? | _TBD_ |
| Notes | _TBD_ |

---

### Test 4 — `[[mid_hook]]` register override

**Method:** Mid-hook at the `mov r14b, al` site itself. Capture
r14 (it's the destination register). Lua callback sets r14 := 0
in the register dump and returns. Trampoline writes captured
registers back to the CPU before returning to the next
instruction.

**Expected:** ⚠️ probably ❌. From Phase 5g investigation: MinHook
re-executes the captured `mov r14b, al` after our callback
returns, overwriting the zero. The mid-hook primitive as
currently designed doesn't support "skip the original
instruction" semantics — we'd need a new primitive (e.g.,
`call_original = false` flag, or a true instruction-replacement
mode).

This test characterizes the failure mode in dev-log detail and
decides whether v0.2 needs the new primitive.

| Field | Value |
|---|---|
| Build | _TBD_ |
| Loaded? | _TBD_ |
| Installed? | _TBD_ |
| Callback fired? | _TBD_ |
| In-game effect? | _TBD_ |
| Notes | _TBD_ |

---

### Test 5 — pak-Lua-only runtime injection

**Method:** Pak mod (Workshop-distributable!) containing only a
Lua script at `scripts/mods/outfit_swap.lua`. The script runs
`kcdx.memory.scan_pattern(AOB):add(13):set_bytes("45 31 F6")`
from pak-Lua-init time. No DLL, no TOML, no `[[patch]]`,
no `[[hook]]`.

**Expected:** ✅ outfit swap works. Proves the novel kcdx
capability — pak mods can now patch code without authors
shipping a DLL, while staying inside Steam Workshop's pak-only
distribution. Before kcdx, this was impossible
(`package.loadlib` is a CryEngine-compiled-out stub).

Open question this test answers: does `kcdx.memory.*` exist by
the time pak Lua's first script-load fires, or does the pak
script have to wait for an event?

| Field | Value |
|---|---|
| Build | _TBD_ |
| Loaded? | _TBD_ |
| Installed? | _N/A (no hook, just byte-write)_ |
| Callback fired? | _N/A_ |
| In-game effect? | _TBD_ |
| Notes | _TBD_ |

---

## Combination tests (after individuals pass)

### Test 6 — `[[patch]]` + `[[hook]]` observe (different effects)

**Method:** Both plugins installed. The patch does the byte
rewrite. The hook just observes registrar entry.

**Expected:** ✅ works (patch makes it work, hook just logs).
Proves orthogonal entries coexist without interference.

| Field | Value |
|---|---|
| Build | _TBD_ |
| Loaded? | _TBD_ |
| Installed? | _TBD_ |
| Callback fired? | _TBD_ |
| In-game effect? | _TBD_ |
| conflict_engine output | _TBD (should be quiet — different addresses)_ |
| Notes | _TBD_ |

---

### Test 7 — `[[hook]]` write + `[[patch]]` same bytes (collision)

**Method:** Both plugins try to modify the same bytes — the patch
via static rewrite, the hook via runtime `set_bytes`. Different
priorities to control who wins.

**Expected:** conflict_engine logs the overlap. First-wins by
priority (lower number = higher priority by convention). The
losing plugin gets its install aborted with a plain-English log
line naming the winner.

| Field | Value |
|---|---|
| Build | _TBD_ |
| Loaded? | _TBD_ |
| Conflict logged? | _TBD (kcdx.log + kcdx-dev.log CONFLICT lines)_ |
| Winner | _TBD_ |
| In-game effect? | _TBD_ |
| Notes | _TBD_ |

---

### Test 8 — pak-Lua dynamic_hook + `[[patch]]` (cross-channel conflict)

**Method:** Both target the same bytes via different channels —
pak-Lua via `kcdx.memory.dynamic_hook` from script, TOML via
`[[patch]]`. Tests whether the runtime install path participates
in the same first-wins map as TOML entries.

**Expected:** Whichever runs first wins. The runtime install
sees the existing entry and aborts (or vice versa).

| Field | Value |
|---|---|
| Build | _TBD_ |
| Loaded? | _TBD_ |
| Cross-channel conflict logged? | _TBD_ |
| Winner | _TBD_ |
| In-game effect? | _TBD_ |
| Notes | _TBD_ |

---

### Test 9 — all three (patch + hook-write + pak-Lua)

**Method:** Maximum chaos. All three plugins installed
simultaneously.

**Expected:** conflict_engine logs cleanly. First-wins decides.
The other two abort with plain-English log lines. Outcome is
deterministic across game restarts.

| Field | Value |
|---|---|
| Build | _TBD_ |
| Loaded? | _TBD_ |
| All conflicts logged? | _TBD_ |
| Final state at the byte | _TBD (was it the patch, the hook, or the pak-Lua write that landed?)_ |
| In-game effect? | _TBD_ |
| Determinism across restarts? | _TBD (run game 3x, check)_ |
| Notes | _TBD_ |

---

## Stress tests

### Test 10 — save / load / alt-tab / repeat

**Method:** With the winning method from Test 9 in place: enter
game, save, load, save again, load again, alt-tab out and back,
trigger outfit swap. Repeat several times. Look for crashes,
broken Lua state, dispatch chain becoming silently broken.

**Expected:** Dispatch chain survives save-load. The Lua state
pointer kcdx captured at first-update-tick is still valid (it
must be — the entire game's Lua state survives save-load too).

| Field | Value |
|---|---|
| Build | _TBD_ |
| Save/load roundtrips survived | _TBD_ |
| Alt-tab survived | _TBD_ |
| Late-session outfit swap still works | _TBD_ |
| kcdx-dev.log clean? | _TBD_ |
| Notes | _TBD_ |

---

## Final capability + limit chart

Filled in after Tests 1–10 land. One row per method, recommending
when to use each, when to avoid each, and the exact failure
mode if any.

| Method | Works? | Use when | Avoid when | Failure mode | Notes |
|---|---|---|---|---|---|
| `[[patch]]` | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ |
| `[[hook]]` observe | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ |
| `[[hook]]` runtime write | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ |
| `[[mid_hook]]` r14 override | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ |
| Pak-Lua dynamic_hook | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ |

---

## How to fill in this doc

Each test goes:

1. I build / install the plugin for the test.
2. You launch game, save, enter combat, attempt outfit swap.
3. Report yes / no / crash + any in-game notes.
4. I read kcd.log + kcdx.log + kcdx-dev.log, fill in the table,
   commit.
5. Move to next test.

When a test's row is filled and the commit lands, the build
column should point at that commit's SHA so anyone reading later
can reproduce exactly.
