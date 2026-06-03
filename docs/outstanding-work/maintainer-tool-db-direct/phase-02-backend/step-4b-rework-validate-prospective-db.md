# Step 4b-rework — re-target the preview Save's validate to the prospective DB state (D19)

**What.** The landed 4b preview-Save endpoints (`f348857`) validate via the data-core's
`validate_prospective_seeds` — the **seed-validate** path (build a prospective seed CSV, run the
validator against it). Under the direct-write model (D19), validation runs against the
**prospective DB state** (the DB as it would be after the edit), not a prospective seed. This
step re-targets the preview Save's validate call to the prospective-DB-state validate path that
step 4c establishes, so the Save preview's `valid`/`errors` verdict comes from the SAME validator
4c's direct write runs — one gate, DB-targeted.

**Scope.**
- Re-point each of the six preview Save endpoints (`routes_save.py`) from
  `validate_only=True` over the seed-validate path to **4c's prospective-DB-state validate**
  (the dry-validate entry 4c exposes — validate the prospective DB state, return the verdict, NO
  write). The field-delta in the response is unchanged (the data-core's `field_delta`, step 3).
- The Save preview stays **preview-only** (NO write, NO transaction, NO held state — the
  Save-previews/Confirm-transacts model is unchanged; only the validate's SOURCE moves from a
  prospective seed CSV to the prospective DB state).
- Deletion-hygiene: if 4c removes/renames `validate_prospective_seeds` (the seed-validate entry),
  sweep 4b's reference to it + the backend seam's re-export + any test/doc naming the seed-validate
  path.

**Assess at build time (the conditional this step guards).** If 4c re-targets the data-core's
validate entry to prospective-DB-state UNDER THE SAME function surface 4b already calls (the
likely clean outcome), this step's code change is minimal — 4b's existing calls validate
correctly once 4c lands, and this step is mostly the test re-point + the deletion-hygiene sweep.
If 4c changes the validate entry's signature/name such that 4b's calls break, this step carries
the larger re-point. Either way, 4b's preview must end this step validating the **prospective DB
state**, matching the gate the Confirm direct-write runs (no validate divergence between Save
preview and Confirm).

**Out of scope.** No write (4b stays preview-only). No Confirm/git (step 5). No data-core write
logic (4c owns it).

**Test bar.** The reworked `tests/test_save_endpoints.py` (already preview-only): each shape's
Save returns the field-delta + `valid:true` for a valid edit (+ the AP18/nothing-changed flags
on create); an invalid edit per shape returns `valid:false` + the validator's error AND **the DB
is byte-identical** (the no-write proof — unchanged) AND the verdict now comes from the
**prospective-DB-state validate** (4c's path), not the seed-validate path. The
create-version-at-a-new-tag preview now returns `valid:true` (the old seed-validate would have
been blind to it / mis-judged it) — assert the new-tag preview validates. Real app + real
data-core over the mini-dump checkout. Run: `python -m pytest
data/maintainer-tool/backend/tests/ -q`.

**Dependencies.** Step 4c (the prospective-DB-state validate path the preview re-points to) +
the landed 4b (`f348857`, the preview Save endpoints this reworks). Sequenced after 4c (the
consumer after its producer, `.claude/rules/incremental-delivery.md`).

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§10 **D19** (validation runs against the prospective DB state) + §5 (the single validator gate,
DB-targeted) + §6 US-3…US-8 (the six job shapes the preview covers). [`../plan-spec.md`](../plan-spec.md)
§"Cross-step invariants" (the Save-previews/Confirm-transacts model — unchanged; only the
validate source moves).

**UX.** N/A directly — a JSON API; the `{field_delta, valid, errors}` shape is unchanged. The
maintainer's Save-shows-the-diff experience is unchanged; the validate is now DB-sourced.

**Disassembler-test / author-burden.** N/A — re-points a validate call; no author-facing input.
