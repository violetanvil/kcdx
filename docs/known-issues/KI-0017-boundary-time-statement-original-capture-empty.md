---
id: KI-0017
opened: 2026-06-11
status: Open
commit_at_filing: 08e2f2a7fe629f00cbbdfa41ba81e2c7c1a325e6
---

# KI-0017 — boundary-time statement registration rejected: original-byte capture empty (length mismatch 0 vs N)

## Symptom

A `kcdx.statement.replace_with` registered from a behavior implementation AT the
behavior apply boundary (9.5 P1 s3's worklist drain, post-`RunPostGameLoad` /
pre-InputLoaded) queues and drains, but is rejected at apply with an
original/replacement length mismatch where the ORIGINAL side is length 0. The
identical statement shape on the identical target registered at LOAD time
applies cleanly in the same session.

## Evidence (facts)

From `kcdx-dev_2026-06-11_15-38-01.log` (suite run at commit `08e2f2a`):

- The boundary-time entry queues and drains (the boundary's own ApplyZone pass):
  - `[15:38:16.072][INFO][engine][LEGACY] lua_registry: queued kind=2 name='comp20_decl_stmt' plugin='comp_20_behavior_declarer' site=...comp-20-behavior-declarer\plugin.lua:91 (handle=86)`
  - `[15:38:16.072][ERROR][engine][LEGACY] [comp20_decl_stmt] aborted: original/replacement length mismatch (0 vs 3)`
  - `[15:38:16.072][ERROR][engine][LEGACY] lua_registry: entry 'comp20_decl_stmt' (plugin='comp_20_behavior_declarer') failed at apply: kcdx.statement.replace_with 'comp20_decl_stmt': the byte write was rejected at the statement VA (...)`
- The one-variable control PASSES in the SAME run: cap-92 (`kcdx.statement.replace_with`,
  same `WHGame.dll` SaveGame target via `kcdx.locator.function_entry()` +
  `kcdx.op.replace_with_noop()`, registered during the load wave) — all four rows
  `RESULT name=cap-92-* verdict=PASS`.
- The fixture's registration site: `test-plugins/comp-20-behavior-declarer/plugin.lua:91`
  (inside the behavior's `implementation`, invoked by
  `behavior_registry::RunApplyBoundary` — `src/behavior_registry.cpp`, the s3
  worklist drain; the boundary's ApplyZone call is `src/hooks.cpp:~586-610`).
- The probe ground truth that boundary-queued registrations DO drain pre-ready:
  `_research/behavior-startup-recon/FINDINGS.md` §5 (F1/F2) — the drain itself is
  not the failure; the entry's content is.
- The suite row's own discrimination: the FAIL reason states the verdict was
  "neither applied, a deploy-state miss, nor the co-located-entry arm — a real
  apply-path regression" (the co-location arm did NOT fire; the rejection reason
  is the length mismatch, not a conflict).

## Hypothesis (NOT verified)

- Hypothesis only — not verified: statement entries capture their ORIGINAL bytes
  during a resolution stage that runs for load-wave registrations but has
  already passed (or is skipped) for entries queued at the apply boundary — so a
  boundary-time entry reaches apply with an empty original and the
  length-preserving check (`replacement.length == original.length`) rejects it.
- Hypothesis only — not verified: the same gap would affect ANY post-load-wave
  statement registration path, including 9.5 P2 s2's queued off-thread set
  (whose toggles execute implementations after boot) — making this a
  boundary-class defect, not a comp-20 fixture quirk.

## Reproduction (if known)

Deterministic: boot-to-menu with the deployed suite (dev mode on) at commit
`08e2f2a` — `comp-20-declarer-statement` FAILs every run; cap-92 passes. The
fixture pair is `test-plugins/comp-20-behavior-declarer/` + `-consumer/`.

## Trail

| Date | Action | Result |
|------|--------|--------|
| 2026-06-11 | PROBE A: static read — trace who fills PatchEntry.original on the statement apply path | Hypothesis DISPROVEN. No original-capture stage exists for ANY statement entry; the defect is registration-time-independent. |

## Facts

- The statement apply handler resolves at apply time, emits the replacement
  (NOP-padded to `byte_range_len`), and builds the carrier `PatchEntry` with
  `resolvedVa` + `replacement` — `pe->original` is NEVER assigned
  (`src/lua_bind_statement.cpp:282-297`). (PROBE A)
- `patch::Resolve` rejects `original.size() != replacement.size()` as its FIRST
  check, before the `resolvedVa` carrier path (`src/patch_engine.cpp:251-257`)
  — every statement write with a non-empty emit is rejected `(0 vs N)`. (PROBE A)
- `refdb::StatementResolution` exposes kind / callee / string_ref / pseudo_text /
  byte_range / captures — NO instruction-bytes field (`src/refdb.h:393-416`);
  the deferred-op comment states the layer "does NOT expose the apply-time
  statement's actual bytes" (`src/lua_bind_statement.cpp:195-199`). (PROBE A)
- cap-92 was never a control for the APPLY: its four rows assert registration +
  error paths + zero-dispatch, none asserts `applied()==true` — the live-apply
  proof is the deliberately-deferred TD-0010 (bucket-2). comp-20-declarer-statement
  is the FIRST fixture to drive `replace_with` through a real apply. (PROBE A)

## Reframe (2026-06-11)

The filed title/hypothesis ("boundary-time registration misses a capture
stage") is wrong — disproven by PROBE A. The mechanism: `kcdx.statement.
replace_with`'s apply path has never successfully applied ANY entry; the
carrier `PatchEntry` ships an empty `original`, and the patch engine's
length-preserving precondition unconditionally rejects it. The boundary-time
registration in comp-20 was merely the first caller to reach the full apply.
TD-0010's deferred live proof is the reason this shipped unexercised.

## Fix forks — SETTLED (Gate A + a follow-on simplification, user-decided 2026-06-11)

1. **Baseline source: read `original` from the live resolved-VA at apply; lean on
   the existing replacement-vs-site idempotent/clobber check. NO DB change, NO
   on-disk read, NO probe.** The DB already owns version-correctness (per-version
   resolution gates which `address_versions` row resolves — `behavior-design.md`
   §9), so the apply needs NO byte baseline for correctness or version-safety.
   The byte-verify's only load-bearing job on this path is the
   **replacement-vs-site** comparison the apply already implements
   (`src/patch_engine.cpp:525` idempotent-skip when site == replacement;
   `:548-565` first-writer-wins reject + the `g_patches` enrichment naming the
   conflicting mod). Fix: in `src/lua_bind_statement.cpp`, before `ApplyPatch`,
   set `pe->original` = the `byte_range_len` live bytes read at the resolved
   statement VA. This satisfies `patch::Resolve`'s length precondition
   (`:251-257`) while the REAL guard (replacement-vs-site) does the protective
   work: a second identical-emit set → clean idempotent skip (comp-20's
   `applied==true` arm); a foreign mod's bytes at the site → loud reject. The
   AP14 "verify-against-itself" objection dissolves — the protective compare is
   replacement-vs-site (non-tautological), not original-vs-site. Boot-only,
   never a hot path. **Supersedes the earlier Gate-A pick (DB `content_hash`
   restore):** that paid a full data-pipeline reshipment + a collision with the
   in-flight `data/db-export` work to detect curated-vs-binary drift, a condition
   the version-resolution layer already catches upstream. Rejected with it:
   on-disk DLL read (same protection, but the live read IS the pristine site for
   a never-touched statement — no file read needed). The earlier-rejected
   live-read AP14 concern does not apply because the surviving guard is
   replacement-vs-site, not a self-comparison.
2. **Fixture target: re-point cap-92 + comp-20 off SaveGame** to a maintainer-
   chosen boot-safe, observably-exercised function (existing curated entity = no
   AP18; new entity = per-entity sign-off). The dev suite must not leave a live
   byte modification. Closes TD-0010's named blocker with a live-apply proof row.
3. **Conflict-engine footprint gap → tracked as TD**, not fixed here. Statement
   carriers are not in `g_patches`, so a statement-vs-statement clobber with
   DIFFERING emits rejects loud but UNNAMED (the `:556-565` enrichment scans
   `g_patches` only), and the conflict report omits statement writes. The
   integrity invariant is already preserved by fork 1's loud reject; only the
   NAMING of the other party is missing. Named trigger: the next
   statement-surface or conflict-engine cycle.

## Scope after the simplification

A single `src/`-only `/execute` fix (fork 1, in `lua_bind_statement.cpp`) + a
fixture RE-FRAME (fork 2, corrected below) + two `/tech-debt` entries
(fork 3 + the live-write blocker). No `/plan` tree, no DB pipeline change, no
owed probe.

## Fork-2 CORRECTED — re-frame the fixtures, do NOT re-point (user-decided 2026-06-11)

The "re-point to a boot-safe curated target" plan was rejected on two checkable
facts the earlier framing got wrong:

1. **No curated function is a safe NOP target.** Every one of the 157 curated
   `address_names_seed.csv` rows is either an author-forwarded shim entry (91
   forwarded via `src/lua_shim.cpp`) or a production hook target. `lua_getlocal`
   specifically is exported to plugins as `GetLocal` (`include/kcdx/Interfaces.h:1117`,
   `src/scripting_interface.cpp:391`, `src/lua_shim.cpp:155`) — NOPing it breaks a
   stack-introspection plugin exactly as NOPing SaveGame breaks save. The DB is a
   registry of functions kcdx USES; there is no junk function in it to NOP.
2. **The live apply has never executed on ANY target** — NO statement tables are
   deployed (`data/db-export/` carries only `address_names_seed` /
   `address_versions_seed` / `module_seed`). `refdb::ResolveStatementByName`
   returns `function_no_statements` for SaveGame AND lua_getlocal alike, and both
   fixtures already PASS on that degraded arm (cap-92 `plugin.lua:111`; comp-20
   `plugin.lua:38`). The byte-write was ASSERTED in code, never OBSERVED.

**The fix (fork 1) still belongs** — the carrier would trip the length
precondition the instant statement data IS deployed; filling `original` is the
correct latent-correctness fix. But its cause-test is the **resolve→register→
verdict wiring**, NOT a live `:applied()==true` claim (which no target can
honestly make today).

**Re-frame both fixtures** (`cap-92-replace-with-registers`,
`comp-20-declarer-statement`): assert resolution reached + registered as
`Kind::Statement` + an HONEST verdict (Pending / degraded `function_no_statements`
/ co-location reject) — never `:applied()==true` against a real game function.
The other cap-92 rows (kind-mismatch, deferred-op, zero-dispatch) stay as-is
(structural reject/static-op properties, no NOP-safe-target dependency). Update
the matrix row text: the falsifiable claim is explicitly "asserts wiring, not a
live write." Both stay PERMANENT regression plugins — this is NOT an exempt;
resolve/register/verdict is real exercised coverage (bucket-1, safe-seam-reachable).

**The live byte-write proof is a real future coverage item** — blocked on a
purpose-built curated test-stub function (one the engine never calls and never
forwards, with statement data deployed for it — net-new curation, AP18-gated,
user-approved). Filed as its own tech-debt entry naming that blocker; NOT folded
into this cycle and NOT dropped.

## Fork-2 CORRECTED AGAIN — assert the live apply (the launch falsified the re-frame's premises, 2026-06-11)

The `402d028` wiring-only re-frame rested on two facts the live launch
(`kcdx-dev_2026-06-11_19-13-16.log`) FALSIFIED:

1. **Statement tables ARE deployed** in the runtime `reference.sqlite` (the full
   corpus from `data/refdata-extractor/`). The earlier "not deployed" read was of
   `data/db-export/` — the curated GIT EXPORT, not the runtime DB. Proof: cap-83
   statement-resolve PASS + the engine STATEMENT line
   `replace_with applied name="comp20_decl_stmt" target="SaveGame"
   op="replace_with_noop" stmt_kind="assign" byte_range_len=3 wrote_bytes=3`
   (log 5751).
2. **The apply HONESTLY LANDS.** `comp20_decl_stmt applied successfully at
   0x...965DD1B04: 90 90 90 -> 90 90 90` (log 5750). The fix works end-to-end —
   the live byte-write proof we kept saying we couldn't have EXISTS, this is it.

**The byte-anchoring correction (load-bearing):** do NOT assert the site bytes
are `90 90 90` — that is a CO-LOCATION ARTIFACT. cap-96 applies FIRST and NOPs
the SaveGame entry (log 5423: `cap96_replace_registers applied at
0x...965DD1B04: E9 4C F4 -> 90 90 90` — the NATIVE bytes are `E9 4C F4`); cap-92
(log 5636) and comp-20 (log 5750) then see `90 90 90 -> 90 90 90` only because
the earlier fixture already NOP'd the shared site. Asserting `== 90 90 90`
couples the test to apply ORDER + the cap-96 fixture + the build's entry
instruction. **Assert the build-stable, order-independent fact instead:**
`:applied()==true` AND `wrote_bytes == byte_range_len` against `function_entry`
resolving to idx0/kind=assign/brl=3. NEVER assert the pre-write bytes.

**Re-frame REVERSED:**
- `comp-20-declarer-statement` + `cap-92-replace-with-registers`: `:applied()==true`
  is now a PASS (with `wrote_bytes==byte_range_len` against the resolved range);
  degraded `function_no_statements` / Pending stay as HONEST FALLBACK arms (a
  future build that doesn't deploy SaveGame statements), NOT the expected arm.
- `cap-92-kind-mismatch` / `-deferred-op` / `-zero-dispatch`: unchanged
  (reject/static-op structural properties, no apply dependency).
- Matrix: the falsifiable claim is `:applied()==true with wrote_bytes==byte_range_len
  against the resolved function_entry`; strike the "deploy-state-DEGRADED is
  expected" framing.

**Safety settled:** the SaveGame `function_entry` NOP is harmless in-suite
(identity re-write of an already-NOP'd site) and even standalone is a 3-byte NOP
at the entry of a function the suite is not calling during the test window. No
foot-gun. Both fixtures stay PERMANENT — the live-apply assertion is the
STRONGEST regression (guards the actual write path end-to-end, the never-applied
defect class). NOT an exempt.

**The future-TD live-byte-write item is DROPPED** — that proof now exists (this
is it). Nothing to defer.

## What this report does NOT do

- Does not propose a fix.
- Does not assign root cause beyond labeled hypothesis.
- Closure handled by `/debug KI-0017` (which lands the fix and closes per
  doc-organization.md).
