---
id: KI-0016
opened: 2026-06-10
status: open
commit_at_filing: 40236a91e1fab7c970c91e2cbb069e333a4416c5
---

# KI-0016 — boot hang ~16s in (after the cap-83 suite summary), since the 9.4 + seeds-migration window

**Status:** open — investigating. Distinct from KI-0015 (the find-path hang, confirmed
FIXED at `40236a9`; cap-99-truncates-loud now PASSES lean). This is a SEPARATE, later
hang.

## Symptom

Every boot since the 2026-06-09 window crashes ~16s in. The suite reaches
`suite: 230/259 passing as of cap-83 stmt-resolve` (27 rows still PENDING, incl.
cap-98), the dev log stops at the cap-83 periodic SUMMARY block (13:26:27.688), and the
watchdog produces a crash bundle ~8s later (13:26:35.6). No minidump (3-byte
bugsplat) → a watchdog HANG, not an access violation. The last logged engine
operations (SURVIVAL / SURVAGREE / KI0001 / STMT_RESOLVE_SELFTEST) all report
`selftest_pass` — nothing is mid-fault; the boot just stops logging, then the watchdog
fires.

## Reproduction

Launch KCD2 with the current build (HEAD `40236a9`) + dev mode on + the dev DB present.
Boot hangs ~16s in; never reaches the menu. (`kcdx-dev_2026-06-10_13-25-57.log` is the
captured failing session.)

## Evidence (ground truth)

- The last clean boot was 2026-06-09 23:46 (`kcdx-dev_2026-06-09_23-46-02.log`):
  reached "update tick" `240/263 passing`, NO crash bundle.
- All three 2026-06-10 boots crash. The 12:39 + 12:49 boots died at `17/244 as of
  kLuaReady` (the KI-0015 find-path hang, since fixed). The 13:25 boot (post-KI-0015-fix)
  got much further — `230/259 as of cap-83` — then hung.
- No minidump; 3-byte bugsplat log → a hang, not an AV.

## Suspect set (the diff since the last clean boot — NOT assumed to be 9.4 alone)

Two independent efforts landed between 2026-06-09 23:46 and now:

1. **Phase 9.4** (`98acc8e` step 0 → `e83d1a4` step 1 → `af1a773` step 2 → `50dcbf8`
   step 3 → `40236a9` KI-0015 fix): adds the refdb dev-DB connection (`g_devDb`, a
   1.3 GB SQLite connection lazy-opened at ~13:26:09 and HELD OPEN through the rest of
   boot), the find/dev_inspect surfaces, and the cap-98 engine self-test (which runs 4
   FindFunctions calls at boot, opening the dev DB). The only persistent 9.4 state
   spanning the whole boot is the held-open dev DB connection.
2. **seeds-to-tracked-csv-migration** (`f5023f5` D38 design → `888eb81` plan →
   `b62e2e1` / `6af2d80` / `99a1aab` / `f8556c2`): retires the seed CSVs, adds a bulk
   exporter + Git LFS tracking for `data/db-export-bulk/`. Touches the data layer.

The crash is at the cap-83 / survival-check / stmt-resolve cluster — NONE of which is
9.4 or seeds-migration code directly. So the hang is a SIDE EFFECT of one of the two
windows' changes on an unrelated subsystem, OR a pre-existing flakiness. The first
probe must DISCRIMINATE which window (if either) is responsible — observe, do not assume.

## Facts

- The hang is a watchdog timeout, not an AV (no minidump; 3-byte bugsplat) (ground truth).
- The boot reaches the cap-83 suite summary (230/259) then stops logging; the watchdog
  fires ~8s later (ground truth).
- The last engine self-tests before the stop all reported `selftest_pass` (ground truth).
- cap-98 never REPORTed — it fires after cap-83 and was not reached (ground truth).
- The find feature works: all 6 cap-99 rows PASS this boot (ground truth).
- The 1.3 GB dev DB connection (`g_devDb`) opened at 13:26:09 and was never closed
  during boot (ground truth — engine log `dev_db_opened` with no matching close).
