---
name: add-db-entity
description: Use this skill to ADD a new row to the Address Library reference DB — a brand-new curated entity (a new game-binary target plugins reference by name) or a new per-version resolve fact for an existing entity. The agent-driven counterpart to the GUI: it gathers the row's fields, validates them through the maintainer-tool backend (the single validator — policy.md is the gate), surfaces the validated row for the user's EXPLICIT per-addition approval (AP18), and only then commits it. Auto-invocable by an agent that has VERIFIED a game-function fact and needs to record it (the seam /research-disassembly §6 hands off to) — but it NEVER writes a row without the user's explicit yes. NOT for verifying the fact itself (that's /research-disassembly — run it FIRST; the address/ABI must be verified evidence, never invented). NOT for UPDATING an existing row (re-verify / supersede / deprecate / edit-notes — the GUI owns those). NOT for editing the CSV export by hand (the DB is the authoring surface; this drives the validated write path).
---

# Add DB entity — record a verified Address Library row through the validated write path

Add a NEW curated row to the reference DB: a brand-new **entity** (a stable cross-version handle a plugin references by `target = "<name>"`) or a new per-version **version row** for an existing entity. This skill is the **agent/programmatic** path — the headless counterpart to the GUI, for an agent that has verified a fact and needs to record it. It drives the maintainer-tool backend's validated `/save` → `/confirm` path; it adds NO validation logic of its own.

**Two hard invariants, both load-bearing:**

1. **The address / ABI / offset is VERIFIED EVIDENCE, never invented (AP1 / AP2 / AP12).** A new entity's whole purpose is that the *name* resolves to the address AND the verified signature, so a modder never hand-writes hex (the disassembler test, `.claude/rules/cornerstones.md`). The fact MUST come from the reuse-first evidence ladder — run `/research-disassembly` FIRST and carry its verified fact + tier here. This skill records a fact; it never guesses one.
2. **The user explicitly approves the specific addition BEFORE any write lands (AP18).** Adding a row grows the DB — a target the project commits to maintaining across game versions. The agent runs up to the validated PREVIEW (which writes nothing), then STOPS and surfaces the row; only on the user's explicit per-addition yes does it confirm. The agent NEVER self-approves (`.claude/rules/design-authority.md`, `.claude/rules/deferral-authority.md`).

---

## Scope

**In:** add a new ENTITY (US-7 `create-entity`) · add a new per-version ROW for an existing entity (US-6 `create-version`). Both are the AP18-gated ADDITIONS.

**Out — refuse and route:**
- **Verifying a game-function fact** (address / ABI / return / vtable slot) → `/research-disassembly`. Run it FIRST; this skill consumes its verified output.
- **UPDATING an existing row** — re-verify, full-column edit, supersede, deprecate, edit-notes → the maintainer-tool GUI (those are not additions, not AP18-gated, and the GUI handles them well).
- **Hand-editing the CSV export** under `data/db-export/` → never. The DB is the authoring surface; its CSV export is derived. This skill drives the validated write path that produces the export.
- **Authoring the entity's test plugin** → `/feature` or `/execute` (this skill SURFACES the owed test, §5; it does not build code).

---

## 1. Precondition — the fact is verified, the server is up

Before gathering anything:

- **The fact is verified.** The `rva`, `signature`, `offset`, `vtable_slot`, `survival_*` values you will record came from `/research-disassembly`'s evidence ladder (an existing seed row, a prior `_research/` dump, a predecessor sig, the Ghidra project, or a fresh disassembly), with a known **evidence tier**. If you do not have the verified fact + its tier, STOP — run `/research-disassembly` first. A value you cannot back with evidence stays empty (honest-uncertainty, AP2); never invent one to fill a column.
- **The backend is running.** This skill drives the maintainer-tool backend API. The agent starts it (`.claude/rules/agent-builds-and-deploys.md` — the agent runs servers; the user does not): from inside `data/maintainer-tool/backend/`, `python -m uvicorn app.main:app --reload` (default `http://127.0.0.1:8000`). Confirm `GET /health` reports `state: resolved` (the DB + the three CSVs present AND the known-versions read succeeds) before proceeding; an `empty`/`error` state names what is missing — surface it, do not write.

