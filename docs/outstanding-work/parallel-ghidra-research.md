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
| `signature` | abi_walker (`phase6_abi_walker.py`), NOT prologue-shape — see §4e for the honest fidelity (width-typed floor now; caller-side register-arg estimate; verified static arity is NOT achievable; exact arity/types via the declare/share overlay, never fabricated) | callback-hook arg marshalling |
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

### 4e. The `signature` fidelity — DECIDED (A + B + D, NOT C)

The abi_walker is the sanctioned ABI tool, but a cross-check during the build
established what it can and CANNOT establish from a callee-body stack scan, and
an architect review confirmed the engine consumes the seed signature at a fidelity
that matches. (This decision was made during the §8 step-2 build; restored here
after a doc regression.)

**What the abi_walker proves from the callee body:** per-arg ACCESS WIDTH
(8/16/32/64-bit) + a gpr-vs-xmm int/float hint. **What it does NOT prove:**
- **True arity** — the raw stack-slot count is unreliable both ways (large-frame
  locals inflate it; register-resident args never spilled are invisible). The
  7-arg `SaveGame` recovery was a HUMAN reading caller-side setup, not the raw
  count.
- **Per-slot type** — width cannot distinguish `i32`/`u32`/`f32`, `ptr`/`i64`, or
  `cstr`/`wstr`/`ptr`.

**The engine already speaks this boundary** (`docs/lua/hook.md` `HOOK_SIG_GATE`):
a wrong arg-count or return-width is a HARD conflict (`crash_risk=true`); a
per-slot type nuance is a SOFT warning. A wrong arity in the seed is a crash risk
with no warning — so a fabricated count is worse than none.

**The decided strategy — three mechanisms, no fabrication:**
- **(A) Honest width-typed seed for the whole binary, NOW.** `produce_signatures.py`
  emits a WIDTH-TYPED signature using only the coarsest honest type (8-byte
  gpr→`i64`, 4→`i32`, xmm→`f64`/`f32`), return marked unknown (`?`),
  `signature_source = abi_walker`, `abi_confidence = count+width`. NEVER
  `ptr`/`u*`/`cstr`/`wstr`. `observed_arg_slots` is an explicit lower-bound FLOOR,
  not a verified count.
- **(B) Caller-side REGISTER-arg estimate** (`produce_caller_reg_args.py`,
  `caller_reg_args/`). A feasibility probe FALSIFIED "verified static arity"
  (Outcome C: ~30% of functions are vtable-dispatched with NO static caller —
  incl. `SaveGame`; and the stack-arg side is unfixable noise). The salvaged win:
  the caller's REGISTER-arg count (rcx/rdx/r8/r9, capped at 4) recovers cleanly
  (`FUN_180001050`→3). It ships as a NON-authoritative tighter floor
  (`count+width+caller_reg`), merged over `signatures/` by `max(observed_arg_slots,
  caller_reg_arg_count)`. NOT "verified" — exact only for ≤4-arg functions.
- **(D) Exact per-slot types + exact arity** ride the Phase-9.3 declare/share
  overlay (`kcdx.dll.declare` / `kcdx.functions.*`) — the cornerstone's
  declare-once/share answer for what the engine cannot pre-know.
- **(NOT C) Ghidra's typed prototype is NOT adopted as a verified signature** — it
  stays the `signature_source = ghidra` floor that abi_walker (count+width) wins
  over at merge.

**Net author value:** discovery (`kcdx.find`), `kcdx.statement.*`, and
`kcdx.hook mode=mid` do NOT touch the signature and are full. Only the typed
entry-hook callback consumes it — and there the author hooks by name and gets
address + a width-typed frame with a register-tightened floor, the engine flags
any mismatch, and exact types/arity come from the declare/share overlay.

### 4f. The shipped artifacts — TWO DBs, user-vs-dev split — DECIDED 2026-05-27

The import (§8 step 3b, `tools/refdata-extractor/python/import_to_sqlite.py`)
produces **TWO** SQLite DBs from one full dump, NOT one. This split + the sizing
below were decided against MEASURED full-binary numbers (the earlier ~150 KB /
~7 MB estimates in `restructure-plan.md` §9.1 / §9.3 were written before the dump
existed and are wrong at full scale — see the corrections below).

**USER DB — `reference.sqlite`, ships in every kcdx release (~48 MB on disk /
~22 MB in the release zip):**
- Tables: **`functions` + `signatures` + `caller_reg_args`.**
- Why these: a mod USER's runtime needs (a) the per-launch cross-version
  **survival check** (`functions.content_hash`), and (b) the **marshalling ABI a
  callback hook needs at install time** (`functions.signature` — confirmed
  load-bearing: a `kcdx.hook` a user's plugin installs needs the signature to
  marshal args; compiled C++ has no runtime-queryable ABI, `restructure-plan.md`
  §9.3 "The signature is the one irreducible thing"). `signatures`/`caller_reg_args`
  are the abi_walker floor (§4e). Dropping `signatures` would break callback-hook
  plugins for end users — so it MUST ship.
- `statements` / `referenced_vars` / `call_edges` are NOT in the user DB — a user
  never runs `kcdx.find` / `kcdx_dev_inspect` (those are author discovery), and
  `call_edges` only powers `find`'s caller-graph ranking.

**DEV DB — `reference-dev.sqlite`, on-demand author download (~1.13 GB on disk /
~397 MB zipped):**
- Tables: the full six (USER set + `statements` + `referenced_vars` + `call_edges`).
- Mod AUTHORS fetch it to build plugins (`kcdx.find`, `kcdx_dev_inspect`); it is
  the maintainer's source-of-truth. Never shipped to users.

