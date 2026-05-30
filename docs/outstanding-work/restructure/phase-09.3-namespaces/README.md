# Phase 9.3 — `kcdx.hook.*` / `kcdx.statement.*` split + value namespaces + multi-region trampoline

**Status: NOT STARTED.** Detail: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3".

The biggest surface phase. Lands the two distinct site-modification namespaces
(`kcdx.hook.*` callback-based interception vs `kcdx.statement.*` static-bytes
modification — genuinely different mechanisms, not aliases), the value namespaces
they consume (`kcdx.locator.*`, `kcdx.op.*`), the function-reference namespace
(`kcdx.functions.*` + `kcdx.dll.declare` + PDB auto-load), and the
trampoline-pool capacity work to support TC scale.

`module` is a REQUIRED positional first arg on every hash-checked verb (no
default) — honest about multi-DLL coverage. The split is honest about a real
mechanism difference: callbacks pay per-call dispatch; static bytes execute
natively forever after install. Authors pick by intent.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| [1 — `kcdx.locator.*` value namespace](step-1-locator-namespace.md) | NOT STARTED | — |
| [2 — `kcdx.op.*` static-op value namespace](step-2-op-namespace.md) | NOT STARTED | — |
| [3 — `kcdx.hook.*` sub-verb split](step-3-hook-subverbs.md) | NOT STARTED | — |
| [4 — `kcdx.statement.*` static-bytes namespace](step-4-statement-namespace.md) | NOT STARTED | — |
| [5 — `kcdx.functions.*` + `kcdx.dll.declare` + PDB auto-load](step-5-functions-and-declare.md) | NOT STARTED | — |
| [6 — multi-region trampoline-pool expansion](step-6-multi-region-trampoline.md) | NOT STARTED | — |
| [7 — C++ parity (`kcdxStatementInterface` / `kcdxFunctionsInterface` + hook sub-methods)](step-7-cpp-parity.md) | NOT STARTED | — |
| [8 — migrate existing hook test plugins to sub-verb shape + new tests](step-8-migrate-and-test.md) | NOT STARTED | — |

## Ordering note

Value namespaces (1, 2) land before the verbs that consume them (3, 4). The
function-reference namespace (5) and trampoline capacity (6) are independent of
3/4 and can interleave. C++ parity (7) follows the Lua shapes. Migration +
verification (8) is last. Each step ends buildable.

## Verification gate (whole phase)

Every existing hook test plugin migrates to the new sub-verb shape; suite stays
green. New `cap-XX-statement-replace` verifies zero per-call Lua dispatch (absence
of dispatch log lines during a tight loop). New `cap-XX-plugin-fn-declare`: plugin
A declares a function, plugin B hooks it by name — cross-plugin access without
disassembly. New `cap-XX-pdb-autoload`: a PDB-shipped plugin's non-exported
internal resolves + a static op applies.
