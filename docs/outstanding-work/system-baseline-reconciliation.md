# System-baseline reconciliation — `/migrate-repo` plan

**Status:** OPEN — signed off; applying in phases.
**Run:** `/setup-repo` → `/adapt-repo` (landed `b83dd66`) → `/migrate-repo` (this pass).

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
| P1d | 6 diverged-rule merges: skeptical-expert (merge→append), public-private-boundary (keep-repo), results-driven (merge→append), deletion-hygiene (merge→append), logging (merge→append), anti-patterns (merge, CONSENT-GATED) | migrate | TODO |
| P2 | TD tree + refile TD-0001..0003 + reference-fixup sweep | migrate | TODO |
| P3 | 14 skill collision-removes (append-authored → dir-removed), each its own gated commit | migrate | TODO |
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
