# Reference-data sourcing — the discovery-index plan

**Status:** REWRITTEN 2026-05-26 after ground-truth enumeration invalidated the
original "parallel Ghidra subagent dump" premise. The evidence behind every
decision here is in
[`_research/parallel-ghidra-research/inventory/ENUMERATION-FINDINGS.md`](../../_research/parallel-ghidra-research/inventory/ENUMERATION-FINDINGS.md)
(the research log; this file is the plan it produced).

**What this document is.** The authoritative plan for how kcdx sources the
per-function / per-statement reference data that backs the Phase 9.x author
surface (`kcdx.find`, `kcdx.hook.*`, `kcdx.statement.*`, `kcdx_dev_inspect`, the
per-version survival check). It defines the end-state shape, the build steps,
and the routing for each step. It supersedes the original subagent-partition
brief (see §10 for why that model was retired).

**What changed and why (one paragraph).** The original brief assumed WHGame.dll
was ~50K functions, partially categorized, partition-able into ~12-20
subsystem-labeled subagents that would name and categorize functions. Ground
truth: **321,120 functions, 99.9% auto-named, ~0% game-subsystem labels, 61.5%
substantial.** There is nothing to cluster by and no way to recover 320K names
from a stripped, static-linked binary. But the Phase 9.x design never needed
names — it needs **discovery**, and discovery rests on data that is fully
mechanical to extract. The whole effort re-shapes from "64-160 reasoning
subagents that name + categorize" to "**one mechanical batch job that extracts
statements + a call graph + content anchors**, plus a thin curated name overlay."

---

## 1. The author UX this data serves (the north star)

A **non-expert** mod author changes game behavior without a disassembler, an
address, or a name they had to reverse-engineer. The path, entirely in-game:

1. **Discover** — the author opens the `~` console and searches by something they
   already observed:
   ```
   kcdx_find WHGame.dll --string "Crime reported"
   kcdx_find WHGame.dll --cvar ai_CombatRange
   kcdx_find WHGame.dll --callee IsInCombat
   ```
   The engine returns candidate **gameplay functions**, ranked.
2. **Inspect** — `kcdx_dev_inspect WHGame.dll <function>` shows the statements
   (kinds, pseudo-text, captures, applicable ops).
3. **Modify** — the author writes one line into `plugin.lua` using what the
   console showed, in author terms, zero assembly:
   ```lua
   kcdx.statement.replace_with("WHGame.dll", fn, kcdx.locator.first_call_to("CanAdd"),
                               kcdx.op.replace_with_return(1))
   kcdx.hook.insert_after("WHGame.dll", fn, kcdx.locator.first_call_to("IsInCombat"),
                          function(captures) ... end)
   ```

**The SQLite reference DB is invisible engine internals.** The author never
queries it, never knows it exists. The console commands are the entire window
onto it. This satisfies the disassembler-test cornerstone
([`cornerstones.md`](../../.claude/rules/cornerstones.md)) **through discovery,
not through pre-naming every function.**

**The UX bar is "magical":** the author types what they saw, the engine does the
heavy search and returns the right function. A heavy/powerful in-game search is
acceptable; optimize where cheap, but **power > search-cost** (user direction,
2026-05-26).

---

## 2. The discovery model — call-graph backbone, anchor + caller neighborhood

This is the load-bearing architectural decision. All discovery reduces to ONE
mechanism:

> **anchor → literal reference site → walk the caller call-graph upward →
> ranked hookable gameplay functions.**

- **Anchors are ENTRY POINTS, not answers.** A string, a cvar name, or a
  localization key is where the author *enters* the graph — it is not itself the
  function they want.
- **The call graph is the backbone.** The literal `"inventory full"` lives in a
  low-level UI/toast helper; the author wants the inventory-add guard a few call
  frames *up* that decided to show it. `find{...}` resolves the anchor's
  reference site, then walks UP the caller edges (callers, callers-of-callers,
  …), ranking results by graph distance.
- **Why this shape:** it leans the entire discovery surface on the ONE signal
  ground-truth-proven 93%+ reliable (the call graph), and demotes the sparse /
  unreliable signals (string literals at 8% coverage, cvars) to entry points
  rather than load-bearing indexes.

**All anchor types collapse into this one mechanism** (validated — §3):

