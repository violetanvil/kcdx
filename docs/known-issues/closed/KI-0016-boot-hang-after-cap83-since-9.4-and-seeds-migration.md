---
id: KI-0016
opened: 2026-06-10
status: closed
closed: 2026-06-10
commit_at_filing: 40236a91e1fab7c970c91e2cbb069e333a4416c5
---

# KI-0016 — boot hang ~16s in (after the cap-83 suite summary), since the 9.4 + seeds-migration window

**Status:** CLOSED 2026-06-10 — root cause = `FindFunctions` full-scanned the dev DB's
5.24M-row `statements` on un-indexed columns (~20s cold on the boot thread; cap-98's 4
scans exceeded the watchdog). Fixed by two indexes + a sargable range rewrite (`e39412b`)
+ an independent SQL-ranking win (`008cd66`); Gate-B-verified; user-confirmed (boot reaches
`update tick` 250/273, all 4 cap-98 rows PASS, no crash bundle). Distinct from KI-0015 (the
find-path eager-materialization hang, FIXED at `40236a9`).

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
| 3 | PROBE C: index the scan columns (`e39412b`) + SQL-rank (`008cd66`); re-launch — is cap-98 now fast? | DONE | PARTIAL. Hang FIXED (boot completes, no crash, 250/273) but cap-98 STILL ~16.7s: dev_db_opened 15:21:34.630 → first find_truncated 15:21:34.681 (queries now 0.05s ✓) → cap-98 RESULT 15:21:51.353 (a ~16.7s gap with queries+ranking already fast). The index + SQL-rank fixes were real but the residual ~16.7s is NOT the ranking loop. **The ranking-loop theory was incomplete** — most of the time is still unexplained. |
| 4 | PROBE D: which engine call eats the ~16.7s? Add timing log lines around each FindFunctions + EnumerateStatements call INSIDE RunSelfTestOnce, on the actual cold boot thread. | DONE | **NONE of them.** All four calls are fast: gate 0ms, find-string 1ms, truncate 65ms, enumerate 1ms — the WHOLE cap-98 block runs 16:00:53.756→53.823 = **67ms total.** The "~16.7s gap" was a MEASUREMENT ERROR: it was the wall-clock between `dev_db_opened` (early-boot, lazy-open) and cap-98's batched RESULT line — filled by THE REST OF BOOT (other plugins + self-tests), NOT by find. cap-98 fires late in the suite and completes in 67ms. The find feature is fast + fully fixed. |

## Reframe (PROBE D) — the post-fix "~16.7s / ~20.5s residual" was a timestamp misread, not real find cost

PROBE D timed each find call ON the boot thread and proved the WHOLE cap-98 block
is **67ms** (gate 0ms, find-string 1ms, truncate 65ms, enumerate 1ms). Every
"residual ~16.7s / ~20.5s" measured in PROBE C and in the instance-2 block below
was the wall-clock between an EARLY-boot log line (`dev_db_opened`, or the first
`find_truncated`) and cap-98's BATCHED RESULT line — and cap-98 fires LATE in the
suite, after the rest of boot runs. The gap is rest-of-boot, NOT find. So:

- **Instance 1 (the genuine hang, PROBE B):** real. One unindexed full scan ~20s
  cold (PROBE B measured `dev_db_opened 13:47:25 → the gate query's own RESULT
  13:47:45` — same-call start/end, a valid measurement). Fixed by the indexes.
- **Instance 2 (the per-id ranking loop):** the SQL-ranking rewrite is a correct,
  beneficial change in its own right (30,393 sequential SQLite round-trips → ONE
  set-based query, verified byte-identical top-500). But the "~20s COLD" cost the
  block below attributes to it was measured the SAME misread way (`first
  find_truncated → cap-98 RESULT`, two non-adjacent log lines) and is therefore
  **unproven** — PROBE D shows the truncate call (which runs that exact ranking
  path) is **65ms**, not ~20s. The rewrite stands as a sound improvement; its cold
  cost was never the boot problem.

