# Cross-version matcher sandbox — STATUS (2026-05-27, post-streamline)

**Status:** matcher is **RE-SCOPED** (not dropped, not pending a hash redesign).
The earlier "paused pending the hash redesign" framing is stale — that redesign
happened; the conclusion was that **the cross-version matching problem was
mis-sized**, not that the matcher was wrong.

**What changed:** the matcher was being judged against an impossible bar
(re-identify all ~321K functions across versions, with 70% accuracy = 96K to
review by hand = infeasible). The actual problem is much smaller: only the
~139-row curated set needs cross-version tracking, because all other targets are
either author-declared per-version (Track 2) or never cross-version-tracked
(Track 3, per-version discovery snapshots). At ~139, even a 7/12-hard-case
matcher saves real maintainer work per patch. The matcher's role is therefore
**maintainer-side assist for re-verifying the curated set per patch** — auto-
confirm the obvious (hash-equal across versions), propose for the changed,
flag genuinely ambiguous for human decision.

The full streamlined model is `docs/outstanding-work/parallel-ghidra-research.md`
§11.8 (three tracks: curated / author-declared via `kcdx.declare(module, name,
versions)` / bulk DEV-DB discovery). This file is the sandbox-local breadcrumb.

## What's here

| File | State | What it is |
|---|---|---|
| `make_sandbox.py` | tracked, DONE | Builds the two-version fixture: v1 = a real ~200-function slice of the WHGame.dll dump; v2 = an authored mutation of v1 with per-case ground truth. Recipe is tracked; the fixture DATA (`v1/`, `v2/`, `ground_truth.csv`, `*.backup/`) is gitignored. |
| `match_versions.py` | tracked, **RE-SCOPED** | The propose-only matcher + per-category self-score. Originally framed as a bulk 321K matcher (and judged "failing" at 7/12); now correctly framed as the curated-set maintainer assist (where 7/12 hard cases is genuinely useful labor-saving). The algorithm + harness scaffold are sound for that role; the signal weighting can still be improved when the assist is wired into a real maintainer workflow. |
| `ground_truth.csv` | gitignored (regenerate via make_sandbox) | The oracle: per v2 entity, expected `MATCHED/NEW/SPLIT/MERGE/DELETED_V1` + a labeled `case`. |
| ~~`BREAKAGE-MATRIX.md`~~ | DELETED (uncommitted) | Its job was to derive per-aspect measures for the auto-track-everything world. The streamline dissolved that world: Track 1 is hand-maintained ~139 (no per-aspect derivation needed — the maintainer just re-verifies); Track 2 is author-declared per-version (no engine pre-check at all, only graceful failure); Track 3 is never cross-version-tracked. The matrix doc had no work to do under the streamlined model. Deletion is intentional, recorded here. |

Regenerate the fixture: `python make_sandbox.py` → writes `v1/`, `v2/`,
`ground_truth.csv`, backups. Score the matcher: `python match_versions.py`.

## The fixture's ground-truth cases (13 labeled categories) — still relevant

These remain the right test set for a maintainer-assist matcher operating on the
curated set: `identical`, `moved`, `changed_body`, `changed_moved`, `bulk_move`,
`deleted`, `added` (NEW), `split` (1→2), `merge` (2→1), `swap_trap`,
`string_leaf`, `ripple`, `curated_changed`.

**Fixture fix already applied:** `make_sandbox.py` rotates a changed function's
per-statement content_hashes when it rotates the function hash (a real body
change changes statement bytes → their hashes). The earlier fixture left
statement hashes identical, which let the matcher cheat on a signal that doesn't
survive a real patch. Verified: `changed_body` 0x1c30→0x801c30 has 0/31
identical statement hashes; unchanged `bulk_move` fns keep identical statement
hashes.

## Where the matcher stands (honest score, correctly framed)

`match_versions.py` scores **7/12 hard cases** on the honest fixture. The 5
"failures" all → UNMATCHED (the matcher correctly abstained rather than guessing
wrong on `changed_body`, `changed_moved`, `curated_changed`, `ripple`,
`string_leaf`).

**Under the original framing (bulk 321K):** 7/12 + UNMATCHED-the-rest = useless,
because 5/12 abstention × 321K = tens of thousands the maintainer must hand-
review per patch. Not feasible.

**Under the re-scoped framing (curated ~139):** 7/12 + UNMATCHED-the-rest =
*useful*, because at ~139 the abstention list is short — the maintainer reviews
a handful per patch instead of re-verifying all 139 from scratch. The propose-
only + flag-don't-guess design is exactly the right shape at this scale.

The matcher's signal weighting (currently `W_STMT=0.60` statement-hash
dominant) is still wrong on the merits — statement hash dies on a changed body,
exactly when matching is hard. When the matcher is wired into the real
maintainer workflow, the call-graph fingerprint + string anchors should be
weighted up. But that improvement is a tuning pass, not a redesign — the
contract (propose-only, abstain don't guess, per-category self-score) is right.

There's also an exit-code bug: it prints `GATE: FAIL` but exits 0. Fix
alongside the weighting pass.

## NEXT STEP (when resumed)

The streamline relocates this work into a coherent sequence (see §11.8.6 in
the design doc):

1. **DONE here** (this STATUS refresh) — record the re-scoping; delete the
   superseded BREAKAGE-MATRIX scaffolding.
2. Narrow the `address_versions` populated set (DONE under the §11.9 flatten:
   USER ships curated rows only -- those with `kcdx_id NOT NULL`; DEV keeps the
   bulk for `kcdx.find`).
3. Design + implement the **Track-2 `kcdx.declare(module, name, versions)`**
   surface (author UX; the engine resolver; integration with `kcdx.hook` /
   `bytes` / `code` so they accept a declared name as a target).
4. Design + implement the **recovery + rollback machinery** that default-ON
   safety requires (`docs/outstanding-work/restructure-plan.md` outstanding-work
   list — the new "Recovery + rollback for Track-2 plugins…" bullet).
5. Re-purpose this matcher to the **curated-set assist role**: tune the signal
   weighting toward call-graph + strings, fix the exit-code bug, wire into the
   maintainer's per-patch re-verification workflow.

The full design context: `docs/outstanding-work/parallel-ghidra-research.md`
§11 (the schema) + §11.8 (the streamline).