| `find{...}` axis | How it enters the graph | Status |
|---|---|---|
| `--callee FN` | direct (the edge IS the query) | ✅ backbone, 93% |
| `--string "X"` | literal ref to "X" → walk up | ✅ literals resolve reliably |
| `--cvar NAME` | cvar name is a string literal → same as `--string` | ✅ 2,421 names present |
| `--text "on-screen text"` (literal key) | loc-XML maps text→key; key is a literal → walk up | ✅ ~350 keys |
| `--text "on-screen text"` (int-ID key) | loc-XML maps text→key; runtime dump maps key→int-ID; static ID→fn → walk up | ⏳ §6 sub-feature |
| `--name_contains X` | curated name overlay (§5) | ✅ where named |

---

## 3. Ground-truth evidence (why each decision holds)

All measured this session against the analyzed WHGame.dll. Full detail +
reproduction in the research log.

- **Scale:** 321,120 functions; 99.9% auto-named; only 352 named (those are
  linked CRT/MSVC/Win32, not game code); 281 have an RTTI namespace.
- **Noise does not collapse it:** 61.5% (197,501) are ≥64B non-thunk; 41% ≥128B.
  ~130-200K substantial real functions. No small clusterable core.
- **Decompile on stripped functions: 100%** (40/40 sampled), avg 128
  statements/fn. The statement backbone is reliable on unnamed code.
- **Callee/call-graph: 93%** (misses are genuine call-free leaves). The backbone.
- **String literals: resolve correctly when present, but only ~8% of functions
  reference one.** Hence anchors-are-entry-points, not per-function indexes.
- **Cvars: 2,421 distinct cvar-name literals** (`ai_*`=765, etc.). Cvar names ARE
  string literals → the cvar axis collapses into the string axis. No separate
  cvar-API/vtable model needed for discovery.
- **Localization text→key: plain XML** in per-language paks
  (`Localization/English_xml.pak` → `text_ui_*.xml`, `<Row><Cell>key</Cell><Cell>text</Cell>`).
  kcdx reads these data files directly; zero RE for the text↔key half.
- **CryEngine is static-linked into WHGame.dll** (no separate CrySystem /
  Cry3DEngine; 132 CryEngine strings inside WHGame.dll). The only separate
  modules at the game-bin root are `BugSplat64.dll`, `BugSplatRc64.dll`,
  `Quatmosphere.dll`, `WhGdk.dll`. WHGameArm.dll (ARM build) is out of scope.

---

## 4. The reference data — end-state shape

The dump is a **mechanical batch job**: no reasoning, no clustering, no naming
judgment. Two extraction products, both computable headless.

### 4a. Per-function + per-statement (mechanical, ALL functions)

Per function:

| Field | Source | Backs |
|---|---|---|
| `auto_name` (`FUN_<va>`) | Ghidra (always populated) | the address authors pass for uncategorized fns |
| `module`, `rva` | Ghidra | resolution |
| `content_hash` (BLAKE3) | hash of fn bytes | per-version survival check |
| `signature` | abi_walker (`_research/phase6-save-load/phase6_abi_walker.py`), NOT prologue-shape (AP2) | callback-hook arg marshalling |
| `decompile_quality` (`clean`/`partial`/`unanalyzable`) | Ghidra | engine gates `statement.*` on quality |

Per statement (each clean/partial function):

| Field | Backs |
|---|---|
| `idx`, `kind`, `pseudo_text` | `kcdx_dev_inspect`; locator matching |
| `byte_range_start`, `byte_range_len` | engine fit-or-trampoline at apply |
| `content_hash` | per-statement survival |
| `captures` (live registers/stack) | mid-function callback marshalling |
| `applicable_ops` | `statement.replace_with` validation (byte-emit must fit `byte_range_len`) |
| `callee` (if call) | `find{callee=}`, `first_call_to` |
| `string_ref` (if refs a literal) | `find{string=}`, `find{cvar=}` (cvar names are literals) |
| `cvar_ref` | redundant with `string_ref` for discovery; kept if cheap |

### 4b. The call graph (mechanical, binary-wide) — NEW, the backbone

The original brief captured only per-function callee lists. The discovery model
needs the **full caller↔callee edge set for the whole binary**, queryable in
both directions:

- `callee_edges(fn)` — what fn calls (already in per-statement `callee`).
- **`caller_edges(fn)` — what calls fn** (the upward walk `find{}` performs).

Stored as an edge table (`caller_function_id, callee_function_id`) indexed both
ways. This is what turns a low-level anchor site into the gameplay function a few
frames up.

### 4c. The curated name overlay (sparse, evidence-backed, NOT bulk)

