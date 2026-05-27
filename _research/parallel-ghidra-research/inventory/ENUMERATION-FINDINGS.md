# Enumeration findings — reference-data research log (2026-05-26)

The research LOG behind the rewritten plan in
`docs/outstanding-work/parallel-ghidra-research.md`. This file holds the raw
numbers, probes, reproduction recipes, and decision trail; the plan file holds
the resulting end-state shape + execution steps. Read the plan first; come here
for the evidence.

Began as pre-dispatch recon for the ORIGINAL subagent-partition brief
("ground the partition in real data before dispatching"). **Result: that model
was retired** — the enumeration premise (~50K functions, partition-able by
existing labels) is invalid against the real binary (321K functions, ~0%
labels). The findings below drove the rewrite to a mechanical-batch +
call-graph-backbone model. See the plan's §10 for the retirement rationale.

## How produced

- Tool: `third-party-ghidra/ghidra_scripts/EnumerateFunctions.java` +
  `enumerate-functions.ps1` launcher (game-agnostic, reusable across re-runs +
  ports). Read-only headless pass over the analyzed `KCD2` Ghidra project.
- Ghidra 12.1 (note: dropped bundled Jython — the dir's older `.py` scripts no
  longer run headless; the enumerator is Java for this reason).
- CSV: `WHGame.dll.functions.csv` (321,120 data rows, ~25 MB).

### Reproduce (exact)

From repo root, PowerShell:

```powershell
$repo = "C:\Users\Michael\Documents\KCD2 Mods\kcdx"
& "$repo\third-party-ghidra\ghidra_scripts\enumerate-functions.ps1" `
    -ProjectDir  "$repo\third-party-ghidra\ghidra_project" `
    -ProjectName "KCD2" `
    -OutDir      "$repo\_research\parallel-ghidra-research\inventory" `
    -Modules     "WHGame.dll" `
    -VersionTag  "release_1_5_1164953_841"
