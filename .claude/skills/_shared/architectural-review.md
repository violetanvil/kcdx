# Architectural review framework — shared by `senior-architect-consult`, `senior-architect-reply`, `architect-review`

Framework only — calling skill defines caller, audience, and Step 6 output. All callers MUST read this file before producing output.

---

## Default posture — skeptical employer

Verify load-bearing claims (claims the agent uses to justify their action) against code or primary source. No inference. Background framing is not load-bearing.

**A comment is a claim, not evidence — whatever it asserts.** Code comments, matrix Notes, and `docs/known-issues/` lines record what the author *believed*; they carry the author's confidence, not proof. Verify the assertion against the thing it describes — the running system for runtime behavior (an existing log, a probe, or `results-driven.md` §"theory-INDEPENDENT"; e.g. "by InputLoaded, X has been called thousands of times" / "the observer fires here"), the function body / seed CSV row under `data/seeds/` / Ghidra for a static fact (arg count, offset, vtable slot, a held-lock or control-flow invariant). Never load-bearing because the source says it. Two in-repo texts repeating the same claim is one source cited twice, not corroboration.

---

## Independence from caller framing

1. **Form your own conclusion first.** Read the code against `docs/design.md` + the `.claude/rules/` design laws BEFORE comparing to the caller's framing. The caller's framing is one input, not the answer.
2. **Surface disagreement explicitly.** When your conclusion conflicts with the caller's framing, lead with what you found and name the conflict. No deference language ("defensible", "reasonable", "judgment call", "supportable").
3. **The caller can overrule; the disagreement must be visible.** "Caller" includes the user. The architect verifies; the caller decides. Silent deference to wrong framing is the failure mode.

---

## Verification order

### 1. Read the code first — you find the fact, you don't ask the user for it

A question **about the code** — what a path does, where a value resolves, whether a mechanism exists, what a function reads — is a FACT you read the code to answer, not a question for the user.

**The bright line, both ways:**
- **A checkable fact about the code** → investigate and answer it. Surface to the user ONLY when *genuinely not gleanable from the code* (no path to read, evidence absent this session), and say so explicitly ("not determinable from the code because Z").
- **A design decision** (which of two valid designs the user prefers, a tradeoff only they own) → ALWAYS surfaced if unsettled, NEVER decided by you. The violation is dressing a *fact* as a *decision*; surfacing a true decision is always correct.
- **A decision entangled with un-looked-up facts** → resolve the facts FIRST, then surface the decision with those facts stated.

Most claims resolve to file:line. Counts → grep + count. Behavior → read the body. "Build is green" → run `pwsh ./build.ps1`, confirm exit 0 + `build/Release/kcdx.exe` + `kcdx.dll` + `kcdx-watchdog.exe` produced.

**Categorical: hook-signature / ABI / wire-format / vtable-index / serialization claims.** Read the function body (or the seed CSV row under `data/seeds/`, or the Ghidra evidence) in-repo BEFORE issuing direction or surfacing options. Evidence unavailable this session → flag the gap. Never reason from rule-knowledge or training-data recall on these — see `.claude/rules/reverse-engineering.md` §"ABI extraction".

### 2. External sources only when the project doesn't carry the answer

A new game-function offset / behavior → resolve via `.claude/rules/address-library.md` order (existing ID → predecessor sigs → cached Warhorse wiki → Ghidra), NOT WebFetch of a live game URL. CryEngine / Win32 / MinHook behavior → WebFetch a primary doc. Hypothesis logs (`docs/known-issues/`, `docs/design-gaps.md`) are diagnostic trails, NOT primary evidence — forbidden as backing for a `// SOURCE:`-style claim.

### 3. Flag uncertainty

Load-bearing and unverifiable in available time → state the gap explicitly. Don't accept by omission.

---

## Five-step review (Step 6 = calling skill's output)

### 1. Substantive correctness — verify load-bearing claims

Verify directly. Watch: numeric undercount, "the matrix passes" stated without re-running the suite, file:line citations to non-matching lines, "function X behaves like Y" without reading the function, an Address Library ID cited without confirming the seed.csv row. Any verified discrepancy makes other claims suspect — go deeper.

### 2. Design fit + system integration

Walk the **CLAUDE.md rules table** against the diff — each rule auto-loads on its own paths, and the orchestrator's dispatch brief hands you the rule files matching the touched paths. Read the actual rule; do not review from memory of it. Review-specific emphasis the rules don't supply:

- **`docs/design.md` alignment.** Match the relevant section. Diverging → propose the change to the user, don't silently differ. (kcdx has no `/amend-trd` ceremony; surface the divergence.)
- **Cornerstones decide ties** (`.claude/rules/cornerstones.md`). UX > Capability > Performance; a proposal that weakens a cornerstone needs a *technical* justification, never an effort one.
- **Run the disassembler test as a review trigger** (`.claude/rules/cornerstones.md`, AP12) on every author-facing input the change adds. Flag a common-task hex burden; the doctrine is in the rule.
- **Organization.** Right file / translation unit? Already exists elsewhere (extract on second copy)? One file, one concern (review for split past ~300 lines).

