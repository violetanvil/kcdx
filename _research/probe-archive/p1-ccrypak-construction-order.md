# PROBE P1 — CCryPak construction order: built BEFORE the first file call AND the ready-bracket (outcome c)

**Answered:** STATICALLY, by reading the WHGame.dll binary this session — NOT a
live launch. The live-launch probe described in the step-3 Scope (an engine-side
`PROBE_P1` marker + a `*(gEnv+0x50)` read at the ready-bracket) was written into
`src/asset_overlay.cpp` + `src/mod_absorb/ctor_bracket.cpp`, then SUPERSEDED and
removed: a fresh binary read settles the ordering with no launch needed, and
static evidence precedes a live probe (`.claude/rules/results-driven.md` §4). The
removed probe left no residue in live source (`.claude/rules/working-artifacts.md`).

**Trust:** PRIMARY EVIDENCE — a fresh disassembly read of WHGame.dll
(`release_1_5_1164953_841`), not hypothesis. The boot-order facts below are read
directly from the binary via the co-located scripts in
`_research/ccrypak-init-order-recon/`.

## The question (design §8 P1 / step 1.3)

When is the engine's `CCryPak` object (reached at `*(gEnv+0x50)`) constructed,
relative to (i) `CSystem::Init`'s first file call and (ii) kcdx's late
`ModManager_ctor` ready-bracket (where `g_kcdxReadyEvent` / `HookedCtor` currently
seats)? This is the window the file-system-takeover vtable swap (step 1.4) must
land in.

## Outcome→meaning map (pre-committed, design §8 P1)

- **(a)** non-null at ready-bracket AND first file call is after the ready-bracket
  → swap in the ready-bracket (the design's assumption holds).
- **(b)** null at ready-bracket → the CCryPak ctor runs later; anchor the swap on
  a CCryPak-ctor hook or the gEnv publish instead.
- **(c)** first file call PRECEDES the ready-bracket → the swap must move earlier
  (to the construction site), NOT the late ready-bracket.

**Outcome (c) holds.** The CCryPak object is constructed and published to
gEnv+0x50, and the engine makes its first `*(gEnv+0x50)` file call, BOTH inside
`CSystem::Init` BEFORE the `ModManager_ctor` ready-bracket runs. The swap must
seat at the construction site, not the ready-bracket.

## Verified facts (Address Library ids + boot-order addresses)

Address Library entities (curated, resolve by name/id; no new seed row this turn):

| Entity | kcdx_id | RVA | Role |
|---|---|---|---|
| `gEnv` | 11 | `0x0492B800` | the global env struct base |
| pCryPak slot (`gEnv_pCryPak`) | 132 | gEnv+0x50 (`0x0492B850`) | the `CCryPak*` pointer slot |
| `CSystem_pCryPak_construct_store` | 158 | `0x9B3C0C` | the helper `CSystem::Init` calls that constructs CCryPak and publishes the pointer into gEnv+0x50 |
| `CCryPak_ctor` | 159 | `0x00D2A570` | the CCryPak constructor itself (stores the pCryPak ptr into gEnv+0x50 + lays the vtable) |

Boot-order facts (read from the binary — `_csysinit_fs_order.txt`,
`_pcrypak_indirect.txt`, `_pcrypak_write_scan.txt`):

- **`CSystem::Init` is at `0x1807A6C64`** (RVA `0x7A6C64`). Its body, in order:
  1. **Construct + publish CCryPak** — `CSystem::Init` CALLs the construct-store
     helper (id 158, RVA `0x9B3C0C`) at `0x1807A71CA`
     (`call 0x1809b3c0c`). The helper constructs the CCryPak object (the ctor is
     id 159, RVA `0xD2A570`) and the `CCryPak*` is published into gEnv+0x50 — the
     store is a register-relative `mov qword ptr [rsi+0x50], r13` inside the ctor
     body (resolved at `0x0180D2A5C2`), so a whole-`.text` scan for a
     *rip-relative* store to the slot finds 0 direct writes (`_pcrypak_write_scan.txt`
     "WRITES … 0") — the publish is indirect, via the gEnv-base pointer in a
     register, the expected shape for a ctor storing through `this`/gEnv. The
     `0x1809B3C87` figure in the step scope is the store-instruction site within
     the helper at `0x9B3C0C` (id 158 names the helper that performs the publish).
  2. **First file call** — `CSystem::Init` READs gEnv+0x50 at `0x1807A7225`
     (`mov rcx, [rip+0x4184624]` — the pCryPak slot, the sole gEnv+0x50 read in the
     Init body before the ctor call) and immediately makes an indirect virtual call
     through it at `0x1807A723A` (`call qword ptr [rax+0x238]`). This is the first
     `*(gEnv+0x50)` dispatch — AFTER the construct/publish (step 1), so the object
     exists when it fires.
  3. **ModManager_ctor (the ready-bracket)** — `CSystem::Init` CALLs
     `ModManager_ctor` LAST of the three, at `0x1807A76FE`
     (`call 0x180da0eb0`). kcdx's `HookedCtor` / `g_kcdxReadyEvent` ready-bracket
     hooks THIS site — so the ready-bracket runs strictly AFTER both the CCryPak
     construct/publish and the first gEnv+0x50 file call.

**Ordering, one line:** construct+publish CCryPak (id 158 helper @ `0x1807A71CA`)
→ first gEnv+0x50 file call (@ `0x1807A723A`) → ModManager_ctor ready-bracket
(@ `0x1807A76FE`). All three are within `CSystem::Init`'s body, in this order.

## Conclusion — swap seats at the construction site, not the ready-bracket

Because the engine's FIRST `*(gEnv+0x50)` file call (`0x1807A723A`) fires BEFORE
the `ModManager_ctor` ready-bracket (`0x1807A76FE`), seating the vtable swap at
the late ready-bracket would miss every file call `CSystem::Init` makes between
the CCryPak publish and the ready-bracket. **The swap must seat at the CCryPak
construction site** — hook `CSystem_pCryPak_construct_store` (id 158, RVA
`0x9B3C0C`) at/just-after the gEnv+0x50 store, so kcdx owns the object the instant
the engine publishes it and before any consumer dispatches through it. This
revises step 1.4's seating anchor and updates design §4.1 / §8 P1 (the
ready-bracket assumption is falsified by (c)).

## Reusable wiring — the static-read scripts (co-located)

The producer scripts + raw dumps that read these facts from the binary live at
`_research/ccrypak-init-order-recon/` (reuse-first; re-run against a new game
build to re-verify the ordering — never re-disassemble from scratch):

- `disasm_csysinit_fs_order.py` → `_csysinit_fs_order.txt` — disassembles
  `CSystem::Init`'s body, tags every gEnv-struct touch + the helper/ctor/ModManager
  call sites in order; the primary ordering dump.
- `find_ctor_callers.py` → `_ctor_callers.txt` — the CCryPak-ctor / construct-store
  call-graph (who calls the helper).
- `find_pcrypak_indirect.py` → `_pcrypak_indirect.txt` — indirect-store leads for
  gEnv+0x50 (locates the `mov [rsi+0x50], r13` publish inside the ctor body).
- `find_pcrypak_write.py` → `_pcrypak_write_scan.txt` — whole-`.text` scan for
  gEnv+0x50 writes/reads (confirms 0 rip-relative writes; 681 reads incl. the
  Init-body read at `0x1807A7225`).

A future re-verification reconstructs the read from these scripts; no probe code
need return to `src/`.
