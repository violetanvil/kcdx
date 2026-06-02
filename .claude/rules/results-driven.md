---
paths:
  - "src/**"
  - "include/**"
  - "test-plugins/**"
  - "_research/**"
---

# Results-driven — a checkable unknown is probed, not theorized

When the answer to a question is empirically checkable, **prove it with a test or probe BEFORE acting on inference.** Applies to ALL work — features, design, RE, refactors, integration questions — proactively, not only after a failure. This is the always-on reasoning floor; the reactive case (a hard bug) is `.claude/skills/debug/SKILL.md`, and the build-time application (a `/feature` step resting on an unverified engine assumption) is the orchestrator's probe-step discipline. kcdx's concrete probe seam, static-evidence sources, and outcome-read recipe are below — this rule is the method.

## What counts as a checkable unknown

Any question the running system, the binary, an artifact on disk, or a small experiment can answer:

- "Will this hook fire at this site?" → install it in a probe plugin, log on entry.
- "How many args / what ABI does this function take?" → walk the reuse ladder (`reverse-engineering.md`): Address Library row → prior `_research/` dumps → predecessor sigs → only then fresh `abi_walker`. Don't read the prologue and guess (AP2).
- "Does this Address Library ID resolve to the right code?" → resolve it in a probe, log the target + a fingerprint.
- "Does the game still boot / does cap-NN still pass?" → write the probe, build + deploy it, hand the user the launch, read the log (below).
- "Which call shape does the engine expect?" → a probe that distinguishes them, outcome→meaning map written first.
- "Does plugin A's hook clobber plugin B's at this address?" → a two-plugin comp-NN fixture, not reasoning about apply order.

When about to write "this is likely because…" / "I think the engine…" as the **basis for a code change**, STOP and ask *is this checkable?* — if yes, probe first. Genuinely not checkable this session (no seam, no observable signal, no fixture)? State so explicitly ("not empirically checkable this session because X"); never present a guess as a conclusion (the skeptical-expert floor, `.claude/rules/skeptical-expert.md`).

## The method — five obligations

1. **Hit an unknown → design a probe, don't theorize.** A theory used as the basis for a code change is the trigger; convert it to a probe.
2. **Write the outcome→meaning map UP FRONT, before running.** For each possible observable: `outcome → what it means → next action`.

   > Probe asks: *<question>*.
   > - Outcome A (`<observable>`) → means *<implication>* → next action *<X>*.
   > - Outcome B (`<observable>`) → means *<implication>* → next action *<Y>*.

   Where several hypotheses could each hold, log the variable AND its confounds so one run eliminates multiple branches.
3. **One variable per probe.** Changing N things → the outcome map must decompose into N attributable sub-outcomes; if it cannot decompose, split the probe on paper first.
4. **Static evidence before a live probe.** The seed CSVs under `data/seeds/` (the reference-DB source), a prior `_research/` dump (`.claude/rules/working-artifacts.md` — reuse-first), predecessor sigs, Ghidra, an existing log, or a read-only in-process probe that settles it IS the test; reach for a live launch only for runtime behavior no static source answers.
5. **Act on the result, not past it.** Do what the answer says; do not re-theorize beyond it. A new question is a new probe with its own map.

## The probe must be theory-INDEPENDENT

A probe shaped by the current theory can only confirm it, never kill it. Four disciplines:

- **Observe ground truth FIRST** — log the raw fact that holds regardless of theory (the actual bytes / arg value / return register / value at stack index 1) before any theory-shaped probe. Not "test whether it's the width."
- **The probe must be able to FALSIFY the leading theory** — design at least one outcome that *kills* it. Prefer the discriminating probe ("is it X or Y?", both answers inform) over the confirming one ("does X happen?", only "yes" informs).
- **Pre-commit every outcome as equally real** — no "expected" outcome, no bias-language. Write `returns 110 → P; returns 0 → Q; returns other → R`, flat. Avoid: "this *should* return 110", "STILL 0".
- **On DISCONFIRMATION, re-observe — do not hop to the next theory.** A killed theory returns to direct ground-truth observation; theory-hopping (A killed → B → C, each with a confirm-only probe) is the circling the anti-pattern catalog forbids (`.claude/rules/anti-patterns.md` AP10).

> **Self-check before any probe:** *Is there an outcome that proves my current theory WRONG?* If no, it is theory-seeking — redesign. *Have I directly observed the raw fact, or am I testing a theory about a fact I never looked at?*

## The floor after a failed fix — probe, never fix #2

**After ONE failed fix on a symptom, the next step is a variable-isolating probe — NOT fix #2 on a new theory.** An in-line theory-independent probe (per the four disciplines above) satisfies this floor; a fresh-frame subagent is a *stronger* response layered on top, not the threshold at which the probe obligation begins.

