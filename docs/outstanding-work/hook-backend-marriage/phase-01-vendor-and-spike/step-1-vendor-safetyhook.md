# Step 1 — license-check + vendor safetyhook

**What.** Bring safetyhook into the repo as a vendored dependency: license-check
against the repo allowlist, vendor the source under `vendor/safetyhook/`, wire it
into the CMake build, and re-confirm the design's header facts against the source
now on disk. Resolves build-gated unknown **U6** (`../context.md`). No hook
behavior changes this step — safetyhook is present and building, used by nothing
yet.

**Scope (commit-grain).**
- **License-check FIRST** (`.claude/rules/dependencies.md`): check safetyhook's
  license on its source repo AND any package metadata; compare against the repo's
  allowlist. safetyhook is permissive (confirm the exact license at vendor time —
  both registry + source). Ambiguous → STOP and ask the user. Record the
  license-manifest row (name, version/commit, license, one-line purpose) in the
  SAME commit.
- Vendor the source under `vendor/safetyhook/` (the headers + impl: `InlineHook`,
  `MidHook`, `Allocator`, the thread-trap). Pin to a specific tag/commit (the
  repo's version scheme), not a floating ref.
- Wire into `CMakeLists.txt` — add safetyhook to the build so kcdx.dll can include
  + link it. Build only; no call site yet.
- **Re-confirm the header facts on disk** (the design rests on this-session header
  reads that become disk-verifiable once vendored, `../context.md` U6): confirm
  `Context64` layout (full GPR + 16 XMM + rflags, by-value writeback, `rip`,
  `trampoline_rsp`); `enable()` calls `trap_threads(...)` unconditionally (no
  no-freeze install path); InlineHook's E9→FF25 fallback. Record any discrepancy
  from the design as a surfaced finding (the design §7 facts would need a
  correction-revision via `/design`), not a silent build-around.

**Test bar.** No kcdx behavior change → no cap-NN row added. Acceptance = `pwsh
./build.ps1` exits 0 with safetyhook compiled + linked into kcdx.dll (the three
artifacts produced), AND a trivial in-tree compile-check that `#include`s a
safetyhook header and references `safetyhook::InlineHook` / `safetyhook::MidHook`
/ `safetyhook::Context64` resolves (proves the vendoring + link, not just that
files exist). The header-fact re-confirmation is recorded in the commit body /
`../context.md`.

**Dependencies.** None — first step.

**Disassembler-test / author-burden note.** None — this step adds no
author-facing surface (a vendored library + build wiring; plugin authors never
see safetyhook).

**Reference.** [`../context.md`](../context.md) U6; design
[`hook-backend-marriage.md §9.6 + §7`](../../../design/hook-backend-marriage.md).
(`.claude/rules/dependencies.md` for the license-check + manifest discipline.)
