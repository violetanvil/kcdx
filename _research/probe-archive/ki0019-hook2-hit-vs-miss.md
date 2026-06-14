# PROBE F (KI-0019) — does a HOOK 2 HIT serve the inventory `.dds` as a kcdx-CRT `FILE*`?

Captured 2026-06-14. The finding + the reusable HIT-logging wiring; the in-source
probe was removed after (working-artifacts.md no-residue). Reconstruct from this
recipe for any "does a HOOK 2 overlay HIT hand the engine a kcdx-CRT handle" question
(KI-0006's `fclose` sibling, the file-system-takeover seating confirmation).

## Archive header

- **VERDICT:** CONFIRMED — a HOOK 2 HIT (overlay-map match) on the loose-overlay FOpen path returns a `FILE*` minted by kcdx's own `/MT` CRT, and that handle IS handed to the engine for a UI texture on the inventory path.
- **WHAT IT PROVED (not a bug root cause — a confirmation probe):** on a HIT, `FOpenLooseOverlay` mints the result `FILE*` via kcdx's CRT (`_wfopen_s`) and returns it as FOpen's value; the inventory-open path produces such HITs, so a kcdx-CRT `FILE*` reaches the engine's FSR2/DLSS init which then `fseek`s it cross-CRT (the KI-0019 null-EACCES crash). PROBE F logged every HIT's `vpath` + the returned `fp` + `winner` + `disk`, distinguishing HIT (kcdx `FILE*`, the crash path) from MISS (engine's own `FILE*` via `call_original`, where HOOK 2 is a no-op). This is the third independent artifact corroborating the dump's `fseek` mechanism end-to-end (dump + source + live HIT).
- **KNOWN-ISSUE BACKLINK:** [KI-0019](../../docs/known-issues/KI-0019-ccrypak-fopen-reentrancy-av-on-inventory-open.md) — Trail F / the HOOK-2-HIT confirmation. Sibling: [KI-0006](../../docs/known-issues/KI-0006-serve-execute-vehicle-not-found.md) (the cross-CRT `fclose`, same HOOK 2 own-`FILE*` hazard class).
- **REVIVAL HINT:** re-add the block below immediately before `return static_cast<void*>(fp);` in `FOpenLooseOverlay` to re-confirm which vpaths take the HIT path and what handle is returned, after an asset-system change or a game update.

## Trust level

**PRIMARY EVIDENCE** — a live probe that fired and whose result was read from the
log (run 12-34-05, landed in commit `04a77b5`), not an agent hypothesis. The finding
is from the log read; the `fseek` crash MECHANISM it corroborates is the leading
diagnosis (dump-grounded), and PROBE F did NOT pin the exact crash file (the gesture
did not reproduce on this run — the crash is non-deterministic).

## What PROBE F established (cite KI-0019 Trail F, commit `04a77b5`)

- Run 12-34-05 (no crash — 2nd non-repro; the crash is non-deterministic) logged
  **3 HITs**: a `.lua` (cap-77 `sl_saveload`) AND a UI `.dds`
  (`libs/ui/textures/apse/attack_mode.dds`, comp-16, 2×). The `.dds` is
  inventory/HUD-texture territory.
- So a kcdx-CRT `FILE*` IS handed to the engine for a UI texture and operated on
  cross-CRT — corroborating the dump's `fseek` mechanism with three independent
  artifacts (dump + source + live HIT).
- The HITs were test-plugin overlays. PROBE F does NOT pin the exact crash file
  (the run did not reproduce the AV).
- It distinguished HIT from MISS: on a MISS the hot path returns earlier via
  `call_original` (the engine's own `FILE*`, HOOK 2 a no-op) and logs nothing; only
  the rare HIT path reaches the probe.

## The reusable wiring (reconstruct in `src/asset_overlay.cpp`, `FOpenLooseOverlay`)

Sat immediately BEFORE the production `return static_cast<void*>(fp);` in the HOOK 2
loose-overlay FOpen path — after the production one-shot `overlay_opened` marker
block, before the `// Return our own CRT FILE* …` comment. Read-only: it logged the
`fp` the function was already about to return; removing it changed no control flow
and no returned value.

Cost/dedup note: fires ONLY on a HIT (overlay-map match), the rare path — the hot
MISS path returns above (via `call_original`) and logs nothing. Un-deduped (every
HIT logs) so multiple HITs in one session are visible; the production marker above is
the deduped first-hit line, distinct from this per-HIT probe.

```cpp
// === DIAGNOSTIC (PROBE F): KI-0019 hit-vs-miss confirmation. The deduped
// marker above logs only the FIRST hit; this logs EVERY HOOK 2 HIT (vpath +
// the kcdx-CRT FILE* about to be returned) so we can see whether a HIT serves
// a file the engine's FSR2/DLSS init then fseek's (the crash) — i.e. whether
// the inventory-open AV is the HOOK-2-HIT path at all, or a MISS (engine FILE*,
// fixing HOOK 2 a no-op). Cost bounded: fires ONLY on a HIT (overlay-map
// match), the rare path — the hot MISS path returns above and logs nothing.
// Read-only: logs the fp it is already about to return. PROBE_F: clean grep.
LOG_DEBUG_KV("PROBE_F", "hook2_hit_returning_kcdx_crt_FILE",
             kcdx::log::KV("vpath", key),
             kcdx::log::KV("fp", static_cast<void*>(fp)),
             kcdx::log::KV("winner", winnerPlugin),
             kcdx::log::KV("disk", diskPath));
```

`key`, `fp`, `winnerPlugin`, `diskPath` are existing production locals on the HIT
path (the return path reads `fp`); the probe introduced no symbol of its own.

## Backlinks

- **KI-0019** ([`docs/known-issues/KI-0019-ccrypak-fopen-reentrancy-av-on-inventory-open.md`](../../docs/known-issues/KI-0019-ccrypak-fopen-reentrancy-av-on-inventory-open.md)) — Trail F, the HOOK-2-HIT confirmation row.
- **File-system takeover design** ([`docs/design/file-system-takeover.md`](../../docs/design/file-system-takeover.md)) — §11 names PROBE F as the owed removal; §9 is the structural fix (kcdx takes total ownership of the CCryPak file object, so every handle the engine receives is one its own CRT owns). The takeover subsumes the whole `asset_overlay.cpp` HOOK 1 + HOOK 2 seam.
