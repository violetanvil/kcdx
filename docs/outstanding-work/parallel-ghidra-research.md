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

- the function is already in `data/seeds/address_names_seed.csv`,
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

The schema in [`restructure/00-original-plan.md`](restructure/00-original-plan.md) Phase 9.1 (functions +
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

**SUPERSEDED by §11.8 STREAMLINE.** The section below describes the pre-streamline
USER-as-bulk-superset framing (USER ~48 MB carrying ALL ~321K functions). The
streamlined model narrowed USER to CURATED ONLY (~0.1 MB, ~140 entities); the
bulk lives only in DEV (~1.13 GB) for `kcdx.find` author discovery. Read §11.8
for the current model. The historical reasoning below is kept as the record of
what the pre-streamline sizing analysis established (and why the bulk-USER
framing seemed correct at the time).

The import (§8 step 3b, `data/refdata-extractor/python/import_to_sqlite.py`)
produces **TWO** SQLite DBs from one full dump, NOT one. This split + the sizing
below were decided against MEASURED full-binary numbers (the earlier ~150 KB /
~7 MB estimates in `restructure/00-original-plan.md` §9.1 / §9.3 were written before the dump
existed and are wrong at full scale — see the corrections below).

**USER DB — `reference.sqlite`, ships in every kcdx release (~48 MB on disk /
~22 MB in the release zip):**
- Tables: **`functions` + `signatures` + `caller_reg_args`.**
- Why these: a mod USER's runtime needs (a) the per-launch cross-version
  **survival check** (`functions.content_hash`), and (b) the **marshalling ABI a
  callback hook needs at install time** (`functions.signature` — confirmed
  load-bearing: a `kcdx.hook` a user's plugin installs needs the signature to
  marshal args; compiled C++ has no runtime-queryable ABI, `restructure/00-original-plan.md`
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
`restructure/00-original-plan.md` §9.1 "No CSV, no diff chain, no install-time assembly step"
— do NOT add an engine-side decompress step.

**Corrections to `restructure/00-original-plan.md` §9.1 / §9.3** (stale pre-dump estimates):
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
> **`data/refdata-extractor/`** (`ghidra/` Java passes + launcher + vendored
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
| 2 | **Build the production extractor** — functions + statements + referenced_vars + call_edges (Java) + signatures + caller_reg_args (Python) → §4 CSV-per-table RVA-sharded dirs. | `/feature` | no | **DONE** (`data/refdata-extractor/`; harness 26/26; BLAKE3 35/35) |
| 2p | **Parallel orchestrator + RVA-range filter** — N workers over disjoint ranges on per-worker project copies, merge by disjoint shards. (Ghidra locks a project exclusively — per-worker COPIES are required, probe-verified.) | `/feature` | no | **DONE** (`run-parallel.ps1`; `614f563`) |
| 3a | **Run the full dump** over WHGame.dll. | batch run | no | **DONE** (8-way parallel, 2026-05-27; 321,120 functions; 5.24M statements; 10.88M referenced_vars; 1.52M call_edges; output at `data/refdata-extractor/dump/refdata-1.5.1164953/`, 1.3 GB; every anchor verified at full scale. Note: the 2026-05-27 run wrote to `C:\kcdx-refdata\refdata-full-20260527-105617\`; on 2026-05-28 the dump was moved into the repo + renamed to the version-keyed convention.) |
| 3b | **Import the dump → SQLite** (maintainer-side, `import_to_sqlite.py`): build the encoded schema, load the CSV-per-table dirs, emit the USER + DEV DBs (the two-DB split + encoding + sizes are §4f). | maintainer import tool | no | **DONE (first cut; being reshaped by §11)** (`3c033be`; USER `reference.sqlite` 48MB/22MB-zip, DEV `reference-dev.sqlite` 1.13GB/397MB-zip; integrity-verified). **SUPERSEDED by the §11 finalized design:** the reshape adds the `function_hashes` history table, v1.5 baseline `kcdx_id` assignment for ALL functions, the curated `overlay`, the cut/fix pass, and the two deprecation axes. The cross-version MATCHER is the §11.6 sandbox problem (not this import). |
| 3c | **Secondary DLLs** (BugSplat64, BugSplatRc64, Quatmosphere, WhGdk) — NOT yet imported into the Ghidra project; dump them after import (own re-run). | import + batch run | no | future |
| L1 | **Loc-manager RE** — locate `CLocalizedStringsManager`, getters, int-ID = vector index. | `/research-disassembly` | no | **DONE** (`LOC-MANAGER-FINDINGS.md`) |
| L2 | **Build the loc runtime-dump probe** — hook the by-ID getters / capture manager-`this`, walk the key↔id table, emit `caller↔id↔key` for `loc_gameplay`; + `loc_content` text table. Step 1 = minimal hook-fires/ABI probe (§6). | `/feature` | no (build); **yes** (run) | pending |
| L3 | **Run the loc dump** (a play session) → integrate edges into `loc_gameplay` feeding `find{text=}`. | launch + import | **yes** | pending |
| 7 | **Engine-side** Phase 9.1-9.4 (SQLite load, `hash_at`, `kcdx.find`, console cmds) consume the data — per `restructure/00-original-plan.md`. | `/feature` per phase | per phase | future |

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
  edges now make first-class). **REFINED by §11:** the v1.5 baseline assigns ids
  to ALL functions now (not just curated); the cross-version MATCHER that
  re-attaches an id after the bytes change is the §11.6 open problem.
- **Re-run model for new game versions** — the EXTRACTOR re-runs over the new
  binary to produce a fresh dump, but the DB is **updated IN PLACE / appended**,
  NOT rebuilt (§11.2 "append-only, updated in place"): the version-update path
  opens the existing DB, adds a `game_versions` row, and the §11.6 matcher
  extends-or-splits each entity's interval. **The hash CANNOT auto-detect "same
  function" after a change** (the hash changing is the tracked event, §11.1) —
  re-identification is the §11.6 fingerprint matcher; each version APPENDS
  `entity_versions` (and trigger-paired `kcdx_overlay_versions`) intervals rather
  than overwriting.
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

## 11. FINALIZED DESIGN — the entity-versioning DB + curated overlay (2026-05-27)

This section is the **controlling spec** for the DB's shape. It supersedes the
single-version framing in §4f / §9 wherever they conflict: the DB's CORE PURPOSE
is not a one-version survival snapshot — it is a **validity-interval history over
every targetable entity, keyed on a stable identity**, so the engine and the
maintainer can answer *"in which game version did the thing my mod targets change,
and therefore which mods went out of date?"* — for any target kind (function
body, curated site, vtable/data slot), not just functions.

Everything here except the cross-version MATCHING MECHANISM is finalized. The
mechanism is an open problem solved separately (§11.6) in a synthetic sandbox.

### 11.1 The core purpose (why this is a DB, not a flat per-version file)

A mod targets a function. When a game update changes that function's bytes, the
mod's assumptions about it may no longer hold → the mod is out of date *for that
function*. The DB exists to make this query trivial:

```sql
SELECT valid_from, valid_through, content_hash FROM entity_versions
  WHERE entity_kind = 'function' AND entity_id = 1;
-- -> every byte-form of function 1 over time, as version INTERVALS. Each row's
--    valid_from IS a version it changed. valid_through IS NULL = the current form.
--    A plugin is out of date if its authored hash != the open (NULL) row's hash.
```

This requires a **stable identity** (`kcdx_id`) that survives a function's bytes
changing AND its rva moving across versions. The hash CANNOT be that identity —
the hash changing is the very event being tracked. So `kcdx_id` is assigned once
(v1.5 baseline) and re-attached to the same logical function in every later
version by the cross-version matcher (§11.6).

### 11.2 The schema — LOCKED 2026-05-27 (SUPERSEDED 2026-05-28 by §11.9)

**Superseded by §11.9 FLATTEN.** The 9-table entity/version + curated-overlay
shape below is kept as the historical record of how the design got to the
flatter form (it captures the entities + entity_versions + kcdx_overlay +
kcdx_overlay_versions split that the flatten dissolved). Read §11.9 for the
current 5-table USER + 8-table DEV schema (address_names + address_versions,
no entities/entity_versions, no kcdx_overlay/kcdx_overlay_versions split, no
pairing trigger).

The schema is **9 tables** (+ `_dict_*`). The earlier separate `functions` /
`signatures` / `caller_reg_args` tables are GONE — folded into `entity_versions`
(their data is per-byte-form, derived from the bytes, so it lives on the interval
row). The `overlay` / `versions` tables are renamed `kcdx_overlay` /
`game_versions`, and the curated layer SPLITS into identity (`kcdx_overlay`) +
temporal (`kcdx_overlay_versions`) — the verified signature/offset/slot/status
move with the binary, so they version exactly like `entity_versions`. The model
is uniform: identity tables (`entities`, `kcdx_overlay`, the `modules` /
`game_versions` registries) each paired with their temporal interval table where
facts can move across a patch.

**Conventions (locked):**
- Every table has an autoincrement `id` PK **except `entities`** (its PK *is*
  `kcdx_id`, the single global id-authority — one PK makes `kcdx_id` collision
  impossible across the whole DB).
- **`kcdx_id` is globally unique** (the `entities` PK). Every other table FKs to
  it. A signature/curated/function id can never collide because they all draw
  from the one `entities` sequence.
- Low-cardinality columns are dict-encoded (`_dict_*`); a `_dict_*` ships in
  whichever DB its parent column ships in.
- **USER? ✅** = ships in the USER `reference.sqlite`; **❌** = DEV-only. The USER
  DB serves only two runtime jobs: cross-version SURVIVAL + hook ABI/address
  RESOLUTION. Everything else (discovery, prose, provenance) is DEV-only.

#### `modules` — module registry (USER ✅)

| Column | Type | Null | USER? | Meaning |
|---|---|---|---|---|
| `id` | INTEGER PK | no | ✅ | |
| `name` | TEXT | no | ✅ | `WHGame.dll`, `BugSplat64.dll`, … (§3c secondary DLLs). |

#### `game_versions` — version registry (USER ✅)

| Column | Type | Null | USER? | Meaning |
|---|---|---|---|---|
| `id` | INTEGER PK | no | ✅ | FK target for the intervals. |
| `tag` | TEXT | no | ✅ | Human version `1.5.1164953` (from `whdlversions.json` MasterMasterPGO config). |
| `ordinal` | INTEGER | no | ✅ | Sort key = the game build number (`1164953`). The game's own monotonic counter → backfill-safe (an earlier 1.4 build is a smaller number; no renumber). |
| `released` | TEXT | yes | ✅ | Release date if known; optional. |

#### `entities` — the id authority (USER ✅)

One row per entity, ever. Version-INDEPENDENT identity only.

| Column | Type | Null | USER? | Meaning |
|---|---|---|---|---|
| `kcdx_id` | INTEGER PK | no | ✅ | Globally-unique stable id. The handle a mod references. Assigned at v1.5 baseline, append-only, never recycled. |
| `entity_type` | INTEGER (dict) | no | ✅ | `function` \| `vtable_slot` \| `data_slot` \| `callsite` \| **`statement`** (reserved, unpopulated — see below). |
| `module_id` | INTEGER FK→`modules` | no | ✅ | Which module — version-independent (WHGame.dll's functions stay in WHGame.dll across game versions), so it lives on identity, not per-interval. |

#### `entity_versions` — temporal forms, the spine (USER ✅, subset of columns)

One row per (entity, version-interval). Carries the per-byte-form facts: hash,
location, AND the abi_walker floor (folded in — it is DERIVED from the bytes, so
it is per-byte-form, not per-entity; `produce_signatures.py` proves this).

| Column | Type | Null | USER? | Meaning |
|---|---|---|---|---|
| `id` | INTEGER PK | no | ✅ | |
| `kcdx_id` | INTEGER FK→`entities` | no | ✅ | The entity. |
| `content_hash` | BLOB(32) | yes | ✅ | BLAKE3 of this byte-form. NULL for non-byte kinds. The survival-check value. |
| `rva` | INTEGER | yes | ✅ | Address of this form. Resolve id→address. |
| `length` | INTEGER | yes | ✅ | Byte length — REQUIRED to reproduce the hashed span `[rva, rva+length)`; a dependency of `content_hash`, not extra data. |
| `value` | INTEGER | yes | ✅ | Payload for non-byte kinds (vtable slot int, data-slot offset) — the entire resolved value for those entities. |
| `signature` | TEXT | yes | ✅\* | abi_walker width-floor (`? (i64, i32)`). |
| `observed_arg_slots` | INTEGER | yes | ✅\* | Arg-count lower-bound floor. |
| `caller_reg_arg_count` | INTEGER | yes | ✅\* | Caller-side reg-arg estimate (≤4). |
| `caller_arg_agreement` | INTEGER (dict) | yes | ✅\* | Caller-site agreement (`agree`/`spread:MIN..MAX`). |
| `auto_name` | TEXT | yes | ❌ | `FUN_<rva>` — discovery DISPLAY only, never a resolution key. |
| `decompile_quality` | INTEGER (dict) | yes | ❌ | Gates `statement.*` (an authoring surface). |
| `valid_from` | INTEGER FK→`game_versions` | no | ✅ | First version this form held. |
| `valid_through` | INTEGER FK→`game_versions` | yes | ✅ | **NULL = still current.** Last version valid. |

Indexes: `(kcdx_id)`; partial UNIQUE `(kcdx_id) WHERE valid_through IS NULL` (one
current form per entity — a corrupt close-and-open fails loudly at insert);
`(rva)`.

**✅\*** = the four ABI-floor columns are USER **pending the D3 engine-fact gate**
(does the user engine re-derive a hook's marshalling ABI from the DB at install,
or does the plugin carry its own baked signature?). Ship-in-USER is the safe
default until verified; if "plugin bakes it," they drop to DEV-only. Resolve at
the Phase 9.1 engine-consumer design.

`SELECT * WHERE kcdx_id=N` → the full version history. "Is my target still
valid?" → `… AND valid_through IS NULL`, compare hash. "Which versions changed
it?" → the set of `valid_from` values. **Removal = the latest row is closed
(`valid_through` set) with no later open (`NULL`) row** — DERIVED from the
intervals, NOT a separate column.

**v1.5 baseline (now):** every function gets ONE open interval (`kcdx_id`, hash,
rva, length, `valid_from=1`, `valid_through=NULL`); curated sites/slots likewise.
`game_versions` gets row 1 (`1.5.1164953`). When v1.6 arrives and the MATCHER
(§11.6) runs, a CHANGED entity's interval is closed (`valid_through=1`) and a new
open interval opens (`valid_from=2, valid_through=NULL`).

**Why `statement` is a reserved-but-unpopulated entity_type (consult 2026-05-27):**
statement-level survival is ALREADY free under the function interval — the
function `content_hash` covers the whole body `[rva, rva+length)` and each
statement hash is a SUB-RANGE of that exact span (BLAKE3-HASH-CONTRACT §2a/§2b).
So **function-hash-unchanged ⟹ every statement in it is byte-identical** — a
proven invariant: no statement byte can change (or move) without flipping the
function hash. Populating 5.24M per-statement intervals would buy NOTHING on
survival, and would force a SECOND cross-version matcher (statements have no
name/call-graph fingerprint). Reserved so later population is no migration;
staged to the Phase 9.x statement-survival design. (Options A/B/C weighed; C —
general schema, staged statement fill — chosen.)

#### `kcdx_overlay` — curated NAME IDENTITY, version-independent (USER ✅, subset)

The maintainer's authoring source-of-truth; the generator projects its verified
rows into seed.csv/kEntries[]. Stays a SEPARATE sparse sidecar (139 rows —
folding onto the 321K-row identity table would make a super-wide mostly-NULL
table). Holds ONLY the version-INDEPENDENT facts of a curated name (the name
itself, its kind, its deprecation state, its provenance — none of which change
because the game patched). The version-DEPENDENT verified facts (signature,
offset, slot, status — which DO move with the binary) live in
`kcdx_overlay_versions`, mirroring the `entities` ↔ `entity_versions` split.
**`id` PK; `kcdx_id` is a NON-UNIQUE FK** — many name-rows may share one entity
(supersession: OldName + NewName both point at one `kcdx_id`).

| Column | Type | Null | USER? | Meaning |
|---|---|---|---|---|
| `id` | INTEGER PK | no | ✅ | The stable *name-row* identity (distinct from the entity's `kcdx_id`). |
| `kcdx_id` | INTEGER FK→`entities` | no | ✅ | The entity annotated (NON-unique — supersession allows multiple rows per entity). |
| `name` | TEXT | yes | ✅ | Gameplay name ("IsInCombat"). NULL allowed (id-before-name). The resolution key. |
| `kind` | INTEGER (dict) | no | ✅ | **9 values** (the seed.csv reality): `function` (108) \| `function_no_sig` (~9) \| `function_variadic` (4) \| `callsite` (3–4, carries `offset`) \| `data_slot` (3) \| `string_anchor` (1) \| `instruction_anchor` (1) \| `vtable_base` (3) \| `vtable_index` (6). |
| `is_deprecated` | INTEGER | no | ✅ | Name-deprecation flag; the old name still resolves (§11.4). |
| `superseded_by` | INTEGER FK→`kcdx_overlay.id` | yes | ✅ | The overlay ROW (name) that replaces this one — you supersede a NAME, not the function. |
| `source` | INTEGER (dict) | no | ❌ | Provenance tier (seed.csv's separate `source`; kept, not folded into status). |
| `notes` | TEXT | yes | ❌ | seed.csv prose; never in USER (also the public-clean reason). |

Index: `ix_ov_name` on `name`.

#### `kcdx_overlay_versions` — verified curated facts that move with the binary (USER ✅)

The version-DEPENDENT half of a curated entry, as validity INTERVALS — mirrors
`entity_versions`. One row per (curated name, version-interval). When a game patch
changes IsInCombat's ABI or shifts a callsite offset or a vtable slot, the
maintainer closes the old interval and opens a new one; the NAME (`kcdx_overlay`)
is untouched.

| Column | Type | Null | USER? | Meaning |
|---|---|---|---|---|
| `id` | INTEGER PK | no | ✅ | |
| `overlay_id` | INTEGER FK→`kcdx_overlay.id` | no | ✅ | The curated name these facts belong to. |
| `signature` | TEXT | yes | ✅ | The VERIFIED DSL ABI for this version range (distinct from the `entity_versions` floor). NULL for non-plain-function kinds. |
| `offset` | INTEGER | yes | ✅ | Callsite consumer offset (the `+13` / `-4`) for this version. |
| `vtable_slot` | INTEGER | yes | ✅ | Slot int for `vtable_index` rows for this version (the slot-32-vs-33-across-builds case). |
| `status` | INTEGER (dict) | no | ✅ | `verified` \| `unverified` **for this version** (verified-on-1.5 ≠ verified-on-1.6 until rechecked). Only verified resolves at runtime. |
| `valid_from` | INTEGER FK→`game_versions` | no | ✅ | First version these verified facts hold. |
| `valid_through` | INTEGER FK→`game_versions` | yes | ✅ | NULL = current. |

Index: `(overlay_id)`; partial UNIQUE `(overlay_id) WHERE valid_through IS NULL`.
**No `rva`** — the curated entity's ADDRESS comes from its `kcdx_id`'s
`entity_versions` interval (one source of truth for location). **No
`verified_on_version` / `authored_against_version`** — the interval's
`valid_from`/`valid_through` IS the version range. **No `signature_source`** — it
was always `curated` in the overlay (a constant), dropped.

**Seed id reconciliation:** the seed.csv ids (1000–3106) are NOT preserved — each
seed row matches the v1.5 baseline `entities.kcdx_id` AT ITS rva (the row's rva →
the baseline entity there → its kcdx_id); non-code rows (`vtable_index`) mint a
curated-only `entities` row. The old seed id-space is discarded (no collision
with the 1..321120 bulk baseline).

#### `meta` — DB header, one row (USER ✅)

| Column | Type | Null | USER? | Meaning |
|---|---|---|---|---|
| `id` | INTEGER PK | no | ✅ | Always 1. |
| `schema_version` | INTEGER | no | ✅ | Consumers (engine + generator) verify the DB shape before trusting it — fail loud on a mismatch. |
| `abi_confidence` | TEXT | no | ✅ | The floor policy (`count+width+caller_reg`). |

(`module` → `modules` + `entities.module_id`; `game_version` → `game_versions`.
Both dropped as single-version/single-module relics the new model obsoletes.)

#### `statements` — per-statement discovery (DEV-only ❌)

5.24M rows; the entire table is DEV-only (ship-tier boundary).

| Column | Type | Null | USER? | Meaning |
|---|---|---|---|---|
| `id` | INTEGER PK | no | ❌ | |
| `kcdx_id` | INTEGER FK→`entities` | no | ❌ | Owning function. |
| `idx` | INTEGER | no | ❌ | Statement ordinal. |
| `kind` | INTEGER (dict) | no | ❌ | call/assign/branch/… |
| `pseudo_text` | TEXT | yes | ❌ | Decompiled line. |
| `byte_range_start` | INTEGER | no | ❌ | Statement code start (RVA). |
| `byte_range_len` | INTEGER | no | ❌ | Statement code length. |
| `content_hash` | BLOB(32) | yes | ❌ | Per-statement hash. |
| `callee` | TEXT | yes | ❌ | NULL'd when redundant `FUN_<rva>`; kept when named (memset etc.). |
| `string_ref` | TEXT | yes | ❌ | Referenced literal (matcher signal). |

(Cut from the dump: `cvar_ref`, `edge_reason` — 100% empty.)

#### `referenced_vars` — per-statement var storage (DEV-only ❌)

| Column | Type | Null | USER? | Meaning |
|---|---|---|---|---|
| `id` | INTEGER PK | no | ❌ | |
| `kcdx_id` | INTEGER FK→`entities` | no | ❌ | Owning function. |
| `statement_idx` | INTEGER | no | ❌ | Owning statement. |
| `storage_kind` | INTEGER (dict) | yes | ❌ | register/stack/global. |
| `data_type` | INTEGER (dict) | yes | ❌ | Approx type. |
| `storage_detail` | TEXT | yes | ❌ | High-cardinality; stays un-dicted. |

#### `call_edges` — the call graph (DEV-only ❌)

1.52M rows; powers `kcdx.find` ranking + the cross-version matcher (§11.6).

| Column | Type | Null | USER? | Meaning |
|---|---|---|---|---|
| `id` | INTEGER PK | no | ❌ | |
| `caller_kcdx_id` | INTEGER FK→`entities` | no | ❌ | Calling function. |
| `callee_kcdx_id` | INTEGER FK→`entities` | no | ❌ | Called function. |

(Cut: `edge_reason`. Indexed both directions.)

#### Provenance — how each column-group is obtained (the build's source map)

| Column-group | Source |
|---|---|
| `entities.kcdx_id` / `entity_type` | maintainer-assigned at the v1.5 baseline (id 1..321120 per dumped function; `entity_type=function` for the dump, slots/sites from the curated seed). |
| `entities.module_id`, `modules.name` | maintainer-supplied at import (the dumped module's filename). |
| `game_versions.tag` / `ordinal` | `<game>/whdlversions.json`, the **MasterMasterPGO** config's `versionId` — `tag=1.5.1164953` (branch `release_1_5` + build), `ordinal=1164953` (the game's monotonic build number). PROBE-verified. (The DLL has no PE VS_VERSIONINFO resource, but it does intern the version twice as a `.rdata` string — at va=0x183c3edef and va=0x183dba258 in 1.5.1164953 — so tools other than this importer may also read it from the DLL directly.) |
| `entity_versions.rva` / `length` | Ghidra `Function.getEntryPoint()−imageBase` / `getBody().getNumAddresses()` (FunctionPass). |
| `entity_versions.content_hash` | BLAKE3 of on-disk `[rva, rva+length)`, no normalization (hash contract; `ContentHash.java`). |
| `entity_versions.value` | the resolved integer for non-byte kinds (vtable slot / data offset) — from the curated seed. |
| `entity_versions.signature` / `observed_arg_slots` | `produce_signatures.py` (abi_walker width-floor; honest width-types only, never fabricated). |
| `entity_versions.caller_reg_arg_count` / `caller_arg_agreement` | `produce_caller_reg_args.py` (caller-side reg-arg scan; non-authoritative floor). |
| `entity_versions.auto_name` / `decompile_quality` | Ghidra (`FUN_<rva>` label; decompiler clean/partial/unanalyzable). DEV-only. |
| `entity_versions.valid_from` / `valid_through` | the import: baseline = `valid_from`=v1.5, `valid_through`=NULL; later = the §11.6 matcher closes/opens. |
| `kcdx_overlay.*` (identity) | seed.csv (`name`, `source`, `notes`); `kind` inferred from the row shape + notes; `kcdx_id` matched seed-rva → baseline entity. |
| `kcdx_overlay_versions.signature` / `offset` / `vtable_slot` | seed.csv (`signature`; offset + slot parsed from notes). `status` from seed.csv `status`, or auto-`unverified` by the pairing trigger when a byte-form changes. |
| `statements.*` / `referenced_vars.*` | Ghidra decompiler per statement (StatementPass). DEV-only. |
| `call_edges.*` | Ghidra call-graph edges (CallEdgePass). DEV-only. |
| `meta.schema_version` / `abi_confidence` | constants set by the import. |

**The cuts (vs the current dump):** the `functions` table (collapsed to
`entities`+`entity_versions`); `functions.signature` (zero-ABI `undefined
FUN_<rva>()`); `functions.signature_source` / `function_name` / `namespace`
(single-value / CRT-only); `auto_name` + `decompile_quality` cut from USER (kept
DEV); `statements.cvar_ref` / all `*.edge_reason` (100% empty);
`statements.callee` NULL'd when redundant `FUN_<rva>`; the separate `signatures`
/ `caller_reg_args` tables (folded into `entity_versions`); `meta.module` /
`meta.game_version` (→ `modules` / `game_versions`); the overlay's
`verified_on_version` / `authored_against_version` / `signature_source` (the
interval's `valid_from`/`valid_through` replaces the first two; the third was a
constant `curated`).

#### The import tool: two modes + the verified version source

The import is `data/refdata-extractor/python/import_to_sqlite.py`, with two modes:

- **Rebuild mode (`--rebuild`, non-default):** from-scratch baseline build from a
  dump dir → a fresh DB. Builds the v1.5 baseline NOW; also the path to use if the
  schema itself changes.
- **Update mode (DEFAULT):** the incremental append (§"append-only" below):
  1. find the most-recent version in the DB (`max(game_versions.ordinal)`);
  2. read the game's on-disk version (source below);
  3. if the disk version is newer → run the version-update import (matcher +
     append). With only v1.5 present this default-runs to "already current,
     nothing to do" — the full step-3 append lands with the §11.6 matcher.

**The version source — PROBED + VERIFIED 2026-05-27 (a checkable unknown, not
assumed):**
- **WHGame.dll has NO PE version resource** — `VS_FIXEDFILEINFO` + `FileInfo`
  both absent (the resource dir is a 480-byte icon/manifest stub). So the DLL is
  NOT the version source.
- **The source is `<game>/whdlversions.json`.** It carries PER-CONFIGURATION
  build ids under `Configurations[]`; the build number is NOT global — Animations
  =1089519, Shared=1069729, **MasterMasterPGO=1164953**, Profiling=1164953. The
  detector MUST read the **`MasterMasterPGO`** configuration (the SHIPPED game
  config — the live game runs from `Bin/Win64MasterMasterSteamPGO/`), via
  `Configurations[].SelectedVersion.versionId`
  (`kcd2_release_1_5_PC_MasterMasterPGO_1164953_7490`), and ignore the other
  configs' differing numbers. Branch = `release_1_5`.
- **`tag`** = branch + build → `1.5.1164953` (matches seed.csv). **`ordinal`** =
  the MasterMasterPGO build number `1164953` — the GAME's own monotonic counter
  (an earlier 1.4 build is a smaller number), so it is **backfill-safe**: a later
  back-import of v1.4 sorts BEFORE v1.5 with no renumbering (which the append-only
  DB requires — you cannot renumber). (Steam's `appmanifest_1771300.acf` buildid
  22819807 is also monotonic but is Steam's depot counter, not the game build —
  not used.)
- `game_versions` v1.5 row: `ordinal=1164953`, `tag='1.5.1164953'`, optionally the
  full `versionId` string for provenance.

#### The DB is APPEND-ONLY and UPDATED IN PLACE per version (not rebuilt)

This is a load-bearing model fact. The DB is NOT regenerated from the dump each
game version — it is **updated in place, appending** the new version's rows to
the existing DB:

- The v1.5 import is the **baseline BOOTSTRAP** — it builds the DB from the dump
  from scratch (the current `import_to_sqlite.py` path).
- v1.6+ is an **incremental VERSION-UPDATE** — a different tool path that opens
  the existing DB and appends: a new `game_versions` row; for each entity, the
  matcher (§11.6) either extends the open interval (unchanged → leave it) or
  closes it and opens a new one (changed); paired overlay-version intervals
  follow (below). The DB grows; old rows are never rewritten.
- This is WHY `kcdx_id` is append-only / never recycled: a stable id must survive
  every in-place update so a plugin authored against v1.5 still resolves after the
  DB has been updated through v1.9. A rebuild-from-dump model would not need
  stable ids; the append-in-place model is exactly what makes the version history
  (and cross-version plugin survival) possible.

#### Trigger-enforced invariant: a curated entity's intervals stay paired

**Every open `entity_versions` interval for an entity that has a `kcdx_overlay`
row MUST have a paired open `kcdx_overlay_versions` interval.** Because the DB is
written across many version-update sessions (above), this is enforced by a
**SQLite trigger at write time**, not left to the exporter:

- When a new `entity_versions` interval opens for an entity that has a
  `kcdx_overlay` row, the trigger closes the entity's current
  `kcdx_overlay_versions` interval and opens a new one — `valid_from` = the new
  version, the prior interval's `signature`/`offset`/`vtable_slot` **carried
  forward as a starting point**, but **`status = unverified`**.
- Carrying the facts forward (not NULL) keeps them as a re-verify reference; the
  `unverified` status is the correctness gate — a byte-form that changed has NOT
  been re-verified, so the overlay must NOT assert a verified ABI for it (that
  would be an AP2/AP12 false-verified). The name still RESOLVES (its address comes
  from `entity_versions`), but the verified-ABI gate reports "unverified on this
  version" until a maintainer re-checks and flips it to `verified`.
- This gives the maintainer a precise worklist after every game update: every
  `kcdx_overlay_versions` row with `status = unverified` on the current version is
  a re-verify task.
- The trigger guards the invariant for the DB's whole append-only life, across
  every write path (baseline build, version-update, a future ad-hoc maintainer
  edit) — not just the one exporter run, which is why it lives in the DB, not the
  tool.

### 11.3 The author reference surface — exactly two handles + the hatch

The author NEVER sees a `FUN_<rva>` and NEVER references one (it's not stable and
doesn't exist materially — it's a render of the rva). Resolution is:

| Tier | Author writes | Resolves via | Stable? |
|---|---|---|---|
| Verified-named | `target = "IsInCombat"` | `kcdx_overlay.name` → (`kcdx_id` → current `entity_versions` interval for the address) + (current `kcdx_overlay_versions` interval for the verified ABI/slot/offset) | ✅ |
| Discovered | `target = <kcdx_id>` | `entities.kcdx_id` → current `entity_versions` interval → address | ✅ (append-only id) |
| Expert hatch | `pattern = "48 8B …"` / `bytes` | scan | ✅ (re-derived per version by the author) |

`kcdx.find` (the in-game author console, the ONLY runtime DB reader, dev-mode,
reads the DEV DB) hands out a **kcdx_id**, never a `FUN_` string. The id is the
forever-stable handle; a curated `name` is an OPTIONAL later maintainer
attachment to that id. ID minting is **maintainer-only** (no in-game promote).

### 11.4 Two SEPARATE deprecation axes (do not conflate)

| Axis | Lives on | Set when | Means |
|---|---|---|---|
| **Name** deprecation | `kcdx_overlay.is_deprecated` + `superseded_by` (→ another overlay ROW) | maintainer renames | "call it NewName; OldName still resolves to the SAME live function" |
| **Function** removal | DERIVED from `entity_versions` (latest interval closed, no later open row) | the maintainer CONFIRMS a `deleted_candidate` proposal (§11.6) | "this function no longer EXISTS in the game" |

The name axis is rows in `kcdx_overlay` (a `kcdx_id` may have BOTH an OldName row
— `is_deprecated=1`, `superseded_by`→the new row — and a NewName row, which is why
`kcdx_id` is a non-unique FK there). The removal axis is NOT a column — it falls
out of the interval model (§11.2), and is applied by `reconcile_transition.py`
only for a maintainer-CONFIRMED deletion (never auto — a missed match looks like a
deletion, §11.6). Both populate only when a version-update is reconciled (a
single-version baseline has nothing to deprecate/remove).

### 11.5 What the v1.5 baseline import builds NOW

**Schema-shape SUPERSEDED 2026-05-28 by §11.9** -- the entity_versions /
kcdx_overlay / kcdx_overlay_versions tables named below are gone (flattened to
address_names + address_versions). The *baseline-build narrative* (what gets
populated and from where) still applies; the *table targets* now point at
`address_versions` (which absorbed entity_versions + kcdx_overlay_versions)
and `address_names` (which absorbed kcdx_overlay). Read §11.9 for the
ground-truth table layout. The text below is kept as historical record.

The matcher can't run with one version, but the baseline must exist so v2 has
stable anchors and the matcher's raw signals are preserved:

1. **Assign `kcdx_id` to ALL 321,120 v1.5 functions** (the baseline, ids 1..N).
   The curated overlay rows are matched into this id-space by rva (the seed row's
   rva → the baseline function there → its kcdx_id); the old seed ids (1000–3106)
   are discarded, NOT preserved. Non-code curated rows (`vtable_index`) get a
   curated-only id.
2. **`entity_versions`** seeded with the v1.5 baseline as OPEN intervals — one per
   function (`kcdx_id`, hash, rva, length, `valid_from=1`, `valid_through=NULL`),
   plus curated sites/slots. `game_versions` gets its first row (`1.5.1164953`,
   ordinal 1); `modules` gets `WHGame.dll`.
3. **Preserve the matcher's signals** — `call_edges` + `statements.string_ref`
   stay (DEV DB); they are the cross-version fingerprint inputs (§11.6).
4. The cut/fix pass + the overlay schema (`kcdx_overlay` identity +
   `kcdx_overlay_versions` temporal) + the seed from seed.csv's 139 rows: each row
   becomes ONE `kcdx_overlay` identity row (name/kind/deprecation/source/notes) +
   ONE open `kcdx_overlay_versions` interval (verified signature/offset/slot/
   status, `valid_from=1`, `valid_through=NULL`), per §11.2 + the decomposition.

### 11.6 The cross-version matcher — THE open problem (solved in a sandbox)

**Problem.** Given function Y in v1.5 (rva, bytes, hash) and v1.6 where Y's rva
moved AND its bytes changed, find the v1.6 function X that IS Y. The hash can't do
this (it changed). Identity must rest on near-invariants:

- **call-graph fingerprint** (who Y calls / who calls Y) — `call_edges`; a
  fixpoint match since edges are themselves expressed via other ids.
- **referenced string/cvar literals** — `statements.string_ref`.
- **curated name + signature** — the ~139 high-confidence seeds that bootstrap.
- **relative position / ordinal** — weak, tie-breaker only.

**The matcher is PROPOSE-ONLY; it mints NOTHING and deletes NOTHING.** Minting a
`kcdx_id` is an append-only, irreversible commitment to the id-space; closing an
interval with no successor is an irreversible "removed." A MISSED match and a
GENUINELY-NEW entity look IDENTICAL to the matcher (both: "no confident v1
match") — and so do a MISSED match and a real DELETION from the v1 side. A
silent guess either way corrupts the version history the DB exists to protect (a
false "NEW" splits one logical function into two ids; a false "DELETED" buries a
live function). So the matcher must NOT adjudicate "new" or "deleted" — it
surfaces them for maintainer confirmation. Its output per entity:

| Verdict | Meaning | Who acts |
|---|---|---|
| `MATCHED <v1_kcdx_id>` | confident re-identification | auto — extend/open the interval under the existing id |
| `AMBIGUOUS [candidates]` | could be one of several v1 entities (split/merge/tie/trap) | maintainer picks |
| `UNMATCHED` | no confident v1 match — *might* be new, *might* be a missed match | **maintainer verifies before any id is minted** |
| `DELETED-CANDIDATE` | a v1 entity with no v2 match — *might* be removed, *might* be a missed match | maintainer confirms removal |

**Three phases, two prod DBs, one scratch DB.** The matcher's proposals are
durable worklist DATA (reviewed across sessions), so they live in a TABLE, in a
SEPARATE per-transition scratch DB — not a flat report, not the prod DBs.

- **Phase 1 — MATCH** (`match_versions.py`, automated, propose-only): reads the v1
  prod DEV DB (the baseline `entities` + the matcher signals `call_edges` /
  `statements.string_ref`) and the v2 dump; writes a `match_proposals` row per v2
  entity into a gitignored scratch DB `transition_<v1>_to_<v2>.sqlite`, all
  `resolution = pending`. NO prod mutation. The proposals table (DEV-tier
  workflow data, never in any shipped DB):

  | Column | Meaning |
  |---|---|
  | `id` | PK |
  | `from_version` / `to_version` | the transition (FK→`game_versions`) |
  | `v2_locator` | the v2 entity (its dump rva) |
  | `verdict` | `matched` \| `ambiguous` \| `unmatched` \| `deleted_candidate` (dict) |
  | `proposed_kcdx_id` | the best-guess existing id (NULL for `unmatched`) |
  | `confidence` | a CONTINUOUS score 0.0–1.0 |
  | `candidates` | for `ambiguous`: candidate ids + per-candidate scores |
  | `evidence` | per-signal breakdown (call-graph overlap fraction, shared-string count, position delta) — why the score is what it is |
  | `resolution` | `pending` \| `approved` \| `rejected` (dict) |
  | `resolved_kcdx_id` | the maintainer's decision (matched id / chosen candidate / a mint-new request) |

- **Phase 2 — REVIEW** (maintainer): triage `match_proposals` (sort by
  `confidence`, inspect `evidence`), set `resolution` + `resolved_kcdx_id`. The
  human gate; minting/deleting is decided HERE, never by the matcher.

- **Phase 3 — RECONCILE** (`reconcile_transition.py`): apply ONLY
  `resolution = approved` rows to the prod DEV DB — mint new `entities`/`kcdx_id`
  for confirmed-new, close+open `entity_versions` for matched-but-changed,
  close-with-no-successor for confirmed deletions, append the `game_versions`
  row (the pairing trigger forks the curated overlay-versions automatically) —
  **then re-derive the USER DB from DEV by the same `USER_COLUMNS` projection the
  baseline import uses.** USER is ALWAYS a strict projection of DEV, so the two
  prod DBs cannot drift; there is one definition of "what USER contains," shared
  by the baseline build and every version update.

Nothing irreversible happens without confirmation. Consistent with "the DB is
maintainer-editable only; promotion is a deliberate maintainer process" (§11.3).

**This is NOT coded blind.** Building a matcher with no second version to
validate against would be theorizing an unfalsifiable mechanism (AP10). The
validation vehicle is a **synthetic two-version sandbox** at
`data/refdata-extractor/sandbox/` (the fixture DATA is gitignored — a real
WHGame.dll dump slice; the `make_sandbox.py` recipe + the matcher code are
tracked):

- **v1** = a real ~200-function slice of the actual WHGame.dll dump (real
  functions, edges, hashes, strings — not toy data), copied into `sandbox/v1/`.
- **v2** = an AUTHORED mutation of v1 (`make_sandbox.py` applies a hand-written
  recipe), so we KNOW the ground-truth v1→v2 mapping by construction. Emitted
  alongside as `ground_truth.csv` (per v2 entity: the expected
  `MATCHED <id>` / `NEW` / `DELETED` / `SPLIT_OF` / `MERGE_OF`).
- The mutation is **comprehensive on the trip-up cases**: identical; moved-
  unchanged; changed-body-same-identity; changed+moved; deleted; added; split
  (1→2); merged (2→1); **fingerprint-swap trap** (two functions exchange
  call-targets/strings, to bait a cross-assignment); string-only-anchor leaf (no
  edges); renamed-callee ripple (a neighbor's change shifts callers'
  fingerprints); curated-entity changed (the overlay-version trigger path).
- The matcher (`match_versions.py`) reads `sandbox/v1/` + `sandbox/v2/` (the two
  dump dirs — "point it at both"), produces its report, and **self-scores against
  `ground_truth.csv`**: it is correct when it confidently MATCHES what it should
  AND correctly ABSTAINS (UNMATCHED/AMBIGUOUS/DELETED-CANDIDATE) on everything it
  cannot be sure of — never silently minting or deleting. A false confident-match
  and a silent mint are both FAILs; an honest abstention is a PASS.

The matcher arrives mechanically with the 2nd KCD2 version, but is BUILT and
validated against the sandbox first.

### 11.7 Sequence (the immediate plan)

**Schema-shape names SUPERSEDED 2026-05-28 by §11.9.** Item-2's mention of
`entity_versions` + `versions` reflects the prior shape; under the flatten the
target tables are `address_versions` and `game_versions`. The sequence is
otherwise as written.

1. **This design — written down** (this §11). The finalized shape, minus the
   matcher mechanism.
2. **Generate the new DB** — the cut/fix pass + overlay + v1.5 baseline ids +
   `entity_versions` (open intervals) + `versions` (the feature decomposition).
   UNBLOCKS the other agent (the generator + engine consumer read this shape).
3. **The sandbox + the matcher + reconcile** (§11.6) — the
   `data/refdata-extractor/sandbox/` fixture (real v1 dump slice + authored v2
   mutation + `ground_truth.csv`), then `match_versions.py` (phase 1, self-scored
   against ground truth) and `reconcile_transition.py` (phase 3, apply-to-DEV +
   project-USER). Built and validated against the sandbox; run for real when v1.6
   lands.

**STATUS (2026-05-27):** items 1 + 2 DONE — the design is locked (§11) and both
prod DBs are regenerated in the locked schema (the generator/engine agent is
unblocked); the two-mode import CLI + version detector ship. Item 3's ambition
was REDUCED — see §11.8 below.

### 11.8 STREAMLINE (2026-05-27, late) — three tracks, no bulk matching

The §11.6 design assumed the matcher would auto-track all 321,120 functions
across versions via the validity-interval apparatus. Feasibility arithmetic
killed that: even a 90% matcher on 321K leaves 32,000 unmatched per patch — no
human reviews tens of thousands. **The size of the problem isn't the binary; it's
the union of functions installed plugins ACTUALLY attach to**, which is bounded
by AUTHORING EFFORT (human-scale: dozens to low thousands even for a deep TC).

The streamlined model has THREE tracks; the §11.6 matcher is RE-SCOPED, NOT
dropped (it stays useful at the right scale):

| Track | Source | Scale | Cross-version mechanism | Matcher? |
|---|---|---|---|---|
| **1. Curated (kcdx-shipped named targets)** | maintainer, by hand, per patch | ~139, growing slowly | author writes `target = "IsInCombat"`; kcdx ships per-version mappings the maintainer maintains | **YES** — re-scoped to **assist the maintainer's per-patch re-verification of the small curated set** (auto-confirm hash-equal, propose for changed, flag ambiguous). Feasible because the input set is ~139, not 321K — at that scale even a mediocre matcher saves real work. |
| **2. Author-declared (TC + bespoke)** | author, in their own plugin | bounded by author effort | `kcdx.declare(module, name, [versions_kv])` (§11.8.1 below); resolved once at launch against the running game version | **No** — the author owns their own versions; kcdx provides the canonical version string they key against |
| **3. Bulk DEV DB (discovery)** | regenerated per game version | 321K per version | per-version snapshot only; `kcdx.find` / `kcdx_dev_inspect` consult it for WITHIN-version discovery | **No** — never cross-version-tracked. Authors who discover a bulk function via this DB then declare it in their own plugin (Track 2). |

#### 11.8.1 Track-2 surface — `kcdx.declare(module, name, [versions_kv])`

The universal mechanism for any value that depends on the game version — hook
targets, output handling, constants, masks, offsets, anything. The shape:

```lua
-- per-version table: explicit keys + wildcard, no range objects
kcdx.declare("WHGame.dll", "combatResolver", {
  ["1.5.1164953"] = { pattern = "48 8B 05 ?? ?? ?? ?? 8B" },
  ["1.6.*"]       = { pattern = "48 8B 0D ?? ?? ?? ?? 8B" },
})

-- a version-independent constant: per-version values
kcdx.declare("WHGame.dll", "combatStateMask", {
  ["1.5.1164953"] = 0x0F,
  ["1.6.*"]       = 0x1F,
})

-- table omitted: attempt on ALL versions (the simpler "this works everywhere" path)
kcdx.declare("WHGame.dll", "combatResolver", { pattern = "48 8B 05 ?? ?? ??" })

-- thereafter, refer by name — engine resolves to the right per-version value:
kcdx.hook{ target = "combatResolver", after = function(ret)
  if (ret & "combatStateMask") ~= 0 then ... end
end }
```

**Rules locked in this session:**
- **`module` is REQUIRED** on every `declare` (positional first arg). No default
  module — kcdx exists to enable cross-module plugins eventually, and a defaulted
  module silently misroutes when secondaries get involved.
- **Version keys:** explicit (`"1.5.1164953"`) and wildcard (`"1.5.*"`) only. NO
  range objects (`{from=..., to=...}`).
- **Table omitted** = attempt on all versions (the low-ceremony common case).
- **Engine resolves ONCE at launch** using kcdx's canonical game-version string
  (already verified — sourced from `<game>/whdlversions.json` MasterMasterPGO
  config). Authors never see the version source; they just key on the version
  string kcdx surfaces.
- **The DB tables that back this:** `entities.module_id` already exists.
  `kcdx.declare` is implemented purely Lua-side (author's plugin) + an engine
  resolver — it does NOT write to the prod refs DBs.

This generalization deprecates the older "the engine knows every function across
all versions" framing — the engine doesn't have to, because the author tells it
per-name what to look for.

#### 11.8.2 The "attempt on undeclared versions" UX — default ON, UI badge

Default behaviour is **DEFAULT ON** (the engine attempts to resolve a Track-2
plugin on a game version it didn't declare for) — because the alternative
(default OFF = silent breakage on every patch until every author ships an update)
is a worse failure mode. Default-ON respects that most patches don't break most
things.

- **Only meaningful for Track-2 plugins on undeclared versions.** Pure curated
  plugins are already version-safe (Track 1); Track-2 plugins running on a
  declared version are already declared-safe.
- **Surfaced as a UI badge** in the future `kcdx.exe` UI (pre-UI: a launch-log
  line) — NOT a per-plugin checkbox the user must pre-decide. The badge fires
  contextually when there IS something to surface. **Two badge levels:**
  - *"Author certified through ≤ X; you're on Y. May or may not work."* —
    untested-on-this-version; pattern hasn't been tried yet.
  - *"Pattern did not resolve on your version."* — author's most-recent declared
    pattern returned no address. Hotter badge; the plugin almost certainly does
    not work as intended.

#### 11.8.3 Safety architecture — graceful failure, NOT pre-checking

The §11.6 / restructure-plan tenet-6 model was *"the engine pre-checks function
hashes; refuses to install if the bytes changed."* That model rested on
auto-tracking every function — which the streamline drops. **We can no longer
pre-check most things.** The honest replacement is RECOVERY, not pre-check:

- **Pure curated-name plugin:** survival works (the ~139 are tracked, the
  maintainer carries the cross-version mapping). This is the only "we can
  pre-check" path.
- **Track-2 plugin on a declared version:** the author certified it — load and
  run; runtime failures are caught (below) like any other.
- **Track-2 plugin on an UNDECLARED version (default-ON path):** we cannot
  pre-verify it will work. We can only catch failure modes:
  - **Install-time failure** (pattern doesn't resolve, ABI mismatch, the bytes at
    the site look wrong) → kcdx rolls back any partial installation for that
    plugin, leaves the game state clean, the plugin doesn't load, badge fires.
  - **Runtime failure** (a hook installed cleanly but fires with wrong args,
    callback throws/crashes) → kcdx catches at the callback boundary, disables
    the offending plugin for the rest of the session, badge upgrades to "failed
    at runtime," game continues running.

**Honest user promise:** *"plugins certified for your version work; plugins not
certified may fail to install (handled cleanly) or fail at runtime (caught, the
plugin disables, the game keeps running) — your save and game stability are
protected, but a specific plugin may not function on an uncertified version."*
That's the SKSE model — a plugin built for the wrong Skyrim version doesn't
crash everything; it just doesn't work.

**LOAD-BEARING ENGINE WORK (not yet built):** the recovery/rollback machinery
default-ON safety REQUIRES is NOT in place today. Tracked as a new outstanding
item in `restructure/00-original-plan.md` — see "Recovery + rollback for Track-2 plugins on
undeclared versions" there. **Default-ON shipping waits on that work.**

#### 11.8.4 What changes in the schema, given the streamline

The schema itself stays mostly intact (the generator agent's consumption path is
unaffected); ONE apparatus simplifies:

- **`entity_versions` COLLAPSES in ambition.** It was sized for "auto-tracked
  interval history for all 321K." That's no longer the job. It becomes:
  per-version hash baselines for the BOUNDED SET kcdx actually tracks (the
  curated 139 + any future targets the maintainer explicitly versions). Same
  table; drastically smaller scale; no matcher driving it across the bulk.
- **Bulk DEV DB becomes per-version snapshots** — one DB per game version, for
  discovery only, never diffed across versions automatically.
- **The pairing trigger stays** — still correct for the curated set's
  overlay-versions.
- **Everything else is unchanged.** `entities` (the global kcdx_id authority),
  `kcdx_overlay` / `kcdx_overlay_versions`, `modules`, `game_versions`, `meta` —
  all retained. The generator/engine agent's consumption path is intact.

#### 11.8.5 What's parked vs dropped vs alive

| Item | Status under the streamline |
|---|---|
| `match_versions.py` (the sandbox matcher) | **Re-scoped, NOT dropped.** Future role: maintainer-side assist for the curated 139 (auto-confirm + short review list per patch). Sandbox + ground-truth fixture stay as its validation harness. The 7/12 hard-case score is USEFUL at 139-scale, not failing. |
| `make_sandbox.py` + fixture | **Alive.** Validates whatever matcher we keep for the curated set. The 13 trip-up cases stay relevant. |
| `entity_versions` as 321K-auto-tracked interval history | **Collapses** to bounded curated/targeted set. |
| Reconcile / propose-only / `match_proposals` table machinery | **Re-scopes** to the bounded curated workflow — same propose-only contract, same maintainer-gated apply, just at 139-scale not 321K-scale. |
| `data/refdata-extractor/sandbox/BREAKAGE-MATRIX.md` (uncommitted scaffolding) | **Superseded + delete.** Its job (derive measures for the all-321K-tracked world) is dissolved by the three-track model. The reason is recorded in SANDBOX-STATUS so the deletion isn't unexplained. |
| `data/refdata-extractor/sandbox/SANDBOX-STATUS.md` | **Refresh.** Update to reflect the streamline: matcher re-scoped (not pending hash redesign); breakage-matrix superseded; three-track model is the answer. |
| Track-2 `kcdx.declare(module, name, versions)` Lua surface | **Designed in this session, NOT implemented.** New work: author UX, engine-side resolver, integration with `kcdx.hook` / `kcdx.bytes` / `kcdx.code` so they accept the declared name as a target. |
| Per-plugin user "attempt on undeclared versions" surfacing | **Designed, NOT implemented.** Default-ON; UI badge in `kcdx.exe` (two levels); not a per-plugin checkbox. |
| Recovery + rollback machinery (install-time + runtime) | **Designed, NOT implemented.** Load-bearing for default-ON safety — tracked in restructure-plan as new outstanding work. Default-ON shipping waits on this. |

#### 11.8.6 Next steps when this resumes

1. Refresh `SANDBOX-STATUS.md` + delete `BREAKAGE-MATRIX.md` (with reason
   recorded in STATUS).
2. Collapse `entity_versions` to the bounded curated/targeted set (schema is
   already correct; the only change is ambition + what the importer/reconcile
   populates).
3. Design + implement the Track-2 surface (`kcdx.declare`, the resolver, the
   integration with `kcdx.hook`/`bytes`/`code` so they accept a declared name).
4. Design + implement the recovery/rollback machinery (the restructure-plan
   item). Default-ON safety depends on it.
5. Re-purpose the parked matcher to the curated-set assist role.

### 11.9 FLATTEN 2026-05-28 — the schema as actually shipped

The §11.2 schema split entities (the kcdx_id authority) and entity_versions
(temporal byte-form facts) into two tables, then split the curated layer into
kcdx_overlay (identity) + kcdx_overlay_versions (temporal verified facts), and
used a SQLite trigger to keep the two version tables paired across game-version
inserts. That structure was correct but over-built once the streamline narrowed
USER to curated-only:

- `entities` only ever held (kcdx_id, entity_type, module_id) -- a thin
  id-authority table whose every row had a matching entity_versions row.
- `kcdx_overlay` held (id PK, kcdx_id FK non-unique, name, kind, ...) and was
  1:1 with kcdx_overlay_versions in practice (no aliases at baseline).
- The trigger existed solely to mirror entity_versions inserts onto
  kcdx_overlay_versions -- only needed because the two were separate.

Flattened: **one table for the curated names, one for the per-version resolve
facts. No id-authority table, no two-table-split curated layer, no trigger.**

The user direction (2026-05-28): "overlay is the canonical lookup for kcdx
stuff. overlay id should be kcdx_id - this is the one that should never change.
we just need to be able to map them to overlay_versions - which can now store
the module ID and the stuff needed to resolve." Followed by: "address_names.id
IS kcdx_id" (no separate kcdx_id column) and "superseded_by points to another
address_names.id" (entity-to-entity supersession).

**USER tables (5):** `modules`, `game_versions`, `address_names`,
`address_versions`, `meta`.

**DEV adds (3):** `statements`, `referenced_vars`, `call_edges`.

#### `address_names` -- the curated entity registry

One row per curated entity, ever. `id` IS the kcdx_id (AUTOINCREMENT starting
at 1; sequential per the order curated entities are added). The kcdx_id is
the stable cross-version handle a plugin references; it does NOT track the
bulk function set's rva-ordinal (those are separate id-spaces -- see
`address_versions` below).

| Column | Meaning |
|---|---|
| `id` (PK, AUTOINCREMENT) | the **kcdx_id** -- the stable cross-version handle plugins reference. Sequential 1..N in addition order. There is NO separate kcdx_id column; the PK IS the handle. |
| `name` | the curated name (`IsInCombat`, etc.). Unique per row. |
| `is_deprecated` | 1 if this entity is superseded by another entity. |
| `superseded_by` | another `address_names.id` -- entity-to-entity supersession (rename + identity change). |
| `source` (DEV) | provenance tier dict. |
| `notes` (DEV) | the maintainer's provenance prose. |

#### `address_versions` -- per-version resolve facts (the spine)

One row per (entity, version-interval). **`kcdx_id` is NULLABLE.** When set,
FKs to `address_names.id` (the row is a curated entity). When NULL, the row is
a bulk uncurated DEV function -- present only in the DEV DB, never in USER.

In USER: every row has `kcdx_id IS NOT NULL` (curated only). In DEV: ~143
curated rows + ~321K bulk rows with `kcdx_id NULL`.

| Column | Meaning |
|---|---|
| `id` (PK, AUTOINCREMENT) | the universal "which function row" handle. DEV-only tables (statements, referenced_vars, call_edges) FK on this column. Assigned in bulk-rva order 1..N(functions); minted seed-only rows (callsites, vtable_index) get ids N+1..N+M. |
| `kcdx_id` | NULLABLE FK to `address_names.id`. Set when the row is a curated entity; NULL for uncurated bulk DEV rows. Non-unique (one open row per kcdx_id, partial-unique below). |
| `kind` | `function` / `function_no_sig` / `function_variadic` / `callsite` / `data_slot` / `string_anchor` / `instruction_anchor` / `vtable_base` / `vtable_index`. Dict-encoded. |
| `module_id` | -> `modules.id`. Per-version (a future re-architecture could move an entity between modules; trivial to support). |
| `rva` | address in this version. NULL for non-byte kinds (`vtable_index`). |
| `length` | byte length (reproduces the hashed span). |
| `content_hash` | BLAKE3 of the on-disk bytes (32-byte BLOB). NULL for non-byte kinds. |
| `value` | integer payload for non-byte kinds (slot int, offset). |
| `signature` | verified ABI for curated rows; abi_walker width-floor for bulk. |
| `observed_arg_slots`, `caller_reg_arg_count`, `caller_arg_agreement` | abi_walker floor metadata. |
| `offset` | callsite consumer offset. |
| `vtable_slot` | mirrors `value` for `vtable_index` kind. |
| `status` | `verified` / `unverified` -- only `verified` resolves at runtime. Dict-encoded. |
| `auto_name` (DEV) | `FUN_<rva>` discovery label. |
| `decompile_quality` (DEV) | `clean` / `partial` / `unanalyzable`. |
| `valid_from` | -> `game_versions.id` -- first version this form held. |
| `valid_through` | -> `game_versions.id`, NULL = current. **Partial-unique index** `(kcdx_id) WHERE kcdx_id IS NOT NULL AND valid_through IS NULL` enforces at most one open row per *curated* entity. Bulk rows (kcdx_id NULL) don't participate. |

#### DEV-only tables: TWO FKs to the owning function

`statements`, `referenced_vars`, `call_edges` each carry two FK columns to their
owning function:

- **`address_version_id`** (FK to `address_versions.id`) -- ALWAYS SET. The
  universal "which function row" pointer. `kcdx.find` walks this -- works for
  both curated and uncurated bulk functions (kcdx.find primarily walks bulk).
- **`kcdx_id`** (FK to `address_names.id`) -- NULLABLE, non-unique. Set only
  when the owning function is curated. Ergonomic shortcut for curated-subset
  joins; mostly NULL since most of the binary is uncurated.

call_edges has both pairs (caller + callee): `caller_address_version_id`
(always set), `callee_address_version_id` (always set), `caller_kcdx_id`
(nullable), `callee_kcdx_id` (nullable).

**Resolution path** (a plugin's `target = "name"`):

```sql
SELECT n.id AS kcdx_id, v.kind, v.rva, v.signature, v.status
  FROM address_names n
  JOIN address_versions v
    ON v.kcdx_id = n.id AND v.valid_through IS NULL
 WHERE n.name = ?
```

One join. The kind+address+signature come back from the single open row.

**Bulk discovery walk** (a DEV-only `kcdx.find` use case):

```sql
SELECT v.rva, v.auto_name
  FROM statements s
  JOIN address_versions v ON v.id = s.address_version_id
 WHERE s.string_ref = ?
```

Walks bulk statement → owning function via `address_version_id` (works for both
curated and uncurated; `kcdx_id` would be NULL for 99.9% of cases).

**What survived from §11.2:** the partial-unique-open-interval invariant (now
on address_versions instead of entity_versions); the curated-only USER /
bulk-superset DEV split (USER filters address_versions rows to those whose
kcdx_id is in address_names; bulk rows live only in DEV); the lossless
dictionary encoding; the kcdx_id-is-the-stable-handle promise (now realized
as `address_names.id`).

**What changed from §11.2:**
- `entities` table: **dropped**. Its only version-independent column was
  kcdx_id; the address-name registry IS the id authority.
- `entity_versions` table: **renamed to address_versions** + absorbed
  `kcdx_overlay_versions`' resolve fields (kind, signature, offset,
  vtable_slot, status, module_id). One per-version table, not two.
- `kcdx_overlay` (name registry): **renamed to address_names**, restructured:
  no separate kcdx_id column (id IS the kcdx_id), no `kind` column (kind is
  per-version on address_versions), supersession is entity-to-entity (an
  entity replaces an entity, not a name replaces a name).
- `kcdx_overlay_versions`: **dropped** -- folded into address_versions.
- `trg_pair_overlay_version` (the pairing trigger): **dropped** -- there's no
  second version table to pair with.

**Schema-version bump:** `meta.schema_version` stays 1 during this iteration
phase (consumers either accept the new shape or the old; rapid iteration =
in-place schema churn until the engine consumer is built against the final
form; bump to 2 when we lock).

**Live state at commit time:** the import builds USER at ~0.1 MB / 143 curated
kcdx_ids; DEV at ~1.13 GB / 321,138 kcdx_ids. The harness gates 21/21 against
the real dump. Resolution path verified end-to-end for sample curated names
including the four C_ModManager init-cycle helpers added 2026-05-27.

---

## Related

- [`_research/parallel-ghidra-research/inventory/ENUMERATION-FINDINGS.md`](../../_research/parallel-ghidra-research/inventory/ENUMERATION-FINDINGS.md) — the research log + reproduction recipes + raw numbers this plan rests on.
- [`restructure/00-original-plan.md`](restructure/00-original-plan.md) — Phase 9.1-9.6; the engine work that consumes this data. Schema at its Phase 9.1.
- [`data/seeds/policy.md`](../../data/seeds/policy.md) — naming + ID convention the curated overlay inherits.
- [`.claude/rules/reverse-engineering.md`](../../.claude/rules/reverse-engineering.md) — the RE methodology + reuse ladder (the loc-manager + abi_walker steps follow it).
- [`.claude/rules/cornerstones.md`](../../.claude/rules/cornerstones.md) — the disassembler test, satisfied here by discovery.
- [`.claude/rules/results-driven.md`](../../.claude/rules/results-driven.md) — measure compute (step 1) + probe loc int-ID before theorizing (§6).
