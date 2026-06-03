# Phase 1 step 1 — probe: seam-install ordering (`ModManager_ctor` vs first asset read)

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 1.

## What

The build's opening probe. The asset-resolution seam (step 2) must be live before
the engine's first overridable asset read; the design installs it inside the
already-shipping ready-bracket (`HookedCtor` waits on `g_kcdxReadyEvent`). The one
checkable unknown that decides whether that install point is early enough: **does
`ModManager_ctor` fire BEFORE the engine's first overridable asset read?** This
probe logs both timestamps in one launch and reads them against a pre-committed
outcome→meaning map. The probe is throwaway — it leaves no residue in live source
(`.claude/rules/working-artifacts.md`); its finding is captured to `_research/`.

## Scope

- A throwaway probe (a `// === DIAGNOSTIC (PROBE …)` site in kcdx's own init path
  and/or a minimal read-side log) — agent-written, agent-built, agent-deployed,
  user-launched, agent-read (`.claude/rules/agent-builds-and-deploys.md`). One
  variable; no second live probe stacked (`guard-probe-stack.py`). Captured +
  removed when answered.
- Log: (a) when `HookedCtor` (the `ModManager_ctor` bracket) fires, (b) when the
  engine performs its first overridable asset read (a vpath through
  `AdjustFileName` / the resolver), each with a stable `LOG_DEBUG_KV` category tag
  and a timestamp. Reuse-first: `_research/phase8.5-pak-resolver/` already holds
  the FOpen/AdjustFileName decompiles + the prior SEAM-A timing finding
  (`seamA-probe-timing-finding.md`) — read them before any live launch (static
  evidence first, `.claude/rules/results-driven.md`).

## Outcome → meaning map (pre-committed, flat, theory-independent)

Designed to FALSIFY "the ready-bracket install point is early enough":
- **ctor fires at-or-before the first overridable read** → the design's mechanism
  holds → step 2 installs the seam before `SetEvent(g_kcdxReadyEvent)`; the
  bracket's wait makes the seam live before the game proceeds.
- **the first overridable read fires EARLIER than the ctor** → the design's
  install point is too late → SURFACE a design fork (`design-authority.md`): the
  seam-install / ready point must move earlier than `ModManager_ctor`. Do NOT
  silently pick an earlier point — surface it.
- **the first read precedes even `RefdbOpened`** (the seam can't name-resolve ids
  152–155 yet) → part of the same surfaced finding (the earlier-resolution
  question, design §8).

## Test bar

The probe IS its own verification — the pre-committed outcome map read against the
live log on the user's launch (`acceptance-signal.md` — the agent reads the log,
not the user). No permanent test plugin (that is step 9). The finding is captured
to `_research/` (durable process-output) and the in-source probe removed.

## Dependencies

The landed foundation (the overlay map, `2588b33`) + the shipping ready-bracket
(`src/dllmain.cpp` / `src/mod_absorb/ctor_bracket.cpp`). No prior plan step. This
is correctly ordered FIRST — a probe-/evidence-first step ships no user-facing
behavior but is verifiable by its captured result (`.claude/rules/incremental-delivery.md`).

## Reference

Design authority: [`../../../design/asset-replacement.md`](../../../design/asset-replacement.md)
§8 (seam install timing) + §9 (build-gated unknown 1). Shared spec:
[`../plan-spec.md`](../plan-spec.md) §"Build-gated unknowns".

## Disassembler-test / author-burden

None — a probe adds no author-facing surface.
