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
