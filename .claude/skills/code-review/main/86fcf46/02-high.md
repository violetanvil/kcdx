# High findings

## H1 — Four new author-facing surfaces ship with zero new `test-plugins/` rows (AP7)

**Files (surfaces with no regression coverage in this change):**

- src/lua_bind_bytes.cpp:431-639 — `kcdx.bytes.<name>{...}` smart-resolver (NEW author-facing Lua surface).
- src/lua_bind_hook.cpp:1098-1660 — `kcdx.hook.<name>.<mode>(cb)` smart-resolver (NEW author-facing Lua surface).
- src/declare_interface.cpp + include/kcdx/Interfaces.h:1994-2187 — `kcdxDeclareInterface` v1 (NEW C++ interface — Declare + Get).
- include/kcdx/Kcdx.h:560-607 — `kcdx::bytes::Write/TryWrite` empowered wrapper (NEW C++ wrapper surface).

**Evidence:** `git diff private/main..HEAD --stat | grep test-plugins` returns empty. Zero `test-plugins/` files added or modified.

**Per `.claude/rules/test-suite.md` + AP7:** every new capability — a new `kcdx.*` Lua surface, a new C++ interface, a new mode/knob/arg form/locator/resolver tier — ships its permanent regression plugin in the SAME change. "Built but untested" is not a valid checkpoint.

**Each surface needs a falsifiable row, per AP15 (a row that can FAIL):**

- `cap-NN-lua-bytes-smart-resolver` — verifies `kcdx.bytes.<curated_name>{ replacement = "..." }` resolves + applies; FAILS if the install path doesn't reach the underlying `kcdx.bytes` register, or if a typo at `<name>` doesn't surface as nil (the typo-fails-fast contract).
- `cap-NN-lua-hook-smart-resolver` — verifies `kcdx.hook.<name>.<mode>(cb)` for at least one curated name × one mode; FAILS if the install path differs from the flat-table form's result, or if invalid-mode-for-kind doesn't return nil at the `.mode` access.
- `cap-NN-cpp-declare-interface` — a C++ DLL plugin calls `K.declare->Declare(...)` with a pattern entry, hooks the declared name from a separate plugin, asserts the hook fired. Lua peer test exercises `kcdx.declared(name)` against a value-entry declared from the same C++ plugin (parity invariant per `lua-api-surface.md`).
- `cap-NN-cpp-bytes-wrapper` — the C++ peer of `cap-01`'s Lua bytes coverage (matches the existing `cap-39-cpp-bytes` raw-interface test); verifies `kcdx::bytes::Write(K, name, replacement)` installs and applies the same site `cap-39` covers.

The matrix rows in `test-plugins/README.md` must be added with each.

**Tie to C1:** the broken `stringValue` lifetime would be caught by a cap-NN test that calls Declare → Get → another Declare → reads stringValue. The missing test plugin is what let the contract drift from the data structure. The test isn't optional ergonomics; it's the gate that would have caught the defect.
