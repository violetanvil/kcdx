# Survival fingerprint per address-kind — design

**Status:** design only. No engine implementation here. This doc defines WHAT
the survival datum is for each of the 9 address kinds, so the seed/DB carries
the *correct* per-kind verification input when the survival layer is wired to
production later. Today only the function kinds carry a fingerprint
(`content_hash` + `length`); every other kind carries NULL and is skipped
(`survival.cpp` returns `not_applicable`). That skip is a coverage gap: a
callsite / anchor / slot can go stale across a game update and nothing catches
it. This design closes that gap by giving every kind a survival check whose
*form matches how that kind resolves*.

## The principle

A survival fingerprint answers ONE question: **"at the current game version, is
the thing this row names still the thing it was verified to be?"** The check
must hash/compare the bytes whose stability actually means "still valid" — which
is **the bytes the kind resolves against**, not an arbitrary span.

Today's function fingerprint (`BLAKE3([rva, rva+length))`) is correct *because a
function resolves by a stored RVA and its identity IS its body bytes*. Extending
survival to every kind is NOT "hash the same span for all kinds" — it is "store,
per kind, the datum that kind's re-derivation already depends on, and verify
THAT."

**Invariant: the survival datum mirrors the resolution mechanism.** A kind
resolved by an AOB pattern is survival-checked by re-matching that AOB. A kind
resolved by following a RIP-relative displacement is survival-checked by
re-following it. The fingerprint is a stored snapshot of the resolution's
load-bearing fact.

## The current schema's fingerprint columns

`address_versions` carries, today, two function-shaped survival columns:

- `content_hash` (BLOB, 32-byte BLAKE3) — the hash of the verified span.
- `length` (INTEGER) — the span length the hash was computed over.

This design generalizes "the survival datum" beyond those two. See §"Schema
impact" for how the per-kind data is stored.

## Per-kind fingerprint table

For each kind: what it resolves against, what "still valid" means, the survival
datum to store, and the check at survival time.

### function / function_no_sig / function_variadic — body hash (EXISTS TODAY)

- **Resolves by:** stored RVA → the function entry; identity = the body bytes.
- **"Still valid" =** the bytes at `[rva, rva+length)` are unchanged.
- **Survival datum:** `content_hash` = BLAKE3 of the body span; `length` = span.
- **Check:** re-hash `[rva, rva+length)` on the on-disk DLL, compare. (current
  `survival.cpp`.)
- **Source of the datum:** the dump (`functions/` table) — already populated at
  baseline by the bulk row promote. Maintainer never hand-authors it.

### callsite — AOB re-match at the site

- **Resolves by:** an AOB byte-pattern at a mid-function site (e.g. id 7:
  `48 8B 41 08 … 3C 02`, 26 bytes; the RVA is the pattern-hit position, with a
  consumer offset). NOT a function entry.
- **"Still valid" =** the AOB pattern still occurs, and (ideally) still occurs
  UNIQUELY in `.text`, at the stored RVA (or wherever it relocated to).
- **Survival datum:** the **AOB pattern bytes + mask** (the exact pattern the
  resolver matches, including any `?` wildcards). This is already conceptually
  in the notes as the hex pattern; the survival design PROMOTES it from prose to
  a stored, machine-checkable datum.
- **Check:** scan `.text` of the on-disk DLL for the stored AOB. Outcomes:
  unique hit → Unchanged (relocate the RVA to the hit if it moved); zero hits →
  Changed (the site is gone — warn loudly); multiple hits → Ambiguous (the
  pattern is no longer a unique locator — warn; the maintainer must extend the
  pattern, exactly what id 6 "context extension" exists for).
- **Why NOT the function-body hash:** hashing the enclosing function flags
  Changed on any edit anywhere in that function, even edits nowhere near the
  callsite — a false signal. The AOB is the precise survival unit.

### instruction_anchor — resolver-chain re-derivation

- **Resolves by:** a multi-step resolver, NOT a single stored fact (e.g. id 9:
  (1) find the `.rdata` string anchor, (2) scan `.text` for the LEA whose
  RIP-relative target == the string, (3) walk a fixed byte-shape backward to the
  MOV). The RVA is a *re-derived intermediate*, not a stored endpoint.
