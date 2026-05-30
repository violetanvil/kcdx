# Phase 8.5 step 1 — hook the game's pak resolver (production overlay hook)

**Status: PARTIAL.** Ledger row: [`README.md`](README.md) → step 1.

## What

Install the PRODUCTION asset-overlay hook on the game's pak resolver
(`CCryPak_FOpen`), so a virtual-path open can be redirected to a loose
overlay file before the pak-resident asset is read.

## As-built (partial)

- `CCryPak_FOpen` is named in refdb (kcdx_id 131); `src/mod_absorb/...` uses the
  resolved address via `refdb::ResolveAddrByName("CCryPak_FOpen")`.
- An observe-only FOPEN probe (`src/probes/fopen_override_probe.cpp`) detours the
  body in dev mode for **path classification only** — NOT for asset replacement.

## Remaining

The production hook that actually redirects an open to an overlay file is not
installed. This step installs it through the conflict engine (`hook_chain`), not
a raw `MH_CreateHook` — register a footprint, resolve by name. The redirect
decision (overlay map lookup) is step 3; this step lands the production hook site
with a pass-through body so step 3 can fill the decision.

## Test bar

Exercised at step 5 (the phase's `cap-XX-asset-replace` plugin). This step's
own check: the production hook installs and the game still boots (the resolver is
hot — a bad hook here AVs at startup).

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 8.5" → "Phase 8.5a".
