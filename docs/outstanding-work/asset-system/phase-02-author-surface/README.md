# Phase 2 — author surface (namespace + Lua + C++)

The author-facing surface of the asset system: the navigable cross-plugin
namespace (the general `kcdx.plugin.<author>.<plugin>.*` primitive), the
`kcdx.assets.*` Lua verbs (add/reference/publish/register/replace), and the
`kcdxAssetInterface` C++ mirror — full Lua↔C++ parity (`lua-api-surface.md`). The
stale-prose sweep lands here too, after the namespace step proves the `__index`
mechanism it corrects the prose about. The seam (Phase 1) is engine-internal and
below this surface — unchanged by it.

## Step ledger

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash
when `DONE`, `—` otherwise.

| Step | Status | Commit |
|---|---|---|
| [6 — navigable namespace `kcdx.plugin.<author>.<plugin>.*` (`__index` chain)](step-6-navigable-namespace.md) | DONE | a3961df |
| [7 — stale-prose sweep (dotted-`__index` prose + the falsified id-152 seed prose)](step-7-stale-prose-sweep.md) | DONE | 3cc6a67 |
| [8 — `kcdx.assets.get_by_path` (the pure-read verb; the `kcdx.assets.*` table + the cross-plugin `.assets` leaf)](step-8-lua-surface.md) | DONE | c39ac3a |
| [8b — the four runtime verbs (`get_by_name`/`declare`/`register`/`replace`, vanilla-path) + the `asset_namespace` runtime store (design §5.1)](step-8b-runtime-store-verbs.md) | DONE | b7ae899 |
| [8c — cross-mod resolution (a published name / owner+path → the serve-vpath; runtime `replace` packed form + the declarative sidecar `PublishedName`/`PluginPathPair`; the build-time `scoped_out` path → keys the resolved vpath) (design §5.3)](step-8c-cross-mod-resolution.md) | DONE | 2259a76 |
| [9 — `kcdxAssetInterface` C++ mirror (full parity)](step-9-cpp-mirror.md) | NOT STARTED | — |

## Phase verification gate

- **Build green** after every step.
- **Step 6** verified by a probe/test plugin navigating `kcdx.plugin.<a>.<p>.*` and
  the `__index` chain resolving each segment to engine-side data (a non-existent
  segment a teaching error).
- **Step 7** verified by the corrected prose reading true against the as-built
  resolver (no doc claim that dotted dynamic resolution is impossible survives) AND
  the falsified id-152 seed prose corrected to the v2 two-lane/two-hook model
  (design §10.2 — its two sweep targets).
- **Step 8** verified (live) by a plugin referencing its OWN asset
  (`kcdx.assets.get_by_path`) — returns a loadable path — and the cross-plugin
  `kcdx.plugin.<a>.<p>.assets.get_by_path` form resolving through the step-6
  namespace; a path not in the plugin's `assets/` is a teaching error. The other
  four verbs ship as NYI doc entries + deliberately-failing matrix rows pinning
  their contract (design §5.2). (Scope narrowed from the original five-verb step:
  the runtime four depend on the §5.1 store mechanism, settled 2026-06-04 — see
  step 8b. The verb SHAPES were always settled; only the runtime store was the gap.)
- **Step 8b** verified (live) by `get_by_name` / `declare` / `register` /
  `replace` each working against the new `asset_namespace` runtime store (design
  §5.1): publish (`declare`) → resolvable by `get_by_name` (own) and the §6
  cross-plugin form; runtime `register` + **vanilla-path** `replace` take effect
  "thereafter" (an asset opened after the call resolves to kcdx's file). The NYI
  rows from step 8 flip to PASS. The runtime store's RCU-snapshot reads are
  lock-free (the build-time `g_overlayMap` stays untouched) — reviewed against
  `concurrency.md`. The **packed cross-mod** `replace` target returns a teaching
  error ("cross-mod resolution lands next step", §5.3) — never a silent non-serve
  (AP14); the cross-mod SERVE is step 8c.
- **Step 8c** verified (live) by cross-mod resolution serving end-to-end (design
  §5.3): a published name (or owner+path pair) resolves to the vpath its asset
  SERVES AT; a `replace`/sidecar targeting it makes B's asset serve where A's
  would (US-4), the load-order winner serving + the §4.4 conflict line. The
  build-time `overlay_decl_scoped_out` path now keys the resolved vpath (the
  deferral removed); the runtime `replace` packed-form teaching error flips to a
  real serve. Both the runtime verb and the declarative sidecar form resolve
  identically (the one §5.3 resolution).
- **Step 9** verified by the C++ mirror of each BUILT verb producing the SAME
  result as its Lua peer (parity), with the InputLoaded listener-count check
  confirming no ABI break (AP11). (Mirrors whichever verbs exist when it runs —
  ordered after 8c so it mirrors the full surface incl. cross-mod.)
- This phase touches an authoring API surface, not UI — no end-user visual UX gate;
  the "experience" gate is the author-UX bar (disassembler test, errors that teach,
  glanceable docs — `cornerstones.md`, `docs-discipline.md`), verified per step.
