# Step 1.2 — remove PROBE F + capture to the archive

**What.** PROBE F is live in `src/asset_overlay.cpp` (the
`LOG_DEBUG_KV("PROBE_F", "hook2_hit_returning_kcdx_crt_FILE", …)` block, ~L379–391,
3 occurrences of the `PROBE_F`/`PROBE F` marker) — it answered its question
(HOOK 2 serves the UI `.dds` as a kcdx-CRT `FILE*` on the inventory path, the
KI-0019 confirmation, commit `04a77b5`). Per the no-residue rule it must be
captured then removed: write its finding + reusable wiring into
`_research/probe-archive/`, then delete the probe block from the source so the
live source returns to pure production logic — no `#if 0`, no commented corpse, no
runtime-disabled flag.

**Scope.** Capture PROBE F's finding to `_research/probe-archive/` (a new entry:
what it proved, the instrumentation recipe, the KI-0019 backlink), then remove the
`PROBE_F` block from `src/asset_overlay.cpp`. The rest of `asset_overlay.cpp`
(HOOK 1, HOOK 2, the overlay map) stays live — it is subsumed later at step 3.6,
not here. Build green after removal. One commit.

**Test bar.** The existing asset-overlay regression coverage still passes after
the probe removal (the probe was read-only — removing it changes no behavior); a
`grep PROBE_F src/asset_overlay.cpp` returns nothing. Build green
(`pwsh ./build.ps1`). This is a behavior-preserving cleanup, so the bar is "no
regression in the asset-overlay matrix rows + the probe is gone."

**Dependencies.** None (independent of the takeover build — owed regardless of
when the takeover finishes, so it lands early).

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §11 (PROBE F
owed-removal); `.claude/rules/working-artifacts.md` §"A scratch probe leaves NO
residue in live source"; `.claude/rules/results-driven.md` §"Probe leaves no
residue".

**Disassembler-test / author-burden.** N/A — no author-facing surface.