## 2. Gather the row — the per-kind COLLECT checklist

`policy.md` (`data/maintainer-tool/policy.md`) is the AUTHORITY for what each column requires and `schema.py` (`data/refdata-extractor/python/seeds_shared/schema.py`) for the enums — read them for the rules; this checklist is only what to COLLECT per kind. The backend validator is the gate that REJECTS anything wrong — do not re-implement its rules here.

**Always required (every row):** the `kind` (one of the nine — verify against the actual RE finding, never the importer's guess), the `module` (the DLL, normally `WHGame.dll`), the target `version_tag` (a known `game_versions.tag`, e.g. `1.5.1164953`). For an entity: a `name` (the `target = "..."` string — snake_case for new, CamelCase when it matches a canonical engine identifier; `policy.md` §Naming).

**Per-kind — collect the column(s) the kind's form needs (leave the rest empty):**

| Kind | Collect (beyond name/kind/module/version) |
|---|---|
| `function` / `function_variadic` | `rva`, `signature` (verified ABI). NO offset, NO survival columns — a function's survival datum is its body fingerprint. |
| `function_no_sig` | `rva` only (signature not yet known — leave empty, not invented). |
| `callsite` | `rva`, `offset`, `survival_aob`, `survival_expect_unique`. |
| `data_slot` | `offset`, `survival_rule`, `survival_derives_from`, `value`. |
| `vtable_base` | `rva`, `survival_slot_count`, `survival_derives_from`. |
| `vtable_index` | `vtable_slot` (the slot INDEX; NO rva). Survival is DEFERRED — leave its survival columns empty. |
| `string_anchor` | `survival_anchor_string`, `survival_expect_unique`. |
| `instruction_anchor` | `rva`, `survival_aob`, `survival_expect_unique`, `survival_derives_from`. |

**The verification audit trio — all-or-nothing (`policy.md` §"the trio").** If you are recording this row as verified at the version (the normal case for a fact you just confirmed), set ALL THREE: `last_verified_at_version` (the version tag), `verified_by` (the maintainer identity — the backend supplies its configured default), `verified_date` (`YYYY-MM-DD`), and `evidence_kind` (the tier from `/research-disassembly`: `maintainer_ghidra` / `predecessor_sig` / `pattern_scan` / `live_test_plugin` / `live_production`). If the row is NOT yet verified, leave all three empty (a partial trio is a hard error the validator rejects).

## 3. PREVIEW — validate, write nothing

POST the gathered row to the matching preview endpoint (`http://127.0.0.1:8000`). This VALIDATES through the data-core's single gate and writes NOTHING.

- **New entity** → `POST /save/create-entity` with `{version_tag, name, first_version_columns: {<the collected columns>}}`.
- **New version row** → `POST /save/create-version` with `{version_tag, kcdx_id, valid_from_version, columns: {<the collected columns>}}`.

Read the response:
- `valid: false` → the validator rejected the row; the `errors` name why (a missing required column, a partial trio, an out-of-enum kind, a duplicate tuple, a broken FK). FIX the input (or, if it reveals the fact is wrong, return to `/research-disassembly`) and re-preview. Never work around a validator rejection.
- `valid: true` → the response carries the `field_delta` (exactly what will be written, `field: old → new`), the `ap18_new_row` flag, and (for an entity) the assigned `kcdx_id`. This is the row to surface.

## 4. SURFACE for explicit approval — AP18, before any write

The validated preview is NOT permission to write. Surface the addition to the user and STOP:

- Run the option set past `.claude/rules/cornerstones.md` first (the addition is author-facing — state that the address/ABI is verified evidence, not invented, and name its tier).
- Present, via the `AskUserQuestion` tool: the entity `name` (+ assigned `kcdx_id`) / the `(kcdx_id, valid_from_version)` row, the `kind`, the validated `field_delta`, the evidence tier, and the owed test plugin (§5). The decision is **approve this specific addition / refine / cancel**, with the recommendation naming what it wins on.
- **STOP and wait.** The user's explicit yes is the ONLY thing that authorizes the write (AP18; CLAUDE.md hard rule "Adding an Address Library DB row requires explicit user approval"). No yes → no write. The agent never self-approves to keep moving.

## 5. CONFIRM — transact, then surface the owed test plugin

On the user's explicit approval, POST the SAME body to the matching confirm endpoint:

- **New entity** → `POST /confirm/create-entity`.
- **New version row** → `POST /confirm/create-version`.

The backend runs the whole atomic transaction (write → export the three `data/db-export/` CSVs → integrity check → git commit + push to `private`) with robust rollback — on any failure NOTHING lands. Read the response: `status: saved` (report the entity + version + whether it pushed) / `status: failed` (report the cause — nothing landed) / `status: busy` (a shared git lock — surface Retry).

**Then surface the owed test plugin (mandatory, `policy.md` §"Test plugin requirement").** A new entity is only re-verifiable across game versions if a `test-plugins/` plugin exercises it. This skill records the row; it does NOT build the plugin. Surface the obligation explicitly as the owed follow-up — "OWED: a `test-plugins/cap-NN` (or `comp-NN`) row exercises `<name>` + a `test-plugins/README.md` matrix row; route to `/feature` or `/execute`" — so it is never silently dropped (`.claude/rules/deferral-authority.md` — the user decides whether to do it now or next; the skill flags it).

## 6. Stop

Report: the row added (`name` + `kcdx_id` + version, or `(kcdx_id, valid_from_version)`), the commit the backend made, and the owed test-plugin follow-up. Do NOT auto-proceed into authoring the test plugin — the user routes that.

---

## Hard rules

- **The fact is verified evidence, never invented** (AP1 / AP2 / AP12). The address / ABI / offset / survival datum comes from `/research-disassembly`'s ladder with a known tier. A value you cannot back stays empty (honest-uncertainty); never guess one to fill a column. Building the row on an invented RVA/signature is the defect this skill exists to prevent.
- **The user explicitly approves the specific addition before any write** (AP18; CLAUDE.md hard rule). The agent runs to the PREVIEW (no write), surfaces the validated row, and STOPS. No explicit yes → no `/confirm`. The agent NEVER self-approves a DB addition, even auto-invoked.
- **The backend validator is the single gate** — this skill restates none of policy.md's rules. It collects per the §2 checklist, previews, and lets the validator reject. A re-implemented rule here would drift from `policy.md` / `schema.py` / the data-core.
- **Drive the validated path; never hand-edit the export.** All writes go through `/save` → `/confirm`. Never edit `data/db-export/*.csv` or the DB directly — the validated path owns the write, the export, and the git commit.
- **The agent runs the server; the user does not** (`.claude/rules/agent-builds-and-deploys.md`). Starting the backend, hitting `/health`, previewing, and confirming are the agent's tool actions. The user's only action is the approval decision in §4.
- **New rows only.** An UPDATE to an existing row (re-verify / supersede / deprecate / edit-notes) is the GUI's job — refuse and route. This skill adds; it does not mutate approved entities.
- **The owed test plugin is surfaced, never dropped** (`policy.md` §"Test plugin requirement"; `.claude/rules/deferral-authority.md`). A new entity without its `test-plugins/` exercise is a policy debt — flag it as the owed follow-up; the user decides when.
- **Commit cadence is the backend's.** The `/confirm` transaction makes its own git commit; this skill does not also invoke `/commit` for the DB row. A new `_research/` artifact from the preceding `/research-disassembly` is committed by that skill, per its own §7.

## Anti-patterns

- Inventing an `rva` / `signature` / `offset` to fill a column instead of carrying a verified fact from `/research-disassembly` (AP1 / AP2 / AP12).
- Calling `/confirm` without the user's explicit per-addition approval (AP18) — a validated preview is not permission to write.
- Re-stating policy.md's required-column / trio / FK / enum rules in this skill (a 4th drifting copy) instead of letting the validator be the gate.
- Hand-editing `data/db-export/*.csv` or the DB to add a row, bypassing the validated path.
- Treating a `valid: false` preview as something to work around instead of a fact to fix at its source.
- Adding an entity and never surfacing the owed test plugin — a silent policy debt.
- Using this skill to UPDATE an existing approved row — that is the GUI's, not an addition.
