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
needs attention.

## Current open

- [BugSplat dmp files don't reach disk for AV crashes.md](BugSplat%20dmp%20files%20don't%20reach%20disk%20for%20AV%20crashes.md)
  — KCD2's BugSplat upload chain writes the dmp under a filename
  containing a colon (`Kingdom Come: Deliverance II`). Windows
  rejects, dmp is lost. Working around via in-process
  `MiniDumpWriteDump` from kcdx-watchdog; upstream bug remains.

## Closed (historical reference)

- [closed/kcdx lua_newtable corrupts the process heap.md](closed/kcdx%20lua_newtable%20corrupts%20the%20process%20heap.md)
  — dual-Lua dummynode sentinel mismatch. FIXED 2026-05-20 via
  FIX C (vendored Lua patch); PROBE Q canary in production as
  permanent regression guard. Full PROBE A → Q investigation
  trail preserved.
- [closed/cap-04 skip-original codegen does not skip the original instruction.md](closed/cap-04%20skip-original%20codegen%20does%20not%20skip%20the%20original%20instruction.md)
  — `[[mid_hook]] call_original=false / "auto"` codegen unmasked
  by FIX C. FIXED 2026-05-20 via commit `03dd155`; CAP-04 sub-tests
  all PASS.