- The seeds-migration window added ONLY build-time Python (`bulk_exporter.py`, tests,
  `import_to_sqlite.py` edits) — NO engine `src/` code, and it did NOT touch the shipped
  `reference.sqlite` (still 569 KB / 2385 statements / 157 av, unchanged). So it ships
  nothing into the running engine boot (ground truth — `git diff` over the window).
  → **seeds-migration EXONERATED as a runtime-boot cause; the only engine-affecting
  change since the last clean boot is Phase 9.4.**

## Probe plan (persisted before running — plan-persistence)

| # | Probe | Status | Result |
|---|-------|--------|--------|
| 1 | PROBE A: disable the cap-98 engine self-test (suite-gate off the boot dev-DB consumer) — does the hang go? | DONE | Boot reached `update tick` 246/273, NO crash bundle (vs the hang at `cap-83` 230/259). cap-98 / dev-DB-at-boot IS the hang; 9.4 confirmed the cause, seeds-migration + pre-existing flakiness eliminated. |
| 2 | PROBE B: re-enable cap-98 but run ONLY the cheap gate query (skip the 30,393-row truncation + the heavy FindFunctions/Enumerate calls) — is it the dev-DB OPEN/connection, or the heavy QUERY, that hangs? | DONE | Boot CLEAN to `update tick` 250/273, NO crash bundle — BUT the ONE cheap gate query took ~20s (dev_db_opened 13:47:25 → gate RESULT 13:47:45). The dev-DB OPEN is fine; the QUERY is the hang. Mechanism: the criteria queries scan `statements` on the UN-INDEXED `string_ref`/`callee` columns (full 5.24M-row scan, `SCAN statements USING INDEX ix_st_av`), ~20s each cold on the boot worker thread; cap-98's 4+ scans exceed the watchdog. |

## Root cause — the COMPLETE story (two instances of one pattern: find did corpus work in C++ instead of SQL)

The hang had ONE root pattern with TWO instances. The index fix (`e39412b`) closed the
first; the re-launch surfaced the second. Both are "find made the engine do corpus-scale
work that SQL should do":

1. **Unindexed full scan (fixed `e39412b`)** — the criteria queries scanned the 5.24M-row
   `statements` table on un-indexed `string_ref`/`callee` (~20s cold). Fixed by the two
   indexes + the sargable `callee_in_subsystem` range rewrite. After this, the QUERIES are
   fast (measured 0.3s in the re-launch: `dev_db_opened` 14:24:03.542 → first
   `find_truncated` 14:24:03.842).

2. **Per-id ranking loop (this fix — folded into KI-0016)** — after collecting the matched
   id set, `FindFunctions` ranked it by reading `(decompile_quality, rva)` for EVERY matched
   id via an individual `SELECT ... WHERE id=?` round-trip, THEN `std::sort`ing, THEN capping
   to 500. For the 30,393-match query that is **30,393 sequential SQLite round-trips** —
   0.48s warm but **~20s COLD** on the boot thread (re-launch: first `find_truncated`
   14:24:03.842 → cap-98 RESULT 14:24:24.360, a ~20.5s gap with the queries already fast).
   So the boot no longer HANGS (cap-98 completes, boot reaches `update tick` 250/273, no
   crash bundle), but the dev tool is still ~20s-slow per broad query — the same
   UX-cornerstone problem the index fix was meant to solve.

**The fix:** rank + limit IN SQL. Replace the 30,393-round-trip per-id loop with ONE
set-based query: `SELECT id, decompile_quality, rva FROM address_versions WHERE id IN
(<the matched set>) ORDER BY (quality, NULL/0→INT_MAX) ASC, rva ASC LIMIT 500`. The
multi-criterion AND-intersect stays in C++ (it already builds `running`); only the
ranking loop becomes SQL; `total_matches = running.size()` is unchanged (the truncation
signal). **Verified byte-exact:** the set-based query's top-500 set + ordering is
IDENTICAL to the current C++ sort (`cpp_top == sql_top` → True), and runs **0.048s vs
~20s** (one round-trip returning only the 500 needed rows, not 30,393).

