# save/load RVA resolution — recon findings

**Task.** Resolve the 5 save/load function-entry AOB locators (the live `*_SIG`
constants in `src/save_load_hooks.cpp`) to their unique current-version RVAs
against `WHGame.dll`, confirm each is present in the dump's `functions/` table
(the `content_hash`/`length` promote dependency for a `kind=function` seed row),
and confirm the ABI. Prerequisite for authoring 5 new Address Library seed rows.

**Binary:** `third-party-ghidra/WHGame.dll`, image base `0x180000000`, KCD2
`release_1_5_1164953_841` (1.5.1164953).

**Method.** Byte-match primitives (`parse_aob`, `count_text_matches`) copied from
`_research/seed-verification-recon/verify_seeds.py` into `resolve_save_load_rvas.py`
(this dir) — `.text` scan with `??`-wildcard mask, assert exactly one hit. Dump
presence checked against `data/refdata-extractor/dump/refdata-1.5.1164953/functions/*.csv`
(keyed by hex `rva`). Raw output: `_scan_output.txt`.

## Result — all 5 resolve UNIQUELY and all 5 are in the dump

| name | RVA | VA | unique? | in dump `functions/`? | dump length |
|---|---|---|---|---|---|
| SaveGame | `0x03581B04` | `0x183581B04` | UNIQUE (1) | YES | 883 |
| LoadGame_wrapper | `0x025BCEEC` | `0x1825BCEEC` | UNIQUE (1) | YES | 167 |
| PostLoadGame | `0x025BCF94` | `0x1825BCF94` | UNIQUE (1) | YES | 552 |
| DeleteSavegame | `0x025BC510` | `0x1825BC510` | UNIQUE (1) | YES | 265 |
| SaveGameRecord_SlotResolver | `0x019DDE78` | `0x1819DDE78` | UNIQUE (1) | YES | 23 |

**Consistency check passed:** the slot resolver resolved to `0x019DDE78`, exactly
the RVA the `save_load_hooks.cpp` comment documents (VA `0x1819DDE78`). Its dump
length 23 matches the comment's "24-byte function" (entry through last byte).

**Dump presence → fingerprint promote:** every RVA is a function entry in the
dump, so at DB rebuild the `content_hash`/`length` columns promote automatically
from the dump's `functions/` table onto the curated row — no NULL fingerprint,
nothing hand-authored for those two columns.

## ABI — verified by live production behavior

The dump's Ghidra signature column reads `undefined FUN_...()` for all 5 (auto-
analysis did not recover arg lists), so the dump does not by itself confirm the
ABI. The verified ABIs come from `src/save_load_hooks.cpp`'s typedefs, which are
live-confirmed by the production hooks that consume them:

| name | verified signature (kcdx DSL) |
|---|---|
| SaveGame | `char (ptr self, cstr filename, u8 reason, u8 flag_a, u32 arg5, u8 flag_b, cstr description)` |
| LoadGame_wrapper | `char (ptr self, u32 playline, u32 slot)` |
| PostLoadGame | `char (ptr self, u32 arg2, ptr arg3)` |
| DeleteSavegame | `char (ptr self, i32 slot, u32 flags)` |
| SaveGameRecord_SlotResolver | `ptr (ptr sub_object, i32 playline_idx, i32 slot_idx)` |

**Discrepancy resolved (SaveGame arg count).** An earlier recon
(`_research/phase6-save-load/SAVE-LOAD-CANDIDATES.md`, 2026-05-19) inferred
SaveGame as a 2-arg `void __thiscall(this, ISaveGame*, ESaveGameReason)` and left
the exact arg list as an open question pending live verification. That inference
was the under-counted ABI the project later caught — `save_load_hooks.cpp:29`
names "the 3-arg-SaveGame bug" the full-body analysis fixed. The CURRENT 7-arg
typedef is the corrected, live-confirmed shape: `HookedSaveGame`
(`save_load_hooks.cpp:168`) is a 7-param `__fastcall` that reads `description`
and passes all 7 through to the trampolined original
(`g_orig_save_game(self, filename, reason, flag_a, arg5, flag_b, description)`,
line 209) — the hook fires on every save and the game proceeds, which a wrong
arg count would not survive. So the 7-arg ABI is verified by production behavior,
superseding the stale 2-arg recon inference.

## Evidence tier for the seed rows

`evidence_kind = live_production` is correct for all 5: each is consumed by a
production hook in `save_load_hooks.cpp` that fires on every save/load (per the
policy definition of `live_production`). The tier becomes truly earned once the
hook resolves the address via `refdb::ResolveAddrByName` (the Phase-1 code
switch) rather than the current in-source AOB scan — until that switch lands,
the row's address is correct but the hook is not yet the by-name consumer.

## Ready for seed authoring (the /execute Phase 1 step)

Per the `kind=function` convention (confirmed with the seed maintainer):
- `kind = function`, `rva = <above>`, `signature = <above>`, audit quartet
  (`last_verified_at_version = 1.5.1164953`, `verified_by`, `verified_date`,
  `evidence_kind = live_production`).
- `survival_aob` and the 4 trailing columns (`value`/`offset`/`vtable_slot`/
  `struct_offset`) stay EMPTY — a function's survival datum is its body
  fingerprint (`content_hash`/`length`, dump-promoted), not an AOB.
- The AOB used to resolve each RVA is the authoring tool, not a stored column;
  if preserved at all it goes in `address_names.notes` prose (never parsed).

This recon resolves the facts only. Authoring the rows + switching the code is
the `/execute` Phase 1 working flow, gated on the user's per-row AP18 sign-off
(the RVAs are now known).
