---
id: KI-0015
opened: 2026-06-10
status: closed
closed: 2026-06-10
closed_by_commit: 40236a9
commit_at_filing: 50dcbf8
---

# KI-0015 — kcdx.find eagerly materializes the corpus into Lua → boot hang on a broad query

**Status:** CLOSED 2026-06-10 — root-caused (falsifiable mechanism below); fixed
`40236a9` (kcdx.find returns lean function headers + a SQL `statement_count`;
statement bodies stay in SQL until kcdx_dev_inspect asks for ONE function);
re-verified at the 2026-06-10 Phase 9.4 acceptance launch — `cap-99-truncates-loud`
PASS (asserts the lean record shape: `statement_count`, no `statements` field).

## Root cause (the mechanism — AP17)

`kcdx.find` was designed (Phase 9.4 step 0) to return, per matched record, the
function's FULL statement list — each statement carrying its captures + applicable-ops
as nested Lua tables. The result is capped to 500 RECORDS, but NOT to total statements.

For a broad query — `kcdx.find({callee="_Init_thread_footer"})`, the cap-99-truncates-loud
test fixture — 30,393 functions match; the 500 capped records together hold **131,691
statement sub-tables + 266,737 capture sub-tables ≈ 400,000 nested Lua tables**, built
on CryEngine's Lua 5.1 on the boot worker thread (tid 63216). Constructing ~400K nested
Lua tables (each `lua_newtable` + multiple `lua_setfield`, under GC pressure) exhausts
memory / stalls the GC and HANGS the suite thread. The watchdog killed the process ~30s
after the last log line and produced a crash bundle with **no minidump / no bugsplat
fault** — confirming a HANG, not an access violation. The game's own thread kept
loading item XML during the stall (`kcd.log` still progressing).

**The architectural defect (user's framing):** the dev DB is a SQL database — queries
should run IN SQL and return ONLY what is asked for. `find` eagerly hauled the full
body of every match across the SQL→Lua boundary instead of letting SQL filter/count/page.
The pure SQL for the same query runs in ~1.5s (measured in Python); the entire stall is
the eager Lua materialization. A SEARCH tool returning the full body of every hit is the
defect.

## Trail

| Date | Action | Result |
|------|--------|--------|
| 2026-06-10 | Phase 9.4 acceptance launch; suite ran to cap-99 | HANG at `cap-99-truncates-loud`; dev log stops at `find_truncated total_matches=30393`; watchdog crash bundle 30s later, NO minidump |
| 2026-06-10 | Theory 1: missing index on the emit-loop queries → full table scans | FALSIFIED — `ix_st_av` / `ix_rv_av` exist; query plans use them; one statements-by-avid query <1ms |
| 2026-06-10 | Probe: time each FindFunctions phase against the real DB in Python | criteria query 0.445s, ranking 0.450s — pure SQL ~0.9s, no stall. The stall is NOT in SQL |
| 2026-06-10 | Measure the Lua-table volume of the 500 capped records | 131,691 statements + 266,737 captures ≈ 400K nested Lua tables — the eager-materialization root cause |

## Facts

- The hang is a memory/GC stall building ~400K nested Lua tables on the boot worker
  thread, not a crash (no AV, no minidump; watchdog timeout).
- The pure SQL completes in ~1.5s; the cost is wholly the SQL→Lua materialization.
- `find` is a SEARCH; returning every statement of every match does not scale AND
  duplicates `kcdx_dev_inspect` (which inspects ONE function).
- The cap is on RECORDS (500), not on total statements — a broad query's 500 records
  can carry hundreds of thousands of statements.

## Resolution path (settled 2026-06-10)

`kcdx.find` returns LEAN function headers: `{function, module, rva, decompile_quality,
statement_count}` — `statement_count` computed in SQL (`SELECT COUNT(*) FROM statements
WHERE address_version_id = ?`), NOT the statement rows. 500 capped records → 500 small
tables, never the ~400K blowup. The statement bodies are `kcdx_dev_inspect`'s scoped
per-function query (one function at a time). The two tools divide: `find` = discover
WHICH function; `dev_inspect` = inspect ONE function's body. Design authority updated:
`docs/outstanding-work/restructure/phase-09.4-discovery/step-0-devdb-search-layer.md`
§"SQL does the work" + §"Result record". cap-99-truncates-loud asserts the lean shape
(records carry `statement_count`, no `statements` field).

## Resolution

**Root cause:** the mechanism above — `kcdx.find` returned the FULL statement list of
every matched record (nested Lua tables for each statement + its captures + applicable-ops),
capped to 500 RECORDS but NOT to total statements; a broad query
(`callee="_Init_thread_footer"`, 30,393 matches) made the 500 capped records carry ~400K
nested Lua tables, built on CryEngine's Lua 5.1 on the boot worker thread under GC
pressure → a memory/GC stall that hung the suite thread (no AV, no minidump — the watchdog
timed out). The original path made the hang inevitable because a SEARCH tool hauled the
full body of every hit across the SQL→Lua boundary instead of letting SQL filter/count/page —
the cost was wholly the eager materialization (the pure SQL ran ~1.5s).

**Fix (`40236a9`):** `kcdx.find` returns LEAN function headers —
`{function, module, rva, decompile_quality, statement_count}` — with `statement_count`
computed in SQL (`COUNT(*)`), never the statement rows. 500 records → 500 small tables, no
~400K blowup. Statement bodies move to `kcdx_dev_inspect`'s scoped per-function query (one
function at a time). The two tools divide: `find` = WHICH function; `dev_inspect` = inspect
ONE function's body.

**Verification:** the 2026-06-10 Phase 9.4 acceptance launch — `cap-99-truncates-loud` PASS
(`kcdx.find{callee="_Init_thread_footer"}` truncated loudly and returned the lean record
shape with `statement_count`, no `statements` field), the boot reached `update tick`
250/273, no crash bundle. (The SEPARATE, later boot hang the lean fix EXPOSED — the
underlying un-indexed dev-DB scan — was KI-0016, also fixed + closed.)
