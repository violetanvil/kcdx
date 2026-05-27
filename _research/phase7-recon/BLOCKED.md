# BLOCKED — items the recon agent could not finish without live game or Ghidra

Phase 7 reconnaissance, 2026-05-19. Each item names a thing the seed CSV
or the docs left at `unverified` because finishing it requires
capabilities outside the recon agent's scope (no live game runs, no
Ghidra interaction beyond reading pre-analyzed decomp).

## 1. ~~`IConsole::AddCommand` vtable slot index~~ — RESOLVED 2026-05-20

vtable[32] holds `AddCommand(ConsoleCommandFunc)` (RVA `0x0100A3D4`),
vtable[33] is the script-string overload, vtable[34] is `RemoveCommand`
(RVA `0x0100955C`), vtable[35] is `ExecuteString` (RVA `0x007A5818`),
vtable[23] is `GetCVar` (RVA `0x009DF818`). All canonical CryEngine
5.2.3 positions — no +1 inserts on IConsole. Resolved via a one-shot
runtime probe in `HookedUpdate` (commit `df56f86`), removed after
the CSV was populated. Seed CSV ids 2000–2003 are now `verified`.

**Original blocker text follows (kept for historical record):**

**Why it's blocked:**

- The canonical CryEngine 5.2.3 vtable slot for the (const char*,
  ConsoleCommandFunc, int, const char*) overload is **32**.
- muyuanjin/kcd2db documents that adjacent KCD2 vtables have +1 slot
  inserts (IScriptSystem::CreateTable at 13 vs canonical 12,
  IScriptTable::SetValueAny at 7 vs canonical 6). Strongly suggests
  IConsole may also have inserts, putting AddCommand at 33 or 34.
- Confirming requires either (a) Ghidra navigation through the IConsole
  vtable starting from gEnv→pConsole, or (b) a live KCD2 run that
  dumps `(*pConsole)->vtable` to a log.
- The recon agent is static-analysis only, so neither path is available
  within this session.

**Affected seed rows:** ids 2000 (iconsole-addcommand), 2001
(iconsole-removecommand), 2002 (iconsole-executestring), 2003
(iconsole-getcvar) — all marked `unverified` with empty `rva`.

**What would unblock:**

1. Cheapest: a one-line patch in `kcdx/src/hooks.cpp`'s `HookedUpdate`
   (post-engine-init, fires once) that reads `**pConsole_ptr` to get
   the vtable, then logs the addresses of vtable slots 30 through 36.
   Play KCD2 once, read `kcdx-dev.log`, fill in the four `rva` columns.
2. Equivalent-cost: a ~½-day Ghidra session, walking the
   `gEnv→pConsole→vtable` path inside the pre-analyzed Ghidra project at
   `third-party-ghidra/ghidra_project/KCD2/`, and identifying
   each of the four functions by the disassembly of their bodies (e.g.,
   AddCommand's body should reference the console's command map; GetCVar's
   should reference the cvar hash table).

Either path takes a human and a live system. The recon agent's job ends
at "documented the path to the answer."

## 2. Verified-against-current-version status for ids 2000–2003 + 3000–3005

**What's missing:** evidence that ids 2000–2003 and 3000–3005 are
correct for game_version 1.5.1164953 — i.e., that the vtable indices
muyuanjin documented for v1.4+ still hold, and that the IConsole
implementation is unchanged.

**Why it's blocked:**

- muyuanjin's source says "live-verified through 1.5 (v1.4+ branch)"
  but the verification is implicit — the kcd2db plugin works against
  KCD2 1.5.1164953 production, so the vtable indices it uses must
  resolve. That's MEDIUM confidence (not LOW), but not the same as
  status=verified for our purposes.
- Promoting ids 3000–3005 to `verified` requires either (a) a kcdx
  maintainer cross-referencing in Ghidra, or (b) one of the muyuanjin
  vtable indices being consumed by a shipping kcdx plugin and confirmed
  to work.

**What would unblock:**

- For ids 3000–3005: a kcdx plugin that uses `gEnv (id 1010) →
  gEnv+0x90 → IGame* → vtable[16] → GetIGameFramework()` (or any other
  documented slot) and logs success. The first plugin to ship that
  pattern signs off all six related rows. Phase 7's own `[[command]]`
  exercise covers ids 2000/2003 once the slot is confirmed.