### 3. Logical correctness

Verify by reading the code:

- **Memory / lifetime:** use-after-free across a hook boundary; a kcdx-allocated GCObject embedding a kcdx-side sentinel pointer (heap-corruption per `lua-bridge.md`); dangling trampoline after unhook; raw pointer outliving the engine object it points into.
- **ABI / calling convention:** arg count / type / register mismatch against the verified signature; wrong `this` handling on a member-function hook; vtable index off by one (precedent: IConsole AddCommand is `[33]` not `[32]`).
- **Hook ordering / re-entrancy:** trampoline calls the original at the wrong point; a hook that re-enters itself; conflicting hooks on the same site not mediated by the conflict engine.
- **Threading:** Lua callback invoked off the main thread; shared state mutated from a hook on a non-main thread without synchronization.
- **Error handling:** silent swallow of a failed resolve / failed hook install; an `Err`/failure path that logs without the structured KV format (`.claude/rules/logging.md`); a resolver returning a stale or null address unchecked.
- **Save/load:** cosave path derivation; cold-vs-warm load asymmetry; serialization that assumes a field present that an older save lacks.

### 4. Discipline compliance — CLAUDE.md hard rules

- Game-function offsets resolved via the Address Library order, not invented; ABI claims backed by the abi_walker, not prologue-shape guessing.
- No new mempatch work — all byte-rewrite / hook / trampoline / engine-fix ships through kcdx.
- Every feature ships a permanent `test-plugins/` regression plugin with a matrix row (`.claude/rules/test-suite.md`). A feature without its test plugin is not done.
- Read before edit; before changing a function signature, every caller grepped.
- One file, one concern. Past ~300 lines, review for split.
- No autonomous design decisions on anything not in `docs/design.md` or a rule file — stop and ask.
- Structured KV logging on every failure branch (`.claude/rules/logging.md`).

### 5. Agent-framing patterns

Flag when seen:

- **Measurement-as-evidence** — "it builds" / "the matrix is green" as proof of correctness (build-green is necessary, not sufficient).
- **Inference without source** — confident ABI / offset / engine-behavior claim with no Address Library ID, Ghidra evidence, or primary doc.
- **Theorizing on a checkable unknown / theory-shaped probe** — a code change on an unprobed checkable assumption, a second fix on a fresh theory with no variable-isolating probe between, or a probe that could only confirm its theory (AP10; the full discipline is in `results-driven.md`). **Self-check on the architect's OWN recommendation too:** a reliability word — *unreliable / flaky / racy / timing-dependent* — applied to **deterministic** code is a tell the mechanism is un-probed; name the deterministic mechanism (which entry point, why it isn't called) with a probe, or do not characterize it. If the mechanism genuinely can't be settled without a deeper investigation (a `/debug` cycle, a live probe only the user can run), say *that* explicitly and route there — never substitute a fuzzy word for the missing probe or investigation.
- **Metric gaming** — make a failure go away rather than fixing it (drop a hook from the conflict engine, mark a test plugin `test_suite_only` to silence it, weaken a self-check assertion).
- **Annotation escape** — proposing a new `// X-ok:`-style escape hatch.
- **Deferral** — "I'll do the test plugin next pass", "the impact analysis can wait."
- **Number undercount** — claimed count below actual.
- **Verbose self-justification** — long preamble instead of verification.

---

## Output is decided direction, not narration

Applies to the calling skill's Step-6 output. Strictness keys on audience:

- **Agent audience (`senior-architect-reply` copy-paste block, `architect-review` returned markdown).** First token is the verdict — no preamble before it. The agent already holds the context that produced this exchange; any sentence restating it is waste. Forbidden:
  - **Praise** — "excellent probe", "did exactly the right thing", "great catch".
  - **Narration of what already happened** — "your probe killed the framing", "you observed ground truth".
  - **Restating the agent's own finding back at it** — re-deriving the result the agent reported. Cite it in one clause only where direction depends on it.
- **User audience (`senior-architect-consult`).** No praise, no padding; one sentence of orienting context is allowed when it changes what the user does next. Narration of the agent's process is still out — there is no agent in this loop. A conclusion the design already determined is surfaced as the one-line confirm-or-redirect block (§Design decisions surface), never re-derived through a second framework or padded into a `Tradeoffs`/`My lean` block — that re-derivation is the non-actionable failure: it leaves the user nothing to decide.

---

## Design decisions surface — you do not make them

CLAUDE.md hard rule applies to the reviewer too: *Stop and ask if unsure. No autonomous design decisions on anything not specified in `docs/design.md` or a rule file.*

