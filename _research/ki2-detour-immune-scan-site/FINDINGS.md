# KI-0002 — a detour-immune, .text-unique AOB for the CAP-70-result scan assertion

## The question

CAP-70-result asserts `kcdx.scan{}` finds count==1 at a known WHGame.dll site at
`input_loaded`. Its original site (luaL_openlibs entry AOB, seed id 115, RVA
0x1449600) is broken because the co-resident cap-33-author-targets plugin
entry-hooks that site — a 5-byte `JMP rel32` detour over the prologue destroys
the AOB's leading bytes before `input_loaded` (KI-0002 PROBE 2 observed bytes
0–4 rewritten `48 89 5C 24 08` → `E9 0E 75 BA FE`). Need a site whose match
window is **detour-immune**: it must not overlap any function-entry prologue a
co-resident `kcdx.hook` before/after-mode plugin can detour, and not be a site
any suite plugin byte-rewrites.

## Ladder

- **Tier 1 (seed rows)** — read `data/seeds/address_versions_seed.csv`. The
  existing `callsite` rows (ids 5–8) are mid-function by NAME but sit close to
  hooked function entries: id 7 (`IsInCombat_callsite_26b`, 0x5605BC) is the
  **pattern-hit 4 bytes PAST a function entry at 0x5605B8 that comp-02-hook-on-patch
  HOOKS** (`offset=-4` → entry) — so a 5-byte entry detour at 0x5605B8 clobbers
  byte 0 of the 0x5605BC window. id 8 (0x566040) IS a function entry (comp-03
  hooks it). id 5 (0x56174C, outfit-swap) has its tail `44 8A F0` rewritten by
  cap-39. **None of the seeded callsite rows is detour-immune** — they exist
  precisely because the suite hooks/patches them.
- **Tier 5 (fresh capstone)** — verified candidates directly against
  `third-party-ghidra/WHGame.dll` (capstone 5.0.7 / pefile). Scripts:
  `verify_candidates.py` (uniqueness + .pdata entry/interior of the seed rows +
  the broken site), `verify_window_stability.py` (aligned disassembly of a
  candidate's containing function to confirm the window is operand-free).

## The verified answer

**AOB:** `41 03 EC 33 DF 41 03 45 CC C4 E2 50 F2 FA 03 C3`
**Module:** WHGame.dll · **RVA:** 0x9800 · **.text-unique match count: 1**
(verified against the binary, build `release_1_5_1164953_841`).

Detour-immunity evidence (all verified against the binary this session):

1. **Deep interior, not an entry.** The window sits 4035 bytes past its
   containing function's entry (0x883D) — far outside any 5-byte entry-detour
   zone. It is not a `.pdata` `RUNTIME_FUNCTION.BeginAddress` (not a function
   entry a `kcdx.hook` before/after detour can target), and no preceding entry
   is within 5 bytes.
2. **No suite plugin references it.** Grep of `test-plugins/` + `src/` for the
   site / its bytes returns nothing — unlike the seeded callsite rows, which are
   comp-02/comp-03/cap-39 targets. Nothing hooks or byte-rewrites it.
3. **Build-stable bytes (operand-free).** Aligned disassembly of the window
   decodes into register-and-small-displacement arithmetic only —
   `add ebp, r12d` / `xor ebx, edi` / `add eax, [r13-0x34]` (disp8) /
   `andn edi, ebp, edx` / `add eax, ebx`. NO `call`/`jmp rel32`, NO
   rip-relative operand — so the bytes do not shift per game build the way a
   relative-displacement operand would. (A scan-finding regression wants a
   pattern whose literal bytes are stable across builds; an operand-bearing
   window is flaky-by-construction across versions.)

It is the inner loop of a hash/compression-style routine — no semantic hook
point, not an export, not an entry. Structurally the kind of site nothing
hooks.

## Recording note (no seed row, no AP18)

This AOB is used as a **raw `pattern =` literal** in the CAP-70-result fixture
(the labeled expert AOB hatch `kcdx.scan{}` already takes) — NOT a named
Address Library target. **No seed CSV row is added** → no AP18 gate. The fact
above is the fixture's inline justification, not a curated DB target the project
commits to maintaining across versions. (If a future change wants this as a
named target, THAT addition is the gated step.)

## Why not the seed rows / the +5 window

- luaL_openlibs +5 window (`57 48 83 EC 20 …`, 11 bytes): **count=3, NOT unique**
  — fails the count==1 assertion.
- All seeded callsite rows: detourable (within 5 bytes of a hooked entry) or
  rewritten — the trap KI-0002 exists to escape.
