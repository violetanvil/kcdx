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
| [9.3 — kcdx.hook.* / kcdx.statement.* split + locator/op namespaces + kcdx.functions.*/kcdx.dll.declare (author self-declaration) + multi-region trampoline](phase-09.3-namespaces/README.md) | DONE | all 7 steps live-verified (1 9802a5e · 2 dce5c35 · 3 b290d9e+a29bf8f · 4 4e901db · 5 cca4c1c · 6 879b4c7 · 7a 3c3b473 · 7b 60c178c · 7c fceb276) — the biggest author-surface phase: the hook/statement split, locator/op/functions/dll value+reference namespaces + PDB auto-load, multi-region trampoline, and full C++ parity (cap-94/96/97 GREEN) |
| [9.4 — kcdx.find{...} discovery + kcdx_dev_inspect console](phase-09.4-discovery/README.md) | NOT STARTED | — |
| [9.5 — kcdx.behavior.* named-behavior catalog](phase-09.5-behaviors/README.md) | NOT STARTED | — |
| [9.6 — kcdx.bytes narrowing + Lua-API rule update + final migration](phase-09.6-bytes-narrowing/README.md) | NOT STARTED | — |
| 9.7 — curated-target sub-verb resolver | SUPERSEDED | — |
| [10 — gameplay event catalog (kcdx.on)](phase-10-event-catalog/README.md) | NOT STARTED | — |
| [11 — kcdx owns the one Lua VM (worker-built, engine-adopted)](phase-11-shim-vm/README.md) | IN PROGRESS | P1 f0a0dc9 · P2 3f6e09e/54d98c8 · P3 18c0ac5/3b99fea (DONE); P4–P7 remain |
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
- **11 IN PROGRESS (build half-landed)** — the settled design
  ([`phase-11-shim-vm/lua-vm-design.md`](phase-11-shim-vm/lua-vm-design.md)) is
  decomposed into a 7-phase build tree, and the keystone is BUILT. kcdx builds the
  one Lua VM on its worker thread (NO force-load — PROBE P3 verified a force-load is
  impossible + unnecessary) and the engine ADOPTS it; the dual-Lua sentinel hazard
  dies by construction. **DONE:** P1 keystone probe (f0a0dc9), P2 symbol shim
  (3f6e09e forward + 54d98c8 stubs), P3 early-hook relocate (18c0ac5) + worker-builds-VM
  + engine-adopts (3b99fea — the load-bearing "kcdx owns the one VM" step). The P3
  keystone (3b99fea) is **live-confirmed**; the P2 shim + P3 relocate commits carry
  `[unverified — pending launch]` (a launch confirms them alongside P4). **Remaining:** P4 cross-thread
  foundation (event gate + RegisterRuntimeOverlay CAS) → P5 startup-sequence author
  contract (steps 7–9 in-flight) → P6 drop static Lua → P7 served-`.lua` execute
  (KI-0006). Per-phase ledger + the P4-foundation re-scope:
  [`phase-11-shim-vm/README.md`](phase-11-shim-vm/README.md) +
  [`phase-11-shim-vm/RESUME-STATE.md`](phase-11-shim-vm/RESUME-STATE.md).
- **12 NOT STARTED** — the C++ empowered-wrapper sweep + `sig_traits` correctness
  fix + UX polish; design-settled, detail in [`00-original-plan.md`](00-original-plan.md)
  §"Phase 12". Subdir authored when the phase is picked up.

## Substantive next-pickups

Independent work that can land now (each by leverage, not phase order):
- **Engine-direct hook migration** — 5 remaining `MH_CreateHook` sites move to
  `hook_chain::AddCEngine`; one `/execute` cycle per site; full spec at
  [`../../tech-debt/TD-0003-engine-direct-hook-migration.md`](../../tech-debt/TD-0003-engine-direct-hook-migration.md).
- **Phase 8.5 asset overlay** — SUPERSEDED; re-planned + spun out to the
  standalone [`asset-system/`](../asset-system/README.md) tree (Phase 1 DONE
  `2b0bd1b` + Phase 2 DONE — the two-hook seam + the full Lua+C++ `kcdx.assets.*`
  surface, acceptance-confirmed). asset-system **Phase 3 is NEEDS REWORK** (step 10
  landed `2087368` — cap-77 + comp-17 PASS — but the core served-`.lua` EXECUTE
  criterion is UNCONFIRMED = `KI-0006`, OPEN). `KI-0006` (a heap-corruption crash
  when a mod-init `.lua` overlay is keyed; serve mechanism proven via CAP-73, execute
  leg unconfirmed) is bundled into Phase 11 (user-approved deferral 2026-06-05) and
  is now Phase 11's **P7** step (the one-VM adoption that reworks serve-execute landed
  in P3 3b99fea; P7 confirms the `.lua`-execute leg on it). The boot-cache in-game
  serve (`KI-0005`, CLOSED resolved-by-design) is delivered by Phase 11 P4+P5. The
  shipping capability (textures, XML, cross-mod, conflict, stock-pak) is proven +
  confirmed; only the `.lua`-execute leg waits.