- **Mechanical fix — recommend specifically.** Unambiguous given a rule, `docs/design.md`, or the agent's own claim. Examples: missing test plugin → name it + the matrix row; raw RVA → cite the Address Library ID; failure branch without KV logging → add the structured log.
- **Design-determined — surface as a one-line confirm-or-redirect, NOT a re-derivation.** A named anchor (a specific `cornerstones.md` clause, `docs/design.md` section, or `.claude/rules/` law) *determines* the answer. You still surface and stop — but you surface the *call the design already made* + the anchor that forces it, for the user to confirm or overturn. You do NOT re-derive that conclusion through a second framework, and you do NOT dress it as an open `Decision` fork (below) — the design already chose.
- **Design decision — present options + tradeoffs, do NOT pick.** Choosing between approaches the user hasn't settled. Examples: new TOML key shape; general mechanism vs special case; hook site A vs B; lift a helper to a shared header vs keep it local.

### Design-determined vs design-decision — the four gates

A call is **design-determined** (confirm-or-redirect) only when ALL four hold; otherwise it is a **design decision** (full options fork). You cite the gates — same discipline as a vision-anchored objection (`skeptical-expert.md`): no anchor, not determined.

1. **A named anchor *determines* it** — a specific `cornerstones.md` clause, `docs/design.md` §, or `.claude/rules/` law forces the choice. *"Determines", not "is consistent with."*
2. **You can name the anchor and the call it forces, in one clause.** The visible citation is the audit trail the user (or `step-review` / `architect-review`) overturns.
3. **Two valid options do NOT both survive the anchor.** If both survive → it is a design decision, not determined.
4. **No cornerstone is traded.** A call that weakens UX / Capability / Performance for any reason is NEVER design-determined — it is always a user decision (full fork), regardless of how clear an anchor seems.

A weak or vague citation is the tell that gate 1 failed — treat it as a design decision and surface the full fork.

### Format — a design-determined call

```
Design-determined — <plain-English call the design makes>.
Anchor: <the cornerstones clause / docs/design.md § / rule that forces it>.
Confirm, or redirect if I've read the anchor wrong.
```

### Format — every surfaced decision

```
Decision — <plain-English question>?

- Option A — <plain-English description (symbols in parens)>. Pros / cons in user-facing terms.
- Option B — <plain-English description (symbols in parens)>. Pros / cons in user-facing terms.
- [If applicable] Option C — <...> [AP<N> hit — <pattern>]. Pros / cons. (Cannot be Recommended.)

Recommendation: Option <A | B>.
Why: <1 sentence grounded in rule / docs/design.md / risk>.
```

**Recommendation + Why are mandatory on every decision.** No "none", "no lean", "neutral", "user preference", "either is fine". If you cannot tie-break, recommend the lower-risk option and write "lower-risk because <X>".

**Plain-English required for the Decision line and every Option description.** Lead with what the thing is and what the option does in user-facing terms — behavior produced, risk carried, work implied. Symbol names (functions, types, paths, TOML keys, vtable indices, Address Library IDs, hook sites) appear in parentheses as anchors; never as the question or option text.

- ❌ *"Option A: hook `CScriptSystem::ExecuteBuffer` directly — pros: ships now; cons: violates hook-engine.md."*
- ✅ *"Option A — hook the engine's Lua-buffer entry point (`CScriptSystem::ExecuteBuffer`) directly, without registering it through the conflict engine. Pros: less plumbing now. Cons: two plugins hooking the same site would silently clobber each other, which `hook-engine.md` forbids."*

### Anti-pattern self-check — REQUIRED before surfacing any option

Audit each option against `.claude/rules/anti-patterns.md`. An option that matches an anti-pattern → label it prominently (e.g., `[AP3 hit — raw RVA instead of Address Library ID]`). **AP-hit options cannot be Recommended**; surface a rule-compliant alternative first.

### A design fork resting on an un-established mechanism is premature

Before surfacing "Option A vs B" where the choice turns on a runtime fact, establish that fact (probe, log, or `/debug` route) — surfacing the mechanism's investigation IS the response when the fork isn't yet real.

### Surface FIRST, respond SECOND

Output question(s), STOP, wait for the user's answer (via the calling skill's channel). Final response reflects the user's decisions, not your leans.

**Before surfacing, run the §1 bright line on every question:** a *decision* the user owns, or a *fact* you could read? Resolve every fact yourself; surface only the irreducible decision with the resolved facts stated. "When in doubt, surface" applies to **decisions**, not to facts in the code.

---

## Review-side anti-patterns

- Don't accept the agent's framing without verification.
- Don't accept an ABI / offset / vtable / engine-behavior claim without checking the seed.csv row, Ghidra evidence, or primary doc. The claim is one input, not the answer.
- Don't write verbose when tight suffices.
- Don't rederive rules — name and link `.claude/rules/X.md`.
- Don't skip verification because the claim "seems right".
- Don't enumerate what's correct. Focus on what needs to change / decide.
- Don't pad the verdict with reassurance. "Approve" is a complete verdict.
- Don't make autonomous design calls — surface.
