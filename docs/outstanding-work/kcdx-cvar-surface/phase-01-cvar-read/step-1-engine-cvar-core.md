# Step 1 — engine CVar-read core (`src/cvar.{h,cpp}`)

## What

A `kcdx::cvar` C++ module that reads a game CVar's value by name, mirroring the
resolve-by-name + cached-function-pointer + ready-latch pattern of `src/console.cpp`.
It resolves `gEnv_pConsole` (id 10), `IConsole_GetCVar` (id 16), `ICVar_GetIVal`,
and `ICVar_GetFVal` by canonical name via `refdb::ResolveAddrByName`, and exposes:

- `bool cvar::GetInt(const char* name, int* out)` — `GetCVar(name) → ICVar*`; if
  non-null, call ICVar `[0x10]` (GetIVal); write `*out`, return true. Null CVar or
  unready surface → log + return false (no garbage write).
- `bool cvar::GetFloat(const char* name, float* out)` — same, via ICVar `[0x20]`
  (GetFVal).

`GetBool` is NOT a separate engine call — it is `GetInt(name) != 0`, computed at
the binding layer (steps 2/3).

## Scope

`src/cvar.h`, `src/cvar.cpp`, and its init wiring (call `cvar::Init()` where
`console::Init()` is invoked, or share the console-ready path). Soft-miss handling
per `console.cpp`'s PrintLine precedent: a name that does not resolve logs a WARN
and leaves the surface returning false — never fails the whole engine. Single-commit.

## Test bar

Exercised at step 4 (the cap-71 plugin drives both surfaces). At THIS step, the unit
evidence is the init resolve: `cvar::Init` logs a ready line naming the four resolved
addresses (like `console.cpp`'s `[console] ready:` line), confirming all four names
resolved against the recorded DB. Build green (`pwsh ./build.ps1` → three artifacts).

## Dependencies

- **Step 0 (EXTERNAL)** — `ICVar_GetIVal` + `ICVar_GetFVal` must be recorded in
  `reference.sqlite` for `ResolveAddrByName` to return their addresses. This step
  is BLOCKED until step 0 lands. (`IConsole_GetCVar` id 16 + `gEnv_pConsole` id 10
  already exist.)

## Design authority

`plan-spec.md` §"Settled design decisions" (the resolve-by-name chain, the two
entities, soft-miss/fail-loud). The resolve pattern to mirror: `src/console.cpp`
`Init()` (lines ~420-500) — resolve-by-name, cache the fn pointer, ready latch,
`DropPendingWithError` soft-miss. No new design call here; if the console-ready
sequencing forces a choice (share `console.cpp`'s latch vs a separate one),
surface it.

## Disassembler-test / author-burden

None added — this is engine-internal. The author-facing surface (steps 2-3)
carries the disassembler-test obligation: the author writes `kcdx.cvar.get_int("x")`,
the engine resolves address + ABI. No hex/offset/signature reaches the author.

## Rules

`no-hardcoded-addresses.md` (resolve by name, never a literal RVA), `logging.md`
(LOG_*_KV with `log::KV(...)` qualified; every failure logged), `cornerstones.md`,
`anti-patterns.md` (AP1, silent-success).
