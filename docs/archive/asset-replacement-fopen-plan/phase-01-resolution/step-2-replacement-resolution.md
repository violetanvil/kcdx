# Step 2 — simple-replacement resolution (fill the FOpen hook body)

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 2.

## What

Fill the production `CCryPak::FOpen` hook body (currently pass-through, from the
landed foundation) with the overlay-map redirect: on a read-open whose normalized
vpath has an overlay-map entry, redirect the open to the winning plugin's loose
file; on a miss, call original unchanged. This is the simple-replacement path —
memory-mapped classes, live-verified during the design probes. The redirect
mechanism (flags, target-path form) is built to step-1's probe result.

## Scope

- Replace the pass-through `OverlayFOpen` body with the overlay-map lookup +
  redirect (the engine-owned Before entry's `args[]`/`outCount` write-back; no
  second detour — `hook-engine.md`, AP4).
- On hit: emit the overlay-hit log line (vpath + winning plugin); redirect to the
  winning loose file per the step-1-confirmed mechanism. On miss: pass through.
- Hot-path discipline: the map lookup is the only per-call cost on a miss; NO
  per-call log (`logging.md`, `memory.md`) — the overlay-hit line is event-driven
  (one per distinct hit, or a bounded form).
- Resolve `CCryPak_FOpen` (id 131) by name; no RVA literal, no new seed row
  (`no-hardcoded-addresses.md`, AP18).

## Test bar

Exercised end-to-end at step 8 (the permanent `cap-XX` plugin). This step's own
check: a known-safe vanilla-path overlay (the design-probe's `.dds`) redirects +
emits the overlay-hit line — confirmed in-game on the user's launch + the dev-log
hit line (`acceptance-signal.md`). A non-overlaid path is untouched (the
falsifiable negative).

## Dependencies

The landed foundation (the overlay map this reads + the hook site this fills);
**step 1** (the probe settles the redirect mechanism this body uses). Ordered
after both — its behavior is exercisable when it lands.

## Disassembler-test / author-burden

None — engine-internal; no author-facing input. The author's surface is the
sidecar (step 3) + the Lua/C++ verbs (Phase 2).

## Reference

[`../plan-spec.md`](../plan-spec.md); design authority
[`../../restructure/phase-08.5-asset-replacement/asset-design.md`](../../restructure/phase-08.5-asset-replacement/asset-design.md)
§4.1 (existence ≠ replacement) + §4.4 (load-order conflict) + §7 (the resolution
facts). Build to §4/§7, not to this summary.
