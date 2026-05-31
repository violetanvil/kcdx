# Step 3 — hand-author the existing values into the new columns (user-verified)

**What.** Fill the new explicit per-kind columns (added in Step 2) with the real
values for every existing row that needs one — by HAND, reading the source
`notes`, transcribing the value, and having the user verify. No tool, no regex,
not even a transient migration script (decision 3). This is a seed data change.

**Scope (commit-grain).**
- For ids 19–24 (`vtable_index`), read each row's `notes` in
  `data/seeds/address_names_seed.csv`, extract the slot int by hand, and author
  it into the new `vtable_index` column on the matching
  `data/seeds/address_versions_seed.csv` row. Values from the audit (confirm
  against notes at author time): id 19 → 4, id 20 → 13, id 21 → 7, id 22 → 16,
  id 23 → 12, id 24 → 13.
- No other kind needs a hand-authored av-row datum (the Phase-1 fork resolved
  that data_slot's offset already lives in `survival_rule` and callsites need no
  apply-offset — see `../context.md` resolved column plan). The `vtable_index`
  column is the only authoring this step does.
- **User verification gate:** present the authored values (the 6 slots + any
  others) for the user to confirm against the notes BEFORE the commit. The user
  experiencing/confirming the values is the acceptance — the agent does not
  self-certify hand-transcribed data.
- These are UPDATEs to existing approved rows (no new entity/version rows), so
  AP18's new-row-approval gate does NOT apply (`../context.md` invariants).
- No code change in this step — seed CSV authoring only. Readers still ignore the
  columns until Step 4, so oracles stay green (values present, unread).

**Test bar.** The four mini-dump apply oracles + full-dump `test_rebuild_oracle`
all PASS unchanged (the authored values are not yet read, so no row value changes
— this proves the authoring did not corrupt the CSV shape). The substantive
acceptance is the USER's confirmation of the transcribed values against the
notes.

**Dependencies.** Step 2 (the columns must exist to author into).

**Reference.** [`../context.md`](../context.md) decision 3 + finding F1. Source of
the slot ints: the `notes` column of ids 19–24 in
`data/seeds/address_names_seed.csv`.
