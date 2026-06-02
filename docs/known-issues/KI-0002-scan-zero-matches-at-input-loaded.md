---
id: KI-0002
opened: 2026-06-01
status: Open
commit_at_filing: 20af53a590b3a41f98d9a0642a56912a5efcf5c6
---

# KI-0002 — scan_engine resolves 0 matches when run at input_loaded (works at plugin load)

## Symptom

A pattern scan (`kcdx.scan{}` Lua verb / the `kcdx_scan` console command) returns
`0 matches` for a pattern that is verified to resolve to exactly 1 match — but
only when the scan is run at the `input_loaded` lifecycle point. The identical
scan path run at plugin-load time resolves the same class of pattern correctly.
The `kcdx_scan` console command and the `cap-70` test rows that fire at
`input_loaded` are the surfaced consumers; the in-game `~` console prints
`[scan] 0 matches`.

## Evidence (facts)

All from one launch, `kcdx-engine/logs/kcdx-dev_2026-06-01_18-46-54.log` (commit
`20af53a` at filing). Quoted observable lines:

- **Plugin-load scan WORKS (the control).** cap-32's `kcdx.scan{}` for the
  outfit-swap pattern, run synchronously at plugin load:
  `[scan 'cap32_outfit_swap'] pattern matches: 1`
  `[scan 'cap32_outfit_swap'] match 1: WHGame.dll+0x56174C -> apply addr 0x00007FF8BCCC174C`
- **input_loaded scan returns 0 for a VERIFIED, NEVER-REWRITTEN site.** cap-70's
  `CAP-70-result` calls `kcdx.scan{ pattern = "48 89 5C 24 08 57 48 83 EC 20 48 8B F9 48 8D 1D", module = "WHGame.dll" }`
  (the luaL_openlibs entry AOB, seed id 115, verified `.text`-unique = EXACTLY 1
  match at RVA 0x01449600 per `data/seeds/address_names_seed.csv:116`) from a
  `kcdx.on("input_loaded")` callback:
  `[scan 'cap70_luaL_openlibs_verify'] pattern matches: 0`
  `FAIL CAP-70-result: ... count=0 (want 1) matches[1]=nil module=nil offset=nil ...`
- **The `kcdx_scan` console command (also fired at input_loaded) returns 0 too:**
  `[scan 'kcdx_scan'] pattern matches: 0`
- **luaL_openlibs is NOT a byte-rewrite or hook target.** It is entry-hooked by
  nobody (`address_names_seed.csv:116` notes: "Entry-hooked by nobody, so the
  prologue stays pristine for a by-name pattern scan"); no test plugin or engine
  site byte-patches it. So a "the bytes were overwritten before the scan" cause
  is ruled out for THIS site — the bytes at 0x01449600 are pristine at all times.
- **The two scans share one code path.** Both `kcdx.scan{}` (`src/lua_bind_scan.cpp`)
  and the `kcdx_scan` console command (`src/console_commands_scan.cpp`) call
  `scan_engine::RunScan` → `scan_engine::ResolveScan` (`src/scan_engine.cpp`),
  which opens the module (`pe::OpenModule`), enumerates executable sections
  (`pe::ExecutableSections`), and scans (`patch::FindAllInBuffer`). The ONLY
  difference between the working (cap-32, load) and failing (cap-70/kcdx_scan,
  input_loaded) invocations is WHEN they run.
- **An earlier "fixture scanned a cap-39-rewritten site" theory was FALSIFIED.**
  The first cap-70 fixture used cap-32's outfit-swap pattern (whose tail cap-39
  rewrites at the apply pass) and returned 0 at input_loaded; that looked like a
  rewrite-timing issue. But repointing cap-70 to the luaL_openlibs AOB (a site
  nothing rewrites) STILL returns 0 at input_loaded — so the cause is NOT a
  rewritten site and NOT the specific pattern. The SCAN_ARGV probe (archived,
  `src/console_commands_scan.cpp` `#if 0` blocks) confirmed the argv + parsed
  pattern arrive intact (argc==3, full pattern in arg2, parsed to 16 bytes,
  module "WHGame.dll") — so the inputs to RunScan are correct; the 0 comes from
  the resolve itself at this timing.

## Hypothesis (NOT verified)