- **Phase 9 high-level Lua surface** — DEFERRED → [`TD-0005`](../../tech-debt/TD-0005-high-level-lua-surface.md).
  Independent, pure RE + binder work (a non-blocking leaf); re-homed to carried
  debt rather than held open on the active ledger. Picked up by scheduling its
  dedicated build phase — the per-step spec stays at
  [`phase-09-high-level-lua/`](phase-09-high-level-lua/README.md).

## Open-items map — tech-debt prerequisites + Phase-11 internal owed items

Full reconciliation 2026-06-10 (`LEDGER-RECONCILE-2026-06-10.md`). The phase
ledger above is the headline; this section is the complete set of cross-cutting
open items, because two of them GATE unbuilt phases.

### Tech-debt that GATES an unbuilt phase (do these first)

- [`TD-0007`](../../tech-debt/TD-0007-unclassified-lua-loader-symbols.md) — 5 Lua C
  API fns unclassified (loadbuffer/loadstring/gsub unwired + newthread/cpcall
  fail-loud). **Must classify before Phase 11 P6 drops static Lua** (the shim can't
  fully serve them yet). Closure gate: a `/research-disassembly` pass before P6.

### Tech-debt for a PRODUCTION surface (not gating an unbuilt restructure phase)

- [`TD-0006`](../../tech-debt/TD-0006-statement-layer-in-user-db.md) — the production
  statement layer is DEV-only; the shipped `reference.sqlite` carries only the curated
  133-fn subset. Backs the PRODUCTION USER-DB statement surfaces (Phase 9.3's
  locator/op/statement, which shipped against the curated subset) + the open per-kind
  model. **NOT a Phase 9.4 gate** — `kcdx.find` / `kcdx_dev_inspect` are dev tools that
  read the DEV DB directly (user-decided 2026-06-10), so 9.4 needs no USER-DB fill.
  Closure: the maintainer tool owning these kinds + projecting them to the USER DB.

### Independent leaf debt (lands anytime, no phase gated on it)

- [`TD-0001`](../../tech-debt/TD-0001-declare-value-string-arena.md) — declare-store value-string arena (same-triple re-Declare use-after-free).
- [`TD-0002`](../../tech-debt/TD-0002-lua-callback-main-thread-guard.md) — dynamic-dispatcher main-thread guard (AP13 gap).
- [`TD-0003`](../../tech-debt/TD-0003-engine-direct-hook-migration.md) — 5 engine-direct `MH_CreateHook` sites → `hook_chain::AddCEngine` (one `/execute` per site).
- [`TD-0009`](../../tech-debt/TD-0009-engine-browser-agreement-superset-kinds.md) — engine↔browser survival-agreement; 3 superset kinds unpinned.
- [`TD-0010`](../../tech-debt/TD-0010-statement-replace-live-native-execution-readback.md) — `replace_with` live native-execution readback (structural proof landed; live proof deferred, bucket-2).
- [`TD-0005`](../../tech-debt/TD-0005-high-level-lua-surface.md) — the DEFERRED Phase 9 itself (high-level gameplay Lua surface); the per-step spec stays at [`phase-09-high-level-lua/`](phase-09-high-level-lua/README.md).

### Phase 11 internal owed items (not phase-ledger rows; see [`phase-11-shim-vm/RESUME-STATE.md`](phase-11-shim-vm/RESUME-STATE.md))

- **P4 is re-scoped to FOUNDATION-ONLY** (event gate + `RegisterRuntimeOverlay`
  two-writer CAS + cap-82 order-inversion regression); the slot-runner + early-bind
  surface moved to P5 (steps 5/7). P4's docs are re-authored to this scope.
- **Event-gate bounded-timeout value** (lean: 5000ms + vanilla-serve + `LOG_WARN_KV`)
  — surfaced, **not yet user-decided**; settled at the P4 build under architect-review.
- **Owed worker-GC-safety probe** (P5 step 1) gates the P5 surface build.
- Step-head probes/reads owed at build: P5 s6 (before-game target ordering, design
  claim 7), P5 s8 (export-name `kcdxPlugin_Preload`-reuse-vs-new read), P5 s7 (the
  AP14 boot-warn narrow/remove decision).

### Other cross-cutting

- [`../fix-a-drop-static-lua.md`](../fix-a-drop-static-lua.md) — the FIX A harvest; Phase 11's P2 shim + P6 drop-static rest on it (no longer an "unblocker" — P1–P3 are built).
- [`../before-game-hooks.md`](../before-game-hooks.md) — Phase 11 consumer (bugsplat builtin; rides the phase, not a step).
- **Recovery + rollback for Track-2 plugins** — load-bearing for the §11.8 STREAMLINE default-ON shipping; spec not yet written (`../track2-recovery-rollback.md`).
