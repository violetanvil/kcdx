# Step 1.3 — probe P1: CCryPak construction timing

**What.** Establish WHEN the `CCryPak` object at `*(gEnv+0x50)` is constructed,
and whether kcdx's ready-bracket runs inside the window [CCryPak constructed,
first file call] — the window the vtable swap (step 1.4) must land in. The
init-cycle recon observed `C_ModManager` timing, a DIFFERENT object; CCryPak's
construction point is unobserved (design §8 P1). This is a probe step — its
observable IS its deliverable, no production behavior changes.

**Outcome (RESOLVED — outcome (c), 2026-06-15).** P1 was answered STATICALLY by
reading the WHGame.dll binary, NOT a live launch — static evidence precedes a live
probe (`.claude/rules/results-driven.md` §4). The live-launch probe in the Scope
below (an engine-side `PROBE_P1` marker + a `*(gEnv+0x50)` read at the
ready-bracket) was written, then SUPERSEDED by the static read and removed from
source (no residue). Outcome (c) held: inside `CSystem::Init`, the CCryPak object
is constructed + published to gEnv+0x50 (`CSystem_pCryPak_construct_store`, id
158, RVA `0x9B3C0C`, @ `0x1807A71CA`) BEFORE the first `*(gEnv+0x50)` file call
(@ `0x1807A723A`) AND BEFORE the `ModManager_ctor` ready-bracket (@ `0x1807A76FE`)
— so the swap seats at the construction site (id 158), NOT the ready-bracket
(step 1.4's anchor updated; design §4.1/§8 P1 updated). Capture:
[`_research/probe-archive/p1-ccrypak-construction-order.md`]; recon scripts
`_research/ccrypak-init-order-recon/`.

**Scope.** A read-only diagnostic probe (engine-side `// === DIAGNOSTIC (PROBE
P1)` or a probe plugin) that logs: `*(gEnv+0x50)` null-vs-non-null at the
ready-bracket entry, and a one-shot marker on the first `CCryPak` file call
(slot 36 fire) with a timestamp/ordering tag relative to the ready-bracket. One
variable: the construction-vs-ready-bracket-vs-first-call ordering. Agent writes,
builds, deploys; the user launches; the agent reads the log. Captured to
`_research/` then removed (no residue).

**Outcome→meaning map** (pre-committed, design §8 P1):
- non-null at ready-bracket AND first file call after ready-bracket → swap in the
  ready-bracket (the design's assumption holds) → proceed to 1.4 as designed.
- null at ready-bracket → the CCryPak ctor runs later → the swap anchor moves to a
  CCryPak-ctor hook or the gEnv publish; 1.4's seating point is revised first.
- first file call precedes the ready-bracket → the swap must move earlier (a
  DllMain/early hook); 1.4's seating point is revised first.

**Test bar.** This is a probe, not a feature — the "test" is the probe firing and
producing a falsifiable timing reading matching one of the three outcomes above.
The reading is agent-read from `kcdx-dev.log` under the `PROBE P1` category. No
permanent regression row (the probe is removed after); the seating it informs is
covered by 1.4's matrix row.

**Dependencies.** None structurally, but ordered before 1.4 (1.4's swap point
depends on P1's outcome). Probe-first per `.claude/rules/results-driven.md` +
`.claude/rules/incremental-delivery.md`.

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §8 P1, §4.1 (the swap),
glossary "the ready-bracket"; `_research/init-cycle-recon/` (the C_ModManager
timing reference — NOT CCryPak); `.claude/rules/results-driven.md` (the probe
discipline + the no-residue capture).

**Disassembler-test / author-burden.** N/A — engine-internal probe, no
author-facing surface. (Any game-binary target the probe resolves — gEnv, pCryPak,
FOpen — resolves by name/id through the Address Library, already-seeded ids 11 /
132 / 131; no new seed row, no AP18.)
