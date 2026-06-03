# Step 3 — C++ mirror `kcdxConsoleInterface::GetCVar{Int,Bool,Float}`

## What

Mirror the Lua surface on the C++ plugin interface (full parity,
`lua-api-surface.md`). The natural home is `kcdxConsoleInterface` (it already wraps
the IConsole surface — commands, ExecuteString, Print). Append three accessors
below its `--- APPEND-ONLY BELOW ---` marker and bump
`kcdxConsoleInterface_Version` 2 → 3:

- `bool (*GetCVarInt)(const char* name, int* out)` — true + `*out` set on success;
  false (no write) if the CVar does not exist or the surface is not ready.
- `bool (*GetCVarBool)(const char* name, bool* out)` — `*out = GetInt != 0`.
- `bool (*GetCVarFloat)(const char* name, float* out)` — float variant.

Wire each to the step-1 `cvar::` core. Out-param + bool-return shape (not a
return-value-with-sentinel) so a "CVar missing" is distinguishable from a real
0/false value — matches the fail-loud invariant.

## Scope

`include/kcdx/Interfaces.h` (the struct fields + version bump + doc-comments) and
the interface's implementation/population site (where the `kcdxConsoleInterface`
function pointers are assigned). Single-commit.

## Test bar

Exercised at step 4 (cap-71's C++ side — a C++ DLL plugin reads a known CVar via
`K.console->GetCVarInt(...)`). Build green.

## Dependencies

Step 1 (the `cvar::` core). Independent of step 2 (parallel-orderable, but sequenced
after step 1). Touches `include/kcdx/Interfaces.h` — a HEAD file: the inline
impact-analysis fires (grep every consumer of `kcdxConsoleInterface` + its version
constant before editing; the append-only ABI rule means no existing field shifts).

## Design authority

`plan-spec.md` §"Settled design decisions" — C++ mirror on `kcdxConsoleInterface`,
append-only, v2→3. `skse-parity.md` (bump the version when the struct changes; the
SKSE interface-versioning convention). `Plugin interface ABI is append-only`
(memory `project_kcdx_plugin_interface_abi`) — never a mid-struct insert; append
below the marker only. No new design call.

## Disassembler-test / author-burden

The C++ author calls `K.console->GetCVarInt("name", &v)` — a name, no hex. Mirrors
the Lua surface's compliance.

## Rules

`lua-api-surface.md` (full parity — Lua + C++ same model, same change),
`skse-parity.md` (interface versioning), `no-hardcoded-addresses.md`, `logging.md`,
`cornerstones.md`. (The interface-ABI-append-only invariant is the standing rule
from `project_kcdx_plugin_interface_abi`.)
