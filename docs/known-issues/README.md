# known-issues/

One file per external/upstream issue kcdx routes around. Each file
is a **diagnostic trail** — a chronological list of what was tried
and what the result was. Not a narrative, not a design document.

Format:

```
# <one-line issue title>

**Status:** open | working-around | fixed-upstream | abandoned

## Trail

| Date       | Action                                   | Result |
|------------|------------------------------------------|--------|
| YYYY-MM-DD | what was tried                            | what happened |
| YYYY-MM-DD | next thing                                | result        |

## Facts

- crisp bullets only; what we know to be true
- one fact per bullet

## Open questions

- what we'd probe next
```

Entries graduate to `closed/` when the root cause is fixed in
kcdx (commit + Resolution section appended to the trail), fixed
upstream, or the workaround is promoted to a permanent design
choice in `../design.md`. Top-level `known-issues/` lists only
open issues so a fresh agent can see at a glance what still
needs attention. A `provisional-mask` issue stays OPEN and
top-level until its real root cause lands.

Graduation is one move, in the closing commit: `git mv` the file
into `closed/`, repoint its `## KI index` row link to the
`closed/` path (the index stays one flat table; the link path
reflects open-vs-closed) and append ` — FIXED` to its Summary,
and add a `## Closed (historical reference)` bullet. The dir and
the index agree at all times.

## File naming

New bugs use `KI-NNNN-<slug>.md` with YAML frontmatter (`id` / `opened` /
`status` / `commit_at_filing`). IDs are unique across `known-issues/` AND
`known-issues/closed/`; the next ID is the highest `KI-####` across both
dirs plus one. First bug = `KI-0001`. The pre-KI-NNNN files below keep
their human-readable names and carry no ID — they are not part of the
allocation sequence.

## KI index (newest first)

| ID | Opened | Summary |
|----|--------|---------|
| [KI-0002](closed/KI-0002-scan-zero-matches-at-input-loaded.md) | 2026-06-01 | CAP-70 scan found 0 at input_loaded — fixture scanned a co-resident-hooked site, not a scan bug — FIXED |
| [KI-0001](closed/KI-0001-save-load-heap-corruption-on-chain-mediated-lua_pcall.md) | 2026-05-29 | Save-load STATUS_HEAP_CORRUPTION on the chain-mediated lua_pcall path — FIXED |

## Current open (pre-KI-NNNN)

- [BugSplat dmp files don't reach disk for AV crashes.md](BugSplat%20dmp%20files%20don't%20reach%20disk%20for%20AV%20crashes.md)
  — KCD2's BugSplat upload chain writes the dmp under a filename
  containing a colon (`Kingdom Come: Deliverance II`). Windows
  rejects, dmp is lost. Working around via in-process
  `MiniDumpWriteDump` from kcdx-watchdog; upstream bug remains.

## Closed (historical reference)

- [closed/KI-0002-scan-zero-matches-at-input-loaded.md](closed/KI-0002-scan-zero-matches-at-input-loaded.md)
  — CAP-70-result found 0 matches at `input_loaded` for a verified site.
  Root cause: NOT a scan bug — the fixture scanned the luaL_openlibs entry
  AOB, which the co-resident cap-33 plugin entry-hooks (a 5-byte `JMP`
  detour over the prologue) before `input_loaded`, so the AOB's leading
  bytes were gone (PROBE 2 observed the bytes change `48 89 5C 24 08` →
  `E9 0E 75 BA FE`; section enumeration byte-identical). A fixture-stability
  defect — the 2nd time this row picked a co-resident-mutated site (after a
  cap-39 rewrite). FIXED 2026-06-01 via `96dce1e` — repointed the scan to a
  detour-immune deep-interior `.text`-unique site (WHGame.dll+0x9800), user-
  confirmed `CAP-70-result PASS`.
- [closed/KI-0001-save-load-heap-corruption-on-chain-mediated-lua_pcall.md](closed/KI-0001-save-load-heap-corruption-on-chain-mediated-lua_pcall.md)
  — save-load STATUS_HEAP_CORRUPTION (0xC0000374) at `kcdx!luaC_step`
  GC frees on WHGame's sentinel objects. The FIX-C mirror: kcdx's
  vendored Lua GC freed WHGame's static-`.rdata` `dummynode_` (and
  later the `t->array` sentinel) the same way WHGame's GC was freeing
  kcdx's pre-FIX-C. FIXED 2026-05-29 via `kcdx_node_freeable` +
  `kcdx_array_freeable` guards in `vendor/lua/ltable.c`; cap-66
  regression row guards both sites.
- [closed/kcdx lua_newtable corrupts the process heap.md](closed/kcdx%20lua_newtable%20corrupts%20the%20process%20heap.md)
  — dual-Lua dummynode sentinel mismatch. FIXED 2026-05-20 via
  FIX C (vendored Lua patch); PROBE Q canary in production as
  permanent regression guard. Full PROBE A → Q investigation
  trail preserved.
- [closed/cap-04 skip-original codegen does not skip the original instruction.md](closed/cap-04%20skip-original%20codegen%20does%20not%20skip%20the%20original%20instruction.md)
  — `[[mid_hook]] call_original=false / "auto"` codegen unmasked
  by FIX C. FIXED 2026-05-20 via commit `03dd155`; CAP-04 sub-tests
  all PASS.
