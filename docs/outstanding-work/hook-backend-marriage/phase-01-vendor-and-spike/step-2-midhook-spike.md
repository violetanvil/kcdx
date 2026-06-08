# Step 2 — keystone spike: cap-04 on safetyhook::MidHook

**What.** The gate-everything spike. Port the cap-04 mid-hook test onto
`safetyhook::MidHook` behind a THROWAWAY seam (not the production
`make_jit_midfunc` replacement — a parallel path the spike exercises), and re-run
the cap-04 matrix live. This proves the design's load-bearing build-gated
unknowns **U1–U4** (`../context.md`) before Phase 3 rewrites the fragile
production code. **The whole Phase 3 rewrite is provisional on this spike's PASS**
(`.claude/rules/results-driven.md` — the `ctx.rip`→three-modes mapping is
read-feasible but UNOBSERVED; observe it before building on it).

**Scope (commit-grain).**
- Stand up a throwaway `safetyhook::MidHook` install for the cap-04 target(s),
  with a mid callback that maps the three call-original modes onto `Context64`:
  - **True** (CAP-04a/d-no-skip) — leave `ctx.rip` alone; safetyhook's trampoline
    runs the captured instruction → result 110.
  - **False** (CAP-04b) — set `ctx.rip = resume_addr` (past the captured
    instruction) → result 10.
  - **Auto+skip** (CAP-04c) — callback conditionally sets `ctx.rip = resume_addr`
    → result 10.
- **Outcome→meaning map (write BEFORE running, `results-driven.md`):**
  - CAP-04a=110 ∧ CAP-04b=10 ∧ CAP-04c=10 ∧ CAP-04d=110 → U1 confirmed
    (`ctx.rip` carries all three modes) → Phase 3 proceeds.
  - any of {CAP-04b, CAP-04c} = 110 → `ctx.rip = resume_addr` did NOT skip → U1
    FALSIFIED → STOP, surface to user; Phase 3 reconsidered (fallback: keep
    `make_jit_midfunc`).
  - a crash at the resume jump → resume_addr is wrong (U3 — safetyhook owns it
    differently than assumed) → re-observe, do not theory-hop.
- **Resolve U3 (resume_addr ownership):** observe whether safetyhook's MidHook
  exposes the past-the-instruction resume address, or kcdx still computes it
  (hde64 accumulate-to-≥5). Record which.
- **Resolve U4 (stack-expression capture coverage):** exercise at least one
  named-register capture AND, if cap-04 covers it, a stack-expression capture;
  confirm read + writeback land via `Context64`. A capture form with no
  `Context64` equivalent is a SURFACED finding (`../context.md` U4), not dropped.
- **Resolve U2 (trampoline-callable contract):** include an around-mode-shaped
  call-original through safetyhook's trampoline if reachable in the spike;
  confirm `.original()` derefs+calls cleanly from a JIT-thunk-shaped call.
- The spike code is a PROBE — it leaves NO residue in production source
  (`.claude/rules/working-artifacts.md`, `results-driven.md` §"Live-game
  unknowns"): capture the finding + the wiring recipe into
  `_research/<task-slug>-recon/` (or `_research/probe-archive/`), THEN remove the
  spike from source. The throwaway seam does NOT become the Phase 3 production
  path — Phase 3 builds the real adapter; this only PROVES the mechanism.

**Test bar.** The cap-04 matrix (CAP-04a/b/c/d) run live through the spike seam:
the agent builds + deploys + enables dev mode, the user launches once, the agent
reads `kcdx-dev.log` for the four CAP-04 rows against the pre-committed outcome
map (`agent-builds-and-deploys.md`). A FALSIFIABLE claim per row (CAP-04b FAILS
if it returns 110 — the original ran when it should have been skipped). This is
the probe's result, NOT a permanent regression row (cap-04 already exists; the
spike re-runs it through the alternate seam).

**Dependencies.** Step 1 (safetyhook must be vendored + building to install a
`MidHook`).

**Disassembler-test / author-burden note.** None — the spike is engine-internal
throwaway code; no author surface.

**Reference.** [`../context.md`](../context.md) U1–U4; design
[`hook-backend-marriage.md §5.1, §9.1–§9.4`](../../../design/hook-backend-marriage.md).
Prior cap-04 scar tissue:
`docs/known-issues/closed/cap-04 skip-original codegen does not skip the original instruction.md`.