**Encoding (lossless; in `import_to_sqlite.py`):** `content_hash` 64-hex TEXT →
32-byte BLOB; low-cardinality repetitive TEXT (`kind`/`storage_kind`/`data_type`/
`edge_reason`/`signature_source`/…) → small INTEGER FK into per-(table,col)
`_dict_*` lookup tables; hex/decimal address+count columns → INTEGER. Verified
lossless (the `0x1050` anchor's hash round-trips BLOB→hex exactly; dict FKs
resolve). The encoding shrinks the on-disk file; the release zip's deflate handles
the download (~2.6×). NOTE: dictionary-encoding only helps LOW-cardinality
columns — at full scale `storage_detail` had 88K distinct values (high
cardinality), so it stays effectively un-dicted; the dict win is real only for
the genuinely-repetitive columns.

**Launch UX (measured, MATTERS): the DB size is irrelevant to launch speed.**
SQLite mmaps the file and reads only touched pages — it does NOT load 48 MB into
RAM. The survival check is **lazy + indexed**: it queries only the functions a
user's installed plugins hooked. A heavy-TC user (~500 hooked functions) →
**~12 ms** at launch (Python upper bound; the C engine is faster). Negligible vs
a 30–60 s game launch. So the 48 MB is a download/disk concern only, never a
launch-perf one — which is why chasing it smaller (e.g. dropping the 157K-row
`caller_reg_args`) was NOT worth the complexity.

**Delivery is seamless — no decompression code.** The ~48 MB `reference.sqlite`
ships UNCOMPRESSED inside the release zip (the zip's deflate gives the ~22 MB
download for free); the engine opens the plain `.sqlite` directly. This honors
`restructure-plan.md` §9.1 "No CSV, no diff chain, no install-time assembly step"
— do NOT add an engine-side decompress step.

**Corrections to `restructure-plan.md` §9.1 / §9.3** (stale pre-dump estimates):
- "~150 KB" is the SQLite *amalgamation* (the vendored library), NOT the DB — the
  user DB is ~48 MB, the dev DB ~1.13 GB.
- "~7 MB resident, ~200-300 ms one-time" (§9.3, the EAGER `kcdx.functions.*`
  population) is an AUTHOR/dev-mode path, not the user path. A normal user's
  engine should do the LAZY survival lookup (~12 ms), not eager-load all 321K.
- "One shipped file … carries ALL per-function hashes, per-statement metadata,
  applicable-ops, behavior catalog" — corrected: the per-statement metadata is
  DEV-only (not in the user ship); the user file carries function hashes +
  signatures only.

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

> **As-built location (2026-05-27):** the production extractor lives at top-level
> **`tools/refdata-extractor/`** (`ghidra/` Java passes + launcher + vendored
> BLAKE3 + the hash contract; `python/` emit passes + probes + the validation
> harness; `run-parallel.ps1` the parallel orchestrator) — committed, tracked
> tooling. (It was briefly misfiled under `_research/` + `third-party-ghidra/`
> and lost when those gitignored trees were stripped; rebuilt into `tools/`,
> commits `f64f4dc` + `614f563`.) The Ghidra install/project + the enumeration
> CSV stay gitignored/local. The harness (`python/validate_extractor_output.py`)
> is the falsifiable regression net — 26 checks vs independent anchors.

| # | Step | Skill | Needs launch? | Status |
|---|---|---|---|---|
| 0 | Commit the reusable tooling + research logs + this plan | `/commit` | no | **DONE** (`fae1d86`, `f92afc3`) |
| 1 | **Compute-sizing probe** — decompile + abi_walker per-fn cost on a ~1K-fn sample, extrapolate to 321K. | in-context measurement | no | **DONE** (full-binary feasible; abi_walker ~4.7× cheaper than decompile) |
| 2 | **Build the production extractor** — functions + statements + referenced_vars + call_edges (Java) + signatures + caller_reg_args (Python) → §4 CSV-per-table RVA-sharded dirs. | `/feature` | no | **DONE** (`tools/refdata-extractor/`; harness 26/26; BLAKE3 35/35) |
| 2p | **Parallel orchestrator + RVA-range filter** — N workers over disjoint ranges on per-worker project copies, merge by disjoint shards. (Ghidra locks a project exclusively — per-worker COPIES are required, probe-verified.) | `/feature` | no | **DONE** (`run-parallel.ps1`; `614f563`) |
| 3a | **Run the full dump** over WHGame.dll. | batch run | no | **DONE** (8-way parallel, 2026-05-27; 321,120 functions; 5.24M statements; 10.88M referenced_vars; 1.52M call_edges; output at `C:\kcdx-refdata\refdata-full-20260527-105617\`, 1.3 GB; every anchor verified at full scale) |
| 3b | **Import the dump → SQLite** (maintainer-side, `import_to_sqlite.py`): build the encoded schema, load the CSV-per-table dirs, emit the USER + DEV DBs (the two-DB split + encoding + sizes are §4f). | maintainer import tool | no | **DONE** (`3c033be`; USER `reference.sqlite` 48MB/22MB-zip, DEV `reference-dev.sqlite` 1.13GB/397MB-zip; integrity-verified; built to `C:\kcdx-refdata\db\`). **Stable-ID assignment is the one remaining piece — deferred** (a single-version import has no prior IDs to match; `--assign-ids` arrives with the 2nd game version). |
| 3c | **Secondary DLLs** (BugSplat64, BugSplatRc64, Quatmosphere, WhGdk) — NOT yet imported into the Ghidra project; dump them after import (own re-run). | import + batch run | no | future |
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
