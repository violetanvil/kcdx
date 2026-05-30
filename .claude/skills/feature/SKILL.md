---
name: feature
description: Use this skill to build a new multi-part kcdx feature — a new [[...]] TOML primitive, a new kcdx.* Lua surface, a new C++ interface, a new engine behavior — anything whose delivery spans more than one commit (parser + engine + binding + test plugin). Runs an adaptive inline audit (collaborative discovery if the request is vague; vision-preserving if it's a specific spec), decomposes into ordered steps, then runs the manager-subagent orchestrator loop per step (build-gate + step-review + commit each), and produces one verification-checkpoint at the end. For a single-commit change (one bug fix, one refactor, one RE patch site) use /execute instead; for a trivial hand-edit use /commit.
---

# Feature — feature-scoped orchestrator for multi-part work

You are the manager for one feature (multiple commits; single-commit changes are `/execute`). Read `.claude/skills/_shared/orchestrator-loop.md` cover-to-cover before dispatching the first subagent. This file holds feature-specific entry (adaptive audit + decomposition), iteration (loop per step), exit (batched checkpoint).

`/feature` writes NO resolved-plan file. The audit happens in conversation; its decisions become the per-step `Resolved ambiguities` the shared loop injects.

---

## A. Audit — adaptive, runs once before any code

### A.1 — Gauge mode from the request's specificity

Read how the user phrased the feature. Pick a mode and **state which one you picked** in your first response (one line: *"Reading this as a <rough idea / specific spec> — <one clause why>."*) so the user can correct you.

- **Discovery mode** — the request is a rough idea ("add a way for plugins to react to quest events", "I want hot-reload"). Work WITH the user to understand the full context and flesh out edges they hadn't considered. Open-ended questions are welcome here.
- **Vision-preserving mode** — the request is a specific spec ("add `[[event]]` with `on`, `filter`, `callback` keys, resolved against the gEnv event bus, callback on main thread"). Do NOT re-litigate the design. Questions are scoped to (a) achieving that exact vision, or (b) a genuine unspecified decision the vision didn't cover, or (c) a real conflict with another project goal. Never "have you considered doing it differently" — the user already decided.

When in doubt between modes, lean vision-preserving and ask one clarifying question rather than running full discovery.

### A.2 — The audit walk

Walk these categories against the feature. Surface a question ONLY when there's a genuine unspecified decision or a real collision — not to demonstrate thoroughness. A category with no open question produces no question.

- **TOML / Lua / C++ surface shape** (`toml-schema.md`, `skse-parity.md`) — key names, arg forms, the general-mechanism-vs-special-case call (`cornerstones.md`: prefer the general). In vision-preserving mode, only if the spec left a key/arg undefined.
- **The disassembler test — author hex/ABI burden** (`cornerstones.md`, AP12). Run it on EVERY author-facing input the feature adds; flag any common-task field making the author supply an address/offset/register/instruction-length/hand-written signature. The doctrine (name → address AND ABI, expert-hatch labeling, surface-the-exception) is in `cornerstones.md`.
- **Hook / patch site + apply order** (`hook-engine.md`) — which site, conflict_engine footprint, priority. Real conflict to surface: does this overlap an existing hook?
- **Game-function evidence** (`address-library.md`, `reverse-engineering.md`) — does the feature need a new offset/ABI/vtable? If so, the resolution is an Address Library ID via the order (existing ID → predecessor sigs → wiki → Ghidra) + abi_walker for any new hook target. Surface if the evidence doesn't exist yet — that's a real gap, possibly its own first step.
- **Lua surface hazards** (`lua-bridge.md`, `lua-callback-threading.md`, `lua-precision.md`) — any new Lua surface: no new static-const sentinel; main-thread-only callbacks; float-precision pointer rules. Surface only if the feature's shape forces a decision here.
- **Save/load impact** — does the feature add a serialized field or touch the cosave path? If so, the cold-vs-warm and ABI concerns from the phase-6 work apply.
- **Project-goal conflicts** (`cornerstones.md`, `docs/design.md`) — does the feature, as specified, sacrifice UX/capability for effort, or contradict a `docs/design.md` section? Surface the conflict; don't silently differ — propose the `docs/design.md` change if one is needed.
- **Test-plugin plan** (`test-suite.md`) — name the `cap-NN`/`comp-NN` plugin(s) and what each auto-pass check verifies. Not usually a question — a statement the user can correct.

Surface forks using the Decision / Options / Recommendation / Why format from `.claude/skills/_shared/architectural-review.md` §Design decisions surface — plain-English, symbols in parens, Recommendation + Why mandatory, AP self-check on each option. Batch related questions; don't drip one at a time.

**Checkable engine-behavior unknown → a probe step, not a guess** (`results-driven.md`, AP10). A feature resting on an unverified runtime assumption (will this hook fire here? does the engine call this overload? does the offset resolve?) makes that assumption its OWN early step: a minimal probe with an outcome→meaning map, resolved before the dependent step is built.

### A.3 — Confirm scope + decomposition

Once the audit forks are resolved, present the feature plan and wait for a go:

```
Feature: <one sentence>
Mode: <discovery | vision-preserving>
Resolved decisions: <bullet list from the audit, or "none surfaced">

Steps (each its own commit):
  1. <step — crate/file scope> — test: <plugin/sub-test or "none yet; tested at step N">
  2. <step> — test: <...>
  ...
  N. <test plugin cap-NN + matrix row> — verifies: <behaviors>

Authorized scope (whole feature): <file paths>
Test plugin(s): <cap-NN/comp-NN names>

Confirm to start step 1, or refine the steps.
```

**Decomposition rules:**
- Order by dependency. A step that needs an offset/ABI that doesn't exist yet → resolving that evidence is step 1 (RE work), the consumer comes after.
- Each step ends in a buildable state (`pwsh ./build.ps1` clean) — never leave the build red between steps.
- The feature is not done until a `test-plugins/` plugin exercises it (AP7). The test plugin is its own step (usually last) unless an earlier step can already self-check.
- The feature is not done until its documentation moved with it (`docs-discipline.md`): the API-doc entry (Lua → `docs/lua/index.md`, C++ → `docs/cpp/index.md`) + any new glossary term + the parity row. Documentation is part of each surface step's deliverable, or its own ordered step — never a follow-up.
- Build only what this feature needs. No anticipatory structure for imagined future features.

On confirm → proceed to §B. On refine → revise, re-present.

---

## B. Run the orchestrator loop, per step

For each step in order, run one cycle of `.claude/skills/_shared/orchestrator-loop.md` §A–§E with these caller-injected parameters:

| Parameter | Feature source |
|---|---|
| **Step scope** | The step text from §A.3, verbatim. |
| **Reading list** | 1–3 load-bearing rule files for that step's path scope + the `docs/design.md` sections the feature cites. |
| **Authorized scope** | The step's files (subset of the feature's authorized scope). |
| **Per-step test bar** | The step's test plugin / sub-test, or "exercised at step N" if a later step owns the test. The final test-plugin step always names the `cap-NN` + matrix row. |
| **Resolved ambiguities** | The §A.2 decisions relevant to this step, verbatim. |
| **Touches-existing-code flag** | `true` if the step modifies a file in HEAD; triggers the inline impact-analysis (grep every caller). |
| **Source work-item** | The active plan-doc ledger row this step lands, as `<doc path> → <ledger step id>` — the cycle flips it to `DONE` + hash in the step's commit (`_shared/orchestrator-loop.md` §C item 3). A feature driven by no plan doc (decomposed in-conversation, tracked nowhere) → `none`; if the feature warrants a tracked plan, author it via `/plan` first (produces the tree + ledger), then run `/feature` against its step docs. |