The original boot HANG was instance-1 (the unindexed scan), fixed. The find
feature is fast (67ms) and fully fixed.

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
   0.48s warm. **(The "~20s COLD" / ~20.5s-gap cost originally written here was
   DISCONFIRMED by PROBE D — see the Reframe section; that gap was `first
   find_truncated → cap-98 RESULT`, two non-adjacent log lines spanning the rest of
   boot, not the ranking loop. PROBE D times the truncate call — which runs this exact
   ranking path — at 65ms.)** The SQL-ranking rewrite is still correct and beneficial
   (30,393 sequential round-trips → ONE set-based query), but it was never the boot
   problem; the boot HANG was instance-1 (the unindexed scan).

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

## Resolution

**Root cause (the falsifiable mechanism).** `refdb::FindFunctions`'s per-criterion
queries filtered the DEV reference DB's 5.24M-row `statements` table on the columns
`string_ref` (string/cvar criteria) and `callee` (callee/callers_of/callee_in_subsystem) —
columns the dev DB carried **no index for** (it indexed only `(address_version_id, idx)`
= `ix_st_av` and `(kcdx_id)` = `ix_st_kcdx`, neither covering `string_ref`/`callee`).
SQLite therefore executed each find criterion as a **full 5.24M-row scan**
(`EXPLAIN QUERY PLAN` → `SCAN statements USING INDEX ix_st_av` — a covering full-index
walk, not a seek). One such scan is ~20s **cold** on the boot worker thread (the 1.3GB
DB not yet OS-page-cached, contending with the game's own boot I/O — measured directly,
PROBE B: `dev_db_opened 13:47:25 → that one gate query's own RESULT 13:47:45`, a 20s
same-call span). cap-98 runs four such scans at boot; their cumulative cold cost exceeded
the watchdog timeout on the boot thread, so the watchdog killed the process — no access
violation, a hang (no minidump; 3-byte bugsplat). The original path made the slow boot
inevitable because it expressed a corpus-scale filter as an un-indexed column predicate:
the DB had the rows but no seek structure, so every find query paid a full-table scan that
only the cold boot thread made fatal. (KI-0015 was a DIFFERENT mechanism at the same site —
eager Lua materialization of ~400K nested tables; its lean fix removed that and EXPOSED
this underlying scan cost.)

**Fix.** Two indexes added to the DEV/bulk extractor tier
(`data/refdata-extractor/python/import_to_sqlite.py`, under the `not user_projection`
guard): `ix_st_string_ref ON statements(string_ref)` + `ix_st_callee ON statements(callee)`
— the two scanned columns now SEEK (`SEARCH … USING INDEX`). Plus a `refdb.cpp` rewrite of
`callee_in_subsystem` from `callee LIKE 'prefix%'` (which SCANs — SQLite's default LIKE is
case-insensitive) to the sargable range `callee >= prefix AND callee < PrefixUpperBound(prefix)`
(which SEEKs) — same semantics for an ASCII prefix. Landed `e39412b`. A second, independent
improvement also landed (`008cd66`): the per-id ranking loop (`SELECT … WHERE id=?` per
matched id — 30,393 round-trips on the broadest query) was replaced with one set-based
TEMP-table query (`… WHERE id IN (SELECT id FROM temp._find_ids) ORDER BY … LIMIT 500`),
verified byte-identical in its top-500 set + ordering. This is a sound efficiency win but was
NOT the boot problem — see the Reframe: the "~20s cold" cost first attributed to it was a
timestamp misread (the gap between early-boot `dev_db_opened` and cap-98's late batched
RESULT line, filled by the rest of boot).

**Verification.** PROBE D (per-call timing markers ON the boot thread, since removed —
wiring archived at `_research/probe-archive/ki0016-find-boot-cost.md`) measured the WHOLE
cap-98 dev-DB block at **67ms** (gate 0ms, find-string 1ms, truncate 65ms, enumerate 1ms),
no crash bundle, suite-complete at normal boot time. The truncate call — which exercises the
exact criteria-scan + ranking path the hang lived in — is 65ms (was the ~20s full scan). The
cause-test is cap-98 (`find_discovery_selftest.cpp`): four falsifiable rows asserting the
find/enumerate surface works against the dev DB at boot; if the scan cost regressed, cap-98's
calls would re-exceed the watchdog and the boot would hang before its RESULT. User-confirmed
via the repro below.
