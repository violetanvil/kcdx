# Asset system — implementation plan

kcdx's **asset system** — the full surface by which a mod author adds, references,
publishes, composes, and replaces game assets: one `assets/` folder, explicit
replacement declarations (sidecar or code), navigable `kcdx.plugin.<author>.<plugin>.*`
cross-plugin references, runtime registration/publishing, transparent per-class
handling. The most-touched mod-authoring surface for total conversions.

**Settled design:** [`../../design/asset-replacement.md`](../../design/asset-replacement.md)
**v2** (the canonical `§`-structured TRD, committed `9c891b1` — the seam is TWO
coordinated hooks: HOOK 1 replaces `CCryPak::AdjustFileName` (slot 1, id 152) for
the resolution DECISION, HOOK 2 returns kcdx's own CRT `FILE*` for the loose OPEN;
both install in the already-shipping ready-bracket; `sys_pakPriority` neither set
nor depended on). **Shared spec + coverage map:** [`plan-spec.md`](plan-spec.md).

The `[entrypoints].assets` parse + the load-order overlay map (`2588b33`) are
**already built + live** — the landed foundation (see `plan-spec.md`). The
superseded FOpen-based plan is archived at
[`../../archive/asset-replacement-fopen-plan/`](../../archive/asset-replacement-fopen-plan/README.md).

This tree is the navigable plan: one phase subdir, one doc per shippable
(commit-grain) step. A landed step flips its step-grain row in the phase README; a
phase's row here flips to `DONE` when all its steps land. The ledger is the
completion record — `/execute` reads each step doc as its `Source work-item` and
flips the row.

## Phase ledger

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash
when `DONE`, `—` otherwise.

| Phase | Status | Commit |
|---|---|---|
| [1 — resolution ownership (the TWO-hook seam, probe-gated)](phase-01-resolution-ownership/README.md) | DONE | 2b0bd1b |
| [2 — author surface (namespace + Lua + C++)](phase-02-author-surface/README.md) | NOT STARTED | — |
| [3 — regression coverage](phase-03-regression/README.md) | NOT STARTED | — |

## Where we are (2026-06-04) — Phase 1 built, batched acceptance + follow-ups owed

**Phase 1 is fully built + committed** (every step DONE in the phase-1 ledger):
the two-hook seam (HOOK 1 AdjustFileName resolver `4a687f3`; HOOK 2 own-`FILE*`
loose open `9590dd4`) + the declaration-required sidecar model (`2b0bd1b`), on the
probe-gated foundation (ordering `d0eadc5`, DS-bypass, outBuf-contract `c28f53d`).

**Proven LIVE:** the `.dds` memory-mapped overlay serves end-to-end through both
hooks — including the cross-runtime `FILE*` question (kcdx's `/MT` CRT handle read
by the game's separate CRT — the dual-Lua hazard class), confirmed portable (the
gray rectangle rendered, no crash). The two hooks install in the ready-bracket and
key off the overlay map.

**Owed before Phase 1 is acceptance-closed (a batched launch, not new code):**
- **Phase-1 acceptance launch** — cap-73's 3 sidecar rows (declared-applies /
  missing-target-loud-error / conflict-line) confirmed in-game, AND the
  **handle-consumed (`.lua`/`.xml`) lane** proven end-to-end. The handle-consumed
  lane is NOT exercisable at boot-to-menu (the engine doesn't open those through
  CCryPak that early — ground-truth from the boot-vpath observer); it needs an
  **in-game gesture** (load a save/level so the engine opens a game script/data
  through CCryPak). Natural home: the Phase-3 step-10 regression plugin (a
  `console`/`in-game`-mode test), per `_research/asset-fopen-handle-recon/FINDINGS.md`.

**Two scoped follow-ups surfaced during Phase 1 (deferred with a named trigger,
NOT dropped):**
1. **Vanilla-target existence oracle** — validating a sidecar's `replaces` vanilla
   vpath actually exists at map-build (calling engine leaves id 153/154) is gated
   on the §8 ctor-vs-first-read install-timing (calling engine fns pre-ready is
   that hazard). Disabled (`nullptr`) in step 5; the resolver MISS path is the
   backstop. Wire when the install-timing window is settled.
2. **`name`-publish + cross-mod reference resolution** — a sidecar's optional
   `name` (publish as `<author>.<plugin>.<name>`, US-5) and the
   published-name/`replaces_plugin`+`replaces_path` cross-mod targets (US-3/US-4)
   are parsed + scoped-out today (no asset-namespace store yet). Built in **Phase 2**
   (the navigable namespace + `kcdx.assets.*` surface). This is the natural
   unblocker for both.

## Phase intents

- **Phase 1 — resolution ownership (the two-hook seam).** Two probes FIRST: the
  seam-install ordering (`ModManager_ctor` vs the first asset read, §8 — findings
  captured) and the DirectStorage texture-arm bypass (§7 caveat, default-off,
  confirm before shipping). Then the two hooks: HOOK 1 replaces
  `CCryPak::AdjustFileName` (the resolution DECISION — overlay HIT decides kcdx's
  file wins, MISS calls through the leaves; removes the dead FOpen/SEAM-A residue);
  HOOK 2 returns kcdx's own CRT `FILE*` for the loose OPEN (no dependence on the
  engine's loose-search), installed alongside HOOK 1 in the ready-bracket. Then the
  per-asset sidecar declarative model + load-order conflict reporting. Ends with
  overlay replacement working in-game (add-new via the loose lane, replace-vanilla
  via the resolver redirect into the pak/mount lane), and stock paks unchanged.
- **Phase 2 — author surface.** The navigable `kcdx.plugin.<author>.<plugin>.*`
  namespace (`__index` resolver chain, the general cross-plugin primitive); the
  stale-comment sweep; the `kcdx.assets.*` Lua surface (add/reference/publish/
  register/replace); the `kcdxAssetInterface` C++ mirror (full parity). Ends with
  authors able to reference, publish, and register assets in code, cross-plugin.
- **Phase 3 — regression coverage.** The permanent `cap-NN`/`comp-NN` test plugin(s)
  exercising override (US-1), cross-plugin reference (US-3), the chain/conflict path
  (US-4), and stock-pak backward-compat (US-7).
