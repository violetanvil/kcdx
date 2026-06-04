# Phase 2 step 8b — the four runtime verbs + the `asset_namespace` runtime store

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 8b.

## What

The four `kcdx.assets.*` verbs that need a runtime store — `get_by_name(name)`
(US-5), `declare(name, file)` (US-5), `register(vpath, file)` (US-6),
`replace(target, with)` (US-6) — plus the runtime store unit they read/write.
Step 8 shipped `get_by_path` (the pure read) and left these four as NYI doc
entries + deliberately-failing matrix rows; this step builds them to the settled
§5.1 mechanism and flips those rows to PASS.

This step exists because the runtime-store mechanism was an unsettled-design gap
when the original five-verb step 8 was attempted: the design specified the verb
SHAPES (§5) but not the engine-side store. The gap was surfaced (architect-review
confirmed it genuine), routed to a focused consult, and settled in design §5.1
(2026-06-04). This step builds to that settled mechanism.

## Scope

- **`src/asset_namespace.{h,cpp}` (NEW unit — `structure-by-responsibility.md` /
  `no-monolith.md`):** owns BOTH runtime stores — the runtime-overlay store (for
  `register`/`replace`) AND the published-name→path store (for
  `declare`/`get_by_name`/the §6 cross-plugin `get_by_name`). A distinct
  responsibility from `src/asset_overlay.cpp` (the build-time map + the two
  hooks), so it is its OWN unit, not bolted onto the hot-path resolver file.
- **Lock-free RCU-snapshot reads (design §5.1):** each store is an immutable
  snapshot behind an `atomic<const Snapshot*>`. A writer (`register`/`declare`/
  `replace` — a one-off author call) builds a new snapshot and swaps the pointer
  (release); the hot resolver loads-acquire and reads a never-mutated snapshot —
  wait-free, allocation-free on the read path (`concurrency.md` atomics-first/
  locks-last; `memory.md` hot-path). The build-time `g_overlayMap`
  (`asset_overlay.cpp`) stays `not-mutated-after-build` and lock-free — UNTOUCHED.
  Old-snapshot reclamation: a generation/epoch or retain-for-session (writes are
  few); the implementer picks, documents the choice. **Surface to the user if the
  reclamation approach needs a design call** (`design-authority.md`).
- **The resolver consults the runtime store ALONGSIDE the build-time map.** Wire
  `AdjustFileNameResolver` (HOOK 1) + `FOpenLooseOverlay` (HOOK 2) to check the
  runtime-overlay snapshot as well as `g_overlayMap` — both lock-free reads.
- **Take-effect = "thereafter" (design-determined, §3 US-6):** a runtime
  `register`/`replace` affects assets opened AFTER the call; no re-resolve of
  already-open handles. Do NOT build a re-resolve mechanism.
- **The four verbs in `src/lua_bind_assets.cpp`** (the unit step 8 created): each
  reads/writes `asset_namespace`. `declare` publishes `<author>.<plugin>.<name>`;
  `get_by_name` resolves a published name (own, no prefix) to a loadable path;
  `register` adds a runtime vpath→file overlay; `replace` registers a runtime
  replacement (incl. the string-key `replace("author.plugin.asset", with)` cross-
  plugin form). A missing name / bad target → a teaching error (AP14), never a
  silent nil.
- **Wire the `.assets.get_by_name` leaf** on the step-6 cross-plugin handle (the
  §6 `kcdx.plugin.<a>.<p>.assets.get_by_name` form resolves through it into the
  published-name store).

## Test bar

The NYI / deliberately-failing matrix rows step 8 pinned now flip to PASS. A
`cap-NN` suite-gated plugin (or extend step 8's): `declare` then `get_by_name`
(own) resolves; the §6 cross-plugin `get_by_name` resolves another plugin's
published name; `register` then an in-game open of that vpath serves kcdx's file
(`in-game` — the seam serves it); `replace` then the target resolves to the new
file thereafter. Each a FALSIFIABLE claim (AP15 — FAILS if the verb returns
nil/a wrong path, or the runtime overlay does not take effect). Build green;
live-confirmed via the launch (`acceptance-signal.md`).

## Dependencies

**Step 8** (the `kcdx.assets.*` table + `src/lua_bind_assets.cpp` + the
`get_by_path` verb + the NYI rows these flip). **Design §5.1** (the settled
runtime-store mechanism — this step builds to it). **Phase 1 HOOK 1 + HOOK 2**
(the resolver this step wires the runtime store into). Ordered after step 8 so the
table + the pure-read verb exist, and after the §5.1 design amendment so the store
mechanism is settled (`incremental-delivery.md`, `spec-conformance.md`).

## Reference

Design authority: [`../../../design/asset-replacement.md`](../../../design/asset-replacement.md)
§5.1 (the settled runtime-store mechanism — the load-bearing authority for this
step) + §5.2 (the build split) + §5 (the verb table) + §6 (the cross-plugin
`get_by_name` form) + §3 US-5/US-6 (acceptance, incl. take-effect "thereafter").
Concurrency: `.claude/rules/concurrency.md` (atomics-first/locks-last; the
RCU-snapshot shape) + `.claude/rules/memory.md` (hot resolver read stays
allocation-free). Shared spec: [`../plan-spec.md`](../plan-spec.md).

## Disassembler-test / author-burden

The author calls `kcdx.assets.declare("shirt", "male/shirt.dds")` /
`get_by_name("shirt")` / `register(vpath, file)` / `replace(target, with)` — a
name or a path, no engine internal, no asset class, no RVA, no knowledge of the
store mechanism (the disassembler test, `cornerstones.md`). Errors teach. The
runtime-store concurrency is entirely engine-internal — invisible to the author.
No hand-written hex/ABI.