- Hypothesis only — not verified: at `input_loaded`, something in the scan path
  differs from plugin-load time. Candidate sub-mechanisms, none observed yet:
  `pe::OpenModule` returns a different/empty `ModuleView` at input_loaded;
  `pe::ExecutableSections` enumerates no (or different) sections then; or the
  `ModuleView` reads a mapped-but-stale view of the module. The next probe must
  observe ground truth INSIDE ResolveScan at input_loaded (moduleLoaded flag,
  the section count + sizes, the module base) and compare to the same observation
  at plugin load — one variable (timing), the scan internals logged at both
  points. Do not fix on a timing theory before observing which step returns
  empty.

## Code-read findings (static, before any live probe — 2026-06-01)

Read of the full resolve path (`src/scan_engine.cpp` `ResolveScan` →
`src/pe_helpers.cpp` `OpenModule` / `Sections` / `ExecutableSections` →
`patch::FindAllInBuffer`):

- **The resolve path carries NO timing-dependent state.** `OpenModule` is
  `GetModuleHandleW("WHGame.dll")` + a fresh read of the live image's
  `IMAGE_DOS_HEADER` / `IMAGE_NT_HEADERS`. `Sections` walks
  `IMAGE_FIRST_SECTION(nt)` and reads each header's `VirtualAddress` +
  `Misc.VirtualSize` from the loaded image every call. `ScanAll` scans
  `[baseBytes+VA, +VirtualSize)`. Nothing caches, nothing is lifecycle-gated.
  On a pure code read, this path returns the **same** result at any timing —
  so a real input_loaded≠load divergence must come from a fact OUTSIDE this
  code that differs by timing (the bytes at the site change), or the two
  compared cells are not the same scan.
- **The diagnostic line distinguishes the failure step.** A `not loaded`
  branch logs `[scan '…'] module '…' not loaded`; the Evidence quotes
  `pattern matches: 0` (the moduleLoaded==true branch). So `OpenModule`
  SUCCEEDED at input_loaded — the 0 is from `ScanAll` (section enumeration or
  the byte scan), not from a failed module open.
- **The two observed cells confound TWO variables, not one.** The working
  control (cap-32, `pattern matches: 1`) scans the outfit-swap AOB
  (RVA 0x56174C) at **plugin load**. The failing cell (cap-70-result,
  `pattern matches: 0`) scans the luaL_openlibs AOB (RVA 0x1449600, ~21 MB
  in) at **input_loaded**. They differ in BOTH timing AND pattern/target-RVA.
  No cell has ever held pattern constant across the two timings — so "timing
  is the isolated variable" (KI Evidence) is unproven. The earlier outfit-swap
  cell at input_loaded is poisoned (cap-39 rewrites its tail at the apply pass,
  which runs before input_loaded), so it cannot serve as the timing-isolating
  cell either.
- SCAN_ARGV (archived) already established the inputs (argc/parsed pattern/
  module) arrive byte-clean — the 0 is in the resolve, not the inputs.

## Probe plan (persisted before running — plan-persistence)

Theory-independent, one variable each, falsifiable. Run in order; flip Status
as each lands. PROBE 1 is designed to KILL the "timing" theory if it is wrong
(its outcome map has an outcome that falsifies timing).

| # | Probe | One variable | Status |
|---|-------|--------------|--------|
| 1 | Same-pattern, both-timings 2×2 isolator: a fixture that runs the SAME luaL_openlibs AOB via `kcdx.scan{}` at BOTH plugin-load AND input_loaded, AND runs cap-32's outfit-swap AOB at input_loaded — logging raw `count` at each cell. Holds pattern constant across timing (and timing constant across pattern), so one launch fills the empty 2×2 cells and attributes the 0 to timing-alone vs target-alone vs both. Fixture: `test-plugins/probe-ki2-scan-timing/` (throwaway). | timing (with pattern held), then pattern (with timing held) | **DONE** — see PROBE 1 outcome below |
| 2 | PROBE 1 showed the SAME pattern goes 1→0 across timing → instrument `ResolveScan` internals at both timings (engine `#if 1 // === DIAGNOSTIC (PROBE KI2-RESOLVE)` in `src/scan_engine.cpp`, name-gated to the `ki2_openlibs` cells): module base + SizeOfImage, every exec section's `[VA, VA+VirtualSize)` + whether it covers target RVA 0x1449600, and the raw 16 bytes at base+0x1449600 (WindowReadable-guarded). One launch logs `KI2RESOLVE` at BOTH timings (the probe fixture scans both cells). Outcome S = section coverage changed; Outcome B = bytes at target overwritten; Outcome N = both same yet count flips (re-observe ScanAll). | the differing read (section coverage vs target bytes) | **DONE** — Outcome B (target bytes overwritten by a JMP detour); see below |
| 3 | Outcome B → find WHO writes the `E9` detour over luaL_openlibs' entry, and why the fixture premise ("entry-hooked by nobody") is false. Static-first: grep the engine for a hook/detour installer targeting luaL_openlibs / lua_newstate / the bootstrap Lua init at that RVA, since the scan path is exonerated. | which installer writes the entry detour | **DONE** — cap-33-author-targets entry-hooks luaL_openlibs; see Root cause below |

