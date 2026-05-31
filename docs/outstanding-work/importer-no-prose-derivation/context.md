# Context — importer: no prose-derivation, every field an authored column

Shared spec for the `importer-no-prose-derivation` plan. Every step doc leans on
this; steps cross-link here rather than restating.

## Goal

The seed CSV is the de-facto, versioned, single source of **every** field the
refdata importer needs. Eliminate **all** prose-derivation and inference from
`data/refdata-extractor/python/import_to_sqlite.py` + `seeds_shared/`: ZERO
regex, ZERO `notes`-parsing, ZERO heuristic inference for any value the importer
writes to a DB column. Each address-kind's needed datum is its **own explicit,
named, authored column** in the seed CSV. `notes` remains human commentary only —
never parsed.

## Settled design decisions (locked by the user — verbatim)

1. **Full audit + fix ALL.** Not just the one regex found — sweep the whole
   importer for every prose-derived / inferred value and fix every one. The
   Phase-1 audit must *prove* each value the importer produces comes from an
   authored column (or a legitimate non-prose source), so nothing is missed.
2. **Every item type gets its own column.** Each kind's datum is its own named
   seed column. Do NOT overload one shared `value` column for "slot int for
   vtable_index, offset for data_slot" — that sprawl (`value` + `offset` +
   `vtable_slot` all overlapping) is collapsed into explicit per-kind columns.
3. **Hand-author the 6 existing vtable_index slots** (ids 19–24). Read each
   row's `notes`, transcribe the slot int into the new column by hand, the user
   verifies the 6 values. NO regex, not even as a transient migration aid.

## Confirmed audit findings (entry state — Phase 1 re-proves + extends)

From the pre-plan audit of `import_to_sqlite.py`:

- **F1 — `kind_offset_and_slot()` regex-scrapes the vtable slot from `notes`.**
  Lines ~321–334. `re.search(r"index\s*=\s*(\d+)")` (+ a fallback
  `r"slot\s+(\d+)\s*\(0-indexed\)"`) pulls the slot int out of prose. LIVE on
  BOTH writers: rebuild `build_rows` (~line 564) and apply `_seed_action_rows`
  (~line 1238). The slot int currently exists ONLY in `notes` prose (e.g. id 20
  `"vtable index = 13 (0-indexed)"`); the CSV `value`/`vtable_slot` columns are
  empty for ids 19–24.
- **F2 — `offset` is hardwired NULL with no authored column.** Same function's
  docstring: "offset stays NULL for the unblock." For `data_slot` the consumer
  offset is a needed value with no authoring surface — it can never be set.
- **F3 — `infer_kind()` is dead prose-sniffer code.** Lines ~295–318. Sniffs
  `kind` from `name`/`notes` substrings. Replaced by the authored `kind` column
  (`authored_kind`); grep confirms ZERO live callers. Still present in the file →
  a loaded gun to delete.
- **F4 — column sprawl.** Schema carries three overlapping columns for the same
  conceptual datum: `value` ("slot int for vtable_index, offset for data_slot"),
  `offset` ("callsite consumer offset"), `vtable_slot` ("mirrors value for that
  kind"). `row_builder.build_curated_row` mirrors `vtable_slot` → `value`
  (~line 154). Decision 2 collapses this into explicit per-kind columns.

## Out of scope — legitimate non-prose regex (do NOT touch)

These are NOT prose-derivation; leave them:

- `seeds_shared/version_resolver.py` `_VERSION_RE` and `import_to_sqlite.py`
  `read_game_version` regexes — parse the GAME's own version string from
  `whdlversions.json` / the DLL `.rdata`. Authoritative external source, not seed
  prose.
- `seeds_shared/validators.py` `_VERIFIED_DATE_RE`, `_AOB_TOKEN_RE` — FORMAT
  validators on already-authored columns (date shape, AOB hex-token shape). They
  validate a column's value; they do not reconstruct a value from prose.

## Cross-step invariants

- **apply == rebuild, byte-identical.** Every change preserves it. The gate per
  phase: the mini-dump oracles (`test_apply_add_entity`, `test_apply_reverify`,
  `test_survival_table`, `test_apply_deprecate_supersede`) + the full-dump
  `test_rebuild_oracle` all green; `test_rebuild_oracle` byte-identical to the
  recorded baseline.
- **`oracle_baseline.json` re-capture is deliberate.** A change to authored
  columns / row shape re-captures the baseline ON PURPOSE, with the reason noted
  in the capture (the schema-column change is the documented intended diff).
- **Seed edits are UPDATEs to existing rows, not new rows.** Adding a column +
  authoring values into existing ids 19–24 (and any data_slot offsets) is an
  UPDATE — AP18's new-row-approval gate does NOT apply (see
  `.claude/rules/address-library.md` + `data/seeds/policy.md`).
- **One writer, two callers.** `build_rows` (rebuild) and `_seed_action_rows` /
  `_apply_one_db` (apply) must read the SAME authored columns the SAME way —
  the shared `seeds_shared/row_builder.build_curated_row` is the single column
  assembler; both paths feed it from the authored columns, never from prose.

## Phase-1 audit result (landed — the value-provenance proof)

Full end-to-end audit of the importer's three written tables. Every column
accounted for; the defect surface is entirely in `address_versions`
(`value` / `offset` / `vtable_slot`). `address_names` and `survival` are clean.

