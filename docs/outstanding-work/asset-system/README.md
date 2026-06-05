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
| [2 — author surface (namespace + Lua + C++)](phase-02-author-surface/README.md) | DONE (6/6: steps 6/7/8/8b/8c/9 DONE) | (landed) |
| [3 — regression coverage](phase-03-regression/README.md) | IN PROGRESS — step 10 landed (cap-77 + comp-17 + the matrix; CAP-77-keyed + COMP-17 PASS), but a CORE acceptance criterion is UNCONFIRMED: a served `.lua` EXECUTING (design §3, the handle-consumed-lane completeness). KI-0006 — the `.lua` serve-AND-EXECUTE gap, being closed NOW (not deferred). The serve mechanism is proven (CAP-73); whether a served `.lua` executes through this seam is unverified + possibly a CAPABILITY gap (does CryEngine Lua `require`/`dofile` route through `CCryPak::FOpen`?). | (landed) |

## Where we are (2026-06-05) — Phases 1–2 built + acceptance-confirmed; Phase 3 BLOCKED → Phase 11 (the `.lua`-execute confirmation = KI-0006, bundled into Phase 11)

**Phases 1–2 are built and acceptance-confirmed.** Phase 1 (the two-hook
resolution seam) is acceptance-closed (`2b0bd1b`). Phase 2 (author surface) is 6/6
and live-verified: steps 6 (`a3961df`), 7 (`3cc6a67`), 8 (`c39ac3a`), 8b
(`b7ae899`), 8c (`2259a76`), 9 (`fe879d0` — the C++ mirror, cap-76 parity rows ALL
PASS live). The full `kcdx.assets.*` surface is live on BOTH languages, one shared
resolution path, full parity. Proven live: replace-vanilla (sidecar), add-new
(loose lane, HOOK 2 serves bytes), cross-mod, conflict, stock-pak transparency
(US-7, COMP-17 PASS).