### PROBE 1 outcome (ran 2026-06-01, log `kcdx-dev_2026-06-01_18-56-49.log`)

Observed (raw `KI2PROBE` lines):

- **CELL A** — luaL_openlibs AOB @ **plugin-load**: `count=1` (offset 2.12721e+07 ≈ 0x1449600). 
- **CELL B** — luaL_openlibs AOB @ **input_loaded**: `count=0`.
- **CELL C** — outfit-swap AOB @ **input_loaded**: `count=0`.

**Verdict: timing IS the isolated variable.** The SAME pattern (luaL_openlibs)
resolves to **1 at plugin-load** and **0 at input_loaded** — pattern held
constant, only timing changed, count flipped 1→0 (CELL A vs CELL B). This
**falsifies** the "high-RVA target unreachable" theory: CELL A==1 proves the
scan reaches RVA 0x1449600 at plugin-load, so the site is reachable; it goes
missing only at input_loaded. CELL B==0 also confirms the bug reproduces
through this probe's isolated `kcdx.scan{}` seam (not specific to cap-70's
console-execute context). CELL C==0 is consistent with the cap-39-rewrite
explanation but is now moot — CELL A vs B alone settles the cause as timing.

**The mechanism narrows hard.** The resolve path (`ResolveScan`/`Sections`)
carries no timing state and re-reads the live image every call — yet the SAME
in-memory bytes are found at load and absent at input_loaded. So between the
two timings, what the scan READS at the target changed: either the section
enumeration (a section's `VirtualAddress`/`VirtualSize` differs, so the
scanned range no longer covers 0x1449600), or the bytes at 0x1449600
themselves were overwritten by input_loaded (despite the seed note "entry-hooked
by nobody" — something else writes there). PROBE 2 observes which, directly.

PROBE 1 outcome→meaning map (pre-committed, flat — no expected outcome):
- **luaL_openlibs at load = 1, at input_loaded = 0** (and outfit-swap at
  input_loaded behaves per its own rewrite) → timing IS the isolated variable
  for THIS site; the divergence is real and timing-bound. Next: PROBE 2
  section-internals branch.
- **luaL_openlibs at load = 0 too** → NOT timing; the high-RVA target is
  unreachable by the scan at every timing → the cap-70 site/fixture is wrong
  again (a target-RVA / section-coverage bug, not a timing bug). Next: PROBE 2
  section-coverage branch.
- **luaL_openlibs at load = 1, at input_loaded = 1** → the bug does NOT
  reproduce through `kcdx.scan{}` at input_loaded in isolation → the 0 is
  specific to the cap-70 fixture's context (e.g. another plugin's apply pass,
  or the console-execute path), not the resolve → re-scope to what cap-70 does
  differently from this isolator.
- **any cell returns `module not loaded`** → `OpenModule` failed at that
  timing (contradicts the current read) → the divergence is in module
  resolution, not the scan. Next: instrument OpenModule.

### PROBE 2 outcome (ran 2026-06-01, log `kcdx-dev_2026-06-01_19-04-52.log`)

`KI2RESOLVE` internals, both timings, name-gated to the `ki2_openlibs` cells:

| Field | plugin-load (`ki2_openlibs_load`) | input_loaded (`ki2_openlibs_input`) |
|---|---|---|
| module base / `size_of_image` | 140705898364928 / 95596544 | **identical** |
| `.text` rva / vsize / end_rva | 4096 / 60821018 / 60825114 | **identical** |
| `.text` `covers_target` (RVA 0x1449600) | true | **true** |
| **raw 16 bytes at base+0x1449600** | `48 89 5C 24 08 57 48 83 EC 20 48 8B F9 48 8D 1D` | `E9 0E 75 BA FE 57 48 83 EC 20 48 8B F9 48 8D 1D` |

