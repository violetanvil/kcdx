# Phase 1 step 1 — probe: seam-install ordering (`HookedCtor` vs first `FOpen`)

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 1.

## What

The build's opening probe. The two-hook seam (steps 3/4) must be live before the
engine's first asset OPEN; the design installs both hooks inside the already-shipping
ready-bracket (`HookedCtor` waits on `g_kcdxReadyEvent`, signaled after kcdx
installs the hooks). The checkable unknown that decides whether that install point
is early enough: **does `HookedCtor` (the `ModManager_ctor` bracket) fire BEFORE the
engine's first `CCryPak::FOpen` call?** (v2-reframed: every asset class opens via
FOpen — design §7 / the load-path map; FOpen, NOT AdjustFileName, is the read-path
marker. The v1 framing of this step against AdjustFileName was moot — AdjustFileName
never fired on the boot/menu path, the finding that drove the v2 redesign.)

**Why a re-probe (the prior data is ambiguous):** the discriminating-probe run
(`_research/phase8.5-pak-resolver/step1-ordering-probe-finding.md`, log 20:51:49)
captured `ctor_fired` AND `first_fopen_call` at the SAME millisecond, SAME thread
(tid=17088) — coincident at wall-clock resolution, so the ORDER is unresolved. But
same-thread means the order is DETERMINISTIC (one strictly precedes the other in
execution) — invisible only because ms timestamps are too coarse. This re-probe
resolves it with a monotonic ordering COUNTER, not wall-clock time.

## Scope

- Re-point the EXISTING `PROBE_CTOR_VS_READ` markers (already in `src/asset_overlay.cpp`
  at `first_fopen_call` + `src/mod_absorb/ctor_bracket.cpp` at `ctor_fired`, from
  commit `4e9cb48`): add a shared monotonic `std::atomic<uint64_t>` ordering counter
  — each marker fetch-adds and logs its sequence number, so coincident-on-the-clock
  events get a DEFINITE order. One variable (the order); no new live detour
  (markers ride the existing FOpen hook + HookedCtor). Agent-written/built/deployed,
  user-launched, agent-read (`.claude/rules/agent-builds-and-deploys.md`).
- Reuse-first: the prior finding + the captured markers already exist — this
  sharpens them, it does not start cold (`.claude/rules/results-driven.md`).
- Throwaway: captured to `_research/`, removed by step 3 (HOOK 1) with the rest of
  the probe residue (`.claude/rules/working-artifacts.md`).

## Outcome → meaning map (pre-committed, flat, theory-independent)

Designed to FALSIFY "the ready-bracket install point is early enough":
- **`ctor_fired` sequence number < `first_fopen_call` sequence number** (ctor strictly
  first) → the ready-bracket install point holds → steps 3/4 install both hooks
  before `SetEvent(g_kcdxReadyEvent)`; the bracket's wait makes them live before the
  first FOpen.
- **`first_fopen_call` sequence ≤ `ctor_fired` sequence** (first FOpen at-or-before
  the ctor) → the design's install point is too late → SURFACE a design fork
  (`design-authority.md`): the hooks must install at an arming point EARLIER than
  `ModManager_ctor` (a different init seam). Do NOT silently pick one — surface it.
- **first FOpen precedes even `RefdbOpened`** (the hooks can't name-resolve ids
  152–155 yet) → part of the same surfaced finding (the earlier-resolution
  question, design §8).

## Test bar

The probe IS its own verification — the pre-committed outcome map (the two sequence
numbers) read against the live log on the user's launch (`acceptance-signal.md` —
the agent reads the log, not the user). No permanent test plugin (that is step 10).
The finding is captured to `_research/`; the in-source probe is removed by step 3.

## Dependencies

The landed foundation (the overlay map, `2588b33`) + the shipping ready-bracket
(`src/dllmain.cpp` / `src/mod_absorb/ctor_bracket.cpp`) + the existing
`PROBE_CTOR_VS_READ` markers (`4e9cb48`). No prior plan step. Correctly ordered
FIRST — a probe-/evidence-first step ships no user-facing behavior but is verifiable
by its captured result (`.claude/rules/incremental-delivery.md`).

## Reference

Design authority: [`../../../design/asset-replacement.md`](../../../design/asset-replacement.md)
§8 (seam install timing — both hooks in the ready-bracket) + §9 (build-gated probe
1). Shared spec: [`../plan-spec.md`](../plan-spec.md) §"Build-gated probes". Prior
finding: `_research/phase8.5-pak-resolver/step1-ordering-probe-finding.md`.

## Disassembler-test / author-burden

None — a probe adds no author-facing surface.
