# KI-0028 — Vanilla init + filesystem end-to-end map, and the kcdx diff (DIRECTION)

**Status:** PLAN ONLY — not started. This doc is the orchestration spec.
**Date:** 2026-06-22.

## Why this exists — the methodology change

26+ probes and 50+ revisions have falsified every concrete file-serve hypothesis
one variable at a time, at a snail's pace, without isolating the KI-0028 cause.
The probe-by-probe crawl is retired. New method:

> **Map the ENTIRE vanilla engine init + filesystem path end-to-end from the
> binary. Document it. Then diff kcdx's implementation against it, surfacing
> EVERY point where kcdx does anything even remotely differently.**

The cause is a side effect of kcdx's slot logic that is invisible in file-level
outputs (every byte kcdx serves is correct; every directory it enumerates is a
superset; existence/size answers are benign). So the cause is a DEVIATION in
HOW kcdx participates in init/FS, not WHAT it returns. A complete vanilla
reference map + a structural diff is the only thing that surfaces a HOW-deviation
the output-level probes are blind to.

## The load-bearing established facts (do NOT re-probe — inputs to the diff)

These are settled this session; the orchestration treats them as given, not as
open questions to re-investigate (token discipline — no re-tread):

- **The swap MECHANISM is innocent.** A no-op swap (kcdx vtable installed, every
  slot THUNKS to the engine original) loads the level → menu. (`PROBE-Z`.)
  ∴ the cause is in kcdx's slot LOGIC, not the vtable-pointer overwrite/timing.
- **kcdx serves every byte correctly.** Zero `index-pak` opens return result=0;
  every served file gets a valid handle. (PROBE W + the differential subagent.)
- **kcdx enumerates as a strict SUPERSET of vanilla.** Zero real drops across 190
  walks; the only divergences are kcdx-adds (synthetic dirs + alias-fold).
  (`PROBE Y` / `ENUM-DIFFERENTIAL-RESULT.md`.)
- **The existence over-report (`IsFileExist kcdx=1/vanilla=0`) is BENIGN.** kcdx
  says exists, then SERVES the file fine (result≠0). Falsified as a cause twice.
- **The level-load abort is the bug.** kutnohorsko auto-loads at boot (the menu is
  staged in-level); a full swap aborts the load in `C_Game::CreateInstance` →
  `MessageBoxA` ("level can't be loaded"). No kcdx frames on the abort stack.
- **`resourcelist.txt` misses are BENIGN** — those files exist nowhere (not loose,
  not in any level pak); vanilla misses them too.

The abort is a DELIBERATE engine decision (a tested condition → MessageBox), not a
crash or a failed serve. The diff must find the kcdx deviation that flips that
tested condition.

## Reuse-first — the existing RE corpus (MAP these BEFORE any fresh disassembly)

The reuse-first ladder (`reverse-engineering.md`) is MANDATORY — fresh Ghidra is
the LAST tier. A large prior corpus already maps much of the FS path; the fronts
CONSUME it and fill gaps, never re-disassemble what is already on disk. Known
high-value priors (Phase 1 inventories the full set):

- `phase8.5-pak-resolver/` (~20 docs) — the open/mount/resolve/read path:
  `front1-full-vtable-surface.md` (every CCryPak slot's role), `front2-open-mount-archive.md`,
  `front3-handle-consume-read-path.md`, `front4_resolution_decision_tree.md`,
  `RESOLUTION-OWNERSHIP-synthesis.md`, `MECHANISM-CONFIRMED-pakpriority-loose.md`,
  `subresolver-decompiled-mechanism.md`, `searchpath-registrar-mechanism2.md`.
- `init-cycle-recon/FINDINGS.md` — the init/construction cycle.
- `fs-takeover-pak-mount-recon/`, `fs-takeover-readslot-abi-recon/`,
  `fs-takeover-slot35-recon/`, `fs-takeover-slot101-callers-recon/` — slot ABIs + callers.
- `ki0027-find-data-abi-recon/`, `ki0028-findfirst-straddle-recon/` — find-data ABI +
  FindFirst/Next/Close contract (the producer return/exhaust contract is settled).
- `ki0028-metadata-consumer-recon/FINDINGS.md` — the 44 metadata-slot consumers
  (pCryPak global 0x18492B850; the byte-scan+correlate xref instrument).
- `probe-archive/p1-ccrypak-construction-order.md`, `p5-subsystem-init-vs-boot-open-ordering.md`,
  `vanilla-pak-format-confirmed.md` — construction order + pak format.

## Scope discipline — what to map, what NOT to (token-aware)

**IN scope (the critical path to the level-load abort):**
1. Boot/init sequence up to and including the level load — `CSystem::Init` →
   subsystem init order → CCryPak construct/seat → pak mount → level-load entry.
2. Pak discovery + mount + search-path/pakPriority registration — esp. the
   `data/levels/<level>/` mount path the level load drives.
3. File resolution (AdjustFileName) → open → handle lifecycle — the vanilla path
   kcdx replaced, slot by slot.
4. The level load itself — `C_Game::CreateInstance`'s FS-driving inner path and
   THE TESTED CONDITION that gates the "level can't be loaded" abort. **This is
   the apex — every front feeds the question "what does the abort test, and which
   kcdx deviation flips it."**
5. The CCryPak vtable surface — each slot's vanilla body behavior (mostly already
   in `front1-full-vtable-surface.md`; confirm + fill).

**OUT of scope (do NOT spend tokens here):**
- Rendering / PSO / swapchain / present internals — the abort is upstream of
  render (no level → no render); render is a DOWNSTREAM symptom, already chased.
