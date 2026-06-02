# Step 5 — db_editor.py: validated atomic lifecycle UPDATE (supersede / deprecate)

**What.** Extend `db_editor.py` with the **lifecycle UPDATE** shape — the entity-level
supersede (Job 4 / US-8) and deprecate (Job 5 / US-8) writes on an `address_names`
row. Supersede sets `superseded_by` + `superseded_at_version` together; deprecate sets
`is_deprecated` + `deprecated_at_version` together, with `deprecation_replacement`
allowed only when deprecated. The shared validator enforces pair-integrity
(both-or-neither), the no-self-supersede rule, supersession ACYCLICITY (no cycle in
the supersede graph), and the `deprecation_replacement`-requires-deprecated rule
(`policy.md`) BEFORE any write; a violation aborts with NO write. One atomic
transaction. These are UPDATEs to an existing approved entity — NOT AP18-gated.

**Scope.** The lifecycle UPDATE entry point on the existing `db_editor.py`. Reuses
`validators.py` (the pair-integrity + acyclicity + replacement rules already live
there — R3). No GUI, no Qt. The GUI lifecycle form is step 15; this is its DB-write
unit.

**Test bar.** `data/refdata-extractor/tests/test_db_editor_lifecycle.py` (new). On the
mini-dump fixture: a valid supersede sets both fields atomically; a valid deprecate
sets both fields (+ optional replacement) atomically; a partial pair aborts (NO
write); a self-supersede aborts; a supersede that would create a cycle aborts; a
`deprecation_replacement` set without `is_deprecated` aborts. Runs headless.

**Test bar runnable now?** Yes — the lifecycle oracle runs when this step lands (the
validator's pair-integrity + acyclicity rules + the fixture exist).

**Dependencies.** Step 3 (the `db_editor.py` module + its validate-then-write
structure). The shared `validators.py` (supersession/deprecation rules exist).
Sequenced after step 3 (independent of step 4's INSERT; ordered here to group the
`db_editor` shapes).

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-8 (supersede/deprecate pair-integrity). `data/seeds/policy.md`
§"Supersession" (pair integrity, no self-supersede, acyclicity) + §"Deprecation"
(pair integrity, replacement-requires-deprecated).

**Disassembler-test / author-burden.** N/A — supersede/deprecate edits entity-level
flags + a successor/replacement NAME (resolved from the existing entity set, a
dropdown — step 15); no game-function address/ABI is authored.