`function_name` / `subsystem` are **not** dump columns populated at scale — they
are a thin curated layer, the SKSE-Address-Library model. A name is assigned ONLY
where evidence pins it:

- the function is already in `data/address-library/seed.csv`,
- it matches a predecessor-sig anchor (`_research/predecessor-sigs/`: 15 CryEngine
  interface headers, yobson1's AOBs, muyuanjin's gEnv/vtable offsets),
- a distinctive string/cvar literal identifies it.

Everything else stays null and is **fully usable via `find` + the returned
reference**. `inferred_purpose` (the field added earlier) stays optional,
evidence-anchored, non-load-bearing — populated only where a statement-level
anchor exists, never as a naming substitute, never backing ID assignment / hash
check / auto-naming. Names and subsystems are load-bearing (authors type them,
`find` filters on them); a fabricated name is worse than an absent one (`find`
handles absent gracefully). This is AP10 discipline applied to the overlay.

### 4d. SQLite schema delta

The schema in [`restructure-plan.md`](restructure-plan.md) Phase 9.1 (functions +
statements + applicable_ops + behaviors + meta) is sound and mostly unchanged.
The rework ADDS:

- a **`call_edges`** table (`caller_function_id`, `callee_function_id`), indexed
  both directions — the call-graph backbone (§4b).
- confirmation that `function_name` / `subsystem` are sparse-overlay columns, not
  dump-populated (§4c) — already noted in the schema row.
- **TWO localization tables** (see §6), sourced from the loc XML + the §6
  runtime dump:
  - **`loc_gameplay`** (`loc_key`, `on_screen_text`, `lang`, `int_id` nullable,
    `caller_function_id` nullable) — the ~13.7K UI/HUD/menu/item keys; backs
    `find{text=}` → function. The `caller_function_id` edges come from the live
    runtime dump (the by-ID-getter hook), NOT static analysis (§6 conclusion 2).
  - **`loc_content`** (`loc_key`, `on_screen_text`, `lang`) — the ~190K
    dialogue/quest/soul keys; text-queryable (find/retext a line) but carries no
    function edges (no gameplay function behind a spoken line).

---

## 5. How the data is produced — a batch job, not subagents

**No reasoning subagents.** The extraction is deterministic; it is a headless
Ghidra script run (the tool class already built — `EnumerateFunctions.java` is
the enumeration peer). Parallelism, if used at all, is mechanical sharding of a
deterministic job across address ranges for wall-time — NOT 64-160 agents making
naming/categorization judgments. The 64-160-subagent problem is gone because
there is nothing to reason about.

The brief's old §5 quality gates that still apply: `signature` via abi_walker not
prologue (AP2); `applicable_ops` validated by byte-emit synthesis; honest
`decompile_quality`; no raw RVA as a primary key (AP1); name+ABI together where
named (AP12). The gates that DON'T apply anymore: anything about subsystem
clustering or per-function naming coverage.

**Maintainer-side post-processing** (unchanged from the original §9, still valid):
IDs are maintainer-assigned, append-only, never recycled; matched across game
versions by name+signature+caller-graph fingerprint; import into
`data/reference.sqlite`. Subagents/scripts never assign IDs.

---

## 6. The localization `find{text=}` sub-feature (RE done; build pending)

Lets an author find the gameplay function behind on-screen text they saw
("Crime reported" → the function that decided to show it). The RE is COMPLETE
(`_research/parallel-ghidra-research/LOC-MANAGER-FINDINGS.md`); what remains is
the runtime-dump build. The findings below SUPERSEDE this section's original
"static ID→fn link" plan — that approach was probed and retired.

### What the RE established (verified, 2026-05-26)

- **text → key is free.** KCD2 localization is plain XML in the language paks
  (`Localization/English_xml.pak` → `text_ui_*.xml`,
  `<Row><Cell>key</Cell><Cell>text</Cell>`). kcdx reads it directly; no RE.
- **The loc manager is CryEngine's `CLocalizedStringsManager`** (static-linked).
  vtable @ `0x183dbcf90`; ctor `FUN_1809f0ce4` (RVA 0x9f0ce4); `LocalizeString`
  `FUN_18051d534`; `AddLocalizedString` `FUN_180eff9f4`.
- **The int-ID is a per-language `vector<entry*>` index** (the engine logs
  "Add new string <%u> with ID %d"); a parallel `map<key,entry*>` gives name
  lookup; key→text is **vtable slot 23**; **by-ID getters are slots 1/27/28**.
