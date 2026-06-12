# Phase 9.5 plan-spec — `kcdx.behavior.*` named-behavior catalog

**Goal:** build the two-tier named-behavior surface per the settled design — registry
core, Lua + C++ verbs, the window law + teaching errors, edges + the auto-order
method, and the shipped catalog — so a simple modder changes the game in one line.

**Design authority:** [`behavior-design.md`](behavior-design.md) (committed
`94668ea`, soundness + fidelity gated). Every step doc back-pointers its §; the
design doc, not a step doc's prose, is what an executor builds to
(`.claude/rules/spec-conformance.md`).

## Settled decisions (source: behavior-design.md §12, all user-decided 2026-06-10)

- Immediate discriminating resolution errors; five branches (§6). Deferred binding rejected.
- Persisted dependency edges + a passive callable auto-order method; programmatic
  seam only (no UI, no console trigger — the future button is a pre-launch surface).
- Apply-once-at-boundary, worklist drain, at-most-once per boundary; optional
  `revert`; last-wins + warn; all failure dispositions per §5.
- The window law: plugin-tier `set` is a main-stop verb; catalog-tier settable from
  any stop (§6).
- Thread contract: command-query split — `Set` queues from any thread; queries are
  load-wave + main-thread-only (§5.4/§8).
- Catalog = plain `.lua` files via a catalog-aware loader path; no SQLite table, no
  TOML import; public-safe headers (§7).
- C++ parity in-phase: `kcdxBehaviorInterface` + the value-handle model (§8).
- Duplicate declare → error against the second; `set(name, nil)` → teaching error (§4).
- Version story: consumer needs none; declarer rides Track-1/Track-2 at the
  hash-checked call sites (§9).

## Cross-step invariants

- Every error/warn in §10's catalog ships with the step that builds its mechanism,
  wording per the errors-teach bar (`.claude/rules/lua-api-surface.md` rule 3).
- Every step's fixtures are §14 rows landing in `test-plugins/` cap plugins, same
  change (`.claude/rules/test-suite.md`); matrix rows `[unverified — pending launch]`
  until the phase's launch.
- The four marked design assumptions (§5 boundary/ApplyZone; §6 C++ stops +
  early interleave; §7 pin-ahead; §8 invoke/pump reuse + boundedness) are
  discharged by Phase 1 step 1 BEFORE any dependent step builds
  (`.claude/rules/results-driven.md`, `.claude/rules/incremental-delivery.md`).
  The design is provisional on those clauses until that step lands.
- Doc increments (docs/lua/, docs/cpp/, glossary) ride each building step
  (`.claude/rules/docs-discipline.md`); P3 step 2 completes the author-facing set.
- No author-facing input in any step accepts hex/offset/signature for a common
  path (`.claude/rules/cornerstones.md`, AP12) — behaviors are names + values only.

## User-decided deferrals (recorded; `.claude/rules/deferral-authority.md`)

- **Lua early-stop window-law fixture** — named trigger: Phase 11 P5's `lua_before`
  slot lands (approved 2026-06-10; design §14). The C++ early-stop leg ships in P2s2.
- **Catalog entry selection** — deferred from plan-time to P3s2's step head, where
  the verified corpus is surveyed and each entry gets user sign-off (AP18 for any
  new DB row) (approved 2026-06-10).
- Out-of-scope per design §13: auto-order UI; in-game settings; presentation
  metadata; general dependency declaration; hot-reload semantics; TD-0005 namespaces.

## Coverage map

| Design element (behavior-design.md) | Covered by | Notes |
|---|---|---|
| §4 declare spec + validation + missing-field error | P1 s2 | |
| §4 duplicate-declare error | P1 s2 | |
| §4 `set(name, nil)` teaching error | P1 s3 | enforcement at set |
| §4 rule-4 reconciliation note | P1 s2 | shape as designed |
| §4 engine-tracked state (declarer/value/edges/applied) | P1 s2–s4 | registry core s2; edges s4 |
| §5.1–.3 record, last-wins+warn, worklist boundary, never-applied, at-most-once, boundary-drain sets, boundary-raise disposition | P1 s3 | |
| §5.4 toggle paths + both failure dispositions + post-load declare WALL | P1 s5 | wall built s5, gated on `BoundaryCompleted` (symmetric with the post-load set gate); the post-load-declare fixture (`cap-100-post-load-declare-error`) is boot-exercisable from `input_loaded` and covered in cap-100 |
| §5.4 command semantics (queued off-thread Set, FIFO, attribution, staging) | P2 s2 | |
| §5/§6/§7/§8 marked assumptions (4) + §9 enforcement-site read + pump boundedness | P1 s1 | probe + static evidence |
| §6 window law (plugin-tier main-stop; catalog any-stop) | P1 s4 | C++ early-stop fixture P2 s2 |
| §6 five resolution-error branches | P1 s4 | |
| §6 edge recording + persistence + self-invalidation + launch-time warn | P1 s4 (in-memory) + P1 s6 (persisted) | |
| §6 auto-order method + write-back + cycle reporting + seam | P1 s7 | |
| §7 catalog pack loading + stamping + pin-ahead + malformed-file error | P3 s1 | |
| §7 shipped entries (5–10) + public-safe headers + promotion story | P3 s2 | selection at step head (user-decided deferral) |
| §8 interface verbs + handle model + generation + accessors + builders + query thread wall | P2 s1 | |
| §8 `Invoke` (callables) + off-thread construction staging | P2 s2 | |
| §9 version story (consumer none; declarer call-site enforcement) | P1 s1 (read) + P1 s3 (consumer fixture) | |
| §10 error catalog (19 rows) | each row ships with its mechanism's step | s2: declare errors; s3: nil/conflict/boundary-raise; s4: 5 branches + out-of-window + stale-edge warn (s6); s5: toggle errors (revert-less set; both declarer-code raises) + the post-load-declare WALL (gated on `BoundaryCompleted`, fixture `cap-100-post-load-declare-error` boot-exercisable from `input_loaded`); P2: query wall, coercion, stale handle, queued attribution; P3: malformed file |
| §11 `behavior_registry` unit + reference doc | P1 s2 | |
| §11 `lua_bind_behavior` binder | P1 s2 | |
| §11 `behavior_interface` + Interfaces.h append | P2 s1 | |
| §11 `load_order` extension (edges + write-back + auto-order) | P1 s6–s7 | |
| §11 `lua_plugin_loader.cpp:170-176` comment update | P1 s4 | same change as the dependency mechanism |
| §11 catalog dir + test plugin | P3 s1 / all steps | |
| §14 verification gate rows | distributed per step Test bars | whole-feature launch at execution time |
| US-1 consumer two-liner | P1 s3 | |
| US-2 declarer | P1 s3 | |
| US-3 promotion = file move | P3 s1–s2 | |
| US-4 wrong order + auto-order | P1 s4 + s7 | |
| US-5 runtime toggle | P1 s5 | |
| US-6 C++ plugin all verbs + symmetric wall | P2 s1–s2 | Lua early leg: user-approved trigger deferral |
| Lua early-stop fixture leg | DEFERRED | named trigger: P11 P5 `lua_before` (user-approved) — TD-0013 |
| Post-load-declare fixture leg | P1 s5 | COVERED by `cap-100-post-load-declare-error` — the declare wall re-grounded on `BoundaryCompleted` (symmetric with the post-load set gate) is boot-exercisable from the `input_loaded` handler (post-boundary); no deferral |
| §13 out-of-scope set | OUT-OF-SCOPE | user-approved at design |
