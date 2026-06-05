# Asset-replacement design — changelog (newest first)

## 2026-06-04 (latest) — §5.4: boot-asset runtime serve is the Lua-VM-lifecycle boundary, deferred to Phase 11

- **The finding (KI-0005, root-caused by PROBE B).** A Lua runtime
  `register`/`replace` of a BOOT asset (cap-75 replacing the menu logo) did not
  serve in-game. Root cause: the engine opens the boot logo ONCE, at boot, BEFORE
  `plugin.lua` runs the register (the Lua VM is created in `CSystem::Init`, the
  same phase that opens + caches the logo), then re-uses the cached texture
  without re-opening the file. The runtime store is NOT buggy — its lookup was a
  correct pre-register MISS; the verb simply runs too late for a boot asset.
- **The framing corrected.** §5.1 take-effect="thereafter" was being read as a
  hard contract boundary ("a boot-cached asset is unreachable, period"). The user
  reframed it correctly: it is the Lua-VM-LIFECYCLE boundary, not a store limit —
  kcdx controls WHEN it applies a declaration (the declarative sidecar already
  wins boot assets, parsed pre-VM in `DiscoverAndLoad`). The Lua runtime verb just
  doesn't reach the early window because the engine VM isn't up yet.
- **§5.4 (new) — the WHEN, by surface.** Declarative sidecar (data, no VM, early)
  + C++ `Plugin_Load` (early) win a boot open; the Lua runtime verb (post-VM,
  late) does not, TODAY. The author rule: a boot/early asset → declarative sidecar;
  the Lua runtime verb → assets opened later (gameplay/on-demand).
