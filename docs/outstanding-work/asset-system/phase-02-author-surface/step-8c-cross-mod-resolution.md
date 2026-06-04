# Phase 2 step 8c — cross-mod resolution (a published name → the serve-vpath)

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 8c.

## What

Resolve a CROSS-MOD reference — a published name (`<author>.<plugin>.<bare>`) or
an owner+path pair — to the **vpath its asset SERVES AT**, so a cross-mod
`replace`/sidecar makes B's asset serve where A's would (US-4). This is the
RESOLUTION mechanism for the cross-mod reference shape §12 already settled; the
prior "resolved by a later phase" framing (the `asset_sidecar.h`
`PublishedName`/`PluginPathPair` comments, the build-time `overlay_decl_scoped_out`
path, the original §5.1/§5.2 wording) was an over-deferral, corrected by design
§5.3 (settled 2026-06-04). Step 8b left the runtime `replace` packed cross-mod
form returning a teaching error ("cross-mod resolution lands next step"); this
step makes it serve.

## The mechanism (design §5.3 — build to it)

A published name resolves to the vpath its asset OCCUPIES/SERVES AT — its own
add-new vpath if the published asset is an add-new, OR the vanilla vpath it
replaces if the published asset is itself a replacement. The same index every
shared name uses (`naming-namespaces.md`), the exact `hook` shape (name →
resolved target). Cross-mod `replace` is two hops: (1) resolve the packed name →
the resolved vpath (the published-name store carries it, §5.1); (2) key the
overlay store by THAT vpath, B winning by load order (§4.4). The owner+path pair
(`replaces_plugin`+`replaces_path`) resolves the same two-hop way (by (owner,
path) instead of by name). The runtime verb AND the declarative sidecar share the
one resolution.

## Scope

- **`src/asset_namespace.{h,cpp}`:** the published-name store carries the resolved
  vpath (the serve-vpath) alongside the disk path (§5.1 amended). A `declare`
  records BOTH (the published asset's serve-vpath + its disk path). A
  name-resolution accessor returns the serve-vpath for the cross-mod-replace
  keying. Same RCU-snapshot concurrency as step 8b (lock-free reads; the resolved
  vpath is just another field on the snapshot's value).
- **`src/lua_bind_assets.cpp`:** the runtime `replace` packed cross-mod form (and
  the §6 cross-plugin path) resolves the packed name → serve-vpath → keys the
  runtime-overlay store by it (replacing the step-8b teaching-error stub). `replace`
  with a vanilla-path target is unchanged (step 8b). A packed name that resolves to
  no published asset → a teaching error (AP14).
- **`src/asset_sidecar.{h,cpp}` + `src/asset_overlay.cpp` (BuildOverlayMap):** the
  `PublishedName` / `PluginPathPair` declarative targets resolve to the serve-vpath
  and the build-time `overlay_decl_scoped_out` path KEYS the resolved vpath (the
  deferral removed) — the same load-order winner/suppressed §4.4 conflict as a
  vanilla-path target. Update the `TargetKind` comments (remove "LATER phase").
- **Load-order dependency note:** A's published asset must be resolvable when B's
  cross-mod replace is processed. The build-time path processes declarations in
  load order — confirm A's publish is visible before B's cross-mod replace resolves
  (a checkable ordering fact — read the build-time pass; surface if a real ordering
  fork). The runtime path: A must have `declare`d before B's `replace` resolves
  (take-effect "thereafter", §3 US-6 — a checkable runtime fact, not a design call).

## Test bar

A `comp-NN` (cross-plugin interaction) suite-gated pair (`test-suite.md`,
`feedback_test_suite_must_grow` — a NEW interaction case, not a migrated row): a
publisher plugin A `declare`s/sidecar-publishes an asset; a consumer plugin B
`replace`s (or sidecar-replaces) A's published name; the engine opening A's
serve-vpath serves B's file (US-4, `in-game` — confirmed by the resolver log
`source=runtime`/`overlay_resolved` naming B as winner). Two plugins replacing
the same cross-mod target → the §4.4 load-order conflict line. Each a FALSIFIABLE
claim (AP15 — FAILS if B does not serve at A's vpath, or no conflict line fires).
The owner+path pair form gets its own row. Build green; live-confirmed via the
launch.

## Dependencies

**Step 8b** (the `asset_namespace` runtime store + the four verbs + the vanilla
`replace` — this step adds the resolved-vpath field + the cross-mod resolution on
top). **Design §5.3** (the settled resolution mechanism). **Phase 1 HOOK 1 + HOOK
2** (the resolver/seam that serves the keyed vpath). **The published-name store
must resolve A's name before B's cross-mod replace** (load-order / take-effect
ordering — §5.3). Ordered after 8b so the store + verbs exist, and after the §5.3
design amendment so the mechanism is settled (`incremental-delivery.md`,
`spec-conformance.md`).

## Reference

Design authority: [`../../../design/asset-replacement.md`](../../../design/asset-replacement.md)
§5.3 (the cross-mod resolution mechanism — THE authority) + §5.1 (the store carries
the resolved vpath) + §6 (the cross-plugin form) + §12 (the cross-mod-resolution
decision row) + §3 US-3/US-4 (acceptance — "serve where A's would" + load-order
conflict) + §4.4 (the conflict-report shape). Shared spec:
[`../plan-spec.md`](../plan-spec.md). The index model: `.claude/rules/naming-namespaces.md`
(the `<author>.<plugin>.<bare>` index, the SAME as hook).

## Disassembler-test / author-burden

The author writes `replace("redmoon.outfit.belt", with)` (or `replaces =
"redmoon.outfit.belt"`) — a NAME, never the vpath A's belt serves at, never an
address/class (the disassembler test, `cornerstones.md`; the same name→resolved-
target shape as a hook target name → resolved address). The engine carries the
serve-vpath. Errors teach (a name resolving to no published asset names it). No
hand-written hex/ABI/vpath.
