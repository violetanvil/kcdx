---
id: TD-0008
opened: 2026-06-08
status: Closed 2026-06-08
area: test-suite (address-library resolution regression tests)
closure_gate: source-fix — repoint each plugin's stale `address_id` / `targets.toml` reference to a current curated id (or re-mint the retired entities, AP18-gated)
owner: test-maintenance / DB-seed lane
commit_at_filing: d09e0e45d528c84da438035e49bcb966b4dbe361
affected_sites:
  - test-plugins/cap-33-author-targets/targets.toml  (address_id 1172 luaopen_math, 1124 lua_toboolean — both retired)
  - test-plugins/cap-34-two-dot-namespace/targets.toml  (address_id 1172 via ui_pump_self; cross-plugin ref to cap-33's 1172 row)
  - test-plugins/cap-20-addrname/  (by-id resolve chain — byId path resolves a retired id → 0x0)
  - test-plugins/cap-28-lua-bytes-smart-resolver/  (error-message-quality row; reds because the upstream resolve fails on a retired id)
related:
  - TD-0006 (the statement-layer-in-user-db work whose DB regeneration surfaced this drift)
---

# TD-0008 — stale `address_id` test fixtures reference retired curated entities

## Context

Nine address-library regression rows go red on the 2026-06-08 launch
(`suite: 190/221 passing`): **CAP-20-addrname, CAP-28-typo-fails-fast,
CAP-33-{alias, prefixed}, CAP-34-{alias-2dot, bare-self, cross-plugin-2dot,
explicit-2dot}, COMP-12-self-wins**. Each fails the same way — a name/id resolve
returns null or `address_id name '…' did not resolve in the Address Library`.
CAP-20 is the clearest discriminator: `byName=0x00007FF98677A5A4` resolves while
`byId=0x0` returns null.

**This is NOT an engine or data defect.** Read-only diagnosis pinned the cause to
ground truth:

- The shipped `data/reference.sqlite` (regenerated locally for the statement-layer
  work, [`TD-0006`](TD-0006-statement-layer-in-user-db.md)) carries **157 curated
  entities with contiguous ids 1…157** — a faithful projection of the seed CSVs.
- The failing fixtures reference **`address_id` 1172 (`luaopen_math`) and 1124
  (`lua_toboolean`)** — both **ABSENT** from `data/seeds/address_names_seed.csv`
  AND `data/seeds/address_versions_seed.csv` (verified: 157 rows, highest id 157).
- CAP-33's `pattern-by-name` row (which uses id **115 `luaL_openlibs`**, still
  present) PASSES — exactly why it is NOT in the red set, while every
  `address_id`-based row in the same plugins fails.

The curated set was renumbered to the contiguous 1–157 scheme (the Phase 9.0/9.1
DB↔Address-Library unification — the seed CSVs are the deterministic export of the
maintainer-tool-authored DB). The test plugins were written against an **older,
larger id space** (ids in the 1000+ range) that the renumbering retired. The DB,
the engine resolution path, and the statement-resolution layer (`cap-83`, verified
PASS on the same launch) are all correct; the **test fixtures point at ids that no
longer exist**.

The seed CSVs are dated 2026-06-03/04 and the matrix recorded these caps
`✅ PASS 6/6` (CAP-33) before — so the renumbering postdates the last green run of
these rows and nobody re-ran them against the renumbered set until this launch.

## Closure blocker

Repoint each affected fixture's stale `address_id` / `targets.toml` reference to a
**current curated id** that satisfies the row's contract (a verified leaf the row's
proof needs — e.g. an unhooked verified function for the prefix/alias/bare-self
proofs, a pristine-prologue site for the bytes row), OR re-mint the retired
entities (`luaopen_math`, `lua_toboolean`) into the curated seed set under new
contiguous ids (an **AP18 net-new seed addition — explicit user approval per
entity**, `.claude/rules/address-library.md`). Closes when all nine rows resolve
against the shipped `reference.sqlite` and report PASS on a launch, with the
`test-plugins/README.md` matrix `Last result` cells updated to the new green state.

A row whose contract is genuinely obsolete after renumbering (no current curated id
fits its proof) is a separate decision — surface it; do not silently drop the row.

## Affected sites

- **`test-plugins/cap-33-author-targets/targets.toml`** — declares targets via
  `address_id = 1172` (`luaopen_math_by_id`, used by CAP-33-prefixed + CAP-33-alias)
  and `address_id = 1124` (`bool_leaf_safe_site` / lua_toboolean, the bytes-by-name
  row). Both ids retired. Closure shape: repoint to a current verified-unhooked leaf
  (prefix/alias) + a current pristine-prologue site (bytes), or re-mint 1172/1124.
