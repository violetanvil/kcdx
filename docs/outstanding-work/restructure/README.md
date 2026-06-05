# kcdx restructure — phase tree + status ledger

The kcdx v0.2+ restructure: manifest-only TOML, Lua-first authoring, owned
launcher (`kcdx.exe`), one unified ordered plugin list, kcdx absorbs pak mods.
Authoritative spec for kcdx v0.2+; supersedes large sections of
[`../../design.md`](../../design.md).

This folder is the **navigable** form of the plan: one subdirectory per phase,
one document per shippable (commit-grain) step for live phases. The original
monolithic plan is preserved verbatim as [`00-original-plan.md`](00-original-plan.md)
— it holds the shared spec every step leans on (the model, the Lua/C++ surface
reference, lifecycle, launcher, Critical files / Risk register / Verification
plan) and is the cross-link target for peer docs that cite section/line anchors.

## How this tree works

- **This `README.md` carries the canonical phase-grain ledger** (below). One row
  per phase. A phase's row flips to `DONE` when all its steps land.
- **Each phase subdirectory has its own `README.md`** with a step-grain ledger
  (one row per shippable step) + the phase's intent. For a DONE phase the subdir
  README is a thin stub (status + commit hashes + a pointer to the original's
  section). For a live phase it carries the full step breakdown, and each step is
  its own document.
- **The ledger is the completion record, not the commit log.** A landed step
  flips its row to `DONE` + short hash in the same commit as the step's diff
  (the orchestrator gate in `../README.md` §"Status ledger"). When a `/feature`
  or `/execute` cycle lands a step here, its `Source work-item` is
  `docs/outstanding-work/restructure/phase-NN-<slug>/step-M-<slug>.md → <that
  step's row>`.

## Phase ledger

Status vocabulary: `NOT STARTED` · `IN PROGRESS` · `DONE` · `BLOCKED` ·
`SUPERSEDED` · `DEFERRED → <TD>` (re-homed to a tech-debt entry — the spec stays
in its subdir, the TD is the tracked-debt handle). Commit cell carries the short
hash when `DONE`, `—` otherwise.

| Phase | Status | Commit |
|---|---|---|
| [1 — launcher exe + paths](phase-01-launcher/README.md) | DONE | live-verified |
| [2 — Lua API skeleton (7 core verbs + domains + docs/lua/ + zone_gate + kcdx.plugin.*)](phase-02-lua-api/README.md) | DONE | — |
| [3 — C++ DLL API parity (kcdxHook/Bytes/Trampoline interfaces + Kcdx.h wrapper)](phase-03-cpp-parity/README.md) | DONE | cdd5e7a / 2b2e6f5 / 38f9dd5 |
| [4 — migrate test suite + engine builtin](phase-04-migrate-suite/README.md) | DONE | — |
| [5 — delete old TOML behavior parsers](phase-05-delete-parsers/README.md) | DONE | 95854fe |
| [6 — probe code cleanup (narrow subset)](phase-06-probe-cleanup/README.md) | DONE | 3f66c47 |
| [7 — zone-rework subset + before_game doc widening](phase-07-zone-rework/README.md) | DONE | 54d7d4d / 9264d6a |
| [8 — ASI-loader cleanup (docs)](phase-08-asi-cleanup/README.md) | DONE | — |
| [8.5 — asset replacement (pak overlay)](phase-08.5-asset-replacement/README.md) | SUPERSEDED → [`asset-system/`](../asset-system/README.md) (Phases 1–2 DONE + confirmed; Phase 3 BLOCKED → Phase 11 — the `.lua`-execute confirmation = KI-0006, bundled into Phase 11 alongside KI-0005's boot-cache serve) | — |
| [9 — high-level Lua surface (player/inventory + namespace stubs)](phase-09-high-level-lua/README.md) | DEFERRED → [`TD-0005`](../../tech-debt/TD-0005-high-level-lua-surface.md) | — |
| [9.1 — SQLite reference DB + lookup primitive + verification cache](phase-09.1-reference-db/README.md) | DONE | 498934c |
| [9.2 — unified named-target surface (kcdx.declare + smart resolver)](phase-09.2-declare-surface/README.md) | DONE | 2dac79b / 1c01c9d / CAP-70 |
| [9.3 — kcdx.hook.* / kcdx.statement.* split + locator/op namespaces + multi-region trampoline](phase-09.3-namespaces/README.md) | NOT STARTED | — |
| [9.4 — kcdx.find{...} discovery + kcdx_dev_inspect console](phase-09.4-discovery/README.md) | NOT STARTED | — |
| [9.5 — kcdx.behavior.* named-behavior catalog](phase-09.5-behaviors/README.md) | NOT STARTED | — |
| [9.6 — kcdx.bytes narrowing + Lua-API rule update + final migration](phase-09.6-bytes-narrowing/README.md) | NOT STARTED | — |
| 9.7 — curated-target sub-verb resolver | SUPERSEDED | — |
| [10 — gameplay event catalog (kcdx.on)](phase-10-event-catalog/README.md) | NOT STARTED | — |
| [11 — force-load WHGame.dll + kcdx owns the one Lua VM](phase-11-shim-vm/README.md) | NOT STARTED | — |
| 12 — C++ empowered-wrapper sweep + correctness fix + UX polish | NOT STARTED | — |

Notes on non-obvious rows:
- **9.2 DONE** — declare store + smart-resolver sub-verb surface + C++ mirror +
  `kcdx.scan{...}` Lua diagnostic + the `kcdx_scan` console command (in-game
  iterative AOB discovery) all landed. The `kcdx_scan` residual shipped as
  CAP-70 (live-verified 2026-06-01) ahead of this tracking tree's split; its
  step doc was reconciled from a drift-authored `NOT STARTED` to `DONE`. See the
  phase subdir.
- **9.7 SUPERSEDED** — merged into Phase 9.2 (the declare store and the smart
  resolver were two halves of one surface). No subdir; the redirect detail lives
  in [`00-original-plan.md`](00-original-plan.md) §"Phase 9.7".
- **11 NOT STARTED (design settled 2026-06-05)** — the prior "BLOCKED on the FIX A
  harvest, ~38% mapped" note was STALE; the harvest is substantially complete (93/117
  LUA_API resolved + ~24 inlined/stripped catalogued). The settled design
  ([`phase-11-shim-vm/lua-vm-design.md`](phase-11-shim-vm/lua-vm-design.md)) is
  decomposed into a 6-phase build tree (keystone probe → shim → force-load+adopt →
  early-slot+boot-swap → drop-static → serve-execute). kcdx builds the one VM; the
  engine adopts it. Phases 1–10 ran in parallel with the harvest.
