# Step 1b — data-core tag seam (optional `version=` on the write path)

**What.** Add an OPTIONAL pre-resolved-version parameter to the data-core's write
path so a caller that already knows the game version (the web backend — the browser
resolved it, D15) can supply it directly, instead of being forced to hand the
data-core a DLL to read the version out of. The web app has **no DLL server-side**
(D14/D15/D18); the landed write functions accept only `dll_path` and call
`resolve_version(dll_path)` themselves. This step gives them a second, additive way
in — a `version=(tag, ordinal)` keyword — WITHOUT changing the existing DLL path or
any landed caller. This is **A2** (settled 2026-06-02): additive + oracle-preserving,
NOT a `dll_path`→`version` replacement.

**Scope (the exact change set — funnels through two functions + five signatures).**
- `import_to_sqlite.apply_seeds(out_dir, dll_path, *, version=None, log=None)` — the
  ONE consumption site (`import_to_sqlite.py:1740`). When `version is not None`, use
  `tag, ordinal = version` and SKIP `resolve_version(dll_path)`; otherwise the body is
  byte-identical to today (`tag, ordinal = resolve_version(dll_path)`). The
  `tag != GAME_VERSION_TAG` refusal gate, the validate→open→apply spine, and the
  returned dict are UNCHANGED. Exactly one caller may pass `dll_path=None` when it
  passes `version=`; assert that exactly one of the two is supplied (a caller passing
  neither, or both-and-they-disagree, is a programming error — raise a clear
  `ValueError`, not a silent pick).
- `seeds_shared/db_editor._drive_apply_over_prospective_seed(out_dir, dll_path,
  prospective_seed_dir, *, version=None, log=None)` — the single chokepoint all five
  public write functions funnel through (`db_editor.py:158/177`). Thread `version=`
  straight into `imp.apply_seeds(...)`. No other change (the seed-path repointing
  convention is untouched).
- The five public write functions each gain `version=None` (keyword-only) and pass it
  to `_drive_apply_over_prospective_seed`: `update_version_row`, `create_version`,
  `create_entity`, `supersede_entity`, `deprecate_entity` (and the private
  `_drive_names_lifecycle_edit` that supersede/deprecate share threads it too).
- Docstrings: each touched function's `version` param documented (one line: "a
  pre-resolved (tag, ordinal); when given, the DLL is not read — the caller already
  resolved the version, e.g. the web backend per D15"). The `dll_path` lines stay.

**Out of scope.** No backend wiring (the adapter calls this in step 4, the save API).
No removal/demotion of `dll_path`. No new validation/version logic — `version` is
trusted as the resolved pair exactly as `resolve_version` would have returned it (the
backend's adapter already validated the tag against the known set in step 1).

**Test bar.** Same change, runnable now (the data-core + its oracle tree exist):
1. **The new seam, both directions, in a new `tests/test_apply_version_seam.py`:**
   - `apply_seeds(out_dir, dll_path=None, version=(GAME_VERSION_TAG, ordinal))` over
     the mini-dump fixture produces the **byte-identical** DB to
     `apply_seeds(out_dir, dll_path)` (the DLL path) — the seam's whole point: the two
     entry routes converge on the same write. Assert DB-level identity (the same
     assertion style the existing apply oracles use).
   - `version=` with a tag `!= GAME_VERSION_TAG` raises `VersionRefusal` (the gate
     still fires on the supplied tag, not only the DLL-read one).
   - neither `dll_path` nor `version` supplied → `ValueError`; both supplied → `ValueError`.
   - one `db_editor` write function (e.g. `update_version_row`) driven with `version=`
     and `dll_path=None` lands the same row as the `dll_path` form.
2. **The landed oracles stay GREEN (the non-negotiable invariant — byte-identity of the
   existing DLL path held):** re-run and show green —
   `python -m pytest data/refdata-extractor/tests/ -q`. The load-bearing oracles:
   `test_rebuild_oracle.py` (apply==rebuild), `test_apply_reverify.py`,
   `test_apply_add_entity.py`, `test_apply_deprecate_supersede.py`,
   `test_db_editor_update.py`, `test_db_editor_insert.py`,
   `test_db_editor_lifecycle.py`, `test_version_resolver.py`. Any pre-existing red
   (TD-0004's stale rebuild-oracle baseline) is recorded as such and NOT attributed to
   this change — the gate is "no oracle goes red that was green before 1b," verified by
   running the suite on HEAD first if any ambiguity.
   - **Run the data-core suite on HEAD (before the 1b edit) and capture the
     green/red baseline FIRST**, so a pre-existing red (TD-0004) is provably not 1b's.
     This is the results-driven floor: observe ground truth (which oracles are green
     now) before the change, so the after-state is attributable.

**Dependencies.** Step 1 (the backend skeleton + its adapter that will pass `version=`
in step 4) — landed (`c0b270c`). The data-core (Phase 1) — landed. This step is the
PRODUCER; the save API (step 4) is the CONSUMER, ordered after it
(`.claude/rules/incremental-delivery.md`: the tag seam lands before the save endpoint
that calls it).

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§5 ("a thin adapter maps a chosen version tag → the data-core's params (no DLL
server-side)") + §10 **D13** (the data-core is the single delivery-agnostic gate; zero
rule logic in the tool) + **D15** (the version resolves client-side; only the tag
reaches the server). [`../plan-spec.md`](../plan-spec.md) §"Cross-step invariants" (the
A2 record — additive + oracle-preserving). The seam shape (A2 over full-A / B / C) was
settled by the user 2026-06-02 after architect-review; the record lives in plan-spec +
this doc (decision-capture: it implements the already-settled D13/D15 contract, not a
new design fork).

**UX.** N/A — a data-core library seam; no user-facing surface.

**Disassembler-test / author-burden.** N/A — internal Python seam; no author-facing
game-function input.