- **gEnv** — reuse muyuanjin's `"exec autoexec.cfg"` recipe
  (`gEnv = pConsole − 0xA8`, v1.4+ context `4C 8B 92 18 01 00 00`); no new RE.

### Two conclusions that reshape the build

1. **TWO loc tables, not one** (see §4d). 93% of the 203,807 loc keys are
   dialogue/quest CONTENT (spoken lines piped to a subtitle renderer — no
   gameplay function behind them) → a separate `loc_content` table, text-
   queryable but NOT walked for function discovery. The ~13.7K gameplay/UI keys
   (menus/items/HUD/misc/tutorials/ingame) are `loc_gameplay`, the only ones
   `find{text=}` resolves to functions.
2. **The static ID→function link is unreliable → the runtime dump IS the
   bridge.** Probed (two runs): the by-ID getters are virtually dispatched (can't
   cheaply isolate loc-manager receivers statically) and the `int id` is
   runtime-computed, not an immediate. So DON'T try to link ID→fn statically.
   Instead, hook the by-ID getters and observe the bridge LIVE: each real call
   yields `(caller-function, id)`, and the id maps to a key via the table. The
   dump's product is **`caller-function ↔ int-ID ↔ key`** — exactly what
   `find{text=}` consumes (text → key → id → the functions seen requesting it).
   Coverage tracks what the player exercises in-game, which fits the
   find-what-they-need goal (text a player sees is text that got requested).

### The build (route to `/feature` — multi-commit; real proof is a live launch)

Steps `/feature` decomposes into (results-driven order — prove the live unknowns
before building atop them):

1. **Minimal live probe FIRST.** Hook ONE by-ID getter (or hook the ctor
   `FUN_1809f0ce4` to capture the manager `this`), log raw `(caller-addr, id)`
   to `kcdx-dev.log`. Proves the hook fires + the getter ABI is right in-game —
   the checkable unknowns that gate everything after (AP10). Needs a live launch.
2. **Static key↔id table walk.** Walk the manager's `vector<entry*>` (table
   fields [9]/[10]) for the full key↔id map — no launch.
3. **Caller↔id↔key output.** Accumulate the live `(caller-fn, id)` edges across a
   play session, joined to the key↔id map, into the `loc_gameplay` feed.
4. **Test plugin + docs** (test-suite + docs-discipline rules).
5. **Verification checkpoint** — the live launch that confirms the dump.

**Why this is `/feature`, not `/execute`:** multi-commit (hook + table-walk +
output + test + docs), and its real verification is a live dump, not the
per-cycle build gate. Build-green can't confirm the hook ABI — only the launch
can (CLAUDE.md "build-green is necessary, not sufficient").

Two fields still unpinned (needed for the probe, not blocking the plan): the
entry struct's key/text offsets, and the `ISystem` slot that returns the manager
(hooking the ctor sidesteps the latter).

---

## 7. Tooling already built (this session, reusable)

Game-agnostic, in `third-party-ghidra/ghidra_scripts/` (Java, because Ghidra 12.1
dropped bundled Jython):

- **`EnumerateFunctions.java`** + **`enumerate-functions.ps1`** — the
  function-inventory dump (the §4a per-function peer). Read-only, parameterized
  (project path, modules, version tag, out dir), serves re-runs + other-game
  ports. Already produced `WHGame.dll.functions.csv` (321K rows).
- **`ProbeAnchorQuality.java`** — the quality probe that validated the model
  (decompile/string/callee/cvar coverage on unnamed functions).
- **Loc RE scripts** (§6): `FindLocManager.java` (anchor→manager methods),
  `DumpLocVtable.java` (vtable + getter shapes), `ProbeLocConsumers.java` /
  `ProbeLocIdCallers.java` (the bridge probes), and
  `_research/parallel-ghidra-research/measure_loc_coverage.py` (the text-key
  coverage / two-table split measurer). Recon outputs are the `loc-*.txt` +
  `loc-coverage-result.txt` beside the findings doc.

The full statement+anchor+call-graph extractor (the §4a/§4b production dump) is
NOT yet built — it is the next tool, an extension of the same class. The loc
runtime-dump probe (§6) is the other not-yet-built tool.

---

## 8. Build steps + routing (the execution plan)

In dependency order. Each routes to the skill that gives it the right discipline.

