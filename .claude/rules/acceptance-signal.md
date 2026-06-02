---
paths:
  - "**/*"
---

# Acceptance signal — a standard result line the agent reads; the user runs and reports

A cross-cutting law for every skill that hands a deliverable to the user for live acceptance (`/verification-checkpoint`, `/debug`'s fix-acceptance gate, any repo orchestrator's acceptance phase). When acceptance requires the user to run something the agent cannot run itself, the run emits a **standardized machine-readable result signal** the agent reads — the user's only job is to perform the irreducible gesture and say it ran. The signal GRAMMAR is fixed here; the repo names only WHERE the signal is written and HOW to read it.

## The canonical signal grammar — fixed system-wide, every repo emits it verbatim

An acceptance test that needs a live run self-reports its verdict as these exact line shapes, written to the repo's signal sink (below). The marker tokens are FIXED — an agent greps the same tokens in any repo:

- **Per-result line** — one per acceptance item:
  `ACCEPT-RESULT: <PASS|FAIL> <id> [— <detail>]`
  `<id>` is the item identifier (a test/matrix-row name); `<detail>` is optional free text (required on `FAIL`, naming what was observed vs. expected).
- **Aggregate line** — exactly one per run, last:
  `ACCEPT-SUITE: <passed>/<total> passing`

A run with zero `FAIL` lines and `ACCEPT-SUITE: N/N passing` is GREEN; any `ACCEPT-RESULT: FAIL` or a short aggregate denies acceptance. These tokens are not the repo's to rename. A repo that already emits a different format adapts it to also emit these lines (a thin adapter over its existing reporter), surfaced as migration drift by `/migrate-repo`.

## The contract — three obligations, every live-acceptance handoff

1. **The acceptance test emits the canonical signal to a known sink.** New functionality that needs a live run ships a test that self-reports via the grammar above into a location the agent knows ahead of time — the repo's signal sink (a log file / output stream / status file, named in the relevant append). A live run whose only evidence is the user's perception fails this obligation. (The signal is the runtime counterpart to `.claude/rules/test-discipline.md`'s same-change test bar and `.claude/rules/logging.md`'s every-failure-logged floor.)

2. **The agent reads the signal — the user never reads raw logs.** When the user signals the run is done ("done" / "review logs" / the repo's handoff word), the agent reads the sink itself, greps the `ACCEPT-RESULT` / `ACCEPT-SUITE` lines, and reports the verdict. Pasting a raw log for the user to interpret is the defect. The user performs the gesture; the agent owns retrieval and interpretation.

3. **The user-rendered procedure contains ONLY the irreducible gestures.** A numbered step list given to the user holds only the physical actions the user alone can perform — launch, navigate, the exact keystroke/command/gesture, "tell me it ran". Everything the agent can do — deploy, enable a test mode, read the signal — is the agent's job and NEVER appears as a user step. One concrete action per step, one observable per step; never "confirm it works".

## What this proves — every procedure states it

Each rendered procedure states, in plain English, **what the run proves and which signal lines the agent will read**: the falsifiable claim (one sentence — what the run confirms or denies) plus the specific `ACCEPT-RESULT <id>` line(s) that confirm or deny it. The user reads this to know why they are running; the agent reads it to know what to retrieve afterward.

## Token economy — keep acceptance retrieval bounded

Acceptance costs near-zero context: the user pastes nothing, and the agent retrieves the `ACCEPT-*` lines (a scoped grep for the fixed tokens), never the whole log. Reading a full log to find a verdict the canonical line already carries is the anti-pattern (`~/.claude/memory/context-token-economy.md`). The fixed grammar IS the budget mechanism — a single greppable token set means the read is always one scoped grep regardless of log size.

## When no signal is possible — the perceptual-only fallback

A purely perceptual outcome the user alone can judge (a visual glitch, a layout, an audio cue) has no machine-readable signal. There, the procedure states the exact expected observation and the user reports what they saw — eyeball-and-confirm. Use this fallback ONLY when no signal is constructible; a result that COULD self-report must emit the canonical signal (the contract above). A deliverable mixing both gets the signal for the assertable part and an eyeball step for the perceptual part.

## What this is NOT

- NOT the test bar itself (`.claude/rules/test-discipline.md`) — that owns "every item has a test, same change" at write time; this owns how a test's result reaches the user at acceptance time.
- NOT the runtime logging discipline (`.claude/rules/logging.md`) — that owns every-failure-logged / event-driven logging; this owns the acceptance-result signal a user-driven run surfaces. They meet where the signal sink is a log file.
- NOT the signal SINK or read recipe — those are the repo's (the relevant append); only the GRAMMAR is fixed here.
- NOT a license to skip the user's acceptance — the user still experiences the deliverable; this rule removes the log-reading from the user's job, not the acceptance.