The shared loop handles dispatch, build-gate (manager runs `pwsh ./build.ps1` itself), step-review, re-task-on-progress / escalate-on-stuck, and architect-review routing. After each step commits, emit the §F per-cycle report and proceed to the next step.

**Mid-feature design questions escalate to the user** per the shared loop §E — never decided autonomously, even if the audit "should have" caught them. A new fork surfacing mid-build is expected, not a failure.

---

## C. Batched checkpoint after the last step

After the final step commits, do NOT tell the user "done." Invoke `/verification-checkpoint`, which enumerates every behavior / failure path / integration point the feature introduced + a manual AP1–9 audit. The user walks the checklist, then does ONE game launch to confirm the test-suite matrix — batched once for the whole feature, not per step. Deploy + dev-mode enable + log read after the run are agent actions per `agent-builds-and-deploys.md`; the user only launches.

A rejected checklist item → identify the owning step, re-task it via the orchestrator loop (fix lands as a new follow-up commit, never an amend), re-present the checkpoint.

---

## Hard rules

Inherits all hard rules from `.claude/skills/_shared/orchestrator-loop.md`. Feature-specific additions:

- **Audit is adaptive, not a fixed checklist.** Surface a fork only on a genuine unspecified decision or a real project-goal conflict. In vision-preserving mode, never re-litigate a design the user already specified.
- **State the mode you picked** in the first response so the user can correct it.
- **Per-step commits, no squash.** Each step (parser / engine / binding / test plugin) is its own commit; revert/bisect/blame stay tight.
- **One game launch per feature, batched at the checkpoint** — not per step.
- **Manager never makes design decisions** — every fork goes to the user, audit-time or mid-build.

## What's out of scope

- A single-commit change (one bug fix, one refactor, one RE patch site, one outstanding-work item) → `/execute`.
- A trivial hand-edit → `/commit` direct.
- A pure design conversation with no code intent yet → `/senior-architect-consult` (then come back to `/feature` to build it).
- A hard bug whose cause isn't obvious → `/debug`.
