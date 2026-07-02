# PROBE PDB-AUTOLOAD — SymEnumSymbols does NOT surface a release-build plugin's own non-exported internals

**Run:** `kcdx-dev_2026-06-09_09-45-32.log` (live, dev-mode, launch-to-menu).
Clean, no FAULTED. Observe-only (a `// === DIAGNOSTIC (PROBE: PDB-AUTOLOAD) ===`
block at the C++ plugin-load drive site in `src/plugin_loader.cpp`, gated to the
cap-89 probe plugin only). Suite stayed green (202/225) — the probe broke nothing.

**Trust:** primary evidence (live observation; the full 717-symbol enumeration was
logged as ground truth before the verdict line, so Outcome A vs B is read from the
raw symbol set, not a boolean the probe set). The probe was theory-independent —
the falsifying branch (B) was reachable and is what fired.

## The question

The Phase-9.3 PDB-autoload design (step 3b) rested on the asserted runtime mechanism:
"a sidecar `.pdb` populates EVERY internal function's address — not just exports —
via `DbgHelp` `SymLoadModuleEx` + `SymEnumSymbols`." That clause read identically
to a correct one; nothing in the repo proved it (`crash_guard.cpp` links dbghelp
but never calls `SymInitialize`). The checkable unknown: does `SymEnumSymbols`
enumerate a foreign plugin DLL's OWN non-exported internal C++ function (name +
address) from a RELEASE-build sidecar PDB?

## The fixture (so the next investigation reconstructs it)

A C++ probe plugin `cap-89-pdb-autoload-probe` whose DLL carries:
- `cap89_internal_probe_target(int)` — a plain internal-linkage free function, NO
  `__declspec(dllexport)`. `dumpbin /EXPORTS` confirmed exactly ONE export
  (`kcdxPlugin_Load`) — the internal is genuinely absent from the export table, so
  an exports-only enumeration cannot list it (Outcome B genuinely reachable).
- It is NOT dead-stripped: `__declspec(noinline)` + referenced from `kcdxPlugin_Load`
  through a `volatile int` sink, so `/OPT:REF` keeps it.
- The DLL's own CMakeLists adds `/Zi` (compile) + `/DEBUG` (link) so `cap-89.pdb`
  is emitted beside `cap-89.dll`. Both deploy to
  `kcdx-plugins/test-suite/cap-89-pdb-autoload-probe/`.

Engine side: `SymInitialize(GetCurrentProcess(), NULL, FALSE)` →
`SymLoadModuleEx(cap-89.dll path + loaded base)` → `SymEnumSymbols(..., "*", cb)`
logging each `Name`+`Address`, then a decisive line, then `SymUnloadModule64` +
`SymCleanup`.

## Observed — the verdict line + the ground-truth set

```
PROBE PDB-AUTOLOAD symload_ok=yes enum_ok=yes enum_lasterr=0 \
  internal_enumerated=no name_found=(none) addr=0x0 total_syms=717
```

`symload_ok=yes` → the PDB loaded (NOT Outcome C; not a setup/path/GUID failure).
`enum_ok=yes`, `total_syms=717` → `SymEnumSymbols` ran and is not broken.
`internal_enumerated=no`, `name_found=(none)` → **`cap89_internal_probe_target` is
NOT in the enumeration.**

The 717 enumerated symbols are ALL CRT/linker/CFG artifacts — `__newclmap`,
`__guard_xfg_dispatch_icall_fptr`, `_fltused`, `__acrt_lconv_c`,
`__scrt_current_native_startup_state`, `__security_cookie_complement`,
`std::bad_exception::\`vftable'`, `__xc_a`/`__xi_z` (CRT init arrays), etc. These
are the CRT's own static data + the linker's CFG tables — public-ish symbols the
CRT's object files carry. The PLUGIN's own user-authored non-exported function is
absent.

## Verdict — the step-3b assumption is FALSE as designed (Outcome B)

