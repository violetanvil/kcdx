# s07 — Status bar + DLL link (verification context)

**Phase & fidelity:** v1, high.

## Purpose / when shown
The persistent bottom strip, always present. Holds the verification context (the linked
game DLL and its resolved version), the result of the last save, and transient notices. It
is where the maintainer establishes the verify context that makes "current row" resolution
and re-verification meaningful (`../design.md` §6, R12) — without it ever blocking work
(law 4).

## Region & position
A fixed bottom bar across the full window width (`../design.md` §"Window skeleton"). Three
segments: **DLL link** (left), **last-save result** (center/right), **transient notice**
(overlays briefly, fades). It navigates nothing and overlays nothing (law 3); it only
reports + hosts the link control.

## Contents
| Element | Component | Data bound | Intent emitted |
|---|---|---|---|
| DLL-link state | `status-bar segment` | linked DLL path + resolved version (mono), or "No DLL linked" | — |
| `[Link DLL…]` | `ghost button` | opens a file picker for a game DLL | `link_dll(path)` |
| `[Unlink]` | `ghost button` (when linked) | — | `unlink_dll()` |
| Last-save result | `status-bar segment` | "Saved `<name> <version>`" / "Save blocked — Retry" / "" | `retry_save()` (when blocked) |
| Transient notice | `status-bar segment` (fade) | a brief one-line notice (validation summary, a non-blocking warning) | — |

**The DLL link = the verification context.** `[Link DLL…]` points the tool at a game DLL;
the resolver scans its `.rdata` for the `release_M_N_BUILD_SUB` version string (the hard
intern-agreement check — ≥2 matching interns, `../design.md` §6 / R12). On success the
segment shows the resolved version (mono); the resolved row is then marked "current /
matches linked DLL" in s02/s03. The resolver is the data-core's (`version_resolver.py`,
already built — the UI binds it, law 6).

**Never required (law 4).** Unlinked is a normal working state, not a degraded/blocked one.
Editing, creating a version, creating an entity all proceed unlinked — the relevant screen
shows the advisory "can't verify — no DLL linked" warning (s04/s05), and the save carries
the "I accept — save anyway" override (s06). The status bar simply reads "No DLL linked".

**Default row when unlinked:** with no linked version to match, s02 default-selects the
**newest authored row** (highest `valid_from_version`) — deterministic, always-works
(`../design.md` §6). The status bar's "No DLL linked" is the only signal needed; there is
no blocking "degraded mode" banner.

## States & variants
- **Unlinked (default)** — segment reads *"No DLL linked"* + `[Link DLL…]`. Neutral
  (`info`/`text_secondary`), not an error — it's the normal state.
- **Linked** — *"Linked: `<dll name>` — version `<M.N.BUILD>`"* (mono) + `[Unlink]`. The
  resolved version drives the "matches linked DLL" marker in s02/s03.
- **Resolver failure** — the linked DLL failed the intern-agreement check (<2 matches or
  disagreement): *"Linked DLL — couldn't resolve version (interns disagree)."*
  (`warning`); the link is held but unverified, and version-stamping actions carry the
  "I accept — save anyway" override (law 4). System-caused copy; names the specific cause.
- **Last-save: success** — *"Saved `<name> <version>`"* (`success`), fades after a few
  seconds; git hash not shown (law 5 — git invisible).
- **Last-save: blocked** — *"Save blocked — files locked by another process."* (`error`) +
  `[Retry]`, persists until retried (`../design.md` §8 — never reaps).
- **Transient notice** — a one-line non-blocking warning/info, fades.
- **Edge content** — a long DLL path truncates (mid-ellipsis), full path on hover; the
  version tag is fixed mono width.

## Links in / out
- **In:** present in every screen; `link_dll` invoked from here.
- **Out:** a link/unlink/resolve updates the "current"/"matches DLL" markers in s02/s03 IN
  PLACE (law 3 — never re-selects a row or navigates); `retry_save` re-runs the s06
  transaction.

## Applicable laws
- **Law 3** — a link/resolve/save result updates status + in-place markers; never
  navigates or re-selects for the maintainer.
- **Law 4** — verification is advisory; unlinked/resolver-failure never blocks; the
  override lives downstream (s06).
- **Law 5** — the save result reports "Saved", not a git hash; git is invisible.
- **Law 6** — the resolver is the data-core's, bound not reimplemented.
- **Law 7** — any read-only status conveyed by more than color.
- **Law 9** — tokens only.
