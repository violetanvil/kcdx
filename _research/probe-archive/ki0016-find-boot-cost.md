# PROBE archive — KI-0016 find-discovery boot cost (instance-by-call timing)

**Verdict:** the find feature is fast (67ms for cap-98's whole dev-DB block). The
original boot hang was ONE unindexed full-scan (~20s cold), fixed by two indexes.
A later "~16.7s residual" I chased was a **timestamp misread** — the gap between
early-boot `dev_db_opened` and cap-98's late-firing batched RESULT line, filled by
the rest of boot, not by find.

**Known-issue:** `docs/known-issues/closed/KI-0016-boot-hang-after-cap83-since-9.4-and-seeds-migration.md`.

## The trap this probe exists to break (reuse hint)

When a self-test opens the dev DB lazily EARLY in boot but REPORTS its result LATE
(the suite batches RESULT lines), the wall-clock between `dev_db_opened` and the
`cap-NN RESULT` line is **rest-of-boot**, not the cap-NN work. Do NOT measure a
boot-thread call's cost by subtracting two non-adjacent log timestamps. Measure
start/end markers around the SAME call, on the boot thread. That is the whole
lesson; the wiring below is how.

## The probe wiring (theory-independent per-call timing)

Bracket each dev-DB call inside `find_discovery_selftest.cpp::RunSelfTestOnce`
with a before/after `LOG_DEBUG_KV` under a stable category tag, so the dev log's
ms timestamps attribute cost to the SPECIFIC call (gate / find-string / truncate /
enumerate), not to a span that includes unrelated boot work:

```cpp
// === DIAGNOSTIC (PROBE D) — KI-0016: time each dev-DB call on the boot thread.
LOG_DEBUG_KV("KI0016", "probe_d_before_gate", ::kcdx::log::KV::BareStr("call", "FindFunctions(impossible-string)"));
refdb::FindResult probe = refdb::FindFunctions(StringCriteria(kImpossibleString));
LOG_DEBUG_KV("KI0016", "probe_d_after_gate", ::kcdx::log::KV::BareStr("call", "FindFunctions(impossible-string)"));
// …repeat the before/after pair around each remaining FindFunctions / EnumerateStatements call.
```

Read with: grep the `KI0016` tag in the newest `kcdx-engine/logs/kcdx-dev_<ts>.log`;
subtract each `*_before_*`/`*_after_*` pair (adjacent, same call) for that call's
true cost.

## Outcome→meaning map (as run)

- A single call eats the seconds → that call's query is the cost; optimize it.
- ALL calls fast, yet `dev_db_opened → cap-98 RESULT` still shows seconds → the
  span was never find's cost; it is rest-of-boot between the early open and the
  late batched report. **← this is what happened (67ms total).**

## Measured result (boot `16:00:53`)

| call | start | end | cost |
|------|-------|-----|------|
| gate (impossible-string) | 53.756 | 53.756 | 0ms |
| find-string (known) | 53.756 | 53.757 | 1ms |
| truncate (over-cap callee, 30,393 matches → cap 500) | 53.757 | 53.822 | 65ms |
| enumerate (known fn) | 53.822 | 53.823 | 1ms |

Whole cap-98 dev-DB block: **67ms.** No crash bundle. Suite-complete `16:00:55`
(normal boot).

## The real fixes (both landed, both correct)

1. **Two indexes** in the extractor (`import_to_sqlite.py`, DEV-tier only):
   `ix_st_string_ref` on `statements(string_ref)` + `ix_st_callee` on
   `statements(callee)` — the two 5.24M-row scan columns. Commit `e39412b`.
2. **`callee_in_subsystem` range rewrite** in `refdb.cpp`: `callee LIKE 'prefix%'`
   does NOT seek (case-insensitive default → SCAN); rewritten to
   `callee >= prefix AND callee < prefix_upper` (PrefixUpperBound) which seeks.
   Commit `e39412b`.
3. **SQL set-based ranking** (`refdb.cpp`): replaced the per-id
   `SELECT … WHERE id=?` ranking loop (30,393 round-trips for the broad query)
   with one TEMP-table `… WHERE id IN (SELECT id FROM temp._find_ids) ORDER BY …
   LIMIT 500`. Verified byte-identical top-500 + ordering. Commit `008cd66`.
