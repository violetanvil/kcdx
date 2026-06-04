# Step 5 — read seam + backend surface the folded columns

**What.** The data-core read seam (`read_api.read_version_rows`) surfaces the folded
`address_versions` columns (per the curated display-column set the read contract defines), and the
maintainer-tool backend passes them through. This migrates the LAST consumer (the maintainer
tool's read path) off any survival-table read onto the av columns — after this step, no consumer
reads the `survival` sibling, so Phase 3's deletion breaks nothing.

**Scope.** `seeds_shared/read_api.py` — `read_version_rows` includes the folded columns in its
returned display set (decide per the read contract: the survival-only columns are
maintainer-display columns like the other authored cells — they belong in the curated display set,
distinct from internal/DEV-only columns; follow the §curated-display-column-set convention the
content_hash curation established). The maintainer-tool backend (`routes_read.py`) passes them
through unchanged (it serializes whatever `read_version_rows` returns). One commit.

**Test bar.** `tests/test_read_api.py` — the `read_version_rows` case asserts the folded columns
are in the returned display set (and the internal/DEV-only exclusions are unchanged). The backend
read-endpoint test (`test_read_endpoints.py`) green — the endpoint passes the folded columns
through. Runnable now — the columns exist + are populated (Phase 1).

**Dependencies.** Step 1 (columns exist), step 2 (populated). Independent of step 4 (the read seam
is Python/data-core; the engine decode is C++ — they consume the same columns but neither depends
on the other; ordered after step 4 only for phase grouping, not a hard dependency).

**Reference.** [`../plan-spec.md`](../plan-spec.md) §"Cross-step invariants" (D13/law 6 — the read
seam's display-set decision is the data-core's; the backend only serializes).

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§5 (the thin-shell backend — it calls the data-core read seam + serializes) + §11.1 (the folded
columns are first-class display columns).

**Disassembler-test / author-burden.** N/A — a read-for-display surface; no game-function input.
