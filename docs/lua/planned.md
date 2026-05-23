# Planned — not yet available
> Part of the [kcdx Lua API](index.md).

The following appear in the kcdx authoring model and the restructure plan but
are **not callable today** — there is no binder for them. Do not write code
against them.

- **`kcdx.cosave.*`** — per-save persistence (read/write data tied to a save).
  Tracked in the restructure plan, Phase 2; not built yet.
- **`kcdx.scan{...}`** — diagnostic AOB scan as a top-level verb. Tracked in the
  restructure plan, Phase 2; not built yet. (For runtime scanning today, use the
  `kcdx.memory.scan_pattern*` domain calls.)

Gameplay domains (`kcdx.player.*`, `kcdx.world.*`, `kcdx.dialogue.*`,
`kcdx.quest.*`, `kcdx.inventory.*`, `kcdx.assets.*`) are roadmap items (Phase 9+)
and are not built.
