## Repo additions — feature

- **Domain audit categories (§A.2)** — beyond the generic classes, the audit walk adds, each with its governing rule:
  - **author-burden / disassembler test** — every author-facing input the feature adds resolves from a name (address AND verified ABI), never a hand-written hex/offset/register/signature for a common task (`.claude/rules/cornerstones.md`, AP12).
  - **TOML / Lua / C++ surface shape** — the `kcdx.toml` manifest stays manifest-only; behavior ships in `plugin.lua` (`kcdx.*` surfaces) or a C++ DLL (`kcdx*Interface`); the Lua and C++ surfaces mirror and read idiomatically (`.claude/rules/lua-api-surface.md`, `naming-namespaces.md`).
  - **hook-site + apply-order** — a new hook installs through the conflict engine with a `g_applyOrder` rank (`.claude/rules/hook-engine.md`).
  - **game-function evidence** — any offset / ABI / vtable the feature needs is resolved by Address Library ID / `abi_walker` / Ghidra on the reuse-first ladder (`.claude/rules/address-library.md`, `reverse-engineering.md`), never invented.
  - **Lua-surface hazards** — dual-Lua sentinel / threading / numeric-precision (`.claude/rules/lua-bridge.md`, `lua-callback-threading.md`, `lua-precision.md`).
  - **save/load impact** — cosave field, cold-vs-warm asymmetry (`docs/design.md` §save).

- **Test-bar shape (§A.3 / §B)** — a new `cap-NN` / `comp-NN` `test-plugins/` regression plugin + a `test-plugins/README.md` matrix row; both surfaces of a capability (Lua + C++) get rows (`.claude/rules/test-suite.md`).

- **Cornerstone gate (Phase A forks)** — every surfaced option set clears `.claude/rules/cornerstones.md` §"Surfaced options clear the cornerstones first" BEFORE presenting.

- **Decomposition extra** — a step needing a game-function offset / ABI that does not exist yet → the evidence-resolution (`/research-disassembly`) is step 1, before the step that consumes it.

- **Acceptance shape (§C)** — one game launch per feature; the matrix is confirmed from `kcdx-dev.log` (the manual shape lives in `verification-checkpoint`).
