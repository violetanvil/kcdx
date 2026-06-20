# Step 5.1 — probe P5: the find-data buffer ABI

**What.** Settle the find-data buffer layout (design §8 P5, §5.1) that the
`FindFirst`/`FindNext` slots (63/64) fill — the `local_158` 36+-byte scratch the
table-loader `FUN_180974484` passes and reads. kcdx's slot-63/64 impl must fill a
find-data the engine consumer reads CORRECTLY (the same field layout the engine's
own FindFirst produces), or the consumer mis-reads every enumerated entry (the
dir-bit or name lands at the wrong offset). This is a checkable ABI fact — read it
from the engine's find-data struct / the consumer's field accesses BEFORE 5.2 mints
a find-data. The hard prerequisite for the enumeration cutover, ordered first
(`.claude/rules/incremental-delivery.md`).

**Scope.** A static disassembly read (the reuse-first ladder per
`reverse-engineering.md` — existing recon → predecessor sigs → fresh Ghidra last;
no live launch, a struct-layout fact). Read what the loader consumer reads off the
find-data: the attr word (the `& 0x10` directory-attr bit the loader tests), the
entry-name byte offsets (the `local_134`/`local_133`/`local_132` `.`/`..`-skip
test the loader does), any size/time fields. If the consumer's accesses alone are
ambiguous, decompile the engine FindFirst body (`0x180973058`) to read what it
WRITES. Produce a `_research/<slug>-recon/` dump (FINDINGS.md + the field-layout
table + any worker script) so 5.2 mints a byte-compatible find-data. No `src/`
change — this step is evidence only.

**Outcome→meaning map** (pre-committed, design §8 P5):
- the field offsets (attr word, name, size/time) are read from the consumer +
  cross-checked → kcdx mints a byte-compatible find-data the engine consumer reads
  correctly → 5.2 builds the triplet against this layout.
- the layout is ambiguous from the consumer alone → decompile the engine FindFirst
  body (`0x180973058`) to read what it writes → the layout is then read from the
  producer.

**Test bar.** A probe, not a feature — the "test" is the find-data layout read with
a falsifiable field-offset table (each field at a cited offset, read from the
binary, not inferred). The captured layout is the durable artifact
(`_research/` recon dump per `.claude/rules/working-artifacts.md`); the slots that
consume it (step 5.2) carry the permanent cap-118 regression row. No live launch —
static evidence settles it (`.claude/rules/results-driven.md` §4).

**Dependencies.** None within this phase — it is the first, evidence-establishing
step. Rests on the existing recon `_research/ki0027-table-glob-dispatch-recon/`
(which read the loader body + the call sites but did NOT decompile the find-data
struct — that gap is exactly this step). Ordered before 5.2 per
`.claude/rules/incremental-delivery.md` (the build mints the find-data this step
defines).

**Game-function evidence.** The find-data struct + the slot-63/64/65 engine bodies
(`0x180973058` / `0x18041d640` / `0x18097383c`) are resolved on the reuse-first
ladder (`.claude/rules/reverse-engineering.md`, `/research-disassembly`), read from
the binary, never invented. This is an ABI/struct-layout fact — the abi_walker /
Ghidra path, not prologue-shape guessing.

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §5.1, §8 P5;
`_research/ki0027-table-glob-dispatch-recon/FINDINGS.md` (the verified dispatch +
the `local_158`/`local_134` caller-read sites this step reads the layout from).

**Disassembler-test / author-burden.** N/A — engine-internal ABI probe; no
author-facing input.