**Outcome B — the bytes at the target were overwritten.** Section enumeration is
IDENTICAL at both timings (Outcome S falsified: `.text` covers 0x1449600 both
times, same base/size). The ONLY change is the first **5 bytes** at the target:
`48 89 5C 24 08` → `E9 0E 75 BA FE`. `E9` is the x86 `JMP rel32` opcode — a 5-byte
relative-jump detour installed over luaL_openlibs' prologue. The trailing 11
bytes (`57 48 83 EC 20 …`) are untouched. The scan is CORRECT: the AOB's leading
5 bytes are genuinely gone by input_loaded, so 0 matches is the right answer for
a now-detoured site. The bug is NOT in the scan.

### PROBE 3 outcome (static — the writer; 2026-06-01)

Static grep of `test-plugins/**/*.lua` for `luaL_openlibs` → the writer is the
**cap-33-author-targets** suite plugin (co-resident in the live install,
confirmed present at `kcdx-plugins/test-suite/cap-33-author-targets/`). It
installs `kcdx.hook{ name = "cap33_pattern_by_name", target = "openlibs_by_pattern" }`
(`test-plugins/cap-33-author-targets/plugin.lua:52`) — a before-mode hook on the
**luaL_openlibs entry** (its `openlibs_by_pattern` declared target IS the
luaL_openlibs 16-byte entry AOB). A before-mode `kcdx.hook` installs a trampoline
detour = a `JMP rel32` over the first 5 prologue bytes. That `E9 0E 75 BA FE` is
cap-33's hook. cap-33's own comment even asserts "NOTHING entry-hooks it, so the
prologue stays pristine" immediately before entry-hooking it
(`plugin.lua:45-47`) — the same false premise cap-70/KI-0002 inherited.

## Resolution

- **Root cause:** the cap-70-result / KI-0002 fixture scans the luaL_openlibs
  16-byte entry AOB (`48 89 5C 24 08 57 …`) at `input_loaded` on the false
  premise that "nothing entry-hooks luaL_openlibs, so the prologue stays
  pristine." That premise is wrong: the co-resident **cap-33-author-targets**
  suite plugin installs a before-mode `kcdx.hook` on the SAME luaL_openlibs
  entry (`target = "openlibs_by_pattern"`), and that hook applies at the
  first-update-tick apply pass, which runs BEFORE `input_loaded`. The hook
  overwrites the first **5 bytes** of the prologue (`48 89 5C 24 08`) with a
  `JMP rel32` detour (`E9 0E 75 BA FE`), so by `input_loaded` the AOB's leading
  5 bytes no longer exist in the image. PROBE 2 observed this directly: section
  enumeration and module base are byte-identical at both timings; ONLY the first
  5 bytes at base+0x1449600 changed, from the prologue to an `E9` jump. The scan
  engine is correct at every step — `OpenModule` succeeds, `.text` covers the
  target, `FindAllInBuffer` faithfully finds 1 pristine match at load and 0
  after the detour. The wrong value is the fixture's AOB-vs-site choice (an
  entry AOB over a site a co-resident plugin entry-hooks before input_loaded);
  the original code path made the 0 inevitable because the AOB's match window
  starts at byte 0 of a prologue another plugin's 5-byte detour clobbers. This
  is the SAME class of defect as the prior cap-39-rewrite iteration — the
  fixture repeatedly picks a site another suite plugin mutates before
  input_loaded. **The scan-engine "input_loaded resolves 0" framing of this KI
  is a misdiagnosis; there is no scan bug.** (Fix design — how to make
  KI-0002/cap-70 stable — is the next step; this section records the verified
  mechanism per the root-cause-before-fix bar.)

## Reproduction

Reliable, every launch (with dev mode on):
1. Launch KCD2; cap-32 (plugin-load scan) reports `CAP-32-resolve` PASS with
   `pattern matches: 1`.
2. cap-70 (input_loaded scan) reports `CAP-70-result` FAIL with `count=0` for the
   verified luaL_openlibs AOB.
3. Optionally, in the `~` console: `kcdx_scan WHGame.dll "48 89 5C 24 08 57 48 83 EC 20 48 8B F9 48 8D 1D"` prints `[scan] 0 matches`.

Both scans in the same launch, same binary — the load-vs-input_loaded split is
the isolated variable.

## What this report does NOT do

- Does not propose a fix.
- Does not assign root cause beyond labeled hypothesis (the timing→which-step
  mechanism is unobserved; a probe inside ResolveScan at both timings is owed
  before any fix).
- Closure handled by `/debug KI-0002` (which lands the fix and closes per the
  known-issues open→closed convention).
