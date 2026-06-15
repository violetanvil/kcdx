# Probe finding — divergent-DLL behavior + per-field attribution (D45 Phase 1 / step 1.1)

**Trust level:** primary evidence — a fresh vitest run of `runVerdictCheck` (the existing s04
in-browser per-kind check) over synthesized divergent-DLL fixtures, captured verbatim. Not a
hypothesis; the verdicts below are the literal observed output.

**Date:** 2026-06-14. **Run against:** `data/maintainer-tool/frontend` (the FE repo), `runVerdictCheck`
at commit `2dc6b42`.

**Why this probe ran:** D45 marks two mechanisms `unverified, probe before building`. The Phase-2
`fixDivergence` worker rests on both. Building it on inference is an AP10 defect. This probe settles
them against a real divergent fixture before the worker is written (`results-driven` + `incremental-delivery`).

---

## The two questions

- **E1 — divergent-DLL behavior.** The existing per-kind check (`runVerdictCheck` →
  `extractSurvivalCheck` → `checkRow`) is documented + tested against a VERSION-MATCHING DLL. Does it
  produce a correct, non-throwing verdict per kind when the DLL is the DIVERGENT build a `failed`
  report row diverged on (recorded survival datum no longer matches the on-disk bytes)?
- **E2 — per-field attribution.** `runVerdictCheck` returns ONE row-level `CheckResult` per row. Can
  that verdict be attributed to the specific diverged kind-relevant field (signature vs rva for a
  function row; the survival column for the others), or does the worker need per-field derivation?

## Outcome→meaning map (committed before running)

- **E1 per kind:** A) defined Changed/Ambiguous/CannotCheck, no throw → divergent-DLL-safe; worker
  calls it directly. B) throws → worker must guard/extend that kind. C) spurious Unchanged on
  divergent bytes → the check is BLIND to that kind's divergence (worse than B).
- **E2 (function):** A) the verdict + extracted inputs distinguish signature-diverged from
  rva-diverged → attribution is a thin mapping. B) one body-hash verdict cannot split them → the
  worker needs explicit per-field derivation; record exactly what each field needs.

---

## E1 — RESULT: Outcome A for every kind (divergent-DLL-safe)

Every kind returned a defined, non-throwing verdict against a divergent fixture. No throw; no spurious
`Unchanged`. Verbatim:

| Kind | Verdict | Reason (verbatim) |
|---|---|---|
| `function` (divergent body) | **Changed** | `body BLAKE3 <got> != recorded content_hash <stored>` |
| `callsite` (divergent .text) | **Changed** | `AOB not found in .text (the site is gone)` |
| `string_anchor` (literal removed) | **Changed** | `literal "…" absent from .rdata (the anchor is gone)` |
| `vtable_base` (qwords out of .text) | **Changed** | `slot 0 value 0x0 is not a .text-range pointer [0x1000, 0x1800)` |
| `vtable_index` | **CannotCheck** | `vtable_index is deferred — no RVA, the slot target needs a runtime resolve` |
| `function` (no content_hash) | **CannotCheck** | `no recorded content_hash to compare against` |

