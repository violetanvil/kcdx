# Headless-testability — behavior under test is reachable without an operator

Every implemented behavior is exercisable through a programmatic, non-UI seam that runs headless. Applies to any repo with a UI or operator-blocking surface; the repo names its concrete seam + acceptance command in the relevant append.

## Rules

- **Build the seam at design time.** A behavior is complete only when a test reaches it with no human at the controls. The seam (command/service/event entry point) is decided when the feature is designed, not retrofitted at test-write time.
- **No test path reaches an operator-blocking primitive** — a modal/dialog, `stdin` read, hardware action, key-press, credential prompt, interactive network endpoint. Lift the gated decision to a seam the test drives; the primitive becomes a thin shell over tested logic.
- **UI-only reachability is a design defect, not an exempt.** A behavior reachable only via UI/operator is bucket-1b (build the seam, then test), never a bucket-2/3 exempt. Surface the missing seam to the user (`.claude/rules/design-authority.md`).
- **Headless acceptance runs first.** Where a deliverable has an automated surface and a residual perceptual part, the headless run self-reports (`.claude/rules/acceptance-signal.md`) before the user's manual gesture.

## What this is NOT

- NOT the test bar (`.claude/rules/test-discipline.md`) — that owns the same-change test + buckets; this owns reachability. They meet at "missing seam = bucket-1b."
- NOT the acceptance-signal rule — that owns how a result reaches the user; this owns that the run be drivable headless.
- NOT the repo's concrete seam / UI-driver / acceptance command — the append's.
- NOT a ban on UI or manual acceptance — logic is reachable headless; a UI is a thin shell, a perceptual outcome still gets an eyeball.