A theory-independent probe debiases the result no matter who runs it; a subagent is not required and not automatically less biased. Escalate to a fresh-frame subagent only when EITHER holds: the self-check above cannot be made to pass (the agent can't redesign the probe to be falsifiable/ground-truth), OR theories have hopped **2+ times on the same symptom**.

Fresh-frame brief (raw facts only):
- INCLUDE: the symptom, directly-observed facts (verbatim log lines / dumps, not interpretations), and **already-killed theories**.
- WITHHOLD: the current leading theory and its lean.
- ASK: *"Design the probe that most directly OBSERVES ground truth for this discrepancy. Assume no prior theory is correct. Your probe must have an outcome that falsifies any single explanation."*

Dispatch via `Agent`, `subagent_type: general-purpose`. Run the probe it designs (or hand the user the launch). A hard bug → `.claude/skills/debug/SKILL.md`.

## Live-game unknowns — agent writes, builds, deploys; user launches; agent reads the log

kcdx's strongest tests are live launches. The user runs ONE thing: the launch. Everything else is the agent's (`agent-builds-and-deploys.md`).

1. **You write the probe code.** A minimal probe isolating the unknown — a `test-plugins/` probe plugin or a `// === DIAGNOSTIC (PROBE X)` site (worked probe-pattern skeletons: `docs/re-reference/probe-patterns.md`). One variable, logged with `LOG_DEBUG_KV` under a stable category tag, no unrelated noise. Asking "should I write the probe?" is a FLOW defect — it is not a decision.
2. **You build and deploy the probe.** Run `pwsh ./build.ps1` yourself; confirm exit 0 + the three artifacts. Copy the rebuilt files to the live install per `loader-architecture.md` deploy mapping; hash-verify each copy. Enable dev mode (`<game-bin>/kcdx-engine/engine.toml` `dev_mode = true`) if not already on.
3. **Hand the user the launch with the outcome map**, one block: *"Probe `<X>` answers `<question>`. Launch (deploy is done; dev mode is on). Outcome A → do X; B → do Y. Tell me it ran and I'll read the log."*
4. **On run signal, read the log yourself.** Open the newest `<game-bin>/kcdx-engine/logs/kcdx-dev_<ts>.log`, grep the `<CATEGORY>` tag, report the outcome against the pre-committed map. Never ask the user to paste log lines. Don't propose the fix that "probably" follows — act on the result per obligation 5 above.

**A probe leaves NO residue in live source (applies outside `/debug` too).** When a `// === DIAGNOSTIC (PROBE X)` probe's question is answered, capture its finding + reusable wiring (the instrumentation recipe / script) into the artifact tree (`_research/probe-archive/`) as durable process-output, THEN remove the probe from source — the live source returns to pure production logic: no dormant branch, no `#if 0` block, no commented-out corpse, no runtime-disabled flag (`.claude/rules/working-artifacts.md` §"A scratch probe leaves NO residue in live source"). A probe adds ZERO cost to live code; the next investigation reconstructs it from the artifact tree, never from source. Never stack two LIVE probe sites; `guard-probe-stack.ps1` enforces no-two-live. The captured finding IS commit-eligible (it ships alongside its known-issue's Resolution; only the in-source probe stays uncommitted, until it is captured-and-removed). Running a probe loop here without entering `/debug` does not exempt the capture-then-remove discipline — if the loop runs more than two probes or the investigation turns hard, switch to `/debug`.

## Triggers

- After ONE failed fix on a symptom → do NOT try fix #2 on a new theory; switch to a variable-isolating probe. Hard bug → `/debug`.
- A `/feature` step resting on an unverified engine assumption → surface it as a probe step, resolved before the dependent step.

## What this is NOT

- NOT the skeptical-expert posture (`.claude/rules/skeptical-expert.md`) — that owns the STANCE (don't guess, build-green is not proof); this owns the METHOD (the probe's design — outcome-map, one-variable, falsifiable, ground-truth-first).
- NOT the reactive investigation flow (`.claude/skills/debug/SKILL.md`) — that owns the tracked active-instrumentation cycle for a hard bug; this is the proactive floor for every unknown, including the first failed fix before `/debug` is entered.
- NOT the working-artifacts rule (`.claude/rules/working-artifacts.md`) — cited for reuse-first static evidence and for capturing a probe's finding + wiring without leaving residue in live source; this owns the decision to probe and the probe's shape.

Related: `.claude/skills/debug/SKILL.md`, `reverse-engineering.md` (`abi_walker` / Ghidra), `test-suite.md` (a capability-proving probe becomes its permanent regression plugin), `anti-patterns.md` AP10 (theorize-don't-probe) + AP17 (symptom-only fix).
