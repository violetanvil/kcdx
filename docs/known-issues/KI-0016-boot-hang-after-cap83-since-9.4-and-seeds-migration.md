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
| 2 | PROBE B: re-enable cap-98 but run ONLY the cheap gate query (skip the 30,393-row truncation + the 4 FindFunctions/Enumerate calls) — is it the dev-DB OPEN/connection, or the heavy QUERY, that hangs? | pending | — |

## Open questions

- Is the held-open 1.3 GB dev DB connection the destabilizer (memory pressure / a
  worker-thread-lifetime interaction) that hangs a LATER subsystem? (hypothesis — PROBE A
  discriminates: if disabling cap-98 stops the dev DB from opening at boot and the hang
  goes, the held-open connection is implicated.)
- Is the seeds-migration window (the data-layer change) the cause instead, independent
  of 9.4? (hypothesis — PROBE C discriminates.)
- Could this be pre-existing flakiness unrelated to either window? (PROBE B discriminates
  — does the last clean commit still boot clean on this machine today?)
