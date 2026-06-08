---
id: KI-0009
opened: 2026-06-08
status: closed
closed: 2026-06-08
commit_at_filing: 153a51f337c02cd9e9fd1e3ea4c0f2c6c9e8f01b
---

# Rebuild oracle red — `address_names` content-hash drift (stale baseline)

**Status:** fixed

## Summary

`test_rebuild_oracle.py::test_rebuild_matches_baseline` reports an `address_names`
content-hash drift in BOTH the USER and DEV rebuilt DBs
(`[user.address_names] content hash differs (baseline 2e046c36.., got 77b6fab9..)`
+ the identical `[dev.address_names]` line). Red all session. Suspected a STALE
BASELINE (a legitimate post-baseline seed edit the oracle was never re-captured
against), NOT a code-path defect — the drift is identical in both DBs, so it is a
seed↔baseline mismatch, not a USER/DEV projection bug. This investigation proves
which seed change caused it and confirms no UNEXPECTED drift rides along, then the
fix is a deliberate, inspected oracle re-capture (the discipline the baseline
doc-comment mandates: "Re-capture ONLY for a deliberate, reviewed output change —
never to paper over an unexplained drift").

## Trail

| Date | Action | Result |
|------|--------|--------|
| 2026-06-08 | PROBE A: when was the oracle baseline last captured vs the address_names seed's edit history? | `oracle_baseline.json` last captured at `36d2682` (survival-fold P3.6 finalize). `address_names_seed.csv` has ONE commit that postdates it: `3cc6a67` (id-152 prose correction). The other three seed commits (`e37b5af`/`8f14bd6`/`3310a0a`) are ANCESTORS of `36d2682` — already in the baseline. Recent single-commit desync, not longstanding. |
| 2026-06-08 | PROBE B: did the seed ROW COUNT change between `36d2682` and HEAD? | No. 157 data rows at `36d2682`, 157 now. Drift is a CONTENT (prose) change to existing rows, not an add/remove. |
| 2026-06-08 | PROBE C: what is the ONLY post-baseline `address_names_seed.csv` change? | `git diff 36d2682..HEAD` on the seed = exactly `3cc6a67`: a prose-only rewrite of row id 152 (`CCryPak_AdjustFileName`) `notes` column, v1 "every by-name consumer calls slot 1" → corrected two-lane/two-hook model. No id/column/count change. |
| 2026-06-08 | PROBE D: full rebuilt-DB-vs-baseline drift set — is `address_names` (the `3cc6a67` prose) + the intended `a9b0e8a` statement-tables the ONLY drift, with nothing unexpected moved? | EXACTLY 4 problem lines, all `[user.*]`, all explained: (1) `table set extra=['referenced_vars','statements']` = intended `a9b0e8a`; (2) `[user.address_names] hash differs` = `3cc6a67`; (3+4) `[user.sqlite_sequence] count 5≠3 + hash` = mechanical (the 2 new USER autoincrement tables bump sqlite_sequence 3→5). ZERO `[dev.*]` lines — DEV fully matched baseline. |
| 2026-06-08 | PROBE E: address_names `notes` carries `3cc6a67` in BOTH DBs (identical column), yet PROBE D's truncated output showed only USER drifting — re-observe each DB's address_names hash vs baseline directly (the oracle's own `_hash_db`). | BOTH drifted identically: baseline user+dev `2e046c36…` → current user+dev `77b6fab9…`; `user drifted: True`, `dev drifted: True`. PROBE D's "zero DEV drift" was a truncated-output artifact (the `[dev.address_names]` line sat below the captured window), NOT a USER/DEV asymmetry — `address_names` is one identical table in both projections, so the `3cc6a67` prose drifts both. |
| 2026-06-08 | FIX: re-capture `oracle_baseline.json` (`python tests/test_rebuild_oracle.py --capture`), then re-run the gate. | Re-captured (12 insertions, 4 deletions — only the explained lines: USER `address_names`+`statements`+`referenced_vars`+`sqlite_sequence`, DEV `address_names`). Gate now PASSES: "rebuild output is byte-identical to the recorded baseline" (USER 15 tables, DEV 16 tables, all hashes match). Sibling `test_csv_exporter`+`test_round_trip` green (5 passed). |

## Facts

- The oracle baseline (`oracle_baseline.json`) was last captured at `36d2682`; `address_names: count 157, hash 2e046c36…` (PROBE A).
- Of the four `address_names_seed.csv` commits in the git log, only `3cc6a67` postdates the baseline; `e37b5af`/`8f14bd6`/`3310a0a` are ancestors of `36d2682` (PROBE A).
- The seed row count is 157 at both `36d2682` and HEAD — unchanged (PROBE B).
- The single post-baseline seed change is `3cc6a67`, a prose-only correction to row id 152's `notes` column (PROBE C).
- `address_names` carries the `notes` column in BOTH the USER and DEV projections (it is one identical table, same baseline hash `2e046c36…` in each); the `3cc6a67` prose edit therefore drifts `address_names` IDENTICALLY in both DBs → `77b6fab9…` (PROBE E).
- This session's `a9b0e8a` (statement-subset) added the `statements` + `referenced_vars` USER tables — an INTENDED drift the baseline also predates; it adds two USER-side autoincrement tables, which mechanically bumps USER `sqlite_sequence` 3→5 (PROBE D).
- The COMPLETE drift set against the baseline is fully explained: (a) `[user] table set extra=['referenced_vars','statements']` + `[user.sqlite_sequence] 3→5` = the intended `a9b0e8a`; (b) `[user.address_names]` + `[dev.address_names]` hash drift = the `3cc6a67` prose. NO table that should be untouched moved; no unexpected drift (PROBE D + PROBE E).

## Open questions

- None. The drift is fully explained as a stale baseline (a legitimate, already-committed `3cc6a67` prose correction + this session's `a9b0e8a` table addition, both postdating the last `36d2682` re-capture). The fix is a deliberate, inspected oracle re-capture.

## Resolution

**Root cause:** the rebuild oracle's recorded baseline (`oracle_baseline.json`, last captured at `36d2682`) predates two legitimate, already-committed output changes, so a fresh rebuild correctly drifts from it. (1) `3cc6a67` (a prose-only correction to `address_names_seed.csv` row id 152's `notes` column — the v1 "every by-name consumer calls slot 1" model rewritten to the corrected two-lane/two-hook model, no row/id/column/count change) is carried into the rebuilt `address_names.notes` cell → the `address_names` content hash flips `2e046c36…` → `77b6fab9…` in BOTH DBs (the column ships to USER and DEV identically). (2) `a9b0e8a` (this session's statement-subset deliverable) added the `statements` + `referenced_vars` tables to the USER projection → the USER table set gains those two entries and, because both are autoincrement, USER `sqlite_sequence` mechanically bumps 3→5 (count + hash). No table that should be untouched moved; the seed row count is 157 at `36d2682` and at HEAD; there is no code-path defect. The oracle was simply never re-captured against either change. This is the exact "stale baseline" the test's own doc-comment anticipates ("Re-capture ONLY for a deliberate, reviewed output change — never to paper over an unexplained drift"); here the drift is reviewed and fully explained, so the re-capture is sanctioned.

**Fix:** re-captured `oracle_baseline.json` from the current code via `python tests/test_rebuild_oracle.py --capture`. The new baseline records the legitimate output state: USER `address_names` hash `77b6fab9…` (the `3cc6a67` prose), the `statements` (2385) + `referenced_vars` (5595) USER tables and the `sqlite_sequence` 3→5 bump (the `a9b0e8a` addition), and DEV `address_names` `77b6fab9…` (the same prose). The `oracle_baseline.json` diff is 12 insertions / 4 deletions — only the explained lines moved, no collateral churn. No code/extractor change — the rebuild path was correct; only the recorded snapshot was stale. Gate B (root-cause-verifier) returned `land-fix`: every claim independently verified, the drift maps 1:1 onto `3cc6a67` + `a9b0e8a` with nothing left over (the sanctioned "deliberate, reviewed output change", not a paper-over).

**Verification:** `test_rebuild_oracle.py::test_rebuild_matches_baseline` now PASSES — "rebuild output is byte-identical to the recorded baseline" (USER 15 tables, DEV 16 tables, all hashes match). The headless rebuild emits zero drift. Sibling data-core tests `test_csv_exporter` + `test_round_trip` confirmed green (5 passed) — no GREEN test went red. Headless gate (extractor + sqlite assertion); no game launch or user gesture — the acceptance signal is the oracle's own PASS line.
