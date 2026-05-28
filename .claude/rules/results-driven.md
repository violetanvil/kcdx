---
paths:
  - "src/**"
  - "include/**"
  - "test-plugins/**"
  - "_research/**"
---

# Results-driven — test the unknown, don't theorize it

When the answer to a question is empirically checkable, **prove it with a test or probe BEFORE acting on inference.** Applies to all work — features, design, RE, refactors — not just crash investigation (the reactive case is `/debug`).

## What counts as a checkable unknown

Any question the running system, the binary, or a small experiment can answer:

- "Will this hook fire at this site?" → install it in a probe plugin, log on entry.
- "How many args / what ABI does this function take?" → walk the reuse ladder (`reverse-engineering.md`): Address Library row → prior `_research/` dumps → predecessor sigs → only then fresh abi_walker. Don't read the prologue and guess (AP2).
- "Does this Address Library ID resolve to the right code?" → resolve it in a probe, log the target + a fingerprint.
- "Does the game still boot / does cap-NN still pass?" → write the probe, build + deploy it, hand the user the launch, read the log (below).
- "Which call shape does the engine expect?" → a probe that distinguishes them, outcome→meaning map written first.
- "Does plugin A's hook clobber plugin B's at this address?" → a two-plugin comp-NN fixture, not reasoning about apply order.

Genuinely not checkable yet (no API path, no observable signal, no fixture)? State so explicitly ("not empirically checkable this session because X"); do not present a guess as a conclusion.

## The rule

1. **Hit an unknown → design a probe, don't theorize.** Trigger: about to write "this is likely because…" / "I think the engine…" as the basis for a code change → STOP, ask *is this checkable?*, probe first.

2. **Write the outcome→meaning map UP FRONT.** Before running:

   > Probe asks: *<question>*.
   > - Outcome A (`<observable>`) → means *<implication>* → next action *<X>*.
   > - Outcome B (`<observable>`) → means *<implication>* → next action *<Y>*.

   Where several hypotheses could each hold, log the variable AND its confounds so one run eliminates multiple branches.

3. **One variable per probe.** Changing N things → the outcome map must decompose into N attributable sub-outcomes. Can't decompose → split it on paper first.

4. **Static evidence before live probes.** `seed.csv`/`kEntries[]`, prior `_research/` dumps, predecessor sigs, Ghidra, an existing log, or a read-only in-process probe answers it → that is the test. Reach for a live launch only for runtime behavior no static source settles.

5. **Act on the result, not past it.** Do what the answer says; do not re-theorize beyond it. A new question = a new probe with its own map.

## The probe must be theory-INDEPENDENT

A probe shaped by the current theory can only confirm it, never kill it. Four disciplines:

1. **Observe ground truth FIRST.** Log the raw fact that holds regardless of theory — *the actual bytes / arg value / return register / value at stack index 1* — before any theory-shaped probe. Not "test whether it's the width."

2. **The probe must be able to FALSIFY the leading theory.** Design at least one outcome that *kills* it. Prefer the probe that discriminates: "is it X or Y?" (both answers eliminate a branch) over "does X happen?" (only "yes" informs).

3. **Pre-commit every outcome as equally real — no "expected" outcome.** Write `returns 110 → P; returns 0 → Q; returns other → R`, flat. Bias-language to avoid: "this *should* return 110", "STILL 0".

4. **On DISCONFIRMATION, re-observe — do not hop to the next theory.** A killed theory → return to direct ground-truth observation. Theory-hopping (A killed → B → C, each with a confirm-only probe) is the circling AP10 forbids.

> **Self-check before any probe:** *Is there an outcome that proves my current theory WRONG?* If no, it's theory-seeking — redesign. *Have I directly observed the raw fact, or am I testing a theory about a fact I never looked at?*

### Fresh-frame escalation

**First, the non-negotiable floor (independent of escalation): after ONE failed fix on a symptom, the next step is a variable-isolating probe — NOT fix #2 on a new theory.** That probe need not be fresh-frame; an in-line theory-independent probe (per the four disciplines above) satisfies it. Fresh-frame escalation is a *stronger* response layered on top, not the threshold at which the probe obligation begins.

A theory-independent probe debiases the result no matter who runs it; a subagent is not required and not automatically less biased. Dispatch a fresh-frame subagent only when EITHER holds:

- the self-check above fails and the agent can't redesign the probe to be falsifiable/ground-truth, OR
- the agent has hopped theories **2+ times on the same symptom** (the in-line probe after fix #1 did not settle it).

Brief (raw facts only):
- INCLUDE: the symptom, directly-observed facts (verbatim log lines / dumps, not interpretations), and **already-killed theories**.
- WITHHOLD: the current leading theory and its lean.
- ASK: *"Design the probe that most directly OBSERVES ground truth for this discrepancy. Assume no prior theory is correct. Your probe must have an outcome that falsifies any single explanation."*

Dispatch via `Agent`, `subagent_type: general-purpose`. Run the probe it designs (or hand the user the launch).

## Live-game unknowns — agent writes, builds, deploys; user launches; agent reads the log

kcdx's strongest tests are live launches. The user runs ONE thing: the launch. Everything else is the agent's (`agent-builds-and-deploys.md`).

1. **You write the probe code.** A minimal probe isolating the unknown — a `test-plugins/` probe plugin or a `// === DIAGNOSTIC (PROBE X)` site (`.claude/skills/debug/references/probe-patterns.md`). One variable, logged with `LOG_DEBUG_KV` under a stable category tag, no unrelated noise. Asking "should I write the probe?" is a FLOW defect — it is not a decision.
2. **You build and deploy the probe.** Run `pwsh ./build.ps1` yourself; confirm exit 0 + the three artifacts. Copy the rebuilt files to the live install per `loader-architecture.md` deploy mapping; hash-verify each copy. Enable dev mode (`<game-bin>/kcdx-engine/engine.toml` `dev_mode = true`) if not already on.
3. **Hand the user the launch with the outcome map**, one block: *"Probe `<X>` answers `<question>`. Launch (deploy is done; dev mode is on). Outcome A → do X; B → do Y. Tell me it ran and I'll read the log."*
4. **On run signal, read the log yourself.** Open the newest `<game-bin>/kcdx-engine/logs/kcdx-dev_<ts>.log`, grep the `<CATEGORY>` tag, report the outcome against the pre-committed map. Never ask the user to paste log lines. Don't propose the fix that "probably" follows — act on the result per §5 above ("Act on the result, not past it").

**Probe-revert hygiene (applies outside `/debug` too).** A `// === DIAGNOSTIC (PROBE X)` engine edit is reverted before the next probe unless the next probe explicitly builds on it — never stack two un-reverted probe sites (the `/debug` rule, `debug/SKILL.md` §2f). A diagnostic edit is never committed: it's reverted once its question is answered. Running a probe loop here without entering `/debug` does not exempt the edit from this — if the loop runs more than two probes or the investigation turns hard, switch to `/debug` for the active-instrumentation tracking.

## Triggers

- After ONE failed fix on a symptom → do NOT try fix #2 on a new theory; switch to a variable-isolating probe. Hard bug → `/debug`.
- A `/feature` step resting on an unverified engine assumption → surface it as a probe step, resolved before the dependent step.

Related: `.claude/skills/debug/SKILL.md`, `reverse-engineering.md` (abi_walker / Ghidra), `test-suite.md` (a capability-proving probe becomes its permanent regression plugin), `anti-patterns.md` AP10.