## Root cause (instance 1 — the falsifiable mechanism — AP17)

`refdb::FindFunctions`'s per-criterion queries filter the 5.24M-row `statements` table
on the **un-indexed** columns `string_ref` (string/cvar criteria) and `callee`
(callee/callers_of/callee_in_subsystem criteria). The dev DB indexes only
`statements(address_version_id, idx)` (`ix_st_av`) and `statements(kcdx_id)`
(`ix_st_kcdx`) — neither covers `string_ref` or `callee`. So every find criterion query
is a FULL scan of all 5.24M rows (`EXPLAIN QUERY PLAN` → `SCAN statements USING INDEX
ix_st_av` — a covering full index scan, not a seek). Measured: one such scan is 0.42s
warm-cache in Python, but **~20s cold** on the boot worker thread (CryEngine's SQLite,
the 1.3 GB DB not yet OS-page-cached, contending with the game's own boot I/O — PROBE B:
`dev_db_opened` 13:47:25 → the one gate query's RESULT 13:47:45). cap-98 runs 4+ such
scans (the gate probe + the known-string find + the 30,393-owner truncation + an
EnumerateStatements); cumulatively they exceed the watchdog timeout on the boot thread →
the watchdog kills the process (no AV — a hang). PROBE A (no query) booted clean; PROBE B
(one ~20s query) booted clean-but-slow; the full cap-98 (4+ scans) hangs. The original
KI-0015 find-hang was a DIFFERENT mechanism (eager Lua materialization) at the same site —
the lean fix removed that, exposing this underlying full-scan cost.

**Index fix verified:** adding `CREATE INDEX ON statements(string_ref)` (build ~1.3s)
makes the gate query `0.0001s` — a ~4000× speedup that eliminates the scan cost. The same
applies to a `callee` index.

**The settled fix (Option 3 + A, user-decided 2026-06-10; architect Gate A; probed):**
- **Two indexes in the extractor** (`data/refdata-extractor/python/import_to_sqlite.py`):
  `statements(callee)` + `statements(string_ref)` — the two 5.24M-row scan columns. Verified:
  with them, `callee=?` and `string_ref=?` SEEK (`SEARCH ... USING INDEX`). Covers
  string / cvar / callee / callers_of.
- **A `refdb.cpp` query rewrite for `callee_in_subsystem`:** its `callee LIKE 'prefix%'`
  does NOT seek the index (SQLite's default LIKE is case-insensitive → `SCAN`, 0.451s warm /
  ~20s cold). Rewrite it to the range form `callee >= prefix AND callee < prefix_upper`,
  which DOES seek (`SEARCH ... USING INDEX`, 0.0001s). Same semantics for an ASCII prefix.
- **`name_contains` needs NO new index:** the curated side already has `ix_an_name`; the
  `auto_name` side is a leading-wildcard `%?%` an index can't seek, but it scans only the
  321k-row `address_versions` table (61× smaller than statements) — measured **0.035s warm**
  (both sides), fast even cold. No `auto_name` index (it would be unused — leading-wildcard).
- **Keep cap-98's boot self-test lean** (defense-in-depth): the heavy truncation query need
  not run at boot once the indexes make it fast; the self-test's contract is still falsifiable.

## Open questions (the fix is a design fork — Gate A)

- **Where does the fix go: the dev DB schema (add the missing indexes) or the boot path
  (don't run heavy find queries at boot)?** Adding `string_ref` + `callee` indexes to the
  dev DB (built by the extractor) fixes find for ALL uses — including an author's in-game
  `kcdx.find` query, which would otherwise also eat a ~20s scan. Moving cap-98 off the boot
  thread (or making it not run the heavy queries at boot) fixes the boot hang but leaves
  every real find query slow. The indexes are the root fix; the boot-path change is a
  symptom mask. (Gate A architect-review decides the design surface — the fix touches the
  dev-DB build / the self-test; surfaced to the user after.)
