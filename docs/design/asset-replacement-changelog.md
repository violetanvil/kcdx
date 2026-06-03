# Asset-replacement design — changelog (newest first)

## 2026-06-03 — v2: the seam corrected to TWO hooks (resolver decision + own-FILE* open), on the gated load-path research

- **The seam is two coordinated hooks, not one.** kcdx owns (1) the resolution
  DECISION by replacing `CCryPak::AdjustFileName` (slot 1, id 152) — which file
  wins, all classes, both byte-lanes — AND (2) the loose OPEN by returning its own
  CRT `FILE*` (the gate-verified Around-`FOpen` mechanism). The v1 "a single
  replaced function is the whole seam" is corrected.
- **Why (the gated findings that corrected v1):** the asset load-path map (commit
  `3193e84`, 6 fronts, §4.5-gated) established that every class opens via `FOpen`
  but bytes arrive on TWO lanes — a loose lane (`FOpen` mints a `FILE*`) and a
  mount/stream lane (pak-resident assets read on a mount-minted `CreateFileA`
  handle, NOT via `FOpen`). The resolver (slot 1) is `sys_pakPriority`-gated — at
  the published default it tests pak-only, so the engine never resolves a vanilla
  path to a loose overlay on its own. So slot-1-alone can't serve a loose file's
  bytes, and FOpen-alone can't replace a pak-resident (vanilla) asset. Owning the
  decision (above the gate) + the open (own handle) covers both.
- **The loose OPEN is settled to return-our-own-`FILE*`** (over a path-only
  redirect), so kcdx never depends on the engine's loose-search — the layer the
  prior path-redirect FAILED at. Gate-verified end-to-end (commit `e6e8e27`).
- **`sys_pakPriority`** stays out of the mechanism (neither set nor depended on);
  the durability point holds.
- **§4.3 staging RETIRED** — HOOK 2 owns the handle, so no `<game>/Data/` staging
  tree / lifecycle is needed; the file is served straight from `assets/`.
- **§9 build-gated unknowns reduced** to runtime acceptances (the two hooks serve
  per-lane live; ctor-vs-first-read ordering; the cFn-ABI pointer-return; the
  default-off DirectStorage texture arm) — the v1 "does the handle-consumed loose
  overlay resolve / does it need staging" mechanism-unknown is gone.
- **§10.2 gains a second sweep target** — the falsified id-152 seed prose
  ("FOpen calls slot 1 / single chokepoint / independent of sys_pakPriority").

**Integrated in:** header "Key changes", §1, §4.3, §7, §8, §9, §10.2, §12.
**Why:** the v1 mechanism rested on the now-falsified "FOpen calls AdjustFileName"
inference (caught by the AP19 gate); the gated load-path map + FOpen-handle finding
replace it with the verified two-lane / two-hook model. The author surface (§1–§6),
the add+replace scope, and the manipulation deferral (§10–§11) are UNCHANGED — the
correction is engine-internal (how kcdx serves the bytes), below the author surface.

## 2026-06-02 — v1: initial canonical asset-replacement design (promoted from the Phase-8.5 planning tree)

- Promoted the Phase-8.5 asset-design to the canonical `docs/design/` home.
- Author surface settled: one `assets/` folder, explicit per-asset sidecar
  declarations + `kcdx.assets.*` code verbs, navigable `kcdx.plugin.<a>.<p>.*`
  cross-plugin refs, add + replace (manipulation deferred).
- Mechanism (SUPERSEDED by v2): "REPLACE `CCryPak::AdjustFileName` as the single
  seam, independent of `sys_pakPriority`" — rested on the "FOpen calls slot 1"
  inference the v2 gated research falsified.