- **`test-plugins/cap-34-two-dot-namespace/targets.toml`** — `ui_pump_self` locates
  `luaopen_math` by `address_id = 1172`; the cross-plugin row references
  `ts.cap_33_author_targets.luaopen_math_by_id` (cap-33's 1172 row). Same repoint.
- **`test-plugins/cap-20-addrname/`** — the by-id resolve chain resolves an
  `address_id` that the renumbering retired → `byId=0x0`. Repoint to a current id
  whose by-name and by-id both resolve to the same VA.
- **`test-plugins/cap-28-lua-bytes-smart-resolver/`** — the typo-fails-fast row's
  upstream resolve fails on a retired id, so the error-message-quality assertion
  never reaches its intended path. Recovers once the underlying id resolves.

(COMP-12-self-wins lives in the cap-33/34 author-target fixtures — the bare
`combat_check` collision target resolves through the same retired-id path.)

## Activity log

- **2026-06-08** — Initial filing. Surfaced by the `cap-83` statement-layer
  acceptance launch (`suite: 190/221`). Read-only seed-CSV diagnosis confirmed
  CASE B: the curated set genuinely retired ids 1172/1124 (contiguous 1–157
  renumbering); the DB + engine + statement layer are correct; the fixtures are
  stale. Not an engine regression.
- **2026-06-08** — Diagnosis SHARPENED (read-only DB query). The retired entities
  were **renumbered, NOT deleted** — both names persist in the current 157-entity
  `reference.sqlite` under new ids:
  - `luaopen_math`: id **1172 → 97** (same function, same `i32 (ptr L)` sig, verified
    unhooked lua leaf — the prefix/alias/self-tier proofs' contract still holds).
  - `lua_toboolean`: id **1124 → 49** (same function, byte 0 still `0x48` — the
    idempotent bytes-write proof's contract still holds).
  - `luaL_openlibs`: id 115 unchanged (the pattern row already PASSES).
  **The fix is a pure id repoint** (`address_id = 1172 → 97`, `1124 → 49` in the two
  `targets.toml`) — **NO AP18 re-minting** (the entities exist). CAP-20/28 recover
  once their underlying id resolves. The closure-blocker's "or re-mint" branch does
  NOT apply.
  - **Out of this TD's scope:** `comp-18-foreign-classifier` (a DIFFERENT red — a
    foreign-hook classifier bug, owned by the hook-backend lane, fixed in parallel);
    `comp-19-foreign-chaining` already PASSES. Neither is a stale-fixture row.

## What this entry does NOT do

- Does not double as a bug report — there is no engine/data defect; the DB faithfully
  matches the seeds and `cap-83` proves the resolution path works. The debt is stale
  test fixtures, with a named source-fix.
- Closure is appended by the skill that lands the fix (`/execute`), which then moves
  this file to `closed/` + reindexes per `.claude/rules/doc-organization.md` — never
  at filing time.

## Resolution

**The fix is a pure id repoint + a stale-id comment sweep — NO AP18.** The retired
entities were RENUMBERED, not deleted: `luaopen_math` persists under id **97**
(formerly 1172) and `lua_toboolean` under id **49** (formerly 1124) in the shipped
157-entity `reference.sqlite` — same functions, same `i32 (ptr L)` / pristine-prologue
signatures, same contracts. So the repoint is the WHOLE fix; no net-new seed row is
authored, and the closure-blocker's "or re-mint (AP18-gated)" branch does NOT apply.

**Mechanism — renumber-not-delete.** Because the entities still exist under new ids,
each stale `address_id = 1172/1124` is repointed to its current id and the row's
contract holds unchanged (the same verified-unhooked leaf / pristine-prologue site).

**What landed (this change):**

- **Functional id repoint — 3 id-based `[[target]]` rows:**
  - `test-plugins/cap-33-author-targets/targets.toml` — `address_id` 1172→97
    (`luaopen_math_by_id`) + 1124→49 (`bool_leaf_safe_site` / lua_toboolean).
  - `test-plugins/cap-34-two-dot-namespace/targets.toml` — `address_id` 1172→97
    (`ui_pump_self`).
  - `test-plugins/comp-12-target-collision/comp_12_target_a/targets.toml` —
    `address_id` 1172→97 (`combat_check`, COMP-12-self-wins' GOOD locator).
- **Stale-id comment sweep (deletion-hygiene) across the 4 plugins** — every remaining
  `1172`/`1124` mention in comments, doc-strings, `kcdx.toml` headers, and Lua
  log/report strings updated to the current id with a "formerly N before the 1–157
  renumber" note: cap-33 `plugin.lua` + `kcdx.toml`, cap-34 `plugin.lua` + `kcdx.toml`,
  comp-12 `plugin.lua`, cap-35 `plugin.lua` + `kcdx.toml`. Zero stale `address_id`
  remain across the four plugins.
- **cap-35-uninstall was NEVER functionally broken** — it resolves `luaopen_math`
  BY NAME (`target="kcdx.luaopen_math"`), which is renumber-immune; only its
  comments cited the old id. Comment-only sweep; no functional change, no recovered
  matrix row.
- **Matrix update (`test-plugins/README.md`)** — the recovered rows (CAP-20-addrname,
  CAP-28-typo-fails-fast, the CAP-33 id-based family, the CAP-34 family,
  COMP-12-self-wins) marked "repointed to renumbered id (97/49); expected GREEN —
  [unverified — pending launch]"; matrix prose citing 1172/1124 swept to 97/49 with
  the renumber note.

**Honest verification state.** The CODE fix is complete (every id-based fixture
resolves against the shipped `reference.sqlite` under the current ids). The GREEN is
confirmed at the verification gate by the user's launch — the recovered cap rows
reporting PASS (no `FAIL` lines for these rows in `kcdx-dev.log`). The matrix cells
are marked pending-launch, not a self-reported PASS.

**Surfaced (NOT in this change — for the maintainer's call):** `cap-34`'s
`targets.toml` carries two stale `1172` COMMENT references (lines 11, 19) the prior
subagent's partial repoint left behind; the authorized scope excluded re-editing that
file. These are comment-only (the functional `address_id = 97` on line 33 is correct);
they should be swept in a follow-up. (See the chunk's surfaced-items note.)
