# P3 step 2 — the shipped entries (5–10)

**What.** The launch catalog: 5–10 working behaviors, each backed by a verified
game-binary fact, each exercised by the suite.

**Scope.** STEP HEAD (user-decided deferral from plan time, plan-spec
§"User-decided deferrals"): survey the verified corpus (`data/db-export/` seed
rows + `_research/` findings, reuse-first per
`.claude/rules/reverse-engineering.md`) and surface the candidate entry list for
user sign-off — per-entry approval; any entry needing a NEW DB row goes through
AP18 (explicit user approval per row) and, where a fact is unverified,
`/research-disassembly` as its own evidence sub-task BEFORE the entry is
authored. Then: author each approved entry as its catalog `.lua` file with a
self-contained PUBLIC-SAFE header (the verified fact in its own words — no
`_research/` paths, no internal scheme tokens,
`.claude/rules/public-private-boundary.md`); the canonical case study
(`kcdx.behavior.outfit_swap_in_combat`) rides this set if its facts verify.
Doc completion: `docs/lua/index.md` gains the tiered author model paragraph
(behavior → hook → statement → bytes — the lineage in
[`../../00-original-plan.md`](../../00-original-plan.md) §"Phase 9.6"'s
index-lead text, brought forward here since behaviors complete the top tier);
glossary entries finalized.

**Test bar.** Per entry: declares cleanly + applies against the live binary —
its own self-reporting §14 row (effect-level where observable). Suite matrix
rows recorded; `[unverified — pending launch]` until the phase launch confirms.

**Dependencies.** P3 s1 (the loader); P1 complete (the machinery the entries'
implementations call); per-entry evidence tasks as surfaced at the step head.

**Reference.** [`../plan-spec.md`](../plan-spec.md) §"User-decided deferrals".

**Design authority.** [`../behavior-design.md`](../behavior-design.md) §7 (the
entry bar + public-safe headers), §1 (success criteria).

**Disassembler-test / author-burden.** Each entry is precisely the engine
absorbing hex ONCE (maintainer-side, verified) so every downstream author gets
a name — the catalog IS the disassembler test's payoff; any entry whose facts
aren't verified resolves evidence FIRST (its own sub-task), never ships a
hand-waved offset.