```

Drop `-Modules` to enumerate every program in the project. Read-only; the
project DB is never mutated. Real Ghidra diagnostics (not the launcher's
stdout) land in `%APPDATA%\ghidra\ghidra_12.1_PUBLIC\application.log`.

**Gotchas that cost time on first run (recorded so the next run doesn't repeat them):**
- `analyzeHeadless.bat` → `launch.bat` ends with a `pause` that fires on
  non-zero exit when `DOUBLE_CLICKED=y`. In a non-interactive shell it
  DEADLOCKS forever (no stdin). The launcher defends with `$env:DOUBLE_CLICKED=""`
  + piping EOF (`$null | & …`). Don't remove those.
- Ghidra project paths must be ABSOLUTE — a `..`-relative `-ProjectDir` aborts
  with "Path element starting with '.' is not permitted".
- An unrelated pre-existing script in the dir (`FindPostUpdateHookCandidates.java`)
  fails to stub-compile against Ghidra 12.1 APIs; headless logs the error and
  SKIPS it. It does not affect the enumeration run.

## WHGame.dll — ground truth

| Metric | Value | Brief assumed |
|---|---|---|
| Total functions | **321,120** | ~50,000 (6.4× low) |
| Named (source/recovered) | **352 (0.1%)** | enough to cluster by |
| With a namespace (RTTI class) | **281 (0.09%)** | — |
| Auto-named (`FUN_*`) | 320,768 (99.9%) | — |
| Thunks | 2,473 | — |
| Externals | 0 | — |
| Image base | 0x180000000 | — |

**The 0.1% that ARE named are almost all statically-linked library code**, not
game subsystems: CRT / Win32 / MSVC runtime (`malloc`, `_Mtx_unlock`,
`API-MS-WIN-CRT-*`, `MSVCP140.DLL`, `std::vector<...>`, `KERNEL32.DLL`,
`HID.DLL`). The game's own recovered symbols number in the low dozens
(`MRECmpImpl`, `ffxFsr2ResourceIsNull`, a few CryEngine bits). There is **no
`inventory` / `combat` / `dialogue` categorization** in the project.

## Why the brief's model can't run as written

The brief's §3 parallelization rests on "decompose WHGame.dll into subsystem
partitions using Ghidra's existing labels + caller-graph clustering" and
"~12-20 subagents, 2K-5K functions each."

1. **No labels to cluster by.** 99.9% auto-named; the named 0.1% is library
   code, not subsystems. The candidate subsystem list (`WHGame.dll/inventory`,
   …) presumes a categorization that does not exist in the analysis.
2. **Subagent count explodes.** 321K ÷ 2K-5K = **64–160 subagents** for WHGame
   alone — a different operational regime than "12-20 in flight."
3. **`function_name`-keyed deliverable is ~empty.** 99.9% of rows would carry
   only an `auto_name` (`FUN_180xxxxxx`), which the brief itself flags as
   per-version-unstable. The §9 stable-ID match (name + signature +
   caller-graph fingerprint) has almost no `name` to match on.

This is a premise mismatch, not a tuning problem. Proceeding to partition along
the brief's subsystem list would theorize a structure the binary lacks (AP10).

## Secondary-DLL scope correction (also ground truth)

Brief §2 lists CrySystem.dll + Cry3DEngine.dll as separate secondary modules.
**They do not exist in the install** — CryEngine is statically linked into
WHGame.dll (no `Cry*.dll` anywhere; 132 CryEngine strings inside WHGame.dll).
Actual separate modules at the game-bin root: `BugSplat64.dll`,
`BugSplatRc64.dll`, `Quatmosphere.dll`, `WhGdk.dll` (+ `WHGameArm.dll`, the ARM
build, out of scope for the x64 live target). These were NOT yet imported into
the Ghidra project when recon halted.

## Decision taken

User chose **STOP — rework the brief first** (over: pivot to demand-driven
scope / mechanical RVA-range partition / investigate noise-vs-real-code split).
No subagents dispatched. The brief governs future re-runs + other-game ports, so
the parallelization model is fixed once, at the source, before any dispatch.

## Noise-vs-real-code breakdown (answered from the CSV, no new Ghidra run)

The "is most of the 321K trivial noise?" question — computed over
`size_bytes` (Ghidra function-body address count) + `is_thunk` + auto_name:

| Cut | Count | % of total |
|---|---|---|
| ≤ 8 bytes | 22,687 | 7.1% |
| ≤ 16 bytes | 44,933 | 14% |
| ≤ 32 bytes | 74,005 | 23% |
| **≥ 64 bytes, non-thunk** | **197,501** | **61.5%** |
| ≥ 128 bytes, non-thunk | 132,163 | 41.2% |
| ≥ 256 bytes | 54,393 | 16.9% |
| thunks | 2,473 | 0.8% |
| `switchD_`/`caseD_` stubs | 13 | ~0% |

**The noise filter does NOT rescue a clustered model.** Trivial stubs are a
small minority — 61.5% of functions are ≥64-byte non-thunks, 41% are ≥128 bytes.
This is a genuinely large body of substantial code (~130–200K real functions),
not a small interesting core buried in stubs. Any revised model must reckon with
that scale; it cannot assume the real count collapses to something cluster-sized.

## Open questions carried into the rework

- Given ~130–200K substantial, essentially-unnamed functions and ~0%
  pre-existing categorization: is full-binary coverage even the right goal, or
  is the SQLite a demand-driven curated subset (brief §6 already calls the
  Address Library "a curated subset of the full reference dump")? The rework
  must settle scope before partitioning.
- If clustering is still wanted, the signal has to be BUILT (caller-graph
  community detection, string/cvar anchoring), not read from existing labels —
  that is itself a research task with its own cost, not a free input.

## Anchor-quality probe (2026-05-26) — Outcome B

Probe: `third-party-ghidra/ghidra_scripts/ProbeAnchorQuality.java`, 40 auto-named
substantial (≥64B non-thunk) functions sampled evenly across WHGame.dll's
address space. Tests whether Ghidra produces the data `kcdx_find` /
`kcdx_dev_inspect` need on UNNAMED functions.

| Anchor axis | Result | Verdict |
|---|---|---|
| Decompile / statement decomposition | **100% OK** (40/40), avg 128 stmts/fn | Solid — the insert/replace backbone is reliable on stripped fns |
| Callee xref (`find{callee=}`, `first_call_to`) | **93%** (37/40; misses are call-free leaves) | Solid |
| String literal ref (`find{string=}`) | resolves correctly WHEN present, but only **8%** of fns ref a literal | Partial — see below |
| Cvar signal (`find{cvar=}`) | **0%** — generic "string-near-call" heuristic found nothing | Unvalidated — needs the real CryEngine cvar API model |

The substantial-auto-named population is **197,421** functions (probe's own count;
matches the ≥64B non-thunk enumeration cut).

### The string-find gap (drove the discovery-model decision)

Two reasons "I saw text X in-game → `find{string=X}`" does NOT trivially work:
1. Only ~8% of functions reference any string literal (expected — strings cluster
   in error/log/registration paths, not most code).
2. On-screen UI text is frequently a **runtime-resolved localization key**, not a
   literal in the consuming function (`reverse-engineering.md`: loc keys are
   int-ID-interned at startup; `cant_*_in_combat` strings have zero LEA xrefs).

And even where a literal IS present, its home function is usually a low-level
helper (a toast/logger/registrar), **not** the gameplay function the author wants
to change.

## DECIDED discovery model — call-graph backbone, anchor + caller neighborhood

(User decision, 2026-05-26.) The reference data's backbone is the **full
caller↔callee call graph** of the binary, NOT four independent per-function
anchor indexes.

- **Anchors (string / cvar / localization-key) are ENTRY POINTS into the graph**,
  not the answer themselves.
- `find{...}` resolves an anchor to its referencing site, then **walks UP the
  call graph (callers, callers-of-callers, …)** to surface hookable *gameplay*
  functions, **ranked by graph distance** from the anchor.
- Rationale: the literal `"inventory full"` lives in a UI helper; the author wants
  the inventory-add guard a few frames up that *decided* to show it. The graph is
  what turns a low-level anchor into the function the author actually hooks.
- This leans the whole discovery surface on the ONE signal the probe proved is
  93%+ reliable (the call graph), and demotes the sparse/unreliable signals
  (strings, cvars) to entry points rather than load-bearing indexes.

**UX bar:** it must feel *magical* to the mod author — they type what they
observed, the engine does the complex graph search and returns the right
function. A heavy/powerful in-game search is acceptable; optimize where cheap,
but power > search-cost here (user direction).

**Data-model consequence:** the dump must capture **caller edges for the whole
binary** (not just per-function callee lists), plus per-function statements +
string/callee anchors, plus a cvar model (see below — RESOLVED) + a
localization-key model (research pending). Graph walk depth: full (option 1),
feeling-magical prioritized over bounded result sets.

## Cvar axis — RESOLVED: cvar names are string literals (2026-05-26)

Probed (reuse-first per `reverse-engineering.md`): the predecessor
`IConsole.h` shows the CryEngine cvar registration API takes the **cvar name as
a string-literal first arg** (`RegisterFloat("g_x", …)`, `RegisterInt`, etc.).
Confirmed against WHGame.dll: **2,421 distinct cvar-prefixed name literals**
present (`ai_*` = 765, `e_*` = 2526 raw, `sys_*` = 251, `p_*` = 444 …),
e.g. `ai_CollisionAvoidanceRange`, `ai_AmbientFireQuota` — textbook CryEngine
cvars, all literals in the binary.

**Consequence: the cvar axis collapses into the string axis + call graph.** A
cvar name is just a string literal; `find{cvar="ai_X"}` = find the literal's
reference site → walk the caller graph (the proven 93% backbone) to the gameplay
function. NO separate cvar-API/vtable model is needed *for discovery*. (The
earlier 0% cvar signal was a bad heuristic, not absent data — the 40-fn sample
didn't hit cvar-touching functions.)

**Unified discovery model — all anchors collapse to one mechanism:**
> *anchor (string OR cvar-name OR loc-key) → literal reference site → walk
> caller graph → ranked hookable gameplay functions.*

This is simpler AND more robust than four independent indexes: one extraction
(literal refs + caller edges), one query path (graph walk).

## Localization-key axis — partially solved + runtime-probe plan (2026-05-26)

On-screen text → key mapping is **trivial**: KCD2 localization is plain XML
tables inside per-language paks (`Localization/English_xml.pak` → `text_ui_*.xml`,
`text_ui_dialog.xml`, etc.), format:
```xml
<Row><Cell>ui_hud_reputation_notification_crime_reported</Cell><Cell>Crime reported</Cell>…</Row>
```
key → English → translated. kcdx reads these data files directly — NO RE for the
text↔key half. (11 XML files in English pak, ~38 MB; `text_ui_dialog.xml` alone
is 31 MB.)

Key → consuming-function splits into two cases:
- **Key IS a binary literal (~293 `ui_*` + ~57 `soul_/quest_/…`):** full path
  works — on-screen text → XML → key → literal ref → caller-graph walk. ✅
- **Key consumed by INT-ID only (HUD notifications, likely most dialog/quest):**
  the XML key is interned to an int ID at startup and the C++ references the int,
  NOT the string (confirmed: `ui_hud_reputation_notification_*` are NOT literals
  in WHGame.dll). Static analysis alone can't link text → consuming function here.

**DECIDED plan for the int-ID case (user, 2026-05-26): solve via RUNTIME probe.**
The XML→int-ID map is computed at startup but **observable live**. Chain:
1. RE the live `CLocalizedStringsManager` structure (engine names it:
   `CLocalizedStringsManager`, `LocalizeString`, `LoadLocalizationXml`,
   `LocalizedStringManager.cpp`). It hangs off `gEnv` (already resolved via the
   muyuanjin predecessor `"exec autoexec.cfg"` anchor).
2. kcdx in-game runtime dump → `key → int-ID` for every interned key.
3. Static pass: find functions referencing each int-ID → link ID → consumer →
   caller-graph walk.

**Open sub-risk in step 3 (checkable, not yet probed):** int-IDs referenced as
immediate operands are statically findable; if the ID is instead looked up
dynamically (key hashed at callsite, or runtime-array index), the static
ID→function link is weak. Probe step 1-2 first (the gating unknown); re-assess 3
on real dumped IDs.

This is a SEPARATE research sub-project (loc-manager RE + runtime dump path +
static ID→fn validation), tracked distinctly from the core dump feasibility.

## Validated discovery model — summary

| Axis | Mechanism | Status |
|---|---|---|
| statements / `kcdx_dev_inspect` | per-fn decompile | ✅ 100% |
| `find{callee=}` / graph backbone | caller↔callee edges | ✅ 93% |
| `find{string=}` | literal ref → graph walk | ✅ (sparse but reliable) |
| `find{cvar=}` | cvar name = literal → graph walk | ✅ (2421 names) |
| `find{text=}` literal-key | XML text→key → literal ref → graph | ✅ (~350 keys) |
| `find{text=}` int-ID key | XML text→key → runtime ID dump → static ID→fn | ⏳ runtime-probe sub-project |

Backbone for ALL: the caller↔callee call graph. Anchors are entry points; the
graph walk surfaces ranked gameplay functions.

## Hash primitive (decided, carries forward)

`content_hash` = **BLAKE3** (brief §4 confirmation). Unaffected by the rework.
