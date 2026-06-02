# System-baseline reconciliation — `/migrate-repo` plan

**Status:** OPEN — migrate-owned phases (P1, P1d, P2, P3) ALL DONE. P4 (hook swap) + P5 routed to `/execute` (authoring/build work outside migrate's lane).
**Run:** `/setup-repo` → `/adapt-repo` (`b83dd66`) → `/migrate-repo` — P1 `32af8ef`+`e52ad3e`, P2 `04ec9a9`, P1d `72ab147`, P3 `6a92e27`. P4/P5 → `/execute`.

## Migrate close (the 4 migrate-owned phases)

All gated by governance-mode architect-review (PROCEED) with zero-content-loss verified per the user's primary constraint (make-new → compare-vs-old → remove):
- **P1** (`32af8ef`) — 24 system rules adopted verbatim + the probe-residue conflict resolved (no-residue model; CLAUDE.md + results-driven.md rewritten).
- **P1b** (`e52ad3e`) — known-issues index synced (8 open + 2 closed rows).
- **P2** (`04ec9a9`) — `docs/tech-debt/` tree + 3 refiles (TD-0001..0003) via lossless `git mv` (R100) + reference-fixup + carve-out sync ×3.
- **P1d** (`72ab147`) — 5 diverged rules merged toward the system body (public-private-boundary kept); zero kcdx-content loss verified old-vs-new; AP1–18 byte-preserved.
- **P3** (`6a92e27`) — 14 bespoke skills + 2 `_shared` fragments collision-removed; 16 appends authored + verified BEFORE removal; probe-patterns relocated lossless; system bodies confirmed present. Removals + preservation atomic in one commit.

## P4 — hand to `/execute` (hook-engine swap; NEW authoring, outside migrate's lane)

The 15 kcdx PowerShell `.ps1` guards → system Python `.py` engine. Per the reconcile-governance audit:
- **Drop (6 exact-twin `.ps1`):** guard-git-lock, guard-force-push, guard-destructive-ops, track-touched-files, tripwire-dynamic-deletion, guard-anti-pattern-consent — a system `~/.claude/hooks/*.py` twin already covers each (registered globally).
- **Fold (4 mechanism `.ps1`):** guard-write + guard-edit (≥300-line cap → system `guard-file-line-cap.py` + `.claude/hooks/line-cap.json`); guard-review-files (→ system `guard-immutable-docs.py`); guard-anti-patterns (AP-catalog delta → system `engine-detectors.py` + an AP detector JSON).
- **PORT (5 bespoke `.ps1` → repo-local `.py`):** guard-public-private-refs, guard-seed-approval, guard-probe-stack, guard-comment-density, guard-push-target — no system twin; each ported guard MUST be verified it actually FIRES (a silent no-op is the failure mode). Rewire `.claude/settings.json` registration from `pwsh -File *.ps1` to `python3 *.py`. Seed the system baseline detectors (`polling.json`, `concurrency-primitives.json`) tuned to C++/Lua.
  ⚠ A partially-rewired `settings.json` leaves guards unprotected — do the swap as one coherent `/execute` cycle (or a tight sequence), verifying each guard fires before dropping its `.ps1`.

## P5 — `/execute` (deferred source-logic work)

## Resolved decisions (§C sign-off)

- **F0 — Scope: FULL CONFORM** (collision-remove the bespoke skills, swap to the Python hook engine, adopt/merge rules, adopt a TD tree).
- **debug + anti-patterns:** conform ALL 14 skills (incl. debug); adopt system `anti-patterns.md` with kcdx APs merged (consent-gate applies).
- **Hook engine:** swap to system Python engine; drop the 6 exact-twin `.ps1`, fold the 4 mechanism `.ps1` into system config/engine, **re-home the 5 bespoke guards as repo-local `.py`** (public-private-refs, seed-approval, probe-stack, comment-density, push-target) — this is AUTHORING → `/execute`.
- **TD tree:** adopt `docs/tech-debt/` (private carve-out); refile the 3 clear ones as `TD-0001..0003` (declare-value-string-arena, lua-callback-main-thread-guard, engine-direct-hook-migration); route `deprecated-toml-token-cleanup` to `/execute` (deletion-hygiene sweep should land, not be carried).
- **Landing: SEQUENCED phased commits**, each its own `architect-review` + build gate.

## Phase plan

| Phase | Scope | Owner | Status |
|---|---|---|---|
| P1a | Adopt 24 missing system rules (verbatim, frontmatter preserved) | migrate | DONE (gate PROCEED) |
| P1b | Mechanical index fixes M1/M2 (known-issues README rows) | migrate | DONE (8 open + 2 closed rows added) |
| P1c | Probe-residue conflict: adopt system no-residue model → rewrote CLAUDE.md probe hard-rule + results-driven.md §"Probe leaves no residue" | migrate | DONE (gate PROCEED) |
| P1d | 6 diverged-rule merges: skeptical-expert (merged inline), public-private-boundary (KEPT), results-driven (merged, P1c no-residue intact), deletion-hygiene (merged), logging (merged), anti-patterns (merged — AP1–18 byte-identical, class-floor added) | migrate | DONE (gate PROCEED; zero kcdx-content loss verified old-vs-new; consent-gate fires on anti-patterns write) |
| P2 | TD tree + refile TD-0001..0003 + reference-fixup sweep | migrate | DONE (gate PROCEED; git mv R100 hash-identical; refs swept clean; carve-out synced ×3; delinked one test-plugins private ref — other pre-existing leaks remain) |
| P3 | 14 skill collision-removes (append-authored → dir-removed) | migrate | DONE (gate PROCEED; 16 appends authored + verified, probe-patterns relocated lossless, refs fixed, system bodies confirmed present — removals + preservation staged atomically) |

### P3 preservation contract (author ALL before any `git rm -r`)

The HIGH-risk loss targets (author these FIRST — they carry the bulk of kcdx substance):
1. **`.claude/repo/orchestrator-loop.append.md`** — build cmd (`pwsh ./build.ps1` + 3 artifacts), §C.6 deploy+`Get-FileHash`+dev-mode (all 3 plugin trees), plans root `docs/outstanding-work/`, AP1-18 + RE-evidence rule, §F game-launch + `suite: X/Y` read, `root-cause-verifier` as domain verifier.
2. **`.claude/repo/architectural-review.append.md`** — design anchor `docs/design.md`+cornerstones, RE evidence order (`data/seeds/`→predecessor sigs→wiki→Ghidra, NOT WebFetch), §2 hook-engine/conflict_engine/MinHook/AP4, §3 lua-bridge sentinel + IConsole vtable[33] + save/load asymmetry + KV logging, AP1-18 table, disassembler-test/cornerstones.
3. **`.claude/repo/debug.append.md`** — cdb.exe crash-dump recipe + watchdog crash-zip + BugSplat locations; consolidated probe methodology (6 pattern shapes + priority order, from probe-patterns.md); probe-Trail format + KI-section discipline (from known-issue-template.md); Gate-A design-surface threshold; provisional-mask policy.
4. **Relocate** `debug/references/probe-patterns.md` worked C++ skeletons → `docs/re-reference/probe-patterns.md` (lossless git mv; append points at it). `known-issue-template.md` → distilled into the debug append (its probe-Trail/section discipline) + the system report-bug/doc-organization shape covers the tree — then removed with debug/.

Thin per-skill appends (mostly template-covered): execute, feature(MED), code-review, commit(MED), governance-architect(SMALL — accepts system-layer scope broadening), plan(MED), report-bug(MED), architect-review, step-review, root-cause-verifier, senior-architect-consult, senior-architect-reply, verification-checkpoint(HEAVY).

Dangling-ref fixes: `results-driven.md:68` → new `docs/re-reference/probe-patterns.md`; the report-bug template pointer → doc-organization shape.
Sub-content: `code-review/main/86fcf46/` — leave in place (path survives in system code-review). The 2 `_shared` fragments swap to system bodies AFTER their appends are authored.
Decisions: KI template SUPERSEDED by system shape (verified — kcdx probe-Trail discipline → debug append); probe-patterns skeletons RELOCATED; governance-architect broadening ACCEPTED.
| P4 | Hook-engine swap: drop 6 twins, fold 4 mechanisms; **5 bespoke guard ports → `/execute`** | migrate + /execute | TODO |
| P5 (deferred → /execute) | `deprecated-toml-token-cleanup` deletion sweep; **migrate existing ~15 `#if 0` probe blocks → `_research/probe-archive/`** (per P1c decision) + sweep their stale references (`test-plugins/README.md:1071-1073`, the 2 KI trail mentions of "the probe-archive hygiene rule") to past-tense; `src/hooks.cpp` over-threshold | /execute | DEFERRED |

**P1a+P1c gate follow-ons (architect-review, non-blocking):** P3's `debug` collision-remove MUST update `.claude/skills/debug/SKILL.md` (lines ~98, ~297) off the `#if 0`-in-place model or it will contradict the adopted no-residue rules. P5 reference-fixup sweeps the test-plugins/KI trail `#if 0` mentions alongside the source-block migration.

**Probe-residue decision:** adopt the system no-residue model (working-artifacts.md stays verbatim); kcdx's `#if 0` archived-probe convention is overturned — CLAUDE.md + results-driven.md rewritten (P1c), existing blocks migrated out of source via `/execute` (P5).

kcdx is the repo the `~/.claude` system governance layer was largely **generalized from** (the system `rules/*.reasons.md` cite kcdx repeatedly). So this is NOT a "conform a fresh adopter" pass — it is reconciling a mature, deliberate pre-system fork against the floor it seeded. Several high-divergence items are legitimately **keep**; the auditors flagged this throughout. Nothing applies before the user signs off per item (`.claude/rules/design-authority.md`).

## Auditor verdicts

| Auditor | Verdict |
|---|---|
| reconcile-skill-references | CLEAN — every `/<name>` resolves to the live roster |
| reconcile-doc-prose | CLEAN — workflow narrative coherent with current skills |
| reconcile-structure | 2 mechanical index-sync fixes + 2 judgment items (probe-residue divergence, outstanding-work index framing) |
| reconcile-artifacts | 4 outstanding-work docs qualify as TD — gated on a "adopt a TD tree?" decision |
| reconcile-governance | whole-engine hook mismatch · 14 skill collisions · 6 diverged rules · ~24 missing system rules |

## Mechanical fixes (auto-apply on approval — listed for visibility)

| Item | Concern | Status | Commit |
|---|---|---|---|
| M1 — `docs/known-issues/README.md`: add index rows for 8 un-indexed open pre-KI files | structure | TODO | — |
| M2 — `docs/known-issues/README.md`: add index rows for 2 un-indexed closed files | structure | TODO | — |

## Judgment items (user's call — the §C decision surface)

| Item | Concern | Status | Commit |
|---|---|---|---|
| F0 — Framing: scope of this reconciliation (keep-fork vs full-conform vs adopt-floors-only) | meta | TODO | — |
| J1 — Hook engine: PowerShell `.ps1` (15 guards) vs system Python `.py` engine | governance | TODO | — |
| J2 — 14 bespoke skill collisions: collision-remove (append-preserve) vs keep-fork | governance | TODO | — |
| J3 — 6 diverged rule copies: adopt-system / keep-repo / merge | governance | TODO | — |
| J4 — ~24 missing system rules: adopt which | governance | TODO | — |
| J5 — `research-disassembly` repo-only skill: keep (recommended) vs convert | governance | TODO | — |
| J6 — Adopt a `docs/tech-debt/` tree + reclassify 4 outstanding-work docs to TD | artifacts | TODO | — |
| J7 — `#if 0` archived-probe residue vs system no-residue invariant (CLAUDE.md-sanctioned divergence) | structure | TODO | — |
| J7a — `src/hooks.cpp` over kcdx's OWN 2-probe migrate threshold (5 archived) → `/execute` | structure | TODO | — |
| J8 — `docs/outstanding-work/README.md` curated-subset vs exhaustive index | structure | TODO | — |

## Untouched (legitimately outside the formula / deliberately divergent)

- 17 repo-only rules (`cornerstones`, `lua-*`, `skse-parity`, `toml-schema`, `address-library`, `reverse-engineering`, `hook-engine`, `concurrency-git`, `agent-builds-and-deploys`, `test-suite`, etc.) — kcdx domain, keep.
- `.claude/repo/{design,tech-debt}.append.md` — already-correct flat-append specializations (landed `b83dd66`).
- Pre-KI-NNNN human-named known-issue files — README explicitly grandfathers them.
- Large by-responsibility source files — `no-monolith` is one-concern, not a hard line count; well-decomposed.
- Working-artifact trees with declared purpose (`_research/`, `data/seeds/`, `data/refdata-extractor/`, `test-fixtures/`, `third-party-ghidra/`).
- Tracked-blob-vs-gitignore axis: CLEAN.

## Apply order (loop §D, on approval)

artifacts (J6) before structure (M1/M2, J7/J8) · governance convert preservation-ordered · references/prose CLEAN (no rewrite pass) · then `architect-review` gate · then `pwsh ./build.ps1` (skipped if no code moved) · then `/commit`.
