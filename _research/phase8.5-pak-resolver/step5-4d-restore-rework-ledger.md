# Step-5 rework ledger — Confirm onto the 4d data-core scoped restore-point

Reworking the kept step-5 WIP Confirm to call `data_core.restore(handle)` (the 4d
scoped restore-point) and DROP the full-file `restore_point.py` snapshot. Most of the
WIP already matches the target (db-export target, direct write, event-driven lock from
a prior iteration); the load-bearing change is rework point 3 (the 4d restore-point) +
the deletion-hygiene sweep.

| # | Item | Status | Notes |
|---|------|--------|-------|
| 1 | config.py db_export_dir + db_export_files | DONE (pre-existing) | already added in a prior WIP iteration; verified present (config.py:71-81) |
| 2 | routes_confirm export→db_export_dir + direct write (4c) | DONE (pre-existing) | export already targets config.db_export_dir; write already routes through apply_direct_edit (defer_commit=True) |
| 3a | data_core.py: add `restore = _seeds_shared.restore` (the seam) | DONE | additive re-export |
| 3b | routes_confirm: drop full-file RestorePoint → call data_core.restore(handle) + backend CSV-revert | DONE | the 4d call (DB rows+seq) + re-export-from-restored-DB for the CSVs |
| 3c | DELETE restore_point.py | DONE | replaced by the 4d call |
| 4 | git_commit event-driven index.lock | DONE (pre-existing) | verified: keys off git's exit, _is_index_lock_stderr; no sleep-poll, never reaps |
| 5 | git_commit staged paths + csv_integrity target | DONE (pre-existing) | already db-export CSVs by exact path; integrity over config.db_export_dir |
| 6 | csv_integrity target → data/db-export/ | DONE (pre-existing) | already config.db_export_dir |
| 7 | Rework test_confirm_endpoint.py for the 4d model + assert sqlite_sequence byte-identity | DONE | post-commit-failure restore proves DB rows+seq reset + CSV revert |
| 8 | deletion-hygiene sweep of restore_point.py (zero survivors) | DONE | swept import + use + docstring refs |
| 9 | Run the backend test green | DONE | 41 passed |
| 10 | Probe: 4d restore is logical-rows-identical not file-byte-identical (test oracle fix) | DONE | VERDICT B; finding captured; _state_hash split DB-rows / CSV-bytes |
