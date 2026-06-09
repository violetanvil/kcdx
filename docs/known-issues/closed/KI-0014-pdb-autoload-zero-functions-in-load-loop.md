---
id: KI-0014
opened: 2026-06-09
status: Closed
closed: 2026-06-09
closed_by_commit: a29bf8f
commit_at_filing: a7925ee53f03b2890e0ac4cece610c79375b12e2
---

# KI-0014 — PDB auto-load reports a /DEBUG:FULL PDB as zero-in-range-functions in the live load loop

## Symptom

Phase 9.3 step 3b (`src/plugin_pdb.cpp`, committed `54d004d`) PDB auto-load: at
the live game launch, `PopulateFromPdb` for the cap-90 test fixture emits the
FASTLINK-fallback WARN (`plugin ships a FASTLINK PDB … zero in-range functions`)
and records NO addresses — so `kcdx.functions["ts.cap_90_pdb_autoload"].cap90_internal_target:resolve()`
returns `found=false reason=not_declared`, and the `cap-90-pdb-internal-address`
test row FAILs. cap-90's PDB is provably `/DEBUG:FULL` (NOT a FASTLINK stub), so
the zero-in-range-functions branch fired on a FULL PDB — the WARN firing for cap-90
is itself the symptom.

## Reproduction

1. Build engine (`pwsh ./build.ps1`) + the cap-90 fixture (`/DEBUG:FULL`, at
   `test-plugins/cap-90-pdb-autoload/build/Release/`).
2. Deploy engine `kcdx.dll` + cap-90 (DLL + PDB + manifest) to the live install
   under `kcdx-plugins/test-suite/cap-90-pdb-autoload/`.
3. Launch to main menu, quit.
4. Read `kcdx-dev.log` `[PDB]` category + the `cap-90-pdb-internal-address` REPORT.

Observed: `[WARN][PDB] … FASTLINK … zero in-range functions namespace="ts.cap_90_pdb_autoload"`
and `REPORT name="cap-90-pdb-internal-address" pass=false reason=not_declared`.

## Evidence (directly observed this session — ground truth, not theory)

- cap-90.pdb on disk is **3460 KB** — a FULL self-contained PDB (a FASTLINK stub
  is a few KB). The PDB is NOT FASTLINK.
- cap-90's link-command tlog shows verbatim `/DEBUG:FULL` + `/INCREMENTAL:NO`; the
  `.vcxproj` Release config shows `GenerateDebugInformation=DebugFull`.
- The build is fresh (pdb timestamp 10:23, after the CMakeLists `/DEBUG:FULL`
  change at 10:19).
- At the launch, `PopulateFromPdb` ran for cap-90 (the WARN names its namespace +
  DLL path) — so the call site fires and reaches the enumerate.
- The SAME FASTLINK WARN fired for ~36 OTHER existing test-suite plugins (cap-04,
  cap-36, …) that ship default-`/DEBUG` (FASTLINK) PDBs — so the fallback path
  itself works correctly; only cap-90 (a FULL PDB) firing it is wrong.
- A prior probe pair (`_research/probe-archive/pdb-autoload-symenum-internals.md`)
  PROVED the identical DbgHelp sequence enumerates a plugin's own non-exported
  internal from a `/DEBUG:FULL` PDB — Outcome A. The feature contradicts that probe.
- The relevant filter: `src/plugin_pdb.cpp` `EnumCb` — rejects `sym->Address <
  ctx->base || sym->Address >= ctx->end`, where `ctx->base = (DWORD64)mi.lpBaseOfDll`
  (the runtime loaded base) and `SymLoadModuleEx` was called with that same base.
- The KEY DIFFERENCE between the working probe and the failing feature: the probe
  ran as a ONE-SHOT block processing ONLY cap-89 (a single plugin). The feature
  runs `PopulateFromPdb` in the real plugin-load LOOP, where ~36 plugins each call
  `SymInitialize` + `SymLoadModuleEx(their base)` + `SymCleanup` BEFORE cap-90.
  cap-90 is NOT the first plugin processed.

