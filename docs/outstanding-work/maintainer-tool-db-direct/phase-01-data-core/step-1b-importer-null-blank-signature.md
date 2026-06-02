# Step 1b — importer: NULL signature on curated function-kind rows with a blank seed cell

**What.** Fix `data/refdata-extractor/python/seeds_shared/row_builder.py`
(`build_curated_row`): a curated `function_no_sig` / `function_variadic` row whose seed
`signature` cell is BLANK must keep the DB `signature` NULL — the importer must NOT
promote the bulk-dump `abi_walker` floor signature (`? (...)`) onto it. Currently the
promote path keeps the bulk floor when the seed authored none, so the DB carries a value
the seed left empty, breaking DB↔CSV information-equivalence (design §4) and the
byte-identity round-trip (D2) for the affected rows. Surfaced while building the exporter
(step 1).

**Scope.** The promote-tail signature logic in `row_builder.build_curated_row` (the
`signature only overwritten when the seed supplied one` branch — invert it for blank
authored signatures: blank seed cell → NULL, not floor). NULL the floor ONLY on a curated
row (`kcdx_id IS NOT NULL`) whose seed cell was blank; a real authored signature is
preserved; the bulk-dump (uncurated) rows keep their floor unchanged. No exporter change,
no GUI.

**Test bar.** `data/refdata-extractor/tests/test_importer_blank_signature.py` (new), OR an
assertion added to the existing apply/rebuild oracle tree. On the mini-dump fixture: after
a `--rebuild`, every curated `function_no_sig` / `function_variadic` row whose seed
`signature` cell is blank has DB `signature` NULL (not `? (...)`); a curated row WITH an
authored signature keeps it; an uncurated bulk row keeps its floor. The round-trip then
holds on those rows (re-import → export reproduces the blank). Run: `python
tests/test_importer_blank_signature.py` from `data/refdata-extractor/`.

**Test bar runnable now?** Yes — the rebuild + the mini-dump fixture + the seed CSVs all
exist; the oracle runs the moment this lands.

**Dependencies.** None — it fixes pre-existing import code. Ordered BEFORE step 1 commits
(step 1's exporter byte-identity test on `address_versions_seed.csv` cannot pass until
this lands) and before step 2's round-trip oracle.

**Touches existing code.** YES — `row_builder.build_curated_row` is in HEAD and is called
by both the `--rebuild` (curated promote/mint) and the incremental `apply` paths. Grep
every caller of the promote-tail signature logic before changing; verify both the rebuild
and apply paths still produce correct rows for: a curated authored-signature row, a curated
blank-signature function-kind row, and an uncurated bulk row.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§4 (the information-equivalence consequence — "the importer must persist NULL for any
authored field the seed left blank") + the changelog entry 2026-06-02 (the decision +
the why-safe). `data/seeds/policy.md` §"Address kinds" (`function_no_sig` = a real function
whose signature is not yet known — explicitly the no-signature kind) + §"Required columns"
(`signature` is OPTIONAL / NULL-valid on `address_versions`).

**Why safe (the load-bearing check).** The survival/fingerprint path keys
`function_no_sig` / `function_variadic` on the body-hash (`survival_builder.py`:
`"function_no_sig": "function_hash"`), NOT on the signature — so NULLing the floor
signature on a curated row changes no survival behavior. Verified against
`survival_builder.py` during the audit; re-verify in the impact-analysis.

**Disassembler-test / author-burden.** N/A — an importer correctness fix; no author-facing
game-function input.