**Confirmed:** F1 (vtable slot regex on `notes`, live at `import_to_sqlite.py:564`
+ `:1238` via `kind_offset_and_slot`), F2 (`offset` hardwired NULL — a needed
value with no authored column), F3 (dead `infer_kind`, zero callers), F4
(`value`/`offset`/`vtable_slot` sprawl). Two additional findings:

- **A1 — `notes_by_kid` is the prose plumbing F1 rides on.** Built at
  `:518` (rebuild) + `:1187` (apply); its ONLY remaining consumer is the F1
  regex (kind is now authored). Removing F1 makes this plumbing dead — remove it
  too.
- **A2 — `value`'s "offset for data_slot" meaning is fictional in code.** No path
  ever writes `value` from that meaning; `value` is a pure mirror of
  `vtable_slot` (`row_builder.py:154`). The schema comment's dual purpose is not
  real → collapsing `value` away is provably safe.

### Resolved column plan (CORRECTED — comprehensive, keep + author, never delete)

**A delete-the-columns lean was WRONG and is superseded.** New evidence: the C++
engine's runtime SELECT (`src/refdb.cpp:543`) reads `value`, `offset`, AND
`vtable_slot` into `NameResolution` — they are plumbed for the imminent
**hardcoded-address migration** (see `docs/outstanding-work/hardcoded-address-audit.md`:
44 findings, incl. 5 `vtable_slot`, 1 `struct_offset`/vtable-byte-offset, 17
not-yet-in-DB). Those fields are NOT dead — they are load-bearing for what lands
next. `value` looking like a "dead mirror" (the superseded A2) was only true
because nothing authored it YET; post-migration each is a real authored datum.

**The defect was never the columns — only the prose REGEX that fed one of them.**

**User decisions (locked):**
1. **Comprehensive** — establish an explicit authored, validated seed column for
   EVERY address-kind's datum the schema needs. Keep `value`/`offset`/`vtable_slot`
   + the engine reads; ADD `struct_offset` (the audit's vtable-byte-offset kind
   has no column home today). No kind left prose-or-NULL.
2. **Call-site-data capture is a HARD requirement (user directive, verbatim):**
   *"we will need to look at our current addresses and confirm what data we are
   using to call them, and ensure we capture all of that in our seeds, full
   stop."* So the audit widens beyond "what the importer WRITES" to "what every
   resolved address is CONSUMED WITH at its call site" — every field the engine
   needs to actually CALL an address (offset, slot, struct offset, arg datum)
   must have an authored seed column. A field needed to call an address that has
   no seed column is a finding.
3. **This plan is the PREREQUISITE** for the hardcoded-address migration (kept a
   separate downstream plan). It builds the authored-column surface + kills the
   regex; the migration writes into the columns this establishes.

| action | item | detail |
|---|---|---|
| **KEEP + AUTHOR** | `vtable_slot` (versions seed col → `address_versions.vtable_slot`) | sourced from an AUTHORED cell, not the F1 regex; validator: non-negative int when present; vtable_index kind |
| **KEEP + AUTHOR** | `offset` (versions seed col → `address_versions.offset`) | authored consumer offset (callsite / any kind the call-site audit shows needs it); no longer hardwired NULL |
| **KEEP + AUTHOR** | `value` (versions seed col → `address_versions.value`) | authored per-kind integer datum; no longer a silent `vtable_slot` mirror |
| **ADD + AUTHOR** | `struct_offset` (new versions seed col + schema + engine SELECT/struct) | the audit's vtable-byte-offset kind (1 finding); needs its own column home + engine read |
| **DELETE** | `kind_offset_and_slot()` (`import_to_sqlite.py:321`) | the prose regex — outputs become direct authored-column reads |
| **DELETE** | `infer_kind()` (`:295`) | dead, zero callers (F3) |
| **REMOVE** | `notes_by_kid` prose plumbing (`:518`, `:1187`, consumers) | dead once the regex is gone (A1) |

Per-kind datum coverage target (Phase 1 confirms the exact set against the
call-site audit; all kinds get ONE authored home):
function/function_no_sig/function_variadic → fingerprint (DUMP); callsite →
authored `offset` + survival `aob`; data_slot → survival `rule`/`derives_from`
(+ authored `offset` if the call-site audit shows a consumer needs it on the hot
row); vtable_index → authored `vtable_slot`; vtable_base → survival `slot_count`;
string_anchor → survival `anchor_string`; instruction_anchor → survival `aob`;
struct_offset-bearing → the new `struct_offset` column. ZERO prose-parsing; the
exact column set is finalized by Phase 1's call-site audit, not assumed here.

The 6 vtable_index values to hand-author (Step 3; user-verified against notes):
id 19 → 4, id 20 → 13, id 21 → 7, id 22 → 16, id 23 → 12, id 24 → 13.

**Coupled plan:** `docs/outstanding-work/hardcoded-address-audit.md` +
`seed-to-db-migration-mapping.md` — the downstream migration that consumes these
authored columns. This plan lands first.

## Source material

- The importer: `data/refdata-extractor/python/import_to_sqlite.py`.
- The shared builder + schema + validators: `data/refdata-extractor/python/seeds_shared/`.
- Authoring law: `data/seeds/policy.md`; Address Library rule:
  `.claude/rules/address-library.md`.
- The seed CSVs: `data/seeds/address_versions_seed.csv` (per-version facts incl.
  the per-kind datum columns), `data/seeds/address_names_seed.csv` (entity-level,
  incl. `notes`).
