# Empowered C++ wrapper for `kcdx::console::Command`

## Status

**Designed gap, not built.** `kcdxConsoleInterface::RegisterCommand` is the
shipped raw surface (Phase 1+); no empowered wrapper exists in
[`include/kcdx/Kcdx.h`](../../include/kcdx/Kcdx.h). Today the C++ author
writes a `kcdxConsoleCommandCallback` taking an opaque
`const kcdxConsoleCmdArgs*` and threads it through `K.console->GetArgCount`
/ `GetArg` / `GetCommandLine` accessors.

```cpp
// Raw form (today)
static void my_cmd_cb(const kcdxConsoleCmdArgs* args) {
    int n = K.console->GetArgCount(args);
    const char* first = (n > 1) ? K.console->GetArg(args, 1) : "";
    /* ... */
}
K.console->RegisterCommand(K.self, "outfit_dump", "Dump outfit state", &my_cmd_cb);
```

```cpp
// Wrapper form (proposed)
kcdx::console::Command(K, "outfit_dump", "Dump outfit state",
    [](auto args) {
        const char* first = args.Get(1, /*default*/"");
        /* ... */
    });
```

**Why this is its own outstanding-work entry, not a Phase 12 row:** the
2026-05-28 wrapper-improvements audit ranked this surface at **#8 (last,
lowest ROI)** — 6→4 lines, ~33% reduction. The raw form is already
passable; the wrapper trades the `(opaque-ptr + 3 free-function accessors)`
ceremony for a `(typed-args-struct + 1 member accessor)` shape. Real but
small win; the raw form does not force a sentinel-init dance or a
mangled-ABI hidden in the call shape (the disassembler-test patterns the
Phase 12 sweep rows close). Phase 12 deliberately scoped to the higher-ROI
surfaces; this one was deferred to outstanding-work as the boundary case
where wrapping risks parity-aesthetics over UX-win.

## Trigger to revisit

EITHER:

1. A user-shipped plugin (or a kcdx-internal test plugin) registers enough
   console commands that the `GetArgCount` / `GetArg` ceremony becomes the
   surface friction the author actually feels. Today the only `cpp-console`
   surface is the BugSplat fix's command set; if that grows or new plugins
   add commands at scale, the ROI changes.
2. The empowered-wrapper sweep (Phase 12 sub-1) ships and the docs flip
   leaves `docs/cpp/console.md` as the only common-path lead that still leads
   with a raw form — the inconsistency itself becomes the friction.

## Design

The shape — settled at the design step when the trigger fires:

- **`kcdx::console::Command(K, "name", "help", lambda)`** in `include/kcdx/Kcdx.h`
  takes a captureless or capturing lambda whose single arg is a typed
  `kcdx::console::Args` proxy.
- **`kcdx::console::Args` proxy** wraps `const kcdxConsoleCmdArgs*` and
  exposes:
  - `int Count() const` — wraps `GetArgCount`
  - `const char* Get(int n, const char* defaultValue = "") const` — wraps
    `GetArg` with a default-bearing form (today's null-on-out-of-bounds is a
    silent footgun)
  - `const char* CommandLine() const` — wraps `GetCommandLine`
  - Optional typed helpers: `int GetInt(int n, int defaultValue = 0)`,
    `double GetDouble(int n, double defaultValue = 0.0)` — parse-and-default
    in one call (the most common per-arg pattern)
- **Capturing-lambda storage.** If the wrapper accepts a capturing lambda,
  the lambda's storage outlives the command registration (the engine retains
  the callback for the process lifetime per the raw interface contract).
  Storage owned by the wrapper, freed at process exit; same lifetime model
  as the Phase 12 sub-1 row 2 `kcdx::task::Run` wrapper.
- **Naming convention.** Per-surface namespace, matching the shipped
  `kcdx::hook::*` exactly (user-locked direction from the 2026-05-28
  `/senior-architect-reply` thread). Autocomplete after `kcdx::console::`
  shows only console verbs.

## Files that need to change

- [`include/kcdx/Kcdx.h`](../../include/kcdx/Kcdx.h) — append the
  `kcdx::console::Args` proxy + `kcdx::console::Command(...)` helper.
  Header-only.
- [`docs/cpp/command.md`](../cpp/command.md) — flip the common-path lead to
  the wrapper form; demote the raw `K.console->RegisterCommand` form to the
  labeled raw-floor drop-down per the 3-floor model
  ([`docs/cpp/wrapper.md`](../cpp/wrapper.md)).
- [`docs/cpp/index.md`](../cpp/index.md) — map entry updated if a new
  per-call file (`console.md`) replaces `command.md`'s role; settled at the
  design step.
- `test-plugins/cap-NN-cpp-console-wrapper/` — both surfaces of the
  capability (raw + wrapper) under permanent regression, paralleling
  cap-36/cap-37 from Phase 3 sub-1.

## Why deferred (the audit's wording)

From the 2026-05-28 wrapper-improvements audit (`restructure/00-original-plan.md`
Phase 12 background):

> **Reads-clarity weakest case:** console. The raw form is already a
> function-pointer + an opaque-args pointer with two accessors. Wrapping it
> adds a typed args struct but doesn't remove ceremony, it just renames it —
> the author still types args+Get pattern. This is the boundary where
> wrapping becomes parity-aesthetics, not UX win.

The "doesn't remove ceremony, it just renames it" framing is what made this
the lowest-ROI row and kept it out of Phase 12's scope. The default-bearing
`Get(n, default)` form is the one piece of real ergonomic improvement (the
raw `GetArg` returning null on out-of-bounds is a footgun); on its own it
might be a fair argument to ship the wrapper anyway.

## Related

- [`restructure/00-original-plan.md`](restructure/00-original-plan.md) Phase 12 — the empowered
  wrapper sweep that this entry was deliberately deferred from.
- [`include/kcdx/Kcdx.h:22-44`](../../include/kcdx/Kcdx.h#L22-L44) — the
  3-floor model that anchors the docs flip.
- [`hook-capturing-lambda-context-slot.md`](hook-capturing-lambda-context-slot.md)
  — if this entry's design ships first WITHOUT capturing-lambda support, it
  could fold into the same engine-ABI cycle that adds the per-callback
  context slot (kcdxConsoleInterface needs the same slot if the wrapper is
  to accept capturing lambdas without per-command global state).