**Conclusion (E1):** `runVerdictCheck` is divergent-DLL-SAFE. The Phase-2 worker can call it directly
for divergence detection per kind — a divergent build yields `Changed` (the field diverged) or an
honest `CannotCheck` (the kind can't be checked / the row was never fingerprinted), never a throw and
never a false `Unchanged`. The E10 honest-states design (no-divergence-found / cannot-check) maps
directly onto these verdicts.

## E2 — RESULT: Outcome B (one verdict cannot split signature-vs-rva)

For a `function` row, three sub-probes against a MATCHING DLL isolate the variable:

| Row mutation | Verdict | What it proves |
|---|---|---|
| body bytes divergent (rva+sig correct) | **Changed** | a real body divergence is caught |
| **rva wrong** (relocated +4, body+sig correct) | **Changed** | a wrong rva relocates the hashed span → also Changed |
| **signature wrong** (rva+body correct) | **Unchanged** | **`signature` is NEVER hashed — the check is blind to it** |

**Conclusion (E2):** The single body-hash verdict for a `function` row conflates body-bytes and rva
(both shift the hashed span → `Changed`) and is **entirely blind to `signature`** (a human-readable
ABI string that never enters the hashed body span — see `checker.ts checkFunction` + `extractFunction`,
which hash `[rva, rva+length)` and compare to `content_hash`; `signature` is read by neither).

**What the Phase-2 worker must do** (the per-field attribution the worker adds, E3):

1. **`function` row, `rva` / body field:** the `runVerdictCheck` verdict IS the attribution for the
   body/rva pair — `Changed` → the body-or-rva diverged (`diverged: true`); `Unchanged` → it survived
   (`diverged: false`). The worker does NOT need to separate "rva" from "body bytes": a `failed`
   report row means the recorded body no longer matches the build; both present as `Changed` and the
   maintainer edits whichever the new build requires. Surface the body/rva divergence as one diverged
   field (the recorded rva + a "body no longer matches" note), not two.
2. **`function` row, `signature` field:** the check CANNOT derive a "diverged?" verdict for
   `signature`. The worker must NOT mark it `diverged: false` (a false "matches"). It marks
   `signature` as **`cannot-derive`** (an honest per-field status — the actual ABI would need a
   separate re-derivation the in-browser check does not do; surface "recorded value shown; divergence
   not statically derivable for the signature" — AP14 fail-loud, never a faked pass). The worker's
   per-field status enum therefore needs a third value beyond `diverged` / `not-diverged`:
   **`cannot-derive`**, distinct from the row-level `cannot-check`.
3. **The other kinds** (`callsite` / `string_anchor` / `vtable_base`) each have ONE kind-relevant
   survival column the verdict attributes to directly — `Changed` → that column's recorded value
   diverged from the build. Attribution is a thin mapping (verdict → the kind's one survival field).
   `vtable_index` is always `cannot-check` (deferred).

## THIRD FINDING (load-bearing for the worker's call shape)

`runVerdictCheck(row, prospective, pe, buffer, candidateRows)` derives its check inputs from the
**`prospective` seed-named edit dict**, NOT from the saved `VersionRow` directly. The first probe pass
passed `{}` and every kind returned a spurious `CannotCheck: "no resolvable rva"` — because
`buildProspectiveRow` reads each field from `prospective[seedName]`, and an absent key → `null`, which
NULLS the saved row's values.

**The worker MUST build the prospective dict from the saved row** via
`savedSeedRow(row)` (`frontend/src/editor/fieldModel.ts`) — exactly as `FieldEditor.tsx` does
(`const saved = useMemo(() => savedSeedRow(row), [row])`) — OR bypass `runVerdictCheck` and call
`extractSurvivalCheck(savedRow, pe, buffer)` + `checkRow(...)` directly with the saved `VersionRow`.
Passing the bare saved row with an empty prospective dict yields all-CannotCheck and is the trap.

For the divergence diff (no maintainer edit yet — the row is being INSPECTED, not edited), the
prospective dict == the saved values, so `savedSeedRow(row)` is the correct input.

## Reusable fixture (how the worker's test reconstructs this)

The fixture is synthesized from the existing `makeFakePE.ts` helpers — no real game DLL needed:

- **A divergent `function` fixture:** `makeMultiSectionPE({ rva: MS_TEXT.rva + 0x10, bytes: <divergent body> })`;
  record `content_hash = blake3(<original body>)` on the row, plant DIFFERENT bytes on disk →
  recorded ≠ on-disk → `Changed`.
- **A matching `function` fixture** (for the E2 rva/sig isolation): same, but plant the ORIGINAL body.
- **Divergent `callsite` / `string_anchor` / `vtable_base`:** a recorded survival pattern/literal/slot
  that does not appear in a zero-filled-or-mismatched `makeMultiSectionPE()` / `makeAgreeingPE()` →
  `Changed`.
- **Row construction:** a baseline `VersionRow` (all columns null) + the kind's relevant columns; the
  check input is `savedSeedRow(row)` (see the third finding).
- **`parsePe` takes a `DataView`**, not a `Uint8Array` — wrap: `parsePe(new DataView(buf.buffer, …))`.

The probe spike that produced this finding is preserved verbatim at
[`fixDivergenceProbe.spike.ts`](fixDivergenceProbe.spike.ts) (the `.test.ts` extension removed so it is
not collected; copy it back under `frontend/src/editor/__probes__/` as a `.test.ts` to re-run). It was
removed from `frontend/src/` after capture (zero residue in live source — `working-artifacts.md`).

## Suite state after the probe

A clean `npx vitest run` with the probe removed is **565/565 passing, 40/40 files — fully green**.
(A `--disable-console-intercept` run WITH the probe file present transiently showed one
`App.test.tsx` "auto-open ONE-SHOT" failure; it did NOT recur once the probe file was removed — a
test-isolation flake induced by the extra probe file in the run, not a real `App.test.tsx` defect.)
The probe left zero residue in `frontend/src/`.