- **"Still valid" =** the resolver chain still completes and lands on an
  instruction of the expected shape.
- **Survival datum:** the **expected instruction-shape signature** at the
  resolved site (the opcode/operand-shape pattern the resolver's final step
  checks — e.g. the `mov rcx, [rip+disp]` encoding shape), stored as an AOB-style
  pattern. Plus the dependency on its anchor row (see "anchor dependency"
  below).
- **Check:** re-run the resolver chain; verify the final instruction matches the
  stored shape. Unchanged iff the chain completes and the shape matches.
- **Why NOT a fixed-RVA hash:** the RVA moves every build by design; the anchor
  exists precisely so the site is *re-found*, not stored. The survival unit is
  "does the re-derivation still work," i.e. the shape match.

### string_anchor — literal presence

- **Resolves by:** a `.rdata` string literal (id 12: `"exec autoexec.cfg"`),
  found by searching for the string. It is itself the seed anchor other rows
  re-derive from.
- **"Still valid" =** the literal still exists in `.rdata` (and, for its role as
  a unique anchor, still has exactly one `.text` LEA xref).
- **Survival datum:** the **string bytes** (the literal itself) — optionally
  with `expect_unique_xref = true` to assert the single-xref property the
  resolver relies on.
- **Check:** search `.rdata` of the on-disk DLL for the stored string. Present →
  Unchanged; absent → Changed (the anchor is gone — every row that re-derives
  from it is now unresolvable, warn loudly). If `expect_unique_xref`, also
  confirm exactly one `.text` LEA references it.
- **Why NOT a span hash at the stored RVA:** the string can relocate within
  `.rdata` across builds while remaining valid; presence-by-content is the
  stable check, the RVA is not.

### data_slot — structural, NOT a byte hash

- **Resolves by:** a `.data` pointer slot (id 10 `gEnv->pConsole`, id 11
  `gEnv`), reached by following a RIP-relative displacement from an instruction
  anchor (or a fixed offset from another slot, e.g. `gEnv = pConsole - 0xA8`).
- **"Still valid" =** the resolver still REACHES the slot and the slot still has
  the expected STRUCTURE — NOT that its bytes are constant. `.data` holds
  runtime/relocated pointer values; its bytes legitimately differ at rest vs
  runtime and across builds. **A byte hash of a data_slot is an anti-signal —
  it would report Changed constantly.**
