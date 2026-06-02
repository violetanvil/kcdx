# UI screens — index

Single-concern screen specs for the maintainer tool's PySide6/Qt6 GUI. Each file specs one
screen/region; the authoritative design system (tokens, laws, window skeleton, component
silhouettes) is **[`../design.md`](../design.md)** — these screens implement it and
reference it by name (a value or law duplicated into a screen is drift).

| Screen | Region | Spec |
|---|---|---|
| s01 — Entity navigator (search · filter · list) | left pane | [s01-navigator.md](s01-navigator.md) |
| s02 — Entity detail (header · lifecycle · version area) | right pane | [s02-entity-detail.md](s02-entity-detail.md) |
| s03 — Version history + side-by-side compare | right pane | [s03-version-history-compare.md](s03-version-history-compare.md) |
| s04 — Field editor (view / edit a version row) | right pane | [s04-field-editor.md](s04-field-editor.md) |
| s05 — Create new entity / new version | modal + right pane | [s05-create.md](s05-create.md) |
| s06 — Save confirm (field delta · approval) | modal | [s06-save-confirm.md](s06-save-confirm.md) |
| s07 — Status bar + DLL link (verification context) | bottom bar | [s07-status-dll-link.md](s07-status-dll-link.md) |
