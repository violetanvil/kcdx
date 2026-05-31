# Step 5.2 — fill the seed survival fields with verified per-kind data

**What.** The DATA half of the per-kind survival fingerprint. A
research-disassembly pass that FILLS the per-kind survival columns 5.1 adds to
`address_versions_seed.csv` with values verified against the binary — the AOB
pattern+mask per callsite, the slot count per vtable_base, the derivation rule +
`derives_from` per data_slot/instruction_anchor, the anchor string per
string_anchor. Authored against `WHGame.dll`, NOT parsed from the `notes` prose
(a probe confirmed prose extraction is unreliable — AOB hex is present for some
callsite rows but not all; slot counts for some vtable_bases but not all).

Runs in PARALLEL with 5.1 (different chat). 5.1 owns the column SHAPE; 5.2 owns
the VALUES. 5.2 writes into the columns 5.1 defines, so 5.1's column format
(see `data/seeds/policy.md` after 5.1 lands) is 5.2's authoring spec.

**Scope.** For each non-function curated entity with a survival-relevant kind,
fill its survival columns in `address_versions_seed.csv`:
- **callsite** (ids 5, 6, 7, 8): the AOB pattern + mask (bytes + `?` wildcards).
  Some are in `notes` already (id 7, 8) — RE-VERIFY against the binary, do not
  trust the prose; ids 5, 6 need the AOB derived.
- **instruction_anchor** (id 9): the expected instruction-shape pattern at the
  resolved site + `derives_from` its string anchor (id 12).
- **string_anchor** (id 12): the literal string bytes (+ unique-xref assertion
  if it holds).
- **data_slot** (ids 10, 11, 132): the derivation rule (the disp-follow / fixed
  offset) + `derives_from` the anchor it derives from.
- **vtable_base** (ids 138, 139, 140, and CScriptSystem_vtable): the slot count,
  verified against the binary.
- **vtable_index** (ids 19–24): DEFERRED — leave empty (gated on the
  runtime-vtable verification path).

Each fill is a seed UPDATE to an existing approved row (not a new-row addition),
so policy.md's new-row-approval gate (AP18) does not apply; the survival columns
are new evidence on an already-approved entity.

**Method.** `/research-disassembly` per
[reverse-engineering.md](../../../../.claude/rules/reverse-engineering.md): reuse
ladder first (existing `notes` prose + `_research/` dumps as a STARTING point to
re-verify, predecessor sigs, then Ghidra). Every value lands with its evidence;
a value that cannot be verified is left EMPTY (5.1's machinery emits an
empty-payload survival row for it — never a guess).

**Test bar.** Not a code test — a data deliverable. The verification is: 5.1's
oracle, run after 5.2's values merge, shows the `survival` rows carrying the
filled per-kind data (no longer empty for the filled kinds), and `apply`==
`rebuild` still holds. Each filled value is independently RE-verified (the
research-disassembly evidence is the proof, per AP1/AP2/AP3).

**Dependencies.** 5.1's seed column shape (the format 5.2 writes into) must land
or be agreed first. Otherwise independent — the RE work proceeds in parallel.

**Reference.**
- [`data/maintainer-tool/fingerprint-per-kind.md`](../../../../data/maintainer-tool/fingerprint-per-kind.md)
  — the per-kind datum spec (what each value MEANS).
- [`step-5.1-survival-machinery.md`](step-5.1-survival-machinery.md) — the
  parallel machinery sub-step (the column shape).
- `data/seeds/policy.md` (after 5.1) — the survival-column authoring format.