## Why it matters

Step 3b's whole value — zero-friction internal-function auto-load for TC authors —
is dead if a correct FULL PDB silently yields nothing. The engine integration is
otherwise confirmed (the fallback fired correctly for ~36 plugins), so this is a
narrow integration defect, not a design failure.

## Probe plan (persisted ledger — one row per planned probe, flipped as each lands)

| Probe | One-variable action | Status | Result |
|---|---|---|---|
| A | Log the RAW enumerated symbol set for cap-90 (each `sym->Address` + `sym->Flags`) + `ctx->base` + `ctx->end`, in the real multi-plugin loop | DONE | A-3 variant. `raw_total=691`, `enum_ok=yes`, bounds correct (`passed_base==loaded_base`, no mismatch). Every in-range symbol is `is_func=no` (CRT publics, `flags=0x2000000`); `cap90_internal_target` is ABSENT from the enumerated set entirely. The enumerate yields only PUBLIC symbols, not the private function stream. |
| B0 | Static: diff cap-90's fixture source against the cap-89 probe fixture that DID enumerate its private fn (still on disk) | DONE | The two `cap-NN.cpp` are STRUCTURALLY IDENTICAL (anon-namespace + `__declspec(noinline)` target, volatile-sink-called from `kcdxPlugin_Load`). The fixture source is NOT the variable. The only remaining difference: cap-89 ran as a ONE-SHOT (single `SymInitialize`/`SymCleanup`, only plugin); cap-90 runs after ~36 plugins each cycled `SymInitialize`+`SymLoadModuleEx`+`SymCleanup`. |
| B | One variable: `SymInitialize` the DbgHelp handler ONCE (process-lifetime), not per-plugin `SymInitialize`+`SymCleanup`; re-run, observe whether cap-90 then enumerates `cap90_internal_target` (is_func=yes, in_range=yes) | DONE | B-2 (hypothesis KILLED). IDENTICAL result: `raw_total=691`, `in_range_funcs=0`, no `is_func=yes`, target absent, bounds correct. The per-call init/cleanup churn is NOT the cause. Re-observe rather than hop. |
| C0 | Static (no launch): offline byte-inspect cap-90.pdb vs the cap-89 probe PDB (both still on disk) — does each PDB CONTAIN its target symbol name? | DONE | BOTH PDBs contain their target name as a raw string; both ~3.45 MB FULL. cap-90.pdb DOES contain `cap90_internal_target`. The difference is NOT in the PDB content/size — it is purely in the runtime enumerate ENVIRONMENT (load-loop position is the only remaining difference: cap-89 ran first/only; cap-90 after ~36 plugins). FASTLINK (probe-2) and handler-churn (probe B) both killed. 2 theories hopped → fresh-frame escalation (§B.5). |
| C-iso | Offline (no launch): a standalone one-shot console program loads cap-90.dll+pdb ALONE and enumerates — is the FILE enumerable in isolation? | SKIPPED | The VS toolchain path isn't readily locatable for an ad-hoc console compile (CMake finds it via its own discovery); not worth the yak-shave when Probe C (in-loop, observes the actual failing condition) is the primary. Revisit only if C is inconclusive on the file-vs-environment fork. |
| C | In-loop (launch): `SymGetModuleInfo64` right after `SymLoadModuleEx` for cap-90, log `SymType`/`LoadedPdbName`/`PdbUnmatched`/`PdbSig70`/`PdbAge`/`NumSyms`/`TypeInfo` — discriminate (a) PDB-loaded-publics-only vs (b) silent image-export fallback vs (c) wrong/stale PDB resolved by name-match | DONE | Outcome (a), NARROWED. `sym_type=3` (SymPdb), `pdb_unmatched=0` (MATCHED), `loaded_pdb` = the correct cap-90.pdb (loaded from the BUILD tree via the DLL's embedded absolute path, not the deployed copy — same machine), `type_info=1`, `global_syms=1` — but **`num_syms=0`**. So DbgHelp matched + loaded the right FULL PDB yet its PRIVATE symbol table came back EMPTY; the 691 enumerated were the image public/export stream. NOT (b) (a PDB loaded), NOT (c) (matched + correct path). The private-symbol table is empty at enumerate time despite a matched FULL PDB. |
| D | Offline isolation (Python `ctypes` → DbgHelp, no game, no compile): fresh `SymInitialize` → `SymLoadModuleEx(cap-90.dll alone)` → `SymGetModuleInfo64` (`num_syms`?) → `SymEnumSymbols("*")` (does `cap90_internal_target` appear?). Splits the LAST fork: file/load-params vs loop-environment. | DONE | **REPRODUCES OFFLINE** (single module, fresh handler, no loop) — IDENTICAL: `NumSyms=0`, `total=691`, `funcs=0`, `SymType=3` matched. Loop-environment theory KILLED. **AND `cap90_internal_target` IS enumerated** (`addr=0x10001000`) — it was in the 691 all along, but its `Flags` lacks `SYMFLAG_FUNCTION` (it comes back as a PUBLIC-stream symbol, `flags=0x2000000`=`SYMFLAG_PUBLIC_CODE`), so the feature's `if ((Flags & SYMFLAG_FUNCTION)==0) reject` filter drops it. ROOT CAUSE: the PDB's PRIVATE/DBI symbol stream is not read (`NumSyms=0`); only the PUBLIC stream is, where a function carries no FUNCTION flag. |
| E | Offline (Python, iterate load params): why is `NumSyms=0` (private/DBI stream unread) for a matched FULL PDB? Vary one at a time — pass a real loaded image base+size, toggle `SYMOPT_LOAD_ANYTHING`/`NO_PUBLICS` — observe when `NumSyms`>0 and the target reads `is_func=yes`. | DONE | `NumSyms=0` + `funcs=0` across ALL 6 variants (fake base, real-mapped base, LOAD_ANYTHING, NO_PUBLICS). Load params are NOT the variable — the private/DBI stream is never read for this PDB regardless. The target enumerates (public stream) with `target_flags=0x0` (no FUNCTION flag). |
| E-ctrl | Offline control: run the SAME probe against cap-89 (probe-2's fixture that supposedly showed `is_func=yes`) | DONE | cap-89 ALSO shows `funcs=0`, `NumSyms=0`, private stream unread — IDENTICAL to cap-90. **This overturns probe-2's premise.** Probe-2 reported `internal_enumerated=yes` because its callback matched the target BY NAME in the public stream; the FEATURE later added a `SYMFLAG_FUNCTION` filter probe-2 never had. Same data, stricter filter → the feature rejects what the probe accepted. The "FULL PDB enumerates internals as functions" premise was FALSE. |
| F | Offline: at the real mapped base, dump exact `Flags` + `Tag` for the target function vs the CRT data publics — find the code-vs-data discriminator | DONE | **THE DISCRIMINATOR IS `Tag`, NOT `Flags`.** The function `` `anonymous namespace'::cap90_internal_target `` enumerates with `Tag=5` (`SymTagFunction`), `Flags=0x0`. The CRT data publics enumerate with `Tag=7` (`SymTagData`), `Flags=0x2000000` (`SYMFLAG_PUBLIC_CODE`). The feature filtered on `Flags & SYMFLAG_FUNCTION` (0x800) — which is set on PRIVATE-DBI function symbols that never load here (`NumSyms=0`); the PUBLIC-stream function carries `Tag=SymTagFunction` but NO `SYMFLAG_FUNCTION` flag. FIX: filter on `sym->Tag == 5` (SymTagFunction), not the flag. |

## Root cause (falsifiable mechanism)

`src/plugin_pdb.cpp` `EnumCb` filtered a symbol as a plugin function with
`if ((sym->Flags & SYMFLAG_FUNCTION) == 0) reject`. That is the wrong field for
how these MSVC `/DEBUG:FULL` plugin PDBs actually enumerate under
`SymEnumSymbols`. For these DLLs, DbgHelp does NOT load the PDB's private/DBI
symbol stream (`SymGetModuleInfo64` reports `NumSyms=0`, `SymType=SymPdb`,
matched) — it serves only the PUBLIC symbol stream. In the public stream a
function appears with **`Tag == SymTagFunction` (5)** but with `Flags == 0`
(the `SYMFLAG_FUNCTION` bit, 0x800, is set only on private-DBI function records,
which are absent here). The CRT/linker data publics in the same stream carry
`Tag == SymTagData` (7) + `Flags == SYMFLAG_PUBLIC_CODE` (0x2000000). So the
feature's flag test rejected EVERY plugin function (none had the flag) and the
namespace was never populated — the `inRangeFuncs == 0` path then fired the
FASTLINK-fallback WARN on a PDB that was, in fact, a matched FULL PDB.

The original "PDB auto-load works with /DEBUG:FULL" premise (probe-2) was a
false positive: probe-2's callback matched the target BY NAME with no
function-kind filter, so it counted the public-stream symbol as "enumerated";
the feature added a flag-based kind filter that the public-stream function
cannot satisfy. The fix is a one-line filter correction: classify a function by
`sym->Tag == SymTagFunction` (5), not `sym->Flags & SYMFLAG_FUNCTION`.

(Probe A — DONE — eliminated A-1 (base correct) and A-2 (691 symbols
enumerated). The result is A-3-shaped but sharper than "wrong PDB loaded": the
FULL PDB loaded and enumerated, but `SymEnumSymbols("*")` returned only the
PUBLIC symbol stream — the same CRT-publics-only shape probe-1's FASTLINK PDB
showed — and the target private function is absent. Probe B isolates WHY this
FULL PDB's private function stream is invisible when probe-2's cap-89 (same flag)
enumerated its private fn: the variable is in the fixture build/source, checkable
without a launch.)

## Outcome → meaning map (Probe A, pre-committed, flat)

- **A-1** — the enumerate yields cap-90's functions but their addresses are
  OUTSIDE `[ctx->base, ctx->end)` → a base/VA-vs-RVA or wrong-base mismatch in the
  loop (the filter rejects valid functions). Next: compare the enumerated address
  base against `mi.lpBaseOfDll` and against what `SymLoadModuleEx` returned.
- **A-2** — the enumerate yields ZERO symbols total (not just zero in-range) → a
  per-call `SymInitialize`/`SymCleanup` state issue across the loop (the symbol
  handler is in a bad state by the time cap-90 is processed). Next: probe the
  handler lifecycle (init-once vs per-plugin).
- **A-3** — the enumerate yields only CRT/data privates, no in-range FUNCTION (the
  same shape the probe saw for a FASTLINK PDB) → cap-90's deployed PDB is somehow
  NOT the FULL one the build produced (a deploy/path mismatch — wrong PDB loaded).
  Next: hash the deployed PDB against the build, confirm `SymLoadModuleEx` loaded
  THAT file.

## Open questions (hypotheses — empirical Facts only above, causal claims labeled here)

- **(Probe B hypothesis)** `plugin_pdb::PopulateFromPdb` does `SymInitialize` + `SymLoadModuleEx` + `SymUnloadModule64` + `SymCleanup` PER PLUGIN — 37 init/cleanup cycles in one process across the load loop. DbgHelp's symbol handler is a single per-process resource; repeated `SymInitialize`/`SymCleanup` churn (and/or many module load+unload cycles) is suspected to leave a later module's PRIVATE symbol stream (the DBI stream a FULL PDB carries) unread, while the PUBLIC stream (publics/exports, readable from the image) still resolves — which is EXACTLY the observed shape (691 publics, no private function). Probe-2's cap-89 ran as a single init/cleanup, so it never hit the churn. UNVERIFIED until Probe B isolates the handler-lifecycle variable.

## Probe B outcome → meaning map (pre-committed, flat)

- **B-1** — with a once-per-process `SymInitialize` (no per-plugin `SymCleanup`), cap-90 enumerates `cap90_internal_target` (`is_func=yes`, `in_range=yes`, recorded) → the per-call init/cleanup churn was the cause → the fix is handler-lifecycle: initialize once, load/unload per module, cleanup at teardown (or never). 
- **B-2** — cap-90 STILL shows only publics → the handler lifecycle is NOT the variable; re-observe (the difference is elsewhere — module load/unload accumulation, or a SymLoadModuleEx flag, or PDB search-path state). Do NOT propose a fix; design Probe C from the new ground truth.

## Fix verification (2026-06-09 launch, kcdx-dev_12-46-54)

The Tag-based filter fix WORKS at the engine level: `[INFO] PDB auto-load
populated internal-function addresses … functions=408 dropped_over_cap=0
rejected_bad_name=0` for cap-90 — was 0, the FASTLINK WARN is gone, the private
functions are now read (via `Tag == SymTagFunction`). The root cause (wrong-field
filter) is FIXED.

BUT `cap-90-pdb-internal-address` still reds: `:resolve found=false
reason=not_declared` for `cap90_internal_target`. A SECOND, distinct defect
surfaced behind the first: the PDB records the function under its
qualifier-decorated name `` `anonymous namespace'::cap90_internal_target ``
(Probe F observed this exact name), but the test (and an author) indexes the
BARE name `cap90_internal_target`. So 408 functions are stored under their
qualified names and the bare-name lookup misses. This is a NAME-KEYING design
question (how `kcdx.functions[ns]` keys PDB-sourced functions — bare /
qualified / both), surfaced to the user. Not the engine filter — that is fixed.

Suite `205/229` — no regression (the fix changed only the function-kind filter;
CAP-20/CAP-28 are the pre-existing KI-0010/KI-0011).

### Second defect (behind the first) + its fix — name keying

The PDB records a function under its DbgHelp-undecorated QUALIFIED name
(`` `anonymous namespace'::cap90_internal_target ``, `CombatState::CanSwap`), but
an author indexes the BARE name (`cap90_internal_target`) per the settled
`<author>.<plugin>.<bare>` naming model (the engine owns the namespace; the
author types the bare leaf). The feature keyed by the full decorated string, so
the bare-name lookup missed. FIX: `plugin_pdb` extracts the bare leaf (substring
after the last `::`) and keys under it; a function in a C++ namespace/class is
ALSO keyed under its qualified name (the unambiguous disambiguation form for a
leaf collision); `lua_bind_functions::RecordPluginAddress` warns once + keeps
first-wins on a bare-leaf collision (per naming-namespaces.md). The fixture's
anonymous-namespace target (the pathological un-shareable case that produced the
ugly decoration) was changed to a normal file-scope external-linkage function —
what a real author ships for cross-mod hooking.

### Verified GREEN (2026-06-09, kcdx-dev_14-37-23)

`cap-90-pdb-internal-address` PASS: `kcdx.functions["ts.cap_90_pdb_autoload"]
.cap90_internal_target:resolve -> found=true has_address=true address=...`. The
full chain works: Tag-based filter reads 408 functions → keyed by bare name →
the author's bare-name lookup hits. `[INFO] PDB auto-load populated
internal-function addresses functions=408`. Suite `206/229` (cap-90 flipped
green; CAP-20/CAP-28 are the pre-existing KIs).

### Surfaced follow-up (NOT this bug — for the user)

The bare-leaf collision warn-once fired for ~9 CRT/compiler-internal functions
the PDB enumerates inside the plugin image (`operator delete`, `bad_exception`,
`_set_new_handler`, `fin$0`, `` `scalar deleting destructor' ``, `operator()`,
…). The warn is working correctly, but it is NOISE — these are C-runtime
plumbing functions no author references; the warn should fire only on a
collision among the AUTHOR'S OWN functions. The 408 count also includes these
CRT internals (they are in-range + Tag=Function), so the namespace carries
runtime plumbing alongside the author's functions. Both are a follow-up quality
item (filter the recorded set to the author's own translation units, e.g. by
source-file or a CRT-name denylist) — surfaced to the user, not folded into this
close. Does not affect the cause-test (the author's function resolves correctly).

## Resolution

**Closed 2026-06-09, fix `a29bf8f`, user-confirmed via the cap-90 launch.**

**Root cause (two layered defects, one symptom).**
1. PDB auto-load classified a plugin function with `(sym->Flags &
   SYMFLAG_FUNCTION)`. For these MSVC `/DEBUG:FULL` plugin PDBs DbgHelp serves
   only the PUBLIC symbol stream (`SymGetModuleInfo64` reports `NumSyms=0`,
   `SymType=SymPdb`, matched — the private/DBI stream is never loaded), where a
   function carries `Tag==SymTagFunction` (5) but `Flags==0` (the
   `SYMFLAG_FUNCTION` bit is set only on private-DBI function records, structurally
   absent here); CRT data publics carry `Tag==SymTagData` (7). So the flag test
   rejected EVERY plugin function — making the `inRangeFuncs==0` FASTLINK-fallback
   fire on a matched FULL PDB inevitable. Established by an offline
   DbgHelp-via-ctypes probe set that reproduced `NumSyms=0`/`funcs=0` in isolation
   across 6 load-param variants, and a control run against a second fixture; the
   earlier "PDB auto-load works" reading was a false positive (that probe matched
   by NAME with no kind filter, so it never exercised the flag).
2. Behind the first: a recorded function was keyed by its DbgHelp-undecorated
   QUALIFIED name, but an author indexes the BARE name per the
   `<author>.<plugin>.<bare>` model — so the bare-name lookup missed.

**Fix (`a29bf8f`).**
- `src/plugin_pdb.cpp` — filter on `sym->Tag == SymTagFunction` (the field
  populated on public-stream functions), not the flag; extract the bare leaf
  (after the last `::`) and key under it, plus the qualified name as the
  disambiguation key.
- `src/lua_bind_functions.cpp` — `RecordPluginAddress` warns-once + first-wins on
  a bare-leaf collision (the existing warn-once + qualified-disambiguates model).
- `test-plugins/cap-90-pdb-autoload/cap-90.cpp` — the target moved out of an
  anonymous namespace (internal linkage, un-shareable, the source of the
  pathological decoration) to a normal file-scope external-linkage non-exported
  function — what a real plugin ships for cross-mod hooking.

**Verification.** `cap-90-pdb-internal-address` PASS (user launch
`kcdx-dev_14-37-23`): `kcdx.functions["ts.cap_90_pdb_autoload"]
.cap90_internal_target:resolve -> found=true has_address=true`; `functions=408`;
suite `206/229` (no regression). Gate B (root-cause-verifier) returned `land-fix`.

**Open follow-up (NOT this bug).** The recorded set + the collision warn include
CRT/compiler-internal functions the PDB enumerates inside the plugin image —
filter to the author's own translation units (surfaced above; does not affect the
author's-own-function resolution).

## Activity log

- **2026-06-09** — Filed. Ground truth (the Evidence facts above) established
  before filing; investigation enters at Probe A (the raw-enumerate instrumentation
  the count-only logging cannot show). Probe A → A-3-shaped (publics-only,
  bounds correct, target absent). B0 (static source diff) eliminated the fixture
  as the variable. Probe B isolates the DbgHelp handler-lifecycle (per-call
  init/cleanup churn) hypothesis.
- **2026-06-09** — CLOSED. Probes A→F (incl. offline ctypes D/E/F) nailed the
  wrong-filter-field mechanism + the name-keying second defect; both fixed
  (`a29bf8f`), cap-90 user-confirmed green, Gate B `land-fix`. The
  CRT-internal-noise quality item is the open follow-up.