**Phase 3 is BLOCKED on KI-0006, which is now bundled into Phase 11.** Step 10
landed the matrix + cap-77 + comp-17 (CAP-77-keyed + COMP-17 PASS), but ONE core
acceptance criterion is UNCONFIRMED: **a served `.lua` actually EXECUTING** (design
§3 — the handle-consumed-lane completeness; the highest-value case for a *scripting*
TC). The serve MECHANISM is proven (CAP-73 — bytes are served); the execute leg is
**KI-0006**. Investigation (4 probes) established: serving a `scripts/mods/<modid>.lua`
overlay correlates with a heap-corruption crash; 3 theories falsified (record-synth,
re-entrancy, mod-init-serve); the cross-CRT `FILE*` free is confirmed-real (WHGame's
`fclose` frees kcdx's `/MT` handle) but is NOT the trigger; the crash tracks a
keyed-but-unopened `overlay_entry` (unexplained). **Bundled into Phase 11**
(user-approved deferral 2026-06-05): FIX A collapses the dual-runtime that creates
the confirmed cross-CRT-free hazard, reworks the serve-execute / VM-lifecycle area,
and gives a kcdx-controlled instrumentable execution slot — so the re-attempt
happens against the architecture KI-0006 ships on, not the soon-replaced one. NOT a
guaranteed fix (the corrupting write is unidentified), but the right sequencing.
Until then, `.lua` REPLACEMENT's execute leg is unconfirmed; the rest of the asset
system (textures, XML, cross-mod, conflict, stock-pak) ships value and is confirmed.

**Also owed:** step-9's cap-76 boot rows confirmed live (`fe879d0`, all PASS); the
in-game register/replace SERVE for a boot-cached asset remains DEFERRED → Phase 11
(KI-0005, the boot-cache lifecycle gap — a user-approved deferral, distinct from
KI-0006 which we are NOT deferring).

Two design gaps were caught + settled mid-build (not assumed): the **runtime-store
mechanism** (§5.1 — the RCU `asset_namespace` store, settled by consult) and
**cross-mod resolution** (§5.3 — a published name resolves to the vpath its asset
serves at; the "later phase" over-deferral corrected). Both built to the settled
design (`asset-replacement.md` changelog 2026-06-04).

**Phase-2 acceptance launch — boot rows PASSED** (the 2026-06-04 runs,
`test-plugins/README.md`): cap-74 (namespace nav ×4), cap-75 (the boot
`kcdx.assets.*` rows incl. the path-return + the boot-asset teaching warn),
COMP-16-replace-code (cross-mod resolution). The runtime store keys + resolves the
verbs correctly at boot (path-return + the two-hop §5.3 resolution PASS). **The
in-game SERVE of a runtime register/replace is DEFERRED → Phase 11 → KI-0005
(closed, resolved-by-design):** a Lua runtime `register`/`replace` runs at
`plugin.lua` time, after the engine opens (and GPU-caches) every boot/menu asset,
so the post-VM store key can never win the open — boot assets use the declarative
sidecar today; the Lua-runtime boot serve is deferred to the DllMain-VM phase
(`before-game-hooks.md` §6b). An AP14 teaching warn shipped (`4eaa60d`) and PASSED
(`19-44-47` run). The two in-game serve matrix rows (`CAP-75-register-serve`,
`COMP-16-serve-code`) are **DEFERRED → Phase 11** (`dd231cc`): a 2026-06-04 attempt
to find an after-VM serve vehicle by the FOPEN open-count failed — the picked
vpaths (`apse/*.dds`) were themselves boot-cached, and the open-count cannot
distinguish a first-open from a cache-refresh
(`_research/probe-archive/ki0005-resolver-dds-observer.md` §"DEAD END"). Phase 11's
DllMain VM + an instrumented after-`NotifyVmReady` probe owns the confirmation.

---

**Phase 1 is fully built, committed, and acceptance-closed** (every step DONE in
the phase-1 ledger): the two-hook seam (HOOK 1 AdjustFileName resolver `4a687f3`;
HOOK 2 own-`FILE*` loose open `9590dd4`) + the declaration-required sidecar model
(`2b0bd1b`), on the probe-gated foundation (ordering `d0eadc5`, DS-bypass,
outBuf-contract `c28f53d`).

**Acceptance launch — PASSED (2026-06-04, batched).** All 4 cap-73 matrix rows
GREEN (see `test-plugins/README.md`):
- **cap-73 sidecar rows (boot-only)** — declared-applies (keyed by declared
  target), missing-target (loud reject, not silent), conflict (load-order
  winner/suppressed line). All confirmed live in `kcdx-dev.log`.
- **`.dds` memory-mapped lane** — overlay serves end-to-end through both hooks;
  the cross-runtime `FILE*` (kcdx's `/MT` CRT handle read by the game's separate
  CRT — the dual-Lua hazard class) is portable (gray rectangle rendered, no crash).
- **handle-consumed `.lua` lane (step-4 gate: BYTES served via own-`FILE*`)** —
  PROVEN in-game. A save load opens 28,266 distinct handle-consumed vpaths
  (`.xml`/`.lua`/`.cfg`) through `CCryPak::FOpen`; HOOK 2 recognizes a keyed
  `.lua` overlay (`map=HIT`) and returns its own CRT `FILE*` for it
  (`probe_fopen_hc_served vpath="scripts/main.lua"`). Finding:
  `_research/asset-fopen-handle-recon/seamA-handle-consumed-served-LIVE.md`.

**One residual moved to Phase-3 step-10 (NOT a defect, NOT a Phase-1 blocker):**
the served-`.lua`-EXECUTES marker (`KCDX_SEAMA_LUA_LOADED`) needs a startup
script the engine RUNS on a save load (`scripts/main.lua` is the already-init'd
boot chunk — opened by HOOK 2, not re-run mid-game, so its served bytes don't
execute this load). The permanent step-10 regression plugin uses a startup-script
vehicle (`scripts/startup/sl_saveload.lua` etc., which the FOpen observer saw the
engine run on load) to prove serve-AND-execute end-to-end.

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
