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
| [6 — navigable namespace `kcdx.plugin.<author>.<plugin>.*` (`__index` chain)](step-6-navigable-namespace.md) | NOT STARTED | — |
| [7 — stale-prose sweep (dotted-`__index` prose + the falsified id-152 seed prose)](step-7-stale-prose-sweep.md) | NOT STARTED | — |
| [8 — `kcdx.assets.*` Lua surface (reference/publish/register/replace)](step-8-lua-surface.md) | NOT STARTED | — |
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
- **Step 8** verified (live) by a plugin referencing its own asset (`get_by_path`),
  referencing another mod's asset (`kcdx.plugin.<a>.<p>.assets.get_by_name`),
  publishing (`declare`), and registering at runtime (`register`) — each returning
  a loadable path / applying.
- **Step 9** verified by the C++ mirror of each verb producing the SAME result as
  its Lua peer (parity), with the InputLoaded listener-count check confirming no
  ABI break (AP11).
- This phase touches an authoring API surface, not UI — no end-user visual UX gate;
  the "experience" gate is the author-UX bar (disassembler test, errors that teach,
  glanceable docs — `cornerstones.md`, `docs-discipline.md`), verified per step.
