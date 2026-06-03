# Step 6 — frontend skeleton + Mantine theme + responsive app shell + API client

**What.** Stand up the React + Mantine frontend in `data/maintainer-tool/frontend/`: the app
entry + the **Mantine theme** that implements the token system (the color roles, type scale,
space rhythm, breakpoints — `ui/design.md` §"Design-system contract"; raw hex/px live ONLY in
the theme, law 9), the **responsive app shell** (the navigation shell: two panes on wide, the
master-detail drill-down on phone — `ui/design.md` §"App shell", law 2), and the **API
client** (the typed calls to the Phase-2 backend). No screen content yet (steps 7–10 fill the
panes); this is the shell + theme + data layer every screen renders into.

**Scope.** The frontend package + build setup + the Mantine theme (the tokens) + the
responsive shell (the breakpoint-driven two-pane ↔ drill-down) + the API client + the
empty/loading shell states. No navigator list (step 7), no detail (step 8), no editing/save.

**Test bar.** The token/theme is verified by the screens that consume it (no token has a
value outside the theme — law 9). The shell's responsive reflow + the API client are verified
at the phase's user-facing acceptance gate (the app boots, shows the two-pane/drill-down
shell + the empty state, talks to the backend). A component/unit test (the frontend's test
convention — e.g. Vitest + Testing Library) covers the API client's request/response mapping
+ the shell's breakpoint switch (a render test at each breakpoint). Runnable now (the Phase-2
read API exists for the client to call; the shell renders without screen content).

**Dependencies.** Phase 2 (the backend API the client calls — at least step 1's health/load
+ step 2's read API). This is the frontend foundation every later frontend step builds on;
it lands first in Phase 3.

**Design authority.** [`data/maintainer-tool/ui/design.md`](../../../../data/maintainer-tool/ui/design.md)
§"App shell" (the responsive two-pane ↔ drill-down skeleton) + §"Design-system contract" (the
token system this theme implements) + §"Global interaction laws" (law 1 layout stability,
law 2 the navigation shell, law 9 no-raw-values). [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§5 (the thin-shell seam — the frontend calls the API) + D14 (React + Mantine).

**UX** (`.claude/rules/ux-first-class.md`, from the UI design layer):
- **Populated** — the responsive shell (two-pane on wide; the navigator home view on phone);
  the curated set loaded (the navigator list fills in step 7; here the shell renders + the
  API is wired).
- **Empty (no DB/seeds resolved)** — the API/health reports the backend couldn't load the
  checkout: the shell shows the s01 empty-state copy (names the operator-side cause), not a
  blank app.
- **Loading** — the initial fetch in flight: a brief progress indication (Loader/skeleton).
- **Responsive** — the shell reflows at the `bp_two_pane` breakpoint: two-pane on wide,
  the drill-down on phone (verified the shell switches without losing state — law 1/3).
- **Accessibility + consistency:** initial focus on the navigator search field; the Mantine
  theme is the one consistent styling source; `prefers-reduced-motion` respected (the shell
  transitions).

**Disassembler-test / author-burden.** N/A — no author-facing game-function input.
