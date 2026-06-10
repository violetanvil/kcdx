# Verification engine — plan spec (the shared authority every step leans on)

## Goal

Build the maintainer-tool **DLL verification engine** — link a game DLL on your
machine and the tool **verifies what you author against the real binary** (not
record-only audit-trio capture), so a wrong RVA / signature / AOB is caught before
it lands, and a whole-DB in-game sweep tells you which rows survived a game update.
This un-defers requirement R5 (driven evidence flows) + restores R12's per-module
DLL link table. The check answers "is this DB entry safe to APPLY on the build the
user is running" — two checks (version-applicability via an on-disk hash +
reachability into live `.text`), run ONCE per author (in the browser) and ONCE at
engine startup in-game, never during gameplay: **version-applicability + reachability
in-game in bulk (at startup)** via a kcdx test-suite plugin → a JSON report → fed
back into the tool.

## The settled design — the authority (read these; build to them; never re-decide)

This plan decomposes an ALREADY-SETTLED design. Every decision below is the user's
call (`.claude/rules/design-authority.md`); a step never re-decides one. The
authoritative artifacts, verbatim with their source:

### TRD — `data/maintainer-tool/design.md`

- **US-11 (§6)** — Verification engine. Two checks (version-applicability + reachability),
  run once per-author in the browser + once at engine startup in-game. All 9 kinds
  DESIGNED; only `vtable_index` population deferred. Static
  mismatch is an **advisory** Changed/Ambiguous, **never a block** (overridable "I
  accept — save anyway"). The browser (JS) check and the C++ engine check
  AGREE on the same on-disk bytes (cross-impl agreement test). The in-game batch plugin
  reports every row's startup verdict (version-applicability + reachability) → JSON
  report → import drives bulk re-verify + fix-me worklist; every applied verdict passes
  the normal validate→field-delta→confirm→commit spine. A linked DLL whose version is
  uncovered offers "add a version row at `<v>`" → create-version flow.
- **D24** — UN-DEFER R5 + RESTORE the R12 link table. The DLL actually checks what
  you author. Supersedes §9's "driven evidence flows out of v1" + D15's
  version-read-only narrowing of R12.
- **D25** — "Verify" = is this DB entry **safe to APPLY on the build the user is
  running**. TWO checks, BOTH run ONCE (per-author in the browser; at engine STARTUP
  in-game; NEVER during gameplay / the hot path): **(1) version-applicability (the
  HASH check)** — does the body at the entry's `rva` match the DB `content_hash`,
  hashed from the **ON-DISK DLL FILE** (NOT the loaded image, which is relocated +
  kcdx-detoured)? Match → valid for this build → apply; mismatch → the build diverged
  → AVOID. This is what `survival.cpp` already does. **(2) reachability (the
  loaded-image check)** — does the address resolve into the live module's executable
  `.text` at all? Reads the loaded image (the only sense of "live" — it reads memory,
  it does NOT re-run during gameplay); catches an entry whose on-disk hash matches but
  whose live resolve is dead/wrong. The in-game check is **NOT a runtime body-hash**
  (the framing this decision corrects). Verdict words unchanged; meanings re-grounded:
  `resolves+works` = on-disk hash matches AND resolves into live `.text`;
  `wrong-target`/`changed` = on-disk hash mismatch (build diverged); `dead` =
  unreachable in live `.text`; `cannot-check` = no hash / non-byte or deferred kind.
- **D26** — the engine runs CLIENT-SIDE (JS port); the DLL never leaves the machine.
  All 9 kinds designed. Pure-byte kinds (function body-hash, `string_anchor` `.rdata`
  search, `vtable_base` table-shape, `callsite` AOB scan) are straightforward;
  `instruction_anchor` + `data_slot` need a **minimal in-browser x86 decoder** (just
  enough to follow a RIP-relative LEA/MOV `disp32` — not a full disassembler);
  `vtable_index` datum SHAPE defined, population DEFERRED.
- **D27** — TWO checkers — the C++ ENGINE is authority, the JS browser checker
  MIRRORS it. A cross-implementation agreement test pins the two to the SAME verdict
  on the same DLL bytes (the `version_resolver.py` test-of-record pattern, extended
  to the full per-kind survival check). BOTH checkers compute the **on-disk
  version-applicability hash** — that on-disk hash is what they agree on; the
  **REACHABILITY (loaded-image) half is engine-only** (the browser can't read the
  loaded image).
- **D28** — the batch verification is an in-game test-suite plugin → a JSON report →
  fed back into the tool. Per row: `kcdx_id`, resolved version, verdict
  (`resolves+works` / `dead` / `wrong-target` / `cannot-check`), detail. The report
  writes alongside `kcdx-dev.log`. Import → worklist: passing → one-click bulk
  re-verify (audit trio auto-filled, `evidence_kind` from the check); failing →
  flagged. Advisory throughout (every verdict through the confirm spine; nothing lands
  silently). NOT report-writes-straight-to-DB; NOT log-only.
- **D29** — a passing check determines `evidence_kind` (verification IS the audit
  evidence): in-game LIVE → `live_production`; browser static AOB-uniqueness →
  `pattern_scan`; manual Ghidra → `maintainer_ghidra` (the default). All editable.
  Composes with the audit-trio auto-fill (the check refines `evidence_kind` from the
  default to the tier it actually establishes).
- **D30** — the link table is in-memory, re-picked each session (no persistence). The
  **version-matching gate**: a check runs only against a linked DLL whose resolved
  version matches the row's version; no matching DLL → unavailable + noted, never
  blocks. **Link-to-create**: a DLL resolving to a version uncovered by any of the
  entity's rows offers "add a version row at `<v>`" → the create-version flow
  prefilled at the DLL's version → author + check → on pass trio auto-fills → save
  (AP18 gates the new row).
- **D31** — verification-engine follow-on forks: **(a)** a callsite AOB matching
  MULTIPLE `.text` sites is an advisory `Ambiguous` that STEERS the maintainer to
  extend the pattern (`survival_expect_unique` / id-6 context-extension), NEVER a hard
  refuse; settles `fingerprint-per-kind.md` §"Open decisions" 3. **(b)** report ingest
  is the frontend reading `report.json` directly via the File API — NO backend read
  seam. **(c)** the report-ingest UX has an explicit ingest progress/loading state.
- **D32** (revised) — the save spine supports a BATCH mutation: bulk re-verify commits
  N UPDATEs as ONE atomic transaction with ONE batched field-delta confirm. There are
  **TWO batch actions** (D35): **verify-all** (the trio + a gap-pass `valid_through`
  extend, D34) and **close-intervals** (a failed row's `valid_through` retract, D35).
  Same validator gate (each row validated), same deferred-commit + D21 robust rollback
  (**all-or-nothing — one row failing rolls back the WHOLE batch**), one git
  commit/push. Both all-UPDATE (re-verify never creates a row; the `valid_through` edits
  + the trio are UPDATEs) → the new-row approval gate (law 8/AP18) does NOT apply; a
  new/variant row is authored per-row via `[Fix ▸]` (AP18 per-row).
- **D33** — the in-game sweep is a **dev-mode-gated** test plugin, runs **once at engine
  startup** (self-skips outside `dev_mode`), over the **curated USER set only** (the
  `kcdx_id` rows — the worklist scale; NOT the ~321k DEV bulk discovery rows, which carry
  no audit trio).
- **D34** — the sweep **ATTRIBUTES** each result to the `address_version` row whose
  fingerprint the swept bytes match (the `content_hash` for a function, the per-kind datum
  otherwise); the report carries the **`matched_address_version_id`** per row (null on a
  non-match/uncheckable). A passing version that fell in a GAP between an entity's intervals
  is attributed to the matched row, and verify-all **extends that row's `valid_through`
  forward** to the swept version (the 1.4-in-the-gap-of-id-1 case). Still all-UPDATE (a
  passing check found the bytes UNCHANGED — nothing new to describe, only coverage to record).
- **D35** — the import shows a **reviewed diff, never an auto-write** — a **verified block**
  (verify-all) + a **failing block** (close-intervals). A failing row's `valid_through`
  retracts to its `last_verified_at_version` (the last version it passed — the sweep
  disproved validity beyond it). A failure needs **no "failed" field**: not advancing
  `last_verified_at_version` already reads UNVERIFIED at the new version by the existing
  derivation (`data/seeds/policy.md`); the seed schema is unchanged. The maintainer fixes a
  failed function individually via `[Fix ▸]` (AP18 per-row).
- **§7 — the save spine** — validate → write → export → round-trip → field-delta
  confirm → atomic commit + push, shared by every mutating story; plus the §7 batch
  mutation (D32) the bulk re-verify reuses at batch scale.

### Per-kind check definitions — `data/maintainer-tool/fingerprint-per-kind.md`

The authority for WHAT each kind's check IS. The survival datum mirrors the resolution
mechanism — a hash answers byte-identity, but most kinds' survival question is a
re-find procedure, not a byte comparison:

- **function / function_no_sig / function_variadic** — re-hash `[rva, rva+length)`
  vs `content_hash`. (Exists today in `survival.cpp`.)
- **callsite** — scan `.text` for the AOB pattern+mask: unique hit → Unchanged
  (relocate RVA); zero → Changed; multiple → Ambiguous (extend the pattern, D31a).
- **string_anchor** — search `.rdata` for the literal (+ optional `expect_unique`
  single-`.text`-LEA-xref assert): present → Unchanged; absent → Changed.
- **instruction_anchor** — re-run the resolver chain (find the `.rdata` anchor, scan
  `.text` for the LEA whose RIP-relative target == the string, walk the byte-shape
  back); verify the final instruction matches the stored shape.
- **data_slot** — re-run the derivation (follow `disp32` from the anchor / a fixed
  offset from another slot); Unchanged iff it still lands in `.data` at a consistent
  offset. NO content hash (a `.data` byte hash is an anti-signal).
- **vtable_base** — at the stored RVA read N qwords; Unchanged iff there are N and each
  resolves into `.text`. NOT a byte hash (the slot pointers relocate every build).
- **vtable_index** — resolve base → read slot → hash that function's body vs the stored
  expected hash. **Population DEFERRED** (needs a verified runtime slot target); the
  datum SHAPE is defined.
- **The anchor dependency (DAG)** — `data_slot` → `instruction_anchor` →
  `string_anchor`; `vtable_index` → `vtable_base`. Survival runs in dependency order: a
  dead `string_anchor` makes everything downstream transitively CannotCheck. The
  `derives_from` column captures the edge.

### Screen specs (the build authority for every FE UI step — `.claude/rules/spec-conformance.md`)

- **`data/maintainer-tool/ui/screens/s02-entity-detail.md`** — the per-module DLL link
  table + the version-match indicator + the link-to-create prompt + the 7 verify states.
- **`data/maintainer-tool/ui/screens/s04-field-editor.md`** — the per-author check
  verdict badge inline + the Ambiguous `[show matches]` steer + `evidence_kind`-from-check
  + the 6 check verdict states.
- **`data/maintainer-tool/ui/screens/s08-verification-worklist.md`** — NEW screen: import
  (File API) + ingest progress bar + pass/fail split + batched bulk re-verify + the 9
  states.
- **`data/maintainer-tool/ui/design.md`** — Layer-1: law 4 (verification is advisory)
  extension to per-author static verdicts + the ingested live report; the 4 new component
  silhouettes (`verdict badge`, `ingest progress bar`, `batch field-delta list`,
  `per-module link row` already named); the version&verify surface growth; the screen
  index + nav map carrying s08.

### Existing built slice (extend, don't rebuild)

- **`data/maintainer-tool/frontend/src/dll-resolver/versionResolver.ts`** — the PE-parse
  foundation (the `.rdata` version scan). The static checker reuses its PE-parse + section
  access. *(The frontend is a SEPARATE gitignored git repo — D23; FE steps gate in the
  nested repo via `npm run build` + Vitest, NOT kcdx's `build.ps1`.)*
- **`src/survival.cpp` + `src/survival_pass.cpp`** — the C++ engine survival checker,
  function-hash-only today; extended to all 9 kinds.
- **`data/refdata-extractor/python/seeds_shared/`** — the headless data-core (pytest); the
  Python per-kind reference checker (the test-of-record) extends `version_resolver.py`'s
  role here.

## Cross-step invariants (every step holds these — they are not re-decided per step)

1. **Client-side / no-upload (D15/D26).** The DLL bytes NEVER cross the wire — every
   static check runs in the browser over a locally-picked ArrayBuffer; only a resolved
   version tag (and, for the report, a maintainer-picked local `report.json`, read via the
   File API — D31b) ever touches the app. No backend read seam for the DLL or the report.
2. **Advisory — never blocks (law 4 / D9).** Every verdict (version-resolve, a per-kind
   static Unchanged/Changed/Ambiguous/CannotCheck, an imported live verdict) is advisory;
   an unverified/Changed/Ambiguous state WARNS + carries "I accept — save anyway", never a
   hard block. Ambiguous STEERS (extend the pattern), never refuses (D31a).
3. **Engine is authority; JS mirrors it (D27).** The C++ engine survival check is the
   batch in-game authority; the JS browser check is the per-author static mirror. A
   cross-impl agreement test pins them to the SAME verdict on the SAME bytes. Both
   checkers hash the **on-disk** file for version-applicability; the **loaded-image
   reachability check** (does the resolve land in live `.text`) is engine-only (the
   browser cannot read the loaded image).
4. **The two-repo split + the cross-repo JSON-report contract.** The browser checker +
   report-ingestion live in the SEPARATE frontend repo (D23, gated by `npm run build` +
   Vitest); the engine checker + in-game plugin live in the kcdx tree (gated by
   `build.ps1` + a live launch). The ONE contract crossing the two repos is the **JSON
   verification report schema** (Phase 1 §1.2) — produced by the TEST plugin, consumed by
   FE s08. A frozen, versioned schema is the cross-repo seam.
5. **All-or-nothing batch rollback (D32/D21).** Bulk re-verify is ONE atomic transaction;
   one row failing rolls back the WHOLE batch (deferred-commit pre-commit + D21 scoped
   restore-point post-commit). One batched field-delta confirm; one git commit/push.
6. **The data-core remains the sole writer (D13/law 6).** Report ingestion authors
   nothing itself — it drives the existing validate→confirm→commit save spine with the
   report's verdicts. No verdict writes straight to the DB.

## Coverage map — every design element → its step (or DEFERRED)

Exhaustive. Every enumerated element from the coverage universe (groups A–I engine-side +
the UI-side groups) maps to a step or an explicit user-decided DEFERRED. A `DEFERRED` cell
records a deferral the USER decided (§9 + D26 + D24), never `/plan`'s own.

### Group A — browser static checker

| Design element | Covered by | Notes |
|---|---|---|
| Feasibility: 86MB WHGame.dll ArrayBuffer + full `.text` AOB scan in-browser | P0 step 1 | Probe — perf/feasibility (D26 "within browser limits") |
| Minimal JS x86 decoder follows RIP-relative `disp32` (feasibility) | P0 step 2 | Probe — id-9/id-10 derivation vs a Ghidra-confirmed target (D26) |
| PE-section scanning foundation (`.text`/`.data` beyond `.rdata`; RVA→file-offset) | P2 step 1 | Extends `versionResolver.ts` PE-parse (D26) |
| The 4 verdict types (Unchanged / Changed / Ambiguous / CannotCheck) | P2 step 1 | The verdict enum the checks return (D26/US-11) |
| The minimal in-browser x86 decoder (RIP-relative `disp32` follow) — named sub-unit | P2 step 2 | Its own sub-unit, not folded into the checker (D26/§5) |
| `function*` body-hash check (re-hash `[rva,rva+length)`) | P2 step 3 | Pure-byte kind (`fingerprint-per-kind.md` §function) |
| `string_anchor` `.rdata`-search check (+ `expect_unique` xref assert) | P2 step 3 | Pure-byte kind (`fingerprint-per-kind.md` §string_anchor) |
| `vtable_base` table-shape check (N qwords each → `.text`) | P2 step 3 | Pure-byte kind (`fingerprint-per-kind.md` §vtable_base) |
| `callsite` AOB-scan check (unique/zero/multiple → Unchanged/Changed/Ambiguous) | P2 step 3 | Pure-byte kind (`fingerprint-per-kind.md` §callsite; D31a) |
| `instruction_anchor` derivation check (resolver-chain re-run) | P2 step 4 | Derivation kind (`fingerprint-per-kind.md` §instruction_anchor) |
| `data_slot` derivation check (follow `disp32`; no content hash) | P2 step 4 | Derivation kind (`fingerprint-per-kind.md` §data_slot) |
| The anchor-dependency DAG ordering (browser) | P2 step 4 | Dependent kind transitively CannotCheck if anchor Changed |
| Advisory-never-blocks (browser verdicts) | P2 step 5, P2 step 6 | Surfaced as warning, override carried (law 4 / D9) |
| Version-match gate (check runs only vs a version-matching DLL) | P2 step 5 | s02 link-table gate (D30) |

### Group B — engine checker

| Design element | Covered by | Notes |
|---|---|---|
| Per-kind dispatch + payload model | P3 step 1 | `SurvivalCheck(kind, payload, derives_from, dll)` (`fingerprint-per-kind.md`) |
| `Ambiguous` status added to `survival::Status` | P3 step 1 | New status (callsite multiple-hit; D31a) |
| function-hash-exists (already in `survival.cpp`) | P3 step 1 | The existing function-body check, kept under dispatch |
| `callsite` static C++ check | P3 step 2 | AOB scan of `.text` |
| `string_anchor` static C++ check | P3 step 2 | `.rdata` literal search |
| `instruction_anchor` static C++ check | P3 step 2 | Resolver-chain re-derivation |
| `data_slot` static C++ check | P3 step 2 | Derivation re-run (no hash) |
| `vtable_base` static C++ check | P3 step 2 | Table-shape (N qwords → `.text`) |
| The anchor-dependency ordering (engine) | P3 step 2 | Dependency-order survival walk |
| The reachability check (resolve lands in live `.text`) + the on-disk version-applicability hash | P3 step 3 | resolves+works (on-disk hash matches AND reachable) / dead (unreachable) / wrong-target (on-disk hash mismatch) — D25 |

### Group C — cross-impl

| Design element | Covered by | Notes |
|---|---|---|
| Python per-kind reference checker (test-of-record) | P1 step 1 | Extends `version_resolver.py`'s role (D27) |
| JS↔C++ agreement test (browser checker == engine checker on same bytes) | P3 step 4 | The cross-impl authority pin (D27) |
| JS↔Python agreement test (the pure-byte + derivation kinds) | P2 step 3, P2 step 4 | JS checked against the Python reference (D27) |
| The cross-impl known-DLL fixture + known per-kind verdicts | P0 step 5 | The fixture the agreement tests pin against |

### Group D — engine rank-ladder + the in-game plugin (RE-PLANNED 2026-06-09 against D36)

The original single in-game-plugin phase (a static boot-automatic sweep) was superseded by D36
(the active-attempt model). It is now Phase 4 (the engine rank-ladder, P4) + Phase 5 (the
console-triggered sweep, P5). The FE report-ingestion phase renumbered to Phase 6 (Groups E/H/I
below).

| Design element | Covered by | Notes |
|---|---|---|
| **7-state verdict enum + the ceiling rule** (verdict = ceiling of strongest method that ran; only rank-1 awards `verified_working`) | **P4 step 1** | D36 — extends the Phase-3 4-verdict `RowVerdict` |
| **`not_applicable` verdict produced** (rank-4 version-check finds the running build's version NOT covered by the row — the gap case; distinct from `cannot_check`) | **P4 step 1** | D36 — the version-gap producer; D36 forbids collapsing it with `cannot_check` |
| **`error` verdict produced** (a check/dispatch fault, caught — the harness blew up, the row is not condemned; distinct from `failed`) | **P4 step 1** | D36 — the harness-fault producer; D36 forbids collapsing it with `failed` |
| **`skipped` verdict produced** (a precondition unmet this run — menu-run with no save loaded) | **P5 step 2** | D36 — produced by the save-load precondition gate |
| **rank-1 observed-execution tier** (hook-fire + pass-through; kcdx's own production call) | **P4 step 2** | D36 — the only tier awarding `verified_working`; observe, never synthetic-invoke |
| **rank-2 safe-read tier** (cvar read + read-only vtable_base walk) | **P4 step 3** | D36 — caps at `passed_not_verified`; reuses `cvar.cpp` |
| **per-kind ceiling matrix** (§11.6 — each kind → highest reachable rank + default `invoke_attempted`/`invoke_skip_reason`) | **P4 step 4** | §11.6 — the per-row method dispatcher |
| The JSON verification report schema v1 (the cross-repo contract) | P1 step 2 | Frozen versioned schema (D28/D31b) |
| Report schema **v2** — `matched_address_version_id` + `schema_version` 1→2 | P1 step 3 | The attribution field (D34); a versioned bump of the frozen schema |
| **Report schema v3** — the 7-state enum + `method_rank` + `invoke_attempted` + `invoke_skip_reason` + redefined `summary`; `schema_version` 2→3 | **P5 step 1** | D36 — the active-attempt cross-repo contract |
| **The incremental-flush contract in the schema** — the per-row JSONL line shape (one `rows[]` element) + the finalized v3 document + the `complete` / `rows_expected` partial-report signal | **P5 step 1** | D37 — the schema defines both shapes so a crashed-mid-sweep report is still ingestible |
| **Per-row incremental flush** — each row's result appended + flushed to the JSONL sink as its attempt completes; a finalize pass wraps to the v3 document at sweep end (NEVER a bulk write) | **P5 step 3** | D37 — the live-exercise tier makes a mid-sweep death a real risk; the user: "we cant bulk flush at the end" |
| The batch verification plugin (drives the engine rank-ladder over the curated set) | P5 step 3 | A kcdx test-suite plugin (D28) |
| **Console command `kcdx_verify_all` (dev-mode-gated) + the save-load precondition** | **P5 step 2** | D33(rev) — replaces the boot-automatic trigger; menu-run → `skipped` (D36) |
| Sweep scope = curated USER set only | P5 step 3 | The `kcdx_id` rows; NOT the ~321k DEV bulk rows (D33) |
| **Per-row console streaming** (a line per row as each attempt completes) | P5 step 3 | D36 — never reads as a hang |
| Attribution: match swept bytes vs each candidate row's fingerprint → matched id | P3 step 3 | Engine reports WHICH row matched (D34); driven per row by P5 step 3 |
| Report emission to v3 schema (per row: kcdx_id, version, the 7-state verdict, method_rank, invoke_attempted, invoke_skip_reason, detail, matched_address_version_id) | P5 step 3 | Written alongside `kcdx-dev.log` (D28/D34/D36) |
| `evidence_kind` from the passing check's tier (live-exercise → `live_production`) | P5 step 3 | D29 — the tier sets the evidence label |
| The test-suite matrix row + deploy to all 3 plugin trees | P5 step 3 | `test-suite.md`; matrix row + 3-tree deploy |
| C++ read pe_helpers surface scoping (does it expose spans + a disp32 follower?) | P0 step 3 | Probe — scoping finding for P3 |
| The in-game version-applicability + reachability signal (the static ranks 3–4 of the ladder) | P0 step 4 | Probe — live-launch de-risk for P3 step 3 / the rank-3/4 static checks |

### Group E — report ingestion (RE-PLANNED 2026-06-10 against the reconciled v3/D36 s08 spec)

The original two-block / v2-token Phase 6 was superseded by the s08 `/ui-design` reconciliation
(`fa8db80`/`9298341`) + the TRD D29 evidence_kind fold (`cc5e1c0`): the producer emits a v3 report
(the 7-state verdict + `method_rank` + the partial-report signal), so s08 is a THREE-block worklist
with a per-row proof-rank chip + a partial-report banner, and verify-all writes `evidence_kind` keyed
by proof rank. The two-step decomposition is unchanged; the content is the v3/D36 contract.

| Design element | Covered by | Notes |
|---|---|---|
| Import (File API, client-side, v3 schema) | P6 step 1 | Frontend reads the report JSON (D31b); validates against the **v3** schema (`schema_version: 3`; a non-v3/malformed report → error state) |
| Ingest progress bar | P6 step 1 | Determinate progress (D31c) |
| The THREE-block worklist (verified / failing / no-action) | P6 step 1 | s08 populated state, three blocks (D36 verdict→block mapping); verified = `verified_working`+`passed_not_verified`, failing = `failed`, no-action (collapsible) = `not_applicable`/`cannot_check`/`skipped`/`error` |
| The per-row `live verdict badge` (7-state) + the matched-`address_version`-id column | P6 step 1 | The 7-state verdict set → the `live verdict badge` (D36); the matched id, non-null on a verified-block row only (D34) |
| The per-row `proof-rank chip` (`rank N · <method>` from `method_rank`) | P6 step 1 | D36 — the proof-strength the verdict alone can't carry (a rank-4 static pass vs a rank-2 safe-read) |
| The conditional invoke-posture detail (`invoke_skip_reason` only when informative) | P6 step 1 | `unsafe_to_call`/`uncontainable` shown; `not_a_callable_kind`/null suppressed (the reconciled s08 detail rule) |
| The partial-report warning banner (`complete: false`, rows < `rows_expected`) | P6 step 1 | D37 — the N-of-M gap named; the present rows stay actionable (a partial report is a valid ingestible report) |
| The s08 states (empty/ingesting/populated-three-block/partial/error-malformed-non-v3/error-unknown-id/per-block-disabled/edge) | P6 step 1 | Built to the reconciled s08 spec §"States & variants" |
| Verify-all → s06 batch confirm (trio + proof-rank `evidence_kind` + gap-pass `valid_through` extend) | P6 step 2 | One batched confirm; `evidence_kind` keyed by proof rank (D29-rev); the matched row's interval extended on a gap-pass (D32/D34) |
| Close-intervals → s06 batch confirm (`valid_through` → `last_verified_at_version`) | P6 step 2 | The failing block's batch action (D35) |
| A failure = UNVERIFIED-by-derivation (no "failed" field) | P6 step 2 | Not advancing `last_verified_at_version` (D35; `policy.md`) |
| Confirm-spine routing (both batches through validate→confirm→commit; all-or-nothing) | P6 step 2 | Data-core sole writer (D28/law 6); one atomic batch txn, one-row failure rolls back the whole batch (D32/D21) |

### Group F — install-set link surface (D30 revised)

| Design element | Covered by | Notes |
|---|---|---|
| Bin-FOLDER pick (`<input webkitdirectory>`, in-session, no persistence) | P2 step 5 | The install-set; one folder pick covers all modules (D30) |
| WHGame.dll resolves the install version; each module's DLL found by filename | P2 step 5 | The `.rdata` scan + per-module DLL-by-filename lookup (D30) |
| Non-WHGame module inherits the install version from WHGame.dll | P2 step 5 | CryEngine DLLs carry no KCD2 version string (D30) |
| Version-match gate (per-module: DLL present AND install version matches the row) | P2 step 5 | The check runs only when matched; exposed for s04 (D30) |
| New-module registration (a surfaced step, AP18 posture) | P2 step 5 | A CryEngine module not yet in the `module` table (D30) |
| Degraded states never block (no folder / no WHGame.dll / module DLL not found) | P2 step 5, P2 step 6 | Advisory, law 4 (D30) |
| Link-to-create (uncovered INSTALL version → add-a-row on-ramp) | P2 step 7 | → s05 prefill at the install version (D30) |
| Multi-store support | **DEFERRED** | User-decided (§9): stores-differ is UNVERIFIED; the content_hash keeps verification correct + fails safe; revisit on a confirmed cross-store binary divergence (a non-Steam probe). |

### Group G — evidence_kind

| Design element | Covered by | Notes |
|---|---|---|
| auto-fill `evidence_kind` by check (static → `pattern_scan`) | P2 step 7, P2 step 6 | The check refines the tier (D29) |
| compose with the audit-trio auto-fill | P2 step 7 | Check refines from the `maintainer_ghidra` default (D29) |
| `evidence_kind` keyed by the in-game proof rank (via s08 bulk re-verify) | P6 step 2 | Set on bulk re-verify in the FE worklist (D28/D29-rev): a rank-1 `verified_working` row → `live_production`; a ranks-2-5 `passed_not_verified` row → `live_test_plugin` (the in-game-test-plugin static tier, NOT `live_production`). The engine-side tier-from-check is P5 step 3 |

### Group H — UX surfaces (covered within the UI steps that build them)

| Design element | Covered by | Notes |
|---|---|---|
| s02 verify-surface UX (folder pick, per-module rows, install-version match, link-to-create banner) | P2 step 5, P2 step 7 | Built to the revised s02 spec (D30 install-set) |
| s02 LAYOUT (compact pinned header + one-line verify summary + collapsible Verify/Lifecycle sections + the work surface gets the room) | P2 step 5 | Built to the revised s02 §"Region & position" + the detail-pane model |
| s02 per-module link-row REFLOW-SAFE structure (stable top line + reserved message space) | P2 step 5 | Built to the revised s02 §"States & variants" (the reflow fix, law 1) |
| s04 verdict-badge UX (inline, reserved, `[show matches]` steer) | P2 step 6 | Built to s04 spec |
| s08 worklist UX (import, progress, split, batch action) | P6 step 1 | Built to s08 spec |
| s06 batch-confirm UX (per-row delta list) | P6 step 2 | Built to s08/§7 (D32) |

### Group I — deferred (user-decided)

| Design element | Covered by | Notes |
|---|---|---|
| `vtable_index` survival POPULATION | **DEFERRED** | User-decided (§9 + D26): needs a verified runtime vtable slot target; the datum SHAPE is designed, only population waits. The other 8 kinds ARE in scope. |
| Job-3 campaign-orchestration UI | **DEFERRED** | User-decided (§9): D28's in-game batch sweep is the producer; the campaign-orchestration UI on top is out of v1. |

### UI-side — Layer-1 (`ui/design.md`)

| Design element | Covered by | Notes |
|---|---|---|
| Law-4 extension (advisory) — 6 clauses (static verdicts + ingested live report + Ambiguous-steers + version-match-gate + no-DLL-upload + no auto-act) | P2 step 5, P2 step 6, P6 step 1, P6 step 2 | Each clause holds in the screen that exercises it (law 4) |
| Component silhouettes (`verdict badge` [s04, 4-state], `per-module link row`, `collapsible section`, `ingest progress bar`, `batch field-delta list`, `warning banner`, + the s08 reconciliation-added `live verdict badge` [7-state] + `proof-rank chip`) | `per-module link row` + `collapsible section` P2 step 5; `verdict badge` P2 step 6; `ingest progress bar` + `warning banner` + `live verdict badge` + `proof-rank chip` P6 step 1; `batch field-delta list` P6 step 2 | Each rendered once in the screen that owns it; the s04 static `verdict badge` (4-state) and the s08 `live verdict badge` (7-state) are distinct silhouettes (the verdict vocabularies diverged at D36); `collapsible section` is the s02-layout disclosure AND the s08 no-action block |
| Version&verify-surface (the compact summary + the collapsible install-set link section) | P2 step 5 | The version `Select` + the one-line verify summary + the Bin-folder pick + per-module rows (revised D30 + the s02 layout) |
| The detail-pane responsive model (lead with the work surface; compact-header + collapse on both breakpoints) | P2 step 5 | `ui/design.md` §"Responsiveness & sizing" |
| Screen-index / nav-map carrying s08 | P6 step 1 | s08 reached from s01 `[Import verification report]` |

### UI-side — s02 (`s02-entity-detail.md`)

| Design element | Covered by | Notes |
|---|---|---|
| Compact pinned header (identity + version Select + one-line verify summary) | P2 step 5 | s02 §"Region & position" + §Contents (the layout revision) |
| Collapsible "Verify against a DLL" + "Lifecycle" sections (collapsed by default) | P2 step 5 | s02 §"Region & position" + §Contents (the `collapsible section`) |
| The install-set link surface (Bin-folder pick + per-module rows) | P2 step 5 | per-module link row ×M + the surface prose (revised D30) |
| Per-module row reflow-safe structure (stable top line + reserved message) | P2 step 5 | s02 §States (the reflow fix, law 1) |
| Link-to-create (Contents + prose) | P2 step 7 | warning banner → s05 at the install version (D30) |
| The verify states (no-folder / folder-resolving / not-a-Bin-folder / resolve-failure / per-module-DLL-not-found / match / mismatch) | P2 step 5, P2 step 7 | s02 §States; mismatch surfaces link-to-create (step 7) |
| The section collapsed/expanded state | P2 step 5 | s02 §States (the disclosure, law 1) |

### UI-side — s04 (`s04-field-editor.md`)

| Design element | Covered by | Notes |
|---|---|---|
| Verdict (Contents + prose) | P2 step 6 | the inline `verdict badge` (D24–D27/D31) |
| `evidence_kind` from check | P2 step 6 | static pass → `pattern_scan` refine (D29) |
| The 6 check verdict states (no-badge / checking / Unchanged / Changed / Ambiguous+show-matches / CannotCheck) | P2 step 6 | s04 §"Check verdict states" |

### UI-side — s08 (`s08-verification-worklist.md` — the reconciled v3/D36 spec)

| Design element | Covered by | Notes |
|---|---|---|
| The Contents elements (import entry, summary header + `partial` marker, partial-report banner, ingest progress, block split control, the THREE-block worklist table + `live verdict badge` + `proof-rank chip` + matched-id column + conditional invoke-posture detail, per-block selects, select-all per action block, the two bulk actions, no-action rows, fix-row, back) | P6 step 1 (import/progress/split/three-block worklist/badge+chip/partial-banner/per-block select/no-action rows/fix/back); P6 step 2 (the two actions → batch confirms) | Built to the reconciled s08 Contents (D34/D35/D36) |
| The s08 states (empty / loading-ingesting / populated-three-block / partial-report / error-malformed-non-v3 / error-unknown-id / per-block-disabled / edge-0-failing / edge-0-verified / edge-all-no-action / edge-long) | P6 step 1 (ingest/worklist/partial states); P6 step 2 (per-block batch-action disabled/edge) | s08 §"States & variants" (reconciled) |
| The Layer-1 `live verdict badge` (7-state) + `proof-rank chip` silhouettes | P6 step 1 | `ui/design.md` — the two new silhouettes the reconciliation added (the s04 `verdict badge` stays 4-state, unchanged) |

## What is NOT in this plan

- **`vtable_index` survival POPULATION** — DEFERRED (user-decided, §9 + D26). The datum
  SHAPE is in scope (the 9-kind dispatch carries it; the check returns CannotCheck for it).
  Only the runtime-slot-target population waits.
- **Job-3 campaign-orchestration UI** — DEFERRED (user-decided, §9). The in-game batch
  sweep (D28, Phase 5) is the producer; the campaign UI on top is out of v1.
- **No design decisions.** A fork surfaced during build routes to `/design` /
  `senior-architect-consult` (`.claude/rules/design-authority.md`); a step never invents a
  default for a spec gap (`.claude/rules/spec-conformance.md`).