A release-build MSVC sidecar PDB built with `/Zi`+`/DEBUG` does NOT make the
plugin's own private (non-exported) C++ functions enumerable via
`SymEnumSymbols "*"`. The CRT/linker privates that DO appear come from the CRT
object files' debug records; the user code's private function symbols are not in
the form `SymEnumSymbols` surfaces here. So PDB-autoload, as the design framed it
("populate EVERY internal's address"), cannot be its internal-address source for a
standard release build.

Confound ruled out: it is NOT "the PDB carries no privates at all" — 717 privates
(CRT) DID enumerate, so the private-symbol stream is present and walked; the
plugin's own user-function record specifically is not enumerable. So this is a PDB
**content/build-config** boundary, not a missing-symbols-stream or an API-options
miss.

## What this forces (design fork — the USER's call)

Step 3b's internal-address source must be re-designed; it does NOT proceed as
designed. Candidate directions to weigh with the user (NOT decided here):
- require a stronger PDB (full `/Z7` or a `/DEBUG:FULL` with the user functions in
  the public-symbol stream — verify a build flag actually makes the user internal
  enumerable before committing to it; this probe's `/Zi`+`/DEBUG` did not),
- a `/MAP`-file or author-declared internal address table (the author already
  declares signatures via `kcdx.dll.declare` — extend it to carry an internal's
  RVA),
- or scope PDB-autoload to exports + declared functions only (drop the
  "every internal, zero-friction" promise) and let `kcdx.dll.declare` +
  the C-export path carry the address.

The cornerstones-clean half (`kcdx.dll.declare`, shipped in step 3a) already gives
TC authors the disassembly-free cross-plugin path; PDB-autoload was the
zero-friction-for-undeclared-internals enhancement on top, and that enhancement is
where the assumption broke.

## Refinement (static evidence, 2026-06-09) — Outcome B's CAUSE is likely the FASTLINK default, not a DbgHelp limit

SOURCE: MSVC `/DEBUG` linker doc
(https://learn.microsoft.com/en-us/cpp/build/reference/debug-generate-debug-info,
fetched 2026-06-09). The load-bearing facts:

- `/DEBUG:FULL` "moves ALL private symbol information from individual compilation
  products (object files and libraries) into a single PDB ... the full PDB can be
  used to debug the executable when no other build products are available, such as
  when the executable is DEPLOYED."
- `/DEBUG:FASTLINK` produces "a limited PDB that INDEXES INTO the debug information
  in the object files ... instead of making a full copy. You can only use this
  limited PDB to debug from the computer where the binary was built."
- The VS "Generate Debug Info" property "enables `/DEBUG:FASTLINK` by DEFAULT in
  Visual Studio 2017 and later."

The cap-89 fixture's CMakeLists set `/Zi` + bare `/DEBUG`. Under CMake's MSVC
generator a Release-with-debug config can resolve to `/DEBUG:FASTLINK` (the IDE
default), producing a FASTLINK stub PDB that only indexes the build-machine OBJs.
The probe ran `SymEnumSymbols` against the DEPLOYED PDB on the game machine — a
FASTLINK stub there carries NO copied private symbols, so the plugin's own
non-exported function was absent. The CRT privates that DID enumerate (717) come
from the CRT's own FULL PDBs on the build machine's symbol search path, not from
the cap-89 stub — which is exactly the FASTLINK split this doc describes.

So Outcome B does NOT prove "a release PDB cannot carry a plugin's internals" — it
proves "a FASTLINK / non-FULL deployed PDB does not." The original step-3b design
(PDB carries internal addresses) may SURVIVE with an explicit `/DEBUG:FULL`. This
is the variable probe-2 isolates: force `/DEBUG:FULL` (override any FASTLINK
default), redeploy the FULL PDB, re-run the same enumerate. Outcome map for
probe-2: internal now enumerated → original 3b design holds (author ships a FULL
PDB); still absent → the limit IS fundamental → fall back to declared-RVA or
exports+declared-only (the user's pre-chosen fallbacks).

## PROBE-2 (`/DEBUG:FULL`) — CONFIRMED: the original design survives

**Run:** `kcdx-dev_2026-06-09_10-06-57.log` (live, dev-mode, launch-to-menu).
Clean, suite 202/225. Same cap-89 fixture + same engine probe as probe-1; the ONE
variable changed: the plugin's PDB links `/DEBUG:FULL` (verified at the
link-command level — `<GenerateDebugInformation>DebugFull</...>` + verbatim
`/DEBUG:FULL` in the link tlog, NO FASTLINK) instead of the default `/DEBUG` that
downgrades to FASTLINK.

Decisive line:
```
PROBE PDB-AUTOLOAD symload_ok=yes enum_ok=yes internal_enumerated=yes \
  name_found=cap89_internal_probe_target addr=0x7FFA... total_syms=691
```

**OUTCOME A.** With `/DEBUG:FULL`, `SymEnumSymbols` surfaces the plugin's OWN
non-exported internal — name AND a real loaded VA — from the deployed sidecar PDB.

This is a clean falsify-then-confirm PAIR with probe-1, not a confirm-only probe:
probe-1 (FASTLINK) → internal ABSENT; probe-2 (FULL) → internal PRESENT; identical
fixture, identical engine, ONLY the PDB flag differs. The probe-1-refinement
theory (FASTLINK was the cause, not a DbgHelp limit) is proven by the controlled
flip.

**Step-3b verdict:** the original PDB-autoload design is BUILDABLE, with ONE
load-bearing constraint it must carry and document: the author must ship a
`/DEBUG:FULL` PDB. A FASTLINK (default-VS2017+) PDB silently yields no internals
when deployed — so 3b's graceful-fallback + teaching-log path must DETECT the
FASTLINK/stub case and tell the author "ship a FULL PDB" (not just "no PDB →
exports-only"). The "every internal, zero-friction" promise holds for a FULL PDB.

## PROBE G (CRT-noise filter design, 2026-06-09) — source-file filtering is viable

`SymGetLineFromAddr64(symbol address)` reports a per-symbol SOURCE FILE that
cleanly separates the author's functions from the CRT/compiler plumbing the
enumerate also yields (probe `_research/ki0014-pdb-recon/probe_g_srcfile.py`):

- `cap90_internal_target` → `…\test-plugins\cap-90-pdb-autoload\cap-90.cpp` (the
  plugin's OWN source).
- `operator delete` → `…\src\vctools\crt\vcstartup\…`; `_set_new_handler` →
  `minkernel\crts\ucrt\…`; `__crt_seh_guarded_call` → `VCCRT\vcruntime\…`.
- Some CRT data/vftables → `(no source)`.

So PDB-autoload can keep only the author's functions by REJECTING a recorded
function whose source file matches a CRT/compiler marker (`vctools\crt`, `ucrt`,
`vcruntime`, `vccrt`, `vcstartup`, `minkernel\crts`) OR has no source file. A
"must be under the plugin dir" positive filter is fragile (build dir varies); the
CRT-source-marker denylist + no-source-reject is the robust shape. Needs
`SYMOPT_LOAD_LINES` (already set) and a per-symbol `SymGetLineFromAddr64` call in
the enumerate callback.

## Reusable wiring

The probe block + the cap-89 fixture are the reconstruction recipe. The engine
probe was removed from `src/plugin_loader.cpp` after this capture (no residue in
live source). To re-run: re-insert the `SymInitialize`/`SymLoadModuleEx`/
`SymEnumSymbols` block at the plugin-load drive site gated to a probe-plugin name,
rebuild a plugin DLL with the candidate PDB flag, deploy DLL+PDB, launch, grep
`PDB_PROBE`.