- **Survival datum:** the **resolution descriptor** — the anchor row it derives
  from + the displacement/offset rule (e.g. "follow disp32 from
  instruction_anchor id 9" or "id 10 RVA − 0xA8"). The survival check verifies
  the *derivation*, not the slot's contents.
- **Check:** re-run the derivation; Unchanged iff it still lands in `.data` at a
  consistent offset relative to its anchor. There is NO content hash.
- **Note:** this is the clearest case that "fingerprint" ≠ "byte hash." The
  data_slot's survival is its derivation chain, full stop.

### vtable_base — table-shape check

- **Resolves by:** a stored RVA pointing at a vtable BASE in `.rdata` (id 1194
  `CScriptSystem_vtable`, "69 slots"; ImodVtable bases). It is the start of an
  array of code pointers, not a function and not an index.
- **"Still valid" =** the RVA still points at a pointer table of the expected
  shape (N slots, each a plausible `.text` code pointer).
- **Survival datum:** the **expected slot count** (N) + a structural assertion
  ("each of the first N qwords is a `.text`-range RVA after relocation"). NOT a
  hash of the table bytes — the pointers in it relocate every build.
- **Check:** at the stored RVA, read N qwords; Unchanged iff there are N and
  each resolves into `.text`. A shrunk/grown table or non-pointer contents →
  Changed.
- **Why NOT a byte hash:** the slot pointers are relocated addresses — different
  every build by construction; hashing them = Changed every build.

### vtable_index — slot-bound method identity (hardest; no RVA)

- **Resolves by:** an INTEGER slot index (ids 3000-3005: slots 4, 13, 7, 16,
  12, 13), NOT an RVA. `rva` is empty. Resolves at runtime to
  `vtable_base[index]`.
- **"Still valid" =** the method at `vtable_base[index]` is still the intended
  method (the engine reorders vtable slots across builds — the whole reason
  `AddCommand` is slot 33 not the canonical 32).
- **Survival datum:** this kind cannot be survival-checked statically from the
  on-disk DLL alone, because the "intended method" identity lives at
  `vtable_base[index]` and which function that is requires resolving the base +
  indexing it. The correct survival datum is a **reference to a vtable_base row +
  the index + the expected slot-target identity**, where the slot-target
  identity is the FUNCTION FINGERPRINT of the method the slot should point at
  (i.e. vtable_index survival = "resolve base, take slot N, hash that function's
  body, compare to the stored expected body hash"). So vtable_index's fingerprint
  is *derived* — it borrows the function-body-hash check, applied to the
  slot's current target.
- **Check:** resolve the vtable_base row → read slot `index` → that yields a
  `.text` RVA → hash that function's body → compare to the stored expected hash.
  Unchanged iff the slot still points at a body matching the verified method.
- **Status flag:** today these 6 rows are `status=unverified` and resolve to 0
  until a runtime vtable hook lands. The survival datum (base-ref + index +
  expected-target-body-hash) is the design target; it is only populatable once
  the slot target is verified, which is itself future work. Mark these
  **deferred within this design** — the datum SHAPE is defined, population waits
  on the runtime-vtable verification path.

## The anchor dependency (cross-row survival)

Several kinds re-derive THROUGH another row: `data_slot` follows an
`instruction_anchor`; `instruction_anchor` follows a `string_anchor`;
`vtable_index` indexes a `vtable_base`. The survival datum for a dependent kind
must record **which row it depends on**, so survival can check in dependency
order: if a `string_anchor` is Changed, every row that re-derives from it is
transitively suspect (a CannotCheck-with-reason, not a silent pass). This is a
DAG of survival dependencies the design must capture (a `derives_from` reference
column or equivalent).

## Why a single uniform hash cannot be the fingerprint

The tempting simplification is "per kind, fold all checks into ONE hash, store
that, and survival = recompute-and-compare." It fails because **a hash answers
only one question — 'are these bytes byte-identical?' — and most kinds' survival
question is not a byte-identity question.** Split the 7 kinds by what their
check actually IS:

- **Fixed-span, byte-identity kinds** (function, string_anchor, and the
  normalized form of vtable_base): the check reduces to "recompute over a known
  location, compare." A stored hash (or normalized digest) works.
- **Search / derivation kinds** (callsite, instruction_anchor, data_slot,
  vtable_index): the check must FIRST FIND the thing — an AOB scan of `.text`, a
  multi-step RIP-relative derivation, a base+index resolve — and the find is a
  *procedure run against the new binary*, not a comparison of stored bytes. Two
  reasons a hash can't serve here:
  1. **A hash can't be searched for.** To re-match a callsite the check needs the
     AOB pattern *in executable form* (bytes + mask to scan). A hash of the
     pattern only proves "the pattern I stored is the pattern I stored" — it
     can't be run against `.text`. So the pattern must be stored as a pattern;
     the hash would be redundant.
  2. **A hash can't encode a procedure.** A data_slot's survival is "follow
     disp32 from anchor X, subtract 0xA8, did we land in `.data`?" — a rule with
     inputs (the anchor ref, the offsets), not an output to compare. The
     destination bytes legitimately change, so there is nothing stable to hash.

So the survival datum for the search/derivation kinds is an **input to a
procedure**, not an **output to compare**. Those are different shapes of thing;
one column typed "hash" cannot hold both.

The right resolution keeps your single-fingerprint ergonomics without the false
premise that the datum is always a hash: **one survival datum per row, one check
entry point, kind-discriminated payload.** The check is uniform
(`SurvivalCheck(kind_form, payload, derives_from, dll)`); the payload's *shape*
is per-kind (a 32-byte hash for function; AOB+mask for callsite; a derivation
rule for data_slot; base-ref+index+target-hash for vtable_index). One
fingerprint per row, dispatched on kind inside the checker.

## Schema decision (design, not built) — a sibling `survival` table

**Decision: store the survival datum in a sibling `survival` table, 1:1 with
`address_versions`, with a kind-discriminated payload and a first-class
dependency FK.** Shape:

```
survival
  address_version_id   FK -> address_versions.id   (1:1; the entity this survives)
  kind_form            the survival shape (function_hash | aob | derivation
                       | table_shape | slot_target)
  derives_from         FK -> address_versions.id, nullable   (the DAG edge)
  payload              the kind-typed datum (AOB+mask | content_hash+length
                       | offset rule | slot count | base-ref+index+target-hash)
```

**Why this over the alternatives:**

- **Over per-kind columns** (a wide table with `aob_pattern` / `anchor_string` /
  `expect_slot_count` / … each NULL for most rows): the columns are
  mutually-exclusive by kind, so every row uses 1–2 of ~10 and the rest are dead
  NULLs the survival pass loads and ignores per row. Sparse-by-kind columns are
  the textbook signal that a sub-type table is wanted, and they invite the
  next maintainer to overload one column for two kinds — the exact kind-
  conflation this whole design exists to prevent.
- **Over a blob inside `address_versions`** (a `survival_datum` JSON column): the
  **dependency DAG** is the decisive factor. `data_slot` → `instruction_anchor`
  → `string_anchor`, and `vtable_index` → `vtable_base` (see §"The anchor
  dependency"). The survival check MUST run in dependency order (a dead
  `string_anchor` makes everything downstream transitively CannotCheck), which
  is a graph walk — and a graph walk needs `derives_from` as a queryable FK you
  can `JOIN`/`WHERE` on, not a value buried in opaque JSON. The moment one field
  of the blob must be queryable, the blob is the wrong container; here that
  field is load-bearing.
- **Hot/cold separation.** `address_versions` is read on every resolve
  (`LoadVersionRowsForEntity` + `PickBestVersionRow`, the hot path). Survival
  data is read only by the (not-yet-wired) survival pass — cold, future. A
  sibling table keeps the hot resolve row lean and joins survival data only when
  the pass runs.
- **It matches the schema's existing grain.** `statements`, `referenced_vars`,
  `call_edges` are already dev-only sibling tables keyed by `address_version_id`
  for exactly this reason — per-function data the resolve path doesn't need. The
  `survival` table is the same pattern.

**The one cost — a join at survival time — is acceptable** because survival runs
as a batch pass (once per launch, not per resolve) and refdb builds an in-memory
cache at `Open()` into which survival data loads the same way, so runtime is a
hash-map hit, not SQL. The join is a build-the-cache cost, amortized to nothing.

Most of the payload data ALREADY EXISTS as prose in the `notes` column (the AOB
hex, the resolver steps, the slot counts). This design's persistence task is to
PROMOTE those facts from prose into the typed `payload` — exactly the
structured-authoring the maintainer tool exists to drive (the maintainer asserts
the AOB / anchor / slot-count; the tool stores it as the survival datum, not
buried in notes).

## What this changes for the DB-updator (Phase 1) — nothing yet

This is a design artifact. The immediate Phase-1 decision is unchanged: today
non-function kinds carry NULL `content_hash`/`length` (the function-shaped
columns), and the rebuild's RVA-coincidence promote (which wrongly stamps the 2
function-entry-colliding non-function rows with a body hash) is the bug to fix —
because under THIS design those rows' survival datum is an AOB / structural
check, never a body hash. So the fix (non-function → no body hash) is correct
under both the current schema and this future design. The per-kind survival
DATA (AOB patterns, anchor refs, slot counts) is populated when the survival
layer is built and the schema decision above is made.

## Open decisions (for when this is built)

Schema shape and the DAG representation are DECIDED above (sibling `survival`
table; `derives_from` FK). What remains:

1. `payload` encoding — typed sub-columns within the `survival` table per
   `kind_form`, vs a small typed blob in the `payload` column. (The table + DAG
   decision stands either way; this is only how the per-kind datum is laid out
   inside the row.)
2. `vtable_index` survival datum population — gated on the runtime-vtable
   verification path that gives the slot a verified target (the datum SHAPE is
   defined; population waits).
3. callsite ambiguity posture — is "multiple AOB hits" a Changed (refuse) or a
   warn-and-pick? (UX: the maintainer extends the pattern, per id 6's existence.)
