## Repo additions — tech-debt

- **Tech-debt tree path** — `docs/tech-debt/` (open TDs in the root, closed in `docs/tech-debt/closed/`,
  index README — per `.claude/rules/doc-organization.md`). No TD has been filed yet; the first
  `/tech-debt` creates the tree at this path.

- **Artifact prefix** — `TD` (the default).

- **Closure-gate vocabulary** — kcdx tracks deferred work against its **phase** plan and named
  engine fixes, so a TD's named blocker is phase- or fix-keyed: e.g. `phase-N` /
  `post-phase-N-acceptance` (the phase that unblocks it) or `FIX-A` / `Phase-11` (a named engine
  capability it waits on — the same vocabulary `docs/outstanding-work/` uses for revisit triggers).
  A vague blocker is refused (the load-bearing distinction from `/report-bug`).

- **Sibling trees** — kcdx files runtime defects with no known fix as `KI-NNNN` known-issues under
  `docs/known-issues/` (via `/report-bug` → `/debug`), and designed-but-deferred items under
  `docs/outstanding-work/`. A TD is deliberately-carried debt with a NAMED phase/fix blocker —
  distinct from both.