- **12 NOT STARTED** — the C++ empowered-wrapper sweep + `sig_traits` correctness
  fix + UX polish; design-settled, detail in [`00-original-plan.md`](00-original-plan.md)
  §"Phase 12". Subdir authored when the phase is picked up.

## Substantive next-pickups

Independent work that can land now (each by leverage, not phase order):
- **Engine-direct hook migration** — 5 remaining `MH_CreateHook` sites move to
  `hook_chain::AddCEngine`; one `/execute` cycle per site; full spec at
  [`../../tech-debt/TD-0003-engine-direct-hook-migration.md`](../../tech-debt/TD-0003-engine-direct-hook-migration.md).
- **Phase 8.5 asset overlay** — SUPERSEDED; re-planned + spun out to the
  standalone [`asset-system/`](../asset-system/README.md) tree (Phases 1–2 DONE +
  acceptance-confirmed — the two-hook seam + the full Lua+C++ `kcdx.assets.*`
  surface). Phase 3 is BLOCKED → Phase 11: the served-`.lua` EXECUTE confirmation
  (`KI-0006` — a heap-corruption bug when a mod-init `.lua` overlay is keyed) is
  bundled into Phase 11 (FIX A collapses the dual-runtime + reworks serve-execute;
  user-approved deferral 2026-06-05), alongside the boot-cache in-game serve
  (`KI-0005`). The shipping capability (textures, XML, cross-mod, conflict,
  stock-pak) is proven + confirmed; only the `.lua`-execute leg waits.
- **Phase 9 high-level Lua surface** — DEFERRED → [`TD-0005`](../../tech-debt/TD-0005-high-level-lua-surface.md).
  Independent, pure RE + binder work (a non-blocking leaf); re-homed to carried
  debt rather than held open on the active ledger. Picked up by scheduling its
  dedicated build phase — the per-step spec stays at
  [`phase-09-high-level-lua/`](phase-09-high-level-lua/README.md).

## Cross-cutting (tracked in `../`, not phase-counted)

`../` carries independent outstanding-work items — ABI extensions,
design-settled-but-unbuilt features, tracked debt. Most relevant to the live
phases:
- [`../fix-a-drop-static-lua.md`](../fix-a-drop-static-lua.md) — Phase 11 unblocker.
- [`../before-game-hooks.md`](../before-game-hooks.md) — Phase 11 consumer (bugsplat builtin).
- [`../../tech-debt/TD-0003-engine-direct-hook-migration.md`](../../tech-debt/TD-0003-engine-direct-hook-migration.md) — the 5-site migration (tech-debt TD-0003).
- [`../../tech-debt/TD-0001-declare-value-string-arena.md`](../../tech-debt/TD-0001-declare-value-string-arena.md) — declare-store completion (tech-debt TD-0001).
- **Recovery + rollback for Track-2 plugins** — load-bearing for the §11.8
  STREAMLINE default-ON shipping; spec not yet written
  (`../track2-recovery-rollback.md`).
