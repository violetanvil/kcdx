# TD-0009 — the engine↔browser survival-check agreement is scoped to the 4 algorithm-identical kinds; 3 superset kinds are unpinned

**Reported:** 2026-06-08
**Status:** Open
**Closure trigger (named):** reconcile the engine-vs-browser asymmetry for `string_anchor` /
`instruction_anchor` / `data_slot` — EITHER (a) teach the JS browser checker + the Python
reference checker the fuller checks the engine runs (the `expect_unique` `.text` LEA-xref for
string_anchor; the full string→LEA resolver chain for instruction_anchor; the lands-in-`.data` +
anchor-DAG threading for data_slot), so all 9 kinds pin engine==browser literally; OR (b) amend
D27 + `cross_impl_fixture.py` to declare the agreement **per-kind-scoped** (annotate each kind with
which implementations agree on it: all-3 for the 4 identical kinds, JS↔Python-only for the 3
subset kinds where the engine is a documented superset) and extend the cap-85 pin to assert
agreement only where the implementations claim it. Either path closes this; the user picks at
closure (`design-authority.md`).

## What it is

Phase 3 step 3.4 (the JS↔C++ cross-implementation survival-agreement pin, D27) landed a HARD,
falsifiable conformance pin (`cap-85-survival-agreement` — `src/survival_agreement_selftest.cpp`)
for the **4 algorithm-identical kinds**: **function** (incl. the cross-impl BLAKE3 hash-agreement —
the engine's vendored BLAKE3 must reproduce the fixture's recorded `content_hash`), **callsite**
(AOB hit-count → Unchanged/Changed/Ambiguous), **vtable_base** (N-qword table-shape), and
**vtable_index** (CannotCheck). For these 4, the C++ engine, the JS browser checker, and the Python
reference checker all reproduce the SAME pinned verdict on the SAME on-disk bytes — the D27
conformance gate.

The **3 remaining in-scope kinds are NOT pinned engine↔browser**, because the engine's 3.2 static
checks are a **superset** of what the browser + Python reference compute — confirmed **by design,
not an engine bug**:

- **`string_anchor`** — the engine ALSO runs the `expect_unique` `.text` LEA-xref assertion; the
  agreed model (the JS browser == the Python reference == the fixture's pinned verdicts) is **pure
  `.rdata` presence**. The Python reference checker (`survival_checker.py`) explicitly documents
  this: "*The optional single-xref assert (survival_expect_unique) needs a `.text` LEA-xref decode —
  a DOCUMENTED LIMITATION of this static presence check (fingerprint-per-kind.md allows the
  `.text`-LEA-xref to be deferred to the decoder; the core presence check is the bar)... the xref
  assertion is not run here.*" Over the fixture's `.rdata`-only slice the engine returns **Ambiguous**
  (0 xrefs) vs the pinned **Unchanged**.
- **`instruction_anchor`** — the engine re-runs the FULL string→LEA resolver chain (needs a `.rdata`
  anchor string + a unique LEA in the PE); the agreed model is the forward-shape + disp32-follow
  primitive (the fixture datum carries no anchor string). Different inputs, different verdict.
- **`data_slot`** — the engine checks "the derivation lands in `.data`" + requires `CheckOrdered`
  anchor-RVA threading; the agreed model checks "the disp32-follow reaches `expected_slot_rva`". A
  single-row dispatch returns `anchor_unresolved`.

`fingerprint-per-kind.md` is the authority that makes the engine's richer checks the FULL model
(string_anchor "optionally with `expect_unique_xref` to assert the single-xref property"; the
instruction_anchor "re-run the resolver chain"); the browser/Python implements a documented SUBSET.
So the engine is CORRECT; the asymmetry is the engine computing more than the agreed-cross-impl
model, not a divergence in shared logic.

## Why deferred (not fixed in 3.4)

D27's text frames the agreement as "all 9 kinds agree on the same DLL bytes" — but the engine
deliberately computes MORE than the browser for these 3 kinds, so a literal engine==browser pin on
all 9 is impossible without one side changing. Reconciling it is one of two larger moves: an
engine-CHECK change (re-opening 3.2's string_anchor/instruction_anchor/data_slot logic — and
arguably weakening the engine's correctness, since it'd stop running the xref/chain checks
fingerprint-per-kind.md calls the full model), OR a fixture-model + D27 amendment (the per-kind
agreement-scope annotation). Both exceed 3.4's "the agreement assertion only" scope. The user chose
(2026-06-08) to land the HARD pin for the 4 identical kinds now and file this reconciliation, rather
than mask the asymmetry (adjusting the fixture to match the engine, OR weakening the assertion,
would be the AP15 gaming the conformance test exists to prevent).

The 3 superset kinds' fixture rows remain pinned on the **JS↔Python axis** (the Phase-2
`crossImplAgreement.test.ts` — green), and the engine's checks for them are exercised by
`cap-84-survival-dispatch` (the 3.2 self-test) against real curated rows. What is unpinned is
specifically the **engine↔browser** agreement for these 3 kinds.

## Closure (when the trigger fires)

1. Pick path (a) reconcile the checks (teach the browser/Python the fuller model) OR (b) formalize
   per-kind agreement scope (annotate the fixture + amend D27) — the user's call.
2. Extend `cap-85-survival-agreement` + `crossImplAgreement.test.ts` to pin the 3 kinds under the
   chosen model (all-3 agreement for (a); per-kind-scoped for (b)).
3. Update `data/maintainer-tool/design.md` D27 to match (the "all 9 agree" framing → the accurate
   scope), and the `cross_impl_fixture.py` header.
4. Confirm the extended pin green (the C++ cap + the JS test + the Python round-trip), close this
   entry (move to `closed/`, reindex).

## Source references

- The pin landed: `src/survival_agreement_selftest.cpp` + `src/survival_agreement_fixture.h`
  (generated) + `data/refdata-extractor/tests/cross_impl_fixture.json` (the cross-language contract).
- The source-of-truth fixture: `data/refdata-extractor/python/seeds_shared/cross_impl_fixture.py`.
- The documented-subset reference checker: `data/refdata-extractor/python/seeds_shared/survival_checker.py`
  (the `string_anchor` xref "DOCUMENTED LIMITATION" note).
- The full-model authority: `data/maintainer-tool/fingerprint-per-kind.md` §string_anchor /
  §instruction_anchor / §data_slot.
- The agreement contract: `data/maintainer-tool/design.md` D27.