- **Deferred to Phase 11 (the user's call, mirroring before_game-hooks).** The
  root is identical to before_game Lua hooks (the Lua VM not up at DllMain); FIX A
  (`fix-a-drop-static-lua.md`) brings the VM up at DllMain so `plugin.lua` can run
  before the boot open. Kept as one coherent Phase-11 deliverable, not a
  now-workaround that duplicates the VM work. **Phase 11's design (`before-game-
  hooks.md`) explicitly accounts for the boot-asset runtime serve** — the early
  Lua slot, the order vs the boot open, and the serve confirmation are a named
  Phase-11 deliverable (the user's instruction).
- **Until Phase 11 — AP14 teaching, not silence.** A Lua runtime verb targeting a
  boot-opened vpath emits a one-time teaching warn (use the sidecar; runtime boot
  replace is Phase 11) — never a silent non-serve. Tracked with KI-0005's close.
- **§9 serve confirmation re-vehicled.** The runtime-store live serve confirmation
  uses an AFTER-VM asset (opened after `plugin.lua` runs), independent of the boot
  window — it proves the store serves live now.
- **Integrated in:** §5.1 (take-effect bullet — the VM-lifecycle clarification),
  §5.4 (new), the changelog.
- **Why:** building/closing on "a boot-cached asset is just out of scope" would
  silently drop a real capability the user wants (`spec-conformance.md` §"No
  filling a spec gap"; `anti-patterns.md` AP13) — it is deferred with a named
  trigger (Phase 11) + an explicit Phase-11 design account, not dropped.

## 2026-06-04 (later) — §5.3: cross-mod resolution defined; the "later phase" over-deferral corrected

- **The error corrected.** §12 "Cross-plugin reference shape" SETTLED the cross-mod
  reference form (the `<author>.<plugin>.<bare>` namespace + string-key, *rejecting*
  the owner-string-arg alternative) — cross-mod reference + replace (US-3/US-4) were
  never deferred. But the build + the earlier §5.1/§5.2 wording deferred their
  RESOLUTION to "a later phase" (`asset_sidecar.h` `PublishedName`/`PluginPathPair`
  comments; the build-time `overlay_decl_scoped_out` path; the §5.1 "later phase"
  framing this changelog's own prior entry introduced). That over-deferral
  contradicted §12 and the US-4 acceptance. User direction: cross-mod replace IS in
  scope; correct it.
- **§5.3 (new) — the resolution mechanism (the user settled every fork in a consult):**
  a published name resolves to **the vpath its asset SERVES AT** (its add-new vpath,
  or the vanilla vpath it replaces) — the same index every shared name uses
  (`naming-namespaces.md`), the exact `hook` shape (name → resolved target, the
  disassembler test). Cross-mod `replace` = (1) resolve the packed name → serve-vpath
  (the published-name store carries it), (2) key the overlay store by that vpath, B
  wins by load order. The owner+path pair (`replaces_plugin`+`replaces_path`) resolves
  the same two-hop way. The runtime verb AND the declarative sidecar share the one
  resolution. **Rejected:** resolving to only the publisher's own path (fails US-4 for
  the A-replaces-vanilla / B-replaces-A chain).
- **§5.1 amended:** the published-name store carries BOTH the loadable disk path
  (`get_by_name`) AND the resolved vpath (cross-mod `replace`).
- **§5.2 amended + §12 row added.** Build split refined: own-namespace verbs +
  vanilla `replace` first; cross-mod resolution its own step after, against §5.3.
  Until it lands, a cross-mod `replace` target returns a teaching error
  ("cross-mod resolution lands next step"), never a silent non-serve (AP14).
- **Integrated in:** §5.1 (store-shape bullet), §5.2 (build split), §5.3 (new), §12
  (new row).
- **Why:** building to a spec that deferred cross-mod resolution is what produced the
  non-serving runtime `replace` packed form; the spec is corrected FIRST
  (`spec-conformance.md`) so the build (the build-time `scoped_out` path, the runtime
  verbs, the store shape, the sidecar forms) lands against the right mechanism. The
  author surface (the verb shapes) is UNCHANGED — only the engine-side resolution.

## 2026-06-04 — §5.1/§5.2: the runtime-store mechanism settled (the §5 verb-table gap)

- **The gap.** v2 specified the five `kcdx.assets.*` verb SHAPES (§5) but was
  silent on the engine-side STORE the runtime verbs need. Step-8 build surfaced
  it; architect-review confirmed it genuine (no store mechanism anywhere in the
  design); a focused consult (2026-06-04) settled it.
- **§5.1 settled (all forks the user's call):** (a) a SEPARATE runtime store, NOT
  a mutation of the lock-free build-time `g_overlayMap` (the resolver consults
  both); (b) lock-free reads via an atomic-pointer (RCU) snapshot — wait-free,
  allocation-free hot read; copy-on-write writer (writes are rare author calls);
  (c) take-effect = "thereafter" (design-determined by §3 US-6 — after-the-call,
  no re-resolve of open handles); (d) a new responsibility unit
  `src/asset_namespace.{h,cpp}` owns BOTH the runtime-overlay store AND the
  published-name store (distinct concern from the build-time map + hooks —
  `structure-by-responsibility.md`).
- **§5.2 build split:** `get_by_path` (a pure read, depends on none of §5.1)
  ships first; the four store-dependent verbs (`get_by_name` / `declare` /
  `register` / `replace`) build later AGAINST §5.1, carrying an NYI doc entry +
  a deliberately-failing matrix row until built (`incremental-delivery.md` —
  dependency ordering, NOT a capability cut; end state is all five at full
  parity).
- **Integrated in:** §5 (new §5.1 + §5.2 after the verb table).
- **Why:** the runtime store is cross-thread shared state (written from Lua,
  read by the hot resolver on engine I/O threads); the mechanism is a
  concurrency design call the user owns (`concurrency.md`, `memory.md`,
  `design-authority.md`), not an executor default (`spec-conformance.md`,
  `anti-patterns.md` AP13). The author surface (§5 shapes) is UNCHANGED.

## 2026-06-03 — v2: the seam corrected to TWO hooks (resolver decision + own-FILE* open), on the gated load-path research

- **The seam is two coordinated hooks, not one.** kcdx owns (1) the resolution
  DECISION by replacing `CCryPak::AdjustFileName` (slot 1, id 152) — which file
  wins, all classes, both byte-lanes — AND (2) the loose OPEN by returning its own
  CRT `FILE*` (the gate-verified Around-`FOpen` mechanism). The v1 "a single
  replaced function is the whole seam" is corrected.
- **Why (the gated findings that corrected v1):** the asset load-path map (commit
  `3193e84`, 6 fronts, §4.5-gated) established that every class opens via `FOpen`
  but bytes arrive on TWO lanes — a loose lane (`FOpen` mints a `FILE*`) and a
  mount/stream lane (pak-resident assets read on a mount-minted `CreateFileA`
  handle, NOT via `FOpen`). The resolver (slot 1) is `sys_pakPriority`-gated — at
  the published default it tests pak-only, so the engine never resolves a vanilla
  path to a loose overlay on its own. So slot-1-alone can't serve a loose file's
  bytes, and FOpen-alone can't replace a pak-resident (vanilla) asset. Owning the
  decision (above the gate) + the open (own handle) covers both.
- **The loose OPEN is settled to return-our-own-`FILE*`** (over a path-only
  redirect), so kcdx never depends on the engine's loose-search — the layer the
  prior path-redirect FAILED at. Gate-verified end-to-end (commit `e6e8e27`).
- **`sys_pakPriority`** stays out of the mechanism (neither set nor depended on);
  the durability point holds.
- **§4.3 staging RETIRED** — HOOK 2 owns the handle, so no `<game>/Data/` staging
  tree / lifecycle is needed; the file is served straight from `assets/`.
- **§9 build-gated unknowns reduced** to runtime acceptances (the two hooks serve
  per-lane live; ctor-vs-first-read ordering; the cFn-ABI pointer-return; the
  default-off DirectStorage texture arm) — the v1 "does the handle-consumed loose
  overlay resolve / does it need staging" mechanism-unknown is gone.
- **§10.2 gains a second sweep target** — the falsified id-152 seed prose
  ("FOpen calls slot 1 / single chokepoint / independent of sys_pakPriority").

**Integrated in:** header "Key changes", §1, §4.3, §7, §8, §9, §10.2, §12.
**Why:** the v1 mechanism rested on the now-falsified "FOpen calls AdjustFileName"
inference (caught by the AP19 gate); the gated load-path map + FOpen-handle finding
replace it with the verified two-lane / two-hook model. The author surface (§1–§6),
the add+replace scope, and the manipulation deferral (§10–§11) are UNCHANGED — the
correction is engine-internal (how kcdx serves the bytes), below the author surface.

## 2026-06-02 — v1: initial canonical asset-replacement design (promoted from the Phase-8.5 planning tree)

- Promoted the Phase-8.5 asset-design to the canonical `docs/design/` home.
- Author surface settled: one `assets/` folder, explicit per-asset sidecar
  declarations + `kcdx.assets.*` code verbs, navigable `kcdx.plugin.<a>.<p>.*`
  cross-plugin refs, add + replace (manipulation deferred).
- Mechanism (SUPERSEDED by v2): "REPLACE `CCryPak::AdjustFileName` as the single
  seam, independent of `sys_pakPriority`" — rested on the "FOpen calls slot 1"
  inference the v2 gated research falsified.