## 3. AddCommand RVA itself (not just vtable index)

**What's missing:** even if we know `AddCommand = vtable[N]`, what's
`AddCommand`'s RVA in `.text`? The vtable holds a function pointer that
points into `.text`, but reading that pointer requires a live process
(the .data slot holding the vtable address itself is computed at link
time but the *contents* — the function pointers — are RIP-relative
fixups resolved at load time).

**Why it's blocked:** see #1, same reasons. Reading vtable contents from
a static PE requires walking the relocations table and computing
post-relocation addresses, which is more work than the simpler "log it
at runtime" or "look in Ghidra" paths.

**What would unblock:** same as #1.

## 4. Live testing of the seed CSV's verified-status claims

**What's missing:** independent confirmation that the seed CSV's
`verified` rows still resolve correctly against `WHGame.dll` from
`release_1_5_1164953_841`.

**Status:** PARTIAL — the recon agent did run `verify_seed_sigs.py`
against the live WHGame.dll and confirmed every code-pattern row
resolves to exactly one location in `.text`. That validates the AOB
side. What's NOT validated:

- That those RVAs are what they claim to be semantically. E.g., id 1003
  (cgame-update-callee-ui-pump) is *believed* to be the per-frame UI
  pump because `test-plugins/cap-03-hook-lua-callback` hooks it and
  observes the expected behavior (callback fires every tick). That's
  validation. But naming is partly heuristic — if someone re-disassembles
  and finds it's actually "the UI pump's WORKER thread setup", the name
  is wrong even though the RVA is right.

**What would unblock:** Phase 7's own command registration test, plus
any future plugin that consumes the seed IDs and reports the resolved
addresses to `kcdx-dev.log`. Time will validate the names.

## 5. xiaoxiao921/ReturnOfModdingBase yielded no KCD2 sigs

**What's missing:** any KCD2-specific signature contribution from
xiaoxiao921's repo.

**Why it's not really blocked:** the repo was always going to be a
methodology source, not a sig source. Its sample code targets Hades.
Documented in the seed coverage report as "did not contribute."

**Future use:** if KCD2 gets a Risk-of-Rain-2-style mod loader as a
secondary install path, ReturnOfModdingBase becomes the reference
implementation. Not blocking Phase 7.

## 6. The string anchors that were searched for and not found

Five strings turned up zero hits in `WHGame.dll`:
`System.LogAlways`, `System.Log`, `RegisterFunction`, `OnSaveGame`,
`OnLoadGame`, `IGameFramework`, `gEnv`, `Initializing System...`.

**Why it's not really blocked:** the workspace's `CLAUDE.md` hard rule
#5 already documented this — KCD2 strips localization keys + many
internal debug-string literals at startup. The `find_extra_anchors.py`
script is run-and-document, not run-and-block. Anchors are bonus
material; the seed CSV doesn't depend on any of them except
`exec autoexec.cfg` (id 1011), which is verified.

If/when a future Ghidra session derives additional reliable anchors
(strings that DID survive interning), they get added as new rows.

## 7. The seed CSV doesn't cover Phase 6 (save/load)

**What's missing:** sigs for the save trigger that Phase 6's
`kcdxSerializationInterface` will hook.

**Why it's not really blocked here:** Phase 6 is the next agent's job
per the prompt. `_research/phase6-save-load/_phase6_*.txt` files
suggest someone has already started this work. The Phase 7 recon agent
deliberately did not chase Phase 6 sigs.

**Future cross-reference:** when Phase 6's findings land, those sigs
get added to the seed CSV in the 4000–4999 range (per the
id-assignment-policy.md).

---

## Summary

**Hard blocks (need human + live system or Ghidra):**

- One: confirming the IConsole vtable slot for AddCommand (item 1).

**Soft blocks (can land via the regular plugin/verification flow):**

- Promoting unverified rows to verified as plugins ship (items 2, 4).

**Not blocks (documented as scope-limit / future work):**

- xiaoxiao921 yielded no KCD2 sigs (item 5).
- Five string anchors didn't survive interning (item 6).
- Phase 6 sigs not included (item 7).

The hard block (item 1) is small. ½-day of Ghidra OR a one-line log
patch + one game launch resolves it.
