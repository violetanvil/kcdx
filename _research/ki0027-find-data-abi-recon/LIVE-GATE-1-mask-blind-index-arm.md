# KI-0027 live gate #1 — the index arm is glob-mask-blind (matched the directory, not the `__*.xml` mask)

**Run:** `kcdx-dev_2026-06-20_15-43-34.log` (step 5.2 `a414d75` deployed). The DB
error STILL fired (`kcd.log`: "Database system error - tables can't be loaded.").

## What the launch proved (ground truth, observed not theorized)

1. **The triplet fires live — the v1.9 dispatch claim is CONFIRMED at runtime.** 194
   real engine `enum slot=FindFirst` dispatches through kcdx slot 63, including the
   table globs: `enum slot=FindFirst pattern="Libs\Tables\rpg\gender__*.xml"
   matched=528`. The KI-0027 enumeration-unserved gap (the prior session's "292
   opens all miss, ZERO enum fires") is CLOSED — the glob is served.
2. **But the match counts are wrong.** `gender__*.xml matched=528`,
   `item_category__*.xml matched=286`, `mailbox_group__*.xml matched=81` — these are
   the counts of EVERY file in the directory, not the `<base>__*.xml`-matching
   subset. A vanilla install has ~0 `__*` override files per base table; 528/286/81
   matches means the filename glob mask was NOT applied.
3. cap-118 PASSED — but its test used `ui/` prefixes with an effectively-`*` mask, so
   it never exercised a RESTRICTIVE `<base>__*.xml` mask. The bug was invisible to it
   (a test gap to close in the fix).

## Root cause (read from the code — `src/fs_takeover/find_slots.cpp`)

`BuildUnifiedFindEntries`'s **INDEX arm (2)** matches on the directory PREFIX only:

```cpp
const std::string normPrefix = IndexDirPrefix(pattern);  // "Libs/Tables/rpg/gender__*.xml" → "libs/tables/rpg/"
...
for (const auto& kv : index) {
    ...
    if (vpath.compare(0, normPrefix.size(), normPrefix) != 0) continue;  // prefix only
    if (vpath.find('/', normPrefix.size()) != npos) continue;            // single-level
    // NO filename-glob mask test — every pak vpath under the prefix is emitted.
    entries.push_back(...);
}
```

The DISK arm (`DiskWalk`) passes the FULL resolved pattern to `_wfindfirst64(wpattern,
...)`, which honors the `<base>__*.xml` filename mask — that arm is correctly
filtered. But the index arm receives only `normPrefix` (the directory), so the
filename mask (`gender__*.xml`) is DROPPED for pak-resident vpaths. The index arm
returns every `Tables.pak` entry under `libs/tables/rpg/` (all 528 tables) as
"`gender__*.xml` matches."

The engine's table-merge loader then receives 528 unrelated tables as "gender
overrides," tries to merge them as override patches → corrupt merge → "Database
system error."

## The fix (mechanical, design-faithful — restores arm symmetry)

`kcdx_FindFirst` must extract the pattern's filename glob mask (the basename of
`pattern` past the last separator — `gender__*.xml`) and pass it to
`BuildUnifiedFindEntries`; the index arm wildcard-matches each candidate base name
against that mask, exactly as `_wfindfirst64` does for the disk arm. Both arms then
honor the SAME glob. A simple `*`/`?` wildcard matcher suffices (the engine's masks
are `<base>__*.<ext>` and `*.<ext>` shapes — no character classes).

cap-118 gains a restrictive-mask assertion: a `<base>__*.xml`-style glob over a
prefix with both matching and NON-matching pak entries returns ONLY the matching
subset (the missing-mask regression).

## Why this is one more step, not a redesign

The triplet ownership, the find-handle lifecycle, the find-data ABI, the disk arm,
the de-dup — all CONFIRMED correct live (194 fires, the disk arm filters right). The
single defect is the index arm dropping the filename mask. The design §5.1 always
intended the index arm to be "the unified index's pak-resident vpaths under the
prefix that MATCH THE GLOB" (the parallel to slot-14 ForEachFile, which the engine's
own pattern-match drives) — the impl narrowed "match the glob" to "match the
prefix." A one-function fix.
