# Cross-version matcher sandbox — STATUS (PAUSED 2026-05-27)

**Why paused:** the matcher's whole IDENTITY/HASH formula is under redesign (see
"The open question" below). Do NOT resume the matcher's signal-weighting work as
it stands — it rests on a measurement model we are reconsidering from the root.
The fixture itself is sound and stays.

## What's here

| File | State | What it is |
|---|---|---|
| `make_sandbox.py` | **tracked, DONE** | Builds the two-version fixture: v1 = a real ~200-function slice of the WHGame.dll dump; v2 = an authored mutation of v1 with per-case ground truth. Recipe is tracked; the fixture DATA (`v1/`, `v2/`, `ground_truth.csv`, `*.backup/`) is gitignored. |
| `match_versions.py` | **tracked, SUPERSEDED — do not trust** | The propose-only matcher + per-category self-score. Works mechanically but its SIGNAL MODEL is wrong (see below). Kept for reference + the harness scaffold, NOT as the answer. |
| `ground_truth.csv` | gitignored (regenerate) | The oracle: per v2 entity, expected `MATCHED/NEW/SPLIT/MERGE/DELETED_V1` + a labeled `case`. |

Regenerate the fixture: `python make_sandbox.py` → writes `v1/`, `v2/`,
`ground_truth.csv`, backups. Score the matcher: `python match_versions.py`.

## The fixture's ground-truth cases (13 labeled categories)

`identical`, `moved` (rva changed, bytes same), `changed_body` (bytes + statement
hashes changed, same logical fn), `changed_moved`, `bulk_move` (the ~187
wholesale image-shift fns), `deleted`, `added` (NEW), `split` (1→2, ambiguous),
`merge` (2→1, ambiguous), `swap_trap` (two fns exchange call-targets+strings —
must NOT cross-assign), `string_leaf` (no edges, only a string anchor), `ripple`
(a callee changes, shifting callers' fingerprints), `curated_changed`.

**Fixture fix applied (uncommitted→committed with this doc):** `make_sandbox.py`
now rotates a changed function's PER-STATEMENT content_hashes too (not just the
function hash). A real body change changes the statements' bytes → their hashes.
The earlier fixture left statement hashes identical, which let the matcher cheat
on a signal that does not survive a real patch. Verified: `changed_body`
0x1c30→0x801c30 now has 0/31 identical statement hashes; unchanged `bulk_move`
fns keep identical statement hashes.

## Where the matcher actually stands (honest score)

After the fixture fix, `match_versions.py` scores **7/12 hard cases**. The 5
FAILURES (all → UNMATCHED): `changed_body`, `changed_moved`, `curated_changed`,
`ripple`, `string_leaf` — i.e. exactly the "body changed but it's the same
function" cases, which are THE POINT of cross-version matching. The matcher
leaned on statement-hash Jaccard (W_STMT=0.60) and had no real fallback when that
signal correctly died. It also has an exit-code bug (prints GATE: FAIL but
exits 0). Both are moot until the redesign below.

## THE OPEN QUESTION — why we paused (the root redesign)

The matcher is downstream of a deeper, unresolved question: **what should we even
measure?** The single whole-function `content_hash` conflates change-events that
break mods DIFFERENTLY:

- a **relocation** (a callee/global moved) flips the byte-hash but breaks nothing
  — pure noise for identity;
- a **balance/constant patch** (`Sleep(100)`→`Sleep(101)`) flips the byte-hash,
  breaks a mod that PATCHED that constant, but NOT a mod that hooks/calls the fn;
- a **real logic change** flips the byte-hash and may break hooks.

The hash's actual purpose is **"did the game change in a way that could break THIS
mod?"** — and whether a change breaks a mod depends on WHAT THE MOD DID to the
function (hook entry / patch a constant / trampoline mid-fn / read a vtable slot /
call by name / replace a statement). So one hash cannot answer it for all mods.

**Measured facts that inform the redesign (verified against the real dump
2026-05-27):**
- Immediates SURVIVE verbatim in `statements.pseudo_text` (`Sleep(100)`, not
  `Sleep(<int>)`) — the balance-change signal IS present in the data.
- Statements are tiny (median 5 bytes) — a constant change is ISOLATED to one
  statement's hash, not smeared across the function.
- Constants are NOT queryable as structured data — they live only inside
  free-text `pseudo_text`; no column says "this fn uses immediate 100".

## NEXT STEP (priority 1 when resumed) — the breakage matrix

Do NOT pick masks/weights yet. Derive the measures from a **change-kind ×
mod-dependency breakage matrix**:
1. Enumerate what a mod can DO to a function (hook entry, patch bytes/constant,
   trampoline mid-fn, read vtable slot, call by name, replace a statement, …).
2. Enumerate the kinds of game change (relocation, constant/balance, logic edit,
   ABI change, moved wholesale, deleted, prologue change, length, …).
3. Fill the cross-product: does change C break mod-dependency D?
4. Each mod-dependency (column) → an ASPECT measure that changes IFF a change
   breaking THAT dependency occurs (e.g. entry/prologue measure, constants
   measure, relocation-invariant identity measure).
5. Per-measure masking falls out of step 4 (the identity hash masks call/jmp/rip
   operands; a "same logic" measure may also mask immediates so a balance patch
   reads as a MATCH; a "patchable constant" measure looks ONLY at immediates).
6. Validate on THIS sandbox (extend it with a balance-patch case): each measure
   fires iff the matrix says a mod depending on it would break.

The matcher then consumes the IDENTITY measure; survival uses the per-aspect
measures; authoring can query the constants measure. The matcher cannot be
finalized until the measures are decided — that is why it is paused here.

The full design context is in the private planning doc (the reference-data
restructure plan, §11.6 + the hash-contract notes).