| # | Step | Skill | Needs launch? | Status |
|---|---|---|---|---|
| 0 | Commit the reusable tooling + research logs + this plan | `/commit` | no | **DONE** (`fae1d86`, `f92afc3`) |
| 1 | **Compute-sizing probe** — time statement+anchor+edge extraction on a ~1K-fn sample, extrapolate to 321K. The feasibility gate for the full-binary dump. | in-context measurement | no | pending |
| 2 | **Build the production extractor** — statements + hash + signature + string/callee anchors + caller-graph edges, emitting the §4 CSV/JSONL. Mechanical batch job. | `/feature` | no | pending |
| 3 | **Run the full dump** over WHGame.dll (+ the 4 separate DLLs); import to `data/reference.sqlite`. | batch run + maintainer import | no | pending |
| L1 | **Loc-manager RE** — locate `CLocalizedStringsManager`, getters, int-ID = vector index. | `/research-disassembly` | no | **DONE** (`LOC-MANAGER-FINDINGS.md`) |
| L2 | **Build the loc runtime-dump probe** — hook the by-ID getters / capture manager-`this`, walk the key↔id table, emit `caller↔id↔key` for `loc_gameplay`; + `loc_content` text table. Step 1 = minimal hook-fires/ABI probe (§6). | `/feature` | no (build); **yes** (run) | pending |
| L3 | **Run the loc dump** (a play session) → integrate edges into `loc_gameplay` feeding `find{text=}`. | launch + import | **yes** | pending |
| 7 | **Engine-side** Phase 9.1-9.4 (SQLite load, `hash_at`, `kcdx.find`, console cmds) consume the data — per `restructure-plan.md`. | `/feature` per phase | per phase | future |

Compute-sizing (step 1) gates step 2's scope: it confirms whether full-binary
extraction is a feasible batch job vs needing a bounded subset. Per
`results-driven.md`, measure it before committing the extractor's scope. The loc
track (L1-L3) is independent of the core dump track (1-3) and can proceed in
parallel; both feed `data/reference.sqlite` and the engine-side consumers (7).

---

## 9. What did NOT change from the original brief

These decisions survive the rework intact:

- **`content_hash` = BLAKE3.**
- **IDs maintainer-assigned, append-only, never recycled**; matched across game
  versions by name+signature+caller-graph fingerprint (the call graph the §4b
  edges now make first-class).
- **Re-run model for new game versions** — same extractor re-run; hash-match
  auto-detects unchanged functions; changed functions surface for re-verify.
- **Other-game ports** — the extractor + this model carry over; another game
  ships its own `reference.sqlite` keyed on its module names.
- **The dump never edits SQLite/seed.csv/address_library directly, opens no PRs**
  — it produces data; the maintainer imports.

---

## 10. Why the original subagent-partition model was retired

Recorded so the decision isn't relitigated. The original brief partitioned
WHGame.dll into ~12-20 subsystem-labeled subagents (inventory, combat, …) that
would name + categorize 2-5K functions each. Three ground-truth facts killed it:

1. **No labels to cluster by** — 99.9% auto-named, the named 0.1% is library code.
   The subsystem partition list presumed a categorization the binary lacks.
2. **Wrong scale** — 321K ÷ 2-5K = 64-160 subagents, not 12-20.
3. **Names aren't recoverable and aren't needed** — you cannot recover 320K names
   from a stripped binary, and the Phase 9.x UX never required them; it requires
   discovery, which is mechanical. Naming was solving a problem the design
   doesn't have.

The replacement (mechanical batch extraction + call-graph backbone + sparse
curated overlay) does what the author UX actually needs, at feasible cost, and is
honest to the data. Proceeding with the old model would have theorized a
structure the binary does not have (AP10).

---

## Related

- [`_research/parallel-ghidra-research/inventory/ENUMERATION-FINDINGS.md`](../../_research/parallel-ghidra-research/inventory/ENUMERATION-FINDINGS.md) — the research log + reproduction recipes + raw numbers this plan rests on.
- [`restructure-plan.md`](restructure-plan.md) — Phase 9.1-9.6; the engine work that consumes this data. Schema at its Phase 9.1.
- [`data/address-library/policy.md`](../../data/address-library/policy.md) — naming + ID convention the curated overlay inherits.
- [`.claude/rules/reverse-engineering.md`](../../.claude/rules/reverse-engineering.md) — the RE methodology + reuse ladder (the loc-manager + abi_walker steps follow it).
- [`.claude/rules/cornerstones.md`](../../.claude/rules/cornerstones.md) — the disassembler test, satisfied here by discovery.
- [`.claude/rules/results-driven.md`](../../.claude/rules/results-driven.md) — measure compute (step 1) + probe loc int-ID before theorizing (§6).