- Re-confirming the settled facts above (swap mechanism, serve correctness, enum
  superset, existence-benign, find-data ABI, pak format).
- Lua / scripting / save-load / console / hooks — not on the boot→level-load FS path.
- Any slot kcdx leaves THUNK that is not touched during boot→level-load.
- Mod/overlay precedence beyond what the level-load path actually exercises.

**Stop-digging rule (every front):** a front maps its region to the depth the
level-load-abort question needs, then STOPS. No exhaustive decompile of a function
whose role is already clear from a prior dump or its callers. Reuse a prior
finding rather than re-reading the body. If a body is not load-bearing for "what
flips the abort," note its role in one line and move on.

## The orchestration — phased, elegant, minimal fan-out

Run as a Workflow (deterministic phases; parallel only where genuinely
independent). Each disassembly agent uses the `research-disassembly` discipline
(reuse-ladder, AP19 — every call-edge read in the OWNING body, honest-uncertainty
over invention). Raw dumps to disk under this dir; only DIGESTS return to the
orchestrator (context economy — the volume stays in the subagent's window).

### Phase 0 — Inventory (1 agent, cheap, reuse-first)
ONE read-only agent reads the existing `_research/` corpus (the list above + a
sweep for more) and returns a COVERAGE MAP: for each IN-scope region (1–5), what
is ALREADY mapped (cite the doc + the settled fact) vs. the GAP that needs fresh
work. Output: a gap list that scopes Phase 1 so no front re-treads. **This phase
alone may answer large parts of the map from disk — fronts only fill gaps.**

### Phase 1 — Parallel disassembly fronts (≤5 agents, gap-scoped)
One front PER IN-scope region, but ONLY for the gaps Phase 0 found (a region
already fully mapped on disk spawns NO front — it goes straight to synthesis from
the prior docs). Each front:
- Consumes the relevant prior docs FIRST (cite them; do not re-derive).
- Reads only the bodies needed to close its gap, grounded per AP19.
- Returns a structured digest: the ordered vanilla steps in its region, each with
  its evidence (body RVA / prior-doc cite), and any call-edge it could NOT verify
  marked unverified. Raw decompiles to disk.
Model tiering: the fronts are comprehension-heavy disassembly reads → inherit the
session model (NOT downgraded — they make load-bearing judgments). The Phase 0
inventory is a digest sweep → may run cheaper.

### Phase 2 — Synthesis: the end-to-end vanilla map (1 agent)
ONE synthesizer assembles the fronts' digests + the prior docs into the single
ordered VANILLA INIT+FS MAP (boot → init → CCryPak seat → pak mount → resolve/open
→ level load → the abort's tested condition). It RE-GROUNDS each load-bearing
step + every cross-front call-edge in the owning body before asserting it
(AP19 / the synthesis re-grounds, it does not stitch). Output: `VANILLA-MAP.md`.

### Phase 3 — The kcdx diff (1 agent, the deliverable)
ONE agent lays kcdx's implementation (`src/fs_takeover/` — the slot table, the
seat `seating_hook.cpp`, the index build `asset_index.cpp`, the resolve
`open_slots.cpp`, the metadata/enum/find slots, the handle rep `file_handle.cpp`)
against `VANILLA-MAP.md` and produces `KCDX-DIFF.md`: a step-by-step table where,
for every vanilla step, it states what kcdx does — IDENTICAL / THUNKS-to-original /
DIFFERENT (and exactly how). Every DIFFERENT row is ranked by likelihood of
perturbing the level-load abort (handle-representation, ordering/timing, mount
participation, a state mutation a slot makes, a missing side effect the vanilla
body had that kcdx's reimplementation dropped). The apex output: the ranked list
of kcdx deviations that could flip the abort's tested condition.

### Phase 4 — Gate + report (orchestrator)
The orchestrator reviews `KCDX-DIFF.md` against the settled facts (a "deviation"
that contradicts an established fact is a synthesis error, not a finding), then
surfaces to the user: the top-ranked deviations as candidate causes, each with the
vanilla-vs-kcdx evidence, for the user to pick the one to probe/fix FIRST. No fix
is authored autonomously (`design-authority.md`).

## Token budget — the discipline

- **Reuse over re-read:** Phase 0 exists precisely so no front re-disassembles what
  is on disk. A front that re-derives a settled prior finding is a defect.
- **Digests, not dumps:** raw decompiles stay on disk; only structured digests
  cross into the orchestrator/synthesizer window.
- **Gap-scoped fronts:** a fully-mapped region spawns no front. Expect FEWER than 5
  fronts if the corpus already covers (likely) most of the open/mount/resolve path.
- **Scoped depth:** each front maps to the abort-question's depth and stops; no
  exhaustive whole-function decompiles for roles already clear.
- **One apex question** orients every phase: "what does the level-load abort test,
  and which kcdx deviation flips it." A finding not serving that question is noted
  in one line, not pursued.

## Deliverables (all under `_research/ki0028-vanilla-init-fs-map/`)
- `COVERAGE-MAP.md` (Phase 0) — what's mapped vs gaps.
- `front-<region>.md` + raw `_*.txt` dumps (Phase 1) — per-region digests + evidence.
- `VANILLA-MAP.md` (Phase 2) — the end-to-end vanilla init+FS reference.
- `KCDX-DIFF.md` (Phase 3) — the side-by-side diff + ranked deviations (THE deliverable).
- A user-facing surface (Phase 4) — top candidate causes for the user to pick.

## What this does NOT do
- Does NOT fix anything (surfaces ranked candidates; the user picks).
- Does NOT re-probe the settled facts.
- Does NOT map render/PSO/Lua/save-load/console (out of scope).
- Does NOT start until the user approves THIS doc.
