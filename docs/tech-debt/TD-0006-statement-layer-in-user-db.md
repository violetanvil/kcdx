---
id: TD-0006
opened: 2026-06-05
status: Open
area: reference DB (data/reference.sqlite) schema — the statement/instruction layer + the named-entity kind model
closure_gate: a dedicated build phase that ships the statement layer in the USER DB (the Phase 9.3 statement-dependent surfaces' producer)
owner: the statement-layer build phase (when scheduled) — the producer Phase 9.3's locator/op/statement steps consume
commit_at_filing: ac42d800e55c60df9cd5267aa36231a6ffff208d
related:
  - TD-0005 (high-level Lua surface — a sibling deferred-surface; unrelated data need)
  - parallel-ghidra-research.md §11.9 (the authoritative shipped schema this spec extends)
  - restructure/00-original-plan.md §"Phase 9.3" (the consumer surfaces) + §"Phase 9.4" (kcdx.find, the other statement consumer)
---

# TD-0006 — the statement/instruction layer is DEV-only; the USER DB cannot back any named statement-level thing

## Context

The shipped reference DB (`data/reference.sqlite`, the USER DB) carries only the
**function-level** facts: an entity's address, length, content hash, ABI floor,
and the curated name overlay. The **statement/instruction layer** — which
instruction inside a function is a given call, its byte range, what it reads, the
call graph — lives ONLY in the DEV DB (`reference-dev.sqlite`, 1.13 GB) and is
**not shipped, not loaded by the engine, not production-consumed**
(`parallel-ghidra-research.md` §11.9; the DEV-only tables are `statements`,
`referenced_vars`, `call_edges`; `entity_type='statement'` is reserved-but-unpopulated).

Phase 9.3's two headline value namespaces **rest entirely on that missing
layer**, so the phase cannot be built as specced until this debt closes:

| Phase 9.3 surface | Needs (statement-level) | In USER DB today? |
|---|---|---|
| `kcdx.locator.first_call_to(fn)` / `last_call_to` / `call_to` / `matching{kind=,callee=,reads_cvar=,references_string=}` | which statement is the call / matches the predicate (`statements.kind`, `.callee`, `.string_ref`, …) | **No** (DEV-only) |
| `kcdx.locator.first_return()` / `function_exit()` / `return_value(v)` | the return statement(s) (`statements.kind='return'`) | **No** |
| `kcdx.op.*` byte-emit fit decision (same-size rewrite vs trampoline) | `statements.byte_range_len` | **No** |
| `kcdx.statement.replace_with` / `insert_before` / `insert_after` | the statement's `byte_range_start` + `byte_range_len` | **No** |
| `insert_after` captures-by-name (`captures.rax`, `[rcx+0x10]`) | what's live at that statement (`referenced_vars` storage/type) | **No** |
| `kcdx.find{...}` discovery (Phase 9.4 — the OTHER consumer) | the bulk statement table + `call_edges` caller-graph ranking | **No** |

The function-level half of 9.3 (`kcdx.hook.*` at function entry, `kcdx.functions.*`,
`kcdx.dll.declare`, PDB auto-load, multi-region trampoline, C++ parity) stands on
built ground and is NOT blocked by this debt — only the statement-dependent
surfaces are.

**What the code does today:** the engine opens the USER DB at `refdb::Open()` and
resolves names/ids to function addresses + ABIs from `entities` /
`entity_versions` / `kcdx_overlay*`. There is no statement table to query, so no
locator-by-content, no op-fit-by-byte-range, no statement-level write can resolve.

**What it does at closure:** the USER DB carries the statement layer for the
curated/needed functions (not necessarily all 5.24 M — see "Scope of the fill"),
the engine loads it, and every named statement-level thing resolves through it.

This debt has TWO coupled parts, both required:
1. **Ship the statement layer in the USER DB** (the data — §A).
2. **Generalize the named-entity model so the DB can back ANY named thing, with
   required-columns-per-kind** (the schema — §B). The user's directive: *"the db
   needs to be able to handle any named thing, no matter what it is… include
   what columns are required per kind."* The current `entity_type` enum is
   closed (function / vtable_slot / data_slot / callsite / statement-reserved);
   §B makes it open + per-kind-validated so a future named kind needs no schema
   migration.

---

## §A — What is MISSING from the USER (non-dev) DB

The authoritative shipped schema is `parallel-ghidra-research.md` §11.9. Against
it, the USER DB is missing the entire statement/instruction layer. Three tables
exist in DEV and must ship (in whole or in a curated subset) to USER:

### A.1 — `statements` (DEV-only today → needs USER)

Per-instruction/statement metadata. 5.24 M rows in the full dump. Current DEV
columns (§11.9):

| Column | Type | Meaning | Needed by |
|---|---|---|---|
| `id` | INTEGER PK | | (all) |
| `kcdx_id` | INTEGER FK→`entities` | owning function | (all) |
| `idx` | INTEGER | statement ordinal within the function | locator ordering (`first_`/`last_`) |
| `kind` | INTEGER (dict) | call / assign / branch / return / … | `matching{kind=}`, `op` kind-validation |
| `pseudo_text` | TEXT | decompiled line | `kcdx_dev_inspect` display |
| `byte_range_start` | INTEGER (RVA) | statement code start | `kcdx.statement.*` write address |
| `byte_range_len` | INTEGER | statement code length | `op` fit decision (rewrite vs trampoline) |
| `content_hash` | BLOB(32) | per-statement hash | statement-level survival check |
| `callee` | TEXT | called function (named; NULL'd when redundant `FUN_<rva>`) | `first_call_to(fn)`, `matching{callee=}` |
| `string_ref` | TEXT | referenced literal | `matching{references_string=}`, matcher signal |

**Gaps even within `statements` for the 9.3 design** (columns the plan's
locator/op vocabulary implies but the dump CUT as 100%-empty or never had):
- `cvar_ref` — `kcdx.locator.first_read_of_cvar(name)` + `matching{reads_cvar=}`
  need it; it was CUT from the dump (100% empty in this build). **Either the
  extractor must populate it (a new decompiler pass) or these two locators are
  unbuildable** — surface as a sub-decision at closure.
- a `condition_text` (the branch condition) — `matching{condition_contains=}`
  needs it; present in the original Phase-9.1 sketch (`statements.condition_text`)
  but not in the §11.9 cut list. Confirm it survives to USER.

### A.2 — `referenced_vars` (DEV-only today → needs USER, for captures)

Per-statement variable storage — what register/stack/global a statement reads or
writes. Backs `insert_after`'s captures-by-name (`captures.rax`,
`captures["[rcx+0x10]"]`).

| Column | Type | Meaning |
|---|---|---|
| `id` | INTEGER PK | |
| `kcdx_id` | INTEGER FK→`entities` | owning function |
| `statement_idx` | INTEGER | owning statement |
| `storage_kind` | INTEGER (dict) | register / stack / global |
| `data_type` | INTEGER (dict) | approximate type |
| `storage_detail` | TEXT | high-cardinality (the actual reg/offset), un-dicted |

### A.3 — `call_edges` (DEV-only today → needs USER only if `kcdx.find` ships)

The call graph (caller→callee). 1.52 M rows. Powers `kcdx.find`'s caller-graph
ranking + the cross-version matcher. **Phase 9.3 itself does NOT need this** —
it is a Phase 9.4 (`kcdx.find` discovery) need. Listed for completeness of "what
is missing"; its closure rides 9.4, not the 9.3 statement layer, UNLESS the fill
is done once for both.

| Column | Type | Meaning |
|---|---|---|
| `id` | INTEGER PK | |
| `caller_kcdx_id` | INTEGER FK→`entities` | calling function |
| `callee_kcdx_id` | INTEGER FK→`entities` | called function |

### A.4 — Scope of the fill (a closure sub-decision, not decided here)

Shipping all 5.24 M statements + 10.88 M referenced_vars + 1.52 M call_edges is
the full DEV DB (1.13 GB) — too large for the USER ship. The closure must decide
the fill scope (surface to the user at build time):
- **(a) curated-only** — statements for just the functions a curated `kcdx_overlay`
  name points at (~139 functions today). Smallest; covers every CURATED
  statement-level target but no author-discovered one.
- **(b) curated + on-demand** — ship curated; let an author trigger a fill for a
  specific function (needs a DEV-DB-at-author-time or an engine-side dump path).
- **(c) full** — ship all statements (the size problem; needs the §11.9 encoding
  + compression analysis redone for the USER tier).

The §11.9 note that "statement survival is already free under the function
interval" (function-hash-unchanged ⟹ every statement byte-identical) means the
statement rows do NOT need their own cross-version matcher — they ride the
function interval. That removes the hardest part of the fill; what remains is the
size/scope decision above.

---

## §B — The general named-entity model: the DB backs ANY named thing, required-columns-per-kind

The user's directive: the DB must handle **any** named thing, no matter what it
is, and adding a new kind in the future must not require a schema migration. The
current model is close but its `entity_type` enum is CLOSED (function /
vtable_slot / data_slot / callsite / statement-reserved). This section specs the
open, per-kind-validated model.

### B.1 — The identity/version split stays (it already generalizes)

The existing two-table spine is the right shape and is kept:
- **`entities`** — version-INDEPENDENT identity: `kcdx_id` (stable, append-only,
  never recycled), `entity_type` (the KIND), `module_id`. One row per named
  thing.
- **`entity_versions`** — version-DEPENDENT facts as validity intervals: the
  per-byte-form data, keyed `(kcdx_id, valid_from..valid_through)`. One row per
  (entity, version-interval).

A NAME is the `kcdx_overlay` row pointing at a `kcdx_id` (many names per entity
allowed — supersession). This split is kind-agnostic and does not change.

### B.2 — `entity_type` becomes an OPEN dictionary, not a closed enum

Today `entity_type` is a fixed enum. Change it to a **dictionary table**
(`_dict_entity_type`, the same `_dict_*` mechanism the schema already uses for
`kind`/`storage_kind`/etc.): one row per kind, with the kind's name + its
**required-column contract** (B.4). Adding a new named kind = inserting a dict
row + its contract, NO schema migration, NO new table. This is what makes the DB
handle "any named thing."

### B.3 — The kinds (current + the ones 9.3/9.4 add + the open future)

Each kind is a named thing an author can resolve by name/id. The columns live on
`entity_versions` (version-dependent) or `entities` (version-independent); a kind
DECLARES which it requires.

| Kind | What it names | New? |
|---|---|---|
| `function` | a function entry (address + ABI) | exists |
| `function_no_sig` | a function whose ABI floor is unknown | exists (seed reality) |
| `function_variadic` | a variadic function | exists |
| `vtable_base` | a vtable's base address | exists |
| `vtable_index` | a specific vtable slot (the slot int) | exists |
| `data_slot` | a static data address/offset | exists |
| `callsite` | a specific call instruction site (carries `offset`) | exists |
| `string_anchor` | a `.rdata` string used as a locator anchor | exists (seed reality) |
| `instruction_anchor` | a specific instruction used as an anchor | exists (seed reality) |
| **`statement`** | a specific statement inside a function (the 9.3 locator target) | **reserved, unpopulated → POPULATE** |
| **`struct_field`** | a struct member offset (named gameplay field) | future (TD-0005 player.* needs this) |
| **`cvar`** | a named console variable | future (`first_read_of_cvar`) |
| **`<any future>`** | whatever a future surface names | **open by construction** |

The model must accept a kind it has never seen by reading its contract from the
dict — never by hardcoding the kind list in engine or generator code.

### B.4 — Required columns PER KIND (the contract)

This is the core of the user's ask. Each kind declares which columns are REQUIRED
(must be non-NULL for a row of that kind to be valid) vs. N/A (must be NULL). The
generator + the engine validate a row against its kind's contract at
import/build (fail loud on a violation, per the existing `meta.schema_version`
fail-loud discipline). The contract table:

| Kind | Required columns (non-NULL) | N/A columns (must be NULL) | Notes |
|---|---|---|---|
| `function` | `rva`, `length`, `content_hash`, `signature` | `value`, `offset`, `vtable_slot`, `struct_offset`, statement FKs | ABI floor required (drops to `function_no_sig` if unknown) |
| `function_no_sig` | `rva`, `length`, `content_hash` | `signature`, `value`, `offset`, `vtable_slot`, `struct_offset` | the honest "address known, ABI unknown" kind |
| `function_variadic` | `rva`, `length`, `content_hash`, `signature` | `value`, `offset`, `vtable_slot`, `struct_offset` | signature carries the variadic marker |
| `vtable_base` | `rva`, `length`, `content_hash` | `signature`, `value`, `offset`, `vtable_slot`, `struct_offset` | base of the table |
| `vtable_index` | `vtable_slot` (the slot int), parent `vtable_base` ref | `rva`, `signature`, `offset`, `struct_offset` | resolves slot int, not an address; the slot-32-vs-33 case lives here per-version |
| `data_slot` | `rva` OR `value` (the offset/address) | `signature`, `length?`, `vtable_slot`, `struct_offset` | a static address or a resolved offset |
| `callsite` | `rva`, `offset` (consumer offset, the `+13`/`-4`) | `signature?`, `vtable_slot`, `struct_offset` | carries the consume offset |
| `string_anchor` | `rva`, `string_ref` (the literal) | `signature`, `value`, `vtable_slot`, `struct_offset` | anchored on a `.rdata` string |
| `instruction_anchor` | `rva`, `byte_range_len`, `content_hash` | `signature`, `value`, `vtable_slot`, `struct_offset` | a specific instruction window |
| **`statement`** | `kcdx_id` (owning fn), `idx`, `kind` (call/assign/branch/return), `byte_range_start`, `byte_range_len` | `signature`, `value`, `vtable_slot`, `struct_offset` | + optional `callee` / `string_ref` / `cvar_ref` / `condition_text` for the matching-predicate locators; `content_hash` for statement-survival |
| **`struct_field`** | `struct_offset` (the member offset), owning-struct ref | `rva`, `signature`, `value`, `vtable_slot` | named gameplay field (TD-0005 consumer) |
| **`cvar`** | `name` (the cvar string) | `rva`, `signature`, `value`, `vtable_slot`, `struct_offset` | the cvar locator target |

The contract is DATA (a `_dict_entity_type` row + a required-columns mask), not
code. A new kind ships its row of this table; the validator reads it. **No engine
or generator code enumerates the kinds** — that is the property that makes the DB
back "any named thing."

### B.5 — The columns the model needs (superset on `entity_versions`)

For B.4 to hold, `entity_versions` (the version-dependent facts) must carry the
union of all kinds' columns, each NULLable, validated by the kind contract.
Against §11.9's current `entity_versions`, the ADDED columns are:

| Column | Type | For kinds | Status |
|---|---|---|---|
| `rva` | INTEGER | most | exists |
| `length` | INTEGER | function*, anchors | exists |
| `content_hash` | BLOB(32) | byte-form kinds | exists |
| `value` | INTEGER | data_slot, vtable_index | exists |
| `signature` | TEXT | function* | exists |
| `offset` | INTEGER | callsite | exists (in `kcdx_overlay_versions` — confirm it generalizes to `entity_versions`) |
| `vtable_slot` | INTEGER | vtable_index | exists (overlay_versions — same) |
| **`struct_offset`** | INTEGER | struct_field | **ADD** (TD-0005 need; not in §11.9) |
| **statement columns** (`stmt_idx`, `stmt_kind`, `byte_range_start`, `byte_range_len`, `callee`, `string_ref`, `cvar_ref`, `condition_text`) | mixed | statement, instruction_anchor | **ADD to USER** (the §A.1 fill) — or kept in the `statements` table with the `statement` entity pointing at it (a design choice at closure: wide `entity_versions` vs. the dedicated `statements` table the dump already has) |

A closure decision (surface at build): do statement-kind entities carry their
facts in a WIDE `entity_versions` (every kind's columns on one table, mostly NULL)
OR does the `statement` kind keep the dedicated `statements` table and
`entity_versions` just holds the identity pointer? §11.9 already has the dedicated
table; the WIDE option is cleaner for the "any named thing" contract but makes a
5.24 M-row-capable super-wide table. **Recommend: keep the dedicated `statements`
table (it exists, it is indexed), the `statement` entity row points at it; the
per-kind contract names the statement FK as the required field for that kind.**
This keeps the general model without a super-wide table.

---

## Closure blocker

**A dedicated build phase that ships the statement layer in the USER DB** — the
producer the Phase 9.3 statement-dependent surfaces (and Phase 9.4's `kcdx.find`)
consume. The phase delivers, in one coherent effort:
1. the §A fill — `statements` (+ `referenced_vars`, + `call_edges` if 9.4 rides
   along) present in the USER DB at the decided scope (§A.4), the engine loading
   them at `refdb::Open()`;
2. the §B generalization — `entity_type` as an open dict with a per-kind
   required-columns contract (§B.4), validated at import/build, so any future
   named kind needs no schema migration;
3. the cut-column resolution — `cvar_ref` / `condition_text` populated by the
   extractor (or the locators depending on them explicitly dropped as a surfaced
   decision).

On that phase's acceptance this TD closes, and Phase 9.3's statement-dependent
steps (locator content-shortcuts, `kcdx.op.*`, `kcdx.statement.*`, insert-captures)
unblock. The phase is the named producer; its scheduling+execution is the blocker
(every prerequisite — the DEV dump, the extractor, the §11.9 schema — already
exists; this is a ship-it-to-USER + generalize-the-kind-model effort, not new RE).

## Affected sites

- `data/reference.sqlite` (the USER DB) — missing the statement layer.
- `data/refdata-extractor/python/import_to_sqlite.py` — the two-DB split that
  keeps `statements`/`referenced_vars`/`call_edges` DEV-only; the place the USER
  fill + the kind-contract validation land.
- `src/refdb.cpp` — the engine consumer; loads `entities`/`entity_versions`/
  overlay today, would load the statement layer + read the kind contract.
- `parallel-ghidra-research.md` §11.9 — the authoritative schema this spec
  extends (the DEV-only markers + `entity_type='statement'` reserved note).
- `docs/outstanding-work/restructure/phase-09.3-namespaces/` steps 1, 2, 4 — the
  blocked consumers (locator content-shortcuts, op, statement).

## Activity log

- **2026-06-05** — Initial filing. Surfaced during the Phase 9.3 `/feature`
  audit: the locator/op/statement value namespaces all depend on the DEV-only
  statement layer, which §11.9 marks not-shipped/not-production-consumed. Spec
  authored covering (§A) exactly what is missing from the USER DB and (§B) the
  general "any named thing, required-columns-per-kind" model the user requested.
  The function-level half of 9.3 is NOT blocked by this and can proceed
  separately.

## What this entry does NOT do

- Does not double as a bug report (no runtime defect — the shipped DB is correct
  for what it ships; this is a missing-capability + schema-generalization debt).
- Does not decide the fill scope (§A.4) or the wide-vs-dedicated-table choice
  (§B.5) — those are closure-time decisions surfaced to the user, recorded here
  as the open questions.
- Closure (the build phase shipping the layer + generalizing the model) is
  appended by the skill that lands it (`/feature` / `/execute`), which then moves
  this file to `closed/` + reindexes per `doc-organization.md` — never at filing.
