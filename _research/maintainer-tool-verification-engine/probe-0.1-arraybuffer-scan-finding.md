# Probe 0.1 finding — 86MB WHGame.dll ArrayBuffer load + full-`.text` AOB scan feasibility

**Kind:** durable process-output (a captured probe finding a future Phase-2 step reuses).
**Trust level:** primary evidence — fresh measured run against the real binary (not an interpretation).
**Date:** 2026-06-05.
**Step:** maintainer-tool verification-engine Phase 0, step 0.1 (the feasibility floor under the
browser static checker; TRD D26 "WHGame.dll ≈ 86 MB — within browser limits").

## Question

Can the in-page static checker (a) load the real ~86MB WHGame.dll as an `ArrayBuffer` AND (b) run
a full-`.text` AOB byte-pattern scan (the `callsite` kind's worst case — a unique-match byte
pattern with `?` wildcards across the whole `.text` section) within an acceptable perf/memory
budget? D26 asserts it; this confirms it on the REAL binary.

## Probe shape

- **Harness:** `data/maintainer-tool/frontend/src/dll-resolver/__probes__/arraybuffer-text-scan.probe.test.ts`
  — a portable Vitest test, `it.skipIf(!existsSync(DLL))` so it is a no-op in CI (the DLL is
  gitignored / absent), runs the measured scan only on a local tree where the DLL is present.
- **Environment measured:** Node (Vitest, jsdom env), Node v24.12.0, on V8 — the same engine the
  browser runs. The feasibility question is "load 86MB into a typed array + linear-scan its `.text`
  span," which is the identical `ArrayBuffer` + `Uint8Array` path the browser checker uses. The
  CPU + memory cost of the typed-array linear scan is therefore representative; **Node's number is a
  valid LOWER BOUND** — the browser adds a one-time File-API → ArrayBuffer read on top (a maintainer
  picks the file once), which this probe isolates OUT by reading via `fs` in place of the File API.
  The bytes and the scan are byte-identical.
- **`.text` location:** the section-table parse mirrors `versionResolver.ts` `parsePe`'s offsets
  EXACTLY (DOS `e_lfanew` → PE sig → COFF → optional-header magic/size → 40-byte section entries;
  raw-data ptr at +20, raw-data size at +16, name in the first 8 bytes), scoped to `.text` instead
  of `.rdata`. `parsePe` is module-private and this is a throwaway probe, so it did not mutate the
  production module's public surface to reach a private helper — the minimal section-table read was
  re-derived in the probe from the same offsets.
- **AOB:** a representative 26-byte pattern with 8 `?` wildcards, `-1`-coded, scanned windowed
  across `.text`, counting hits — the callsite check shape per `fingerprint-per-kind.md` §callsite.
  The probe measures SCAN COST over the full section, not a specific seed pattern's correctness.

## Measured numbers (real WHGame.dll, two stable runs)

| Metric | Value |
|---|---|
| DLL total size | 89,176,576 bytes = **85.0 MiB** |
| `.text` section size | 60,821,504 bytes = **58.0 MiB** (file offset 1024) |
| Load → ArrayBuffer | **23–25 ms** |
| Full-`.text` AOB scan (26-byte, 8 wildcards) | **135–142 ms** |
| Load + scan total | **~159–167 ms** |
| Scan throughput | **~410–430 MiB/s** |
| Process RSS (resident, buffer + views held) | **~342 MiB** |
| V8 heapUsed | **~46 MiB** |
| AOB hits | 0 (the synthetic 26-byte pattern is over-specific; see sanity below) |

**Scanner sanity** (a throwaway check, since removed): the same scan logic found **140,019** hits of
the common x64 prologue `48 89 5C 24 ??` (`mov [rsp+x],rbx`) and **2,203,717** hits of the 2-byte
lead `48 8B`, each at ~140–150 ms — confirming the 0-hit count is the chosen pattern's property, NOT
a broken scanner, and that scan COST (~140 ms) is consistent whether the pattern matches 0 or
millions of times (the inner loop's early-`break` on mismatch dominates either way).

## Outcome→meaning map verdict

| Outcome (pre-committed) | Lands? |
|---|---|
| Loads + scans in an acceptable budget (sub-second-ish, no OOM) → **feasible as designed** | **YES — this row.** |
| Loads but scan too slow / janky → feasible, needs a worker/chunked strategy | No. |
| Cannot load 86MB / OOMs → no-upload premise at risk, STOP | No. |

**ROW 1 — feasible as designed.** Load (~24 ms) + full-`.text` scan (~140 ms) ≈ **160 ms total**,
well under the ~1s "sub-second-ish" bar, at ~342 MiB resident (no OOM, far under browser tab
limits). The in-browser static scan is feasible with the straightforward synchronous scan; no
worker/chunked strategy is required for one full-`.text` AOB pass. **Phase 2 proceeds with the
straightforward client-side scan.**

### Caveats Phase 2 should carry forward (not blockers — sizing notes)

- The ~160 ms is **one** full-`.text` AOB pass on Node/V8. A survival run that scans `.text` once
  PER callsite row (N patterns) costs ~N × 140 ms if done naively as N independent passes — at a few
  dozen callsite rows that is still sub-second to a few seconds, but if the row count grows large, a
  single multi-pattern pass (or batching) keeps it snappy. The single-pass cost is the proven floor;
  N-pass scaling is a Phase-2 design detail, not a feasibility blocker.
- A browser File-API read of the 85 MiB file (the leg this probe isolated out) adds a one-time read
  the user initiates; it is not part of the per-scan cost.
- Measured on a desktop CPU; a low-end machine scans slower but ~400 MiB/s has large headroom under
  the 1s bar for a single pass.

## Reproduce

From inside `data/maintainer-tool/frontend/` (the spaced-path Vitest requirement, see its
`TESTING.md`):

```
npx vitest run src/dll-resolver/__probes__/arraybuffer-text-scan.probe.test.ts --disableConsoleIntercept
```

`--disableConsoleIntercept` is required — Vitest otherwise swallows the `console.log` block that
carries the numbers. The test SKIPS (still green) when the DLL is absent.
