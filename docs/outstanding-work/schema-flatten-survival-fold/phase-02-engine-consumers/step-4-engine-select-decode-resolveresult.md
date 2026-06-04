# Step 4 — engine SELECT + decode + ResolveResult fields

**What.** The engine reads the folded columns off `address_versions`: extend the
`address_versions` SELECT + `DecodeVersionRow` (`src/refdb.cpp`) to decode `aob`/`anchor_string`/
`rule`/`slot_count`/`expect_unique`/`derives_from`, and add the corresponding `ResolveResult`
fields (`src/refdb.h`, append-only per the struct's convention). The engine's survival pass (when
it lands / where it reads survival data) now reads the av columns, NOT a `survival` sibling. This
is where the §11.3 comprehensiveness contract is satisfied for the folded columns: every folded
column the engine consumes gains a `ResolveResult` field; a field with no backing column is a
hole (caught here, at decode/compile).

**Scope.** `src/refdb.cpp` — the `address_versions` SELECT column list + `DecodeVersionRow` gain
the folded columns (with `has_*` flags where a 0/empty is a real value vs absent, per the
struct's existing `has_offset`/`has_vtable_slot` convention). `src/refdb.h` — `ResolveResult` (and
`CuratedEntry`/`VersionRow` as needed) gain the folded fields at the END (append-only, AP11). The
`CuratedEntry`→`ResolveResult` copy carries them. NO survival-table read anywhere in the engine
(if the engine never read the sibling — confirm — this is purely additive decode of the new av
columns). Build-gated. One commit.

**Test bar.** `pwsh ./build.ps1` exit 0 + the three artifacts (the manager runs it). A data-core
/ engine-adjacent assertion that the folded columns decode into `ResolveResult` (the same
column→field back-map the existing decode test covers — extend it for the folded columns). The
comprehensiveness check: every folded `ResolveResult` field has a backing column + vice versa
(the §11.3 invariant, asserted as the back-map test). Build-green is necessary, not sufficient —
the whole-feature checkpoint's game launch confirms no resolve-path regression (the fold is
data-shape-preserving: the av columns carry the same facts).

**Dependencies.** Step 1 (the columns exist), step 2 (populated), step 3 (round-trip — so a
rebuilt DB the engine opens carries them). The engine reads a built DB; the folded columns must
be populated + round-tripping before the engine decode is meaningful.

**Reference.** [`../plan-spec.md`](../plan-spec.md) §"Cross-step invariants" (the ResolveResult
contract wired at the engine step).

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§11.3 (the comprehensiveness contract — the schema is the back-projection of ResolveResult) +
[`src/refdb.h`](../../../../src/refdb.h) (`ResolveResult` — the struct the fields append to,
append-only per AP11).

**Disassembler-test / author-burden.** N/A — the engine decodes already-resolved DB columns; no
new game-function input. (The folded columns may HOLD resolve facts — aob patterns, slot counts —
but they are authored in the DB, resolved by name at runtime, never hand-written in engine source:
`no-hardcoded-addresses.md`.)
