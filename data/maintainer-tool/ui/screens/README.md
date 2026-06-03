# UI screens — index

Single-concern screen specs for the maintainer tool's **React + Mantine web frontend**. Each
file specs one screen/region; the authoritative design system (tokens, laws, app shell,
component silhouettes) is **[`../design.md`](../design.md)** — these screens implement it and
reference it by name (a value or law duplicated into a screen is drift).

| Screen | Region | Spec |
|---|---|---|
| s01 — Entity navigator (search · filter · list) | left pane / phone home | [s01-navigator.md](s01-navigator.md) |
| s02 — Entity detail (header · version&verify surface · lifecycle · version area) | right pane / phone drill-down | [s02-entity-detail.md](s02-entity-detail.md) |
| s03 — Version history + side-by-side compare | detail | [s03-version-history-compare.md](s03-version-history-compare.md) |
| s04 — Field editor (view / edit a version row) | detail | [s04-field-editor.md](s04-field-editor.md) |
| s05 — Create new entity / new version | overlay (modal/sheet) + detail | [s05-create.md](s05-create.md) |
| s06 — Save confirm (field delta · approval) + the toast/overlay concern | overlay | [s06-save-confirm.md](s06-save-confirm.md) |

*(s07 — the desktop status bar + DLL link — is **dissolved** in the web pivot: its
version/verify content moved into s02's header (the version&verify surface); its
save-result + notices became the top-anchored toast, specified in s06's overlay concern.)*
