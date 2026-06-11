---
paths:
  - "_research/**"
  - "src/address_library.*"
  - "src/hook_engine.*"
  - "src/save_load_hooks.*"
---

# Reverse engineering — methodology

For a "verify a game-function fact" task (address, ABI/signature, return type, arg count, vtable slot), run the **`/research-disassembly`** skill — it executes this ladder and enforces AP1/AP2/AP3. Fresh Ghidra is the LAST tier, never the first, even when a task prompt says "read the decompilation."

## Order of operations — reuse-first; stop at the first tier that answers

Applies to BOTH "find an address" AND "find an ABI fact" (return type / arg count / signature / vtable slot). A captured fact is reused, never re-disassembled.

1. **Check the Address Library** — the reference DB (`data/reference.sqlite`), whose curated CSV export is `data/db-export/address_names_seed.csv` (entity registry: id, name, notes) + `data/db-export/address_versions_seed.csv` (per-version rva + signature + verification). Resolve a known fact at runtime via `refdb::ResolveAddrByName` / `ResolveAddrById`; for authoring, read the curated CSV export directly. If an ID exists, use its address; if its prose/`signature` already carries the ABI fact, cite it. A row may answer *partially* (args known, return unknown — the cap-21/cap-22 IConsole case): descend only for the missing piece.
2. **Check prior `_research/<phase>/` disassembly dumps** — `phase6-save-load/`, `phase6b-recon/`, `phase7-recon/`, `phase8-fix-a/`, `cap03-recon/` hold `_abi_<address>.txt` dumps + recon docs + the worker scripts that made them. Grep for the address/name BEFORE re-disassembling — the IConsole/save-load functions are likely already decompiled here.
3. **Check predecessor sigs** in `_research/predecessor-sigs/` — `yobson1/kcd2lua` (KCD2 1.5; `lua_pcall`, `update`, `luaL_loadfile`) and `muyuanjin/kcd2db` (gEnv resolver via `"exec autoexec.cfg"` string anchor, 12+ verified vtable offsets). For any sig they cover, work is already done.
4. **Check cached Warhorse wiki** at `_research/warhorse_wiki/`. Wiki is a JavaScript SPA — fetch articles via YouTrack REST API, never WebFetch the article URL.
5. **Only then, fresh disassembly** for facts none of the above cover — the pre-analyzed Ghidra project (below) or the `pefile + capstone` scripts; for ABI/arg facts the abi_walker, never prologue-shape guessing. Drop the new script + raw output into one investigation dir per the layout below so the next agent finds it at tier 2.

## `_research/` layout — one investigation, one dir

Every fresh-disassembly investigation that produces artifacts lands in ONE new directory. Applies to all callers that write here — `/research-disassembly`, `/debug` probe recon, `/feature` recon — not just one skill.

- **Dir name: `<task-slug>-recon/`** — a short kebab-case slug for the investigation (`init-cycle-recon`, `pak-resolver-recon`, `console-print-recon`). No phase number; phases are no longer the tracking unit. Existing `phaseN-<slug>/` dirs are historical — leave them, do not rename or backfill.
- **One investigation = one dir.** Never leave artifacts loose at `_research/` root; never mix a new investigation's files into an unrelated existing dir. Reuse a dir only to extend the SAME investigation.
- **In-dir layout:**
  - `FINDINGS.md` — one per dir, summarizing the verified facts + provenance (the tier-2 entry point the next agent reads first).
  - `<verb>_<noun>.py` — worker scripts (`find_genv.py`, `dump_wrapper.py`).
  - `_abi_<addr>.txt` and other `_`-prefixed files — raw generated dumps (leading underscore = machine-generated, not hand-authored).

## Ghidra

Pre-analyzed WHGame.dll project at `third-party-ghidra/ghidra_project/KCD2.gpr`. Install at `third-party-ghidra/ghidra_12.1_PUBLIC/`. Cold analysis is hours; use the pre-analyzed project. (Both are git-ignored binaries — see `third-party-ghidra/README.md`.)

- **Ghidra 12.1 dropped Jython.** Write Java scripts. PyGhidra works only if explicitly installed. Java examples already in `third-party-ghidra/ghidra_scripts/` (e.g. `FindIsInCombatSlot.java`).
- Headless invocation: `analyzeHeadless.bat "<project_dir>" KCD2 -process WHGame.dll -postScript <Script> -noanalysis -readOnly`.

## ABI extraction

**Prologue-shape analysis is insufficient.** It produced a 3-arg `SaveGame` typedef when the function actually takes 7 args — silently corrupted saves until caught. Always use `_research/phase6-save-load/phase6_abi_walker.py` (capstone body-wide stack-arg analyzer) on every new hook target.

## Wiki fetch (YouTrack API)

```
https://warhorse.youtrack.cloud/api/articles/KM-A-{N}?fields=summary,content,childArticles(idReadable,summary)
```

Root: KM-A-1. Top-level branches: Technical Overview (KM-A-36), Modding Game Data (KM-A-37), Modding Visuals (KM-A-38), Walkthroughs (KM-A-83), Modding Rules (KM-A-87).

## x64dbg is unstable

x64dbg has been unreliable against KCD2 1.5.1164953 (April 2026 build). Don't rely on it. Verification path: Ghidra static analysis → engine logs (`kcdx.log` / `kcdx-dev.log`) → live in-game test. x64dbg is fallback only.

## Localization keys are int-ID-interned at startup

Searching `WHGame.dll` for `cant_*_in_combat` strings expecting LEA xrefs returns zero hits. Right anchor: an action-map name from `defaultProfile.xml` or a Flash event name — strings the C++ matches against XML at load time.

## Dead end

`ecaii/kcd2-lua-extension` (yobson1's upstream) returns 404 as of 2026-05-19.
