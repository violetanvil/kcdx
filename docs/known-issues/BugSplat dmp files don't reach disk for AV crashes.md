# BugSplat dmp files don't reach disk for AV crashes

**Status:** FIX DESIGNED + LIVE-DE-RISKED, BUILD-DEFERRED TO PHASE 11. The
real fix — a MinHook detour on `BugSplat64.dll!MiniDmpSender::MiniDmpSender`
that rewrites the colon-bearing `szApp`, installed at DllMain/LDR timing —
is confirmed viable (PROBE T). It requires the author-facing **before_game-hook
capability** (`docs/outstanding-work/before-game-hooks.md`), which is Phase 11
work (the Lua tier needs FIX A's DllMain VM; the C++ tier this fix uses is
buildable earlier but bundled into Phase 11 by the user's sequencing decision,
2026-05-26). In-process `MiniDumpWriteDump` (`crash_guard::WriteOwnMinidump`)
already covers the kcdx diagnostic use case — this is BugSplat-dmp fidelity, not
a kcdx coverage gap, so deferring it costs no crash visibility. The disproven
`[[patch]]` LEA approach is retired (the builtin is ship-disabled). The full
fix design + change set: `docs/outstanding-work/before-game-hooks.md` §6.

## Facts

- Crash class `0xC0000005` (`STATUS_ACCESS_VIOLATION`) routes
  through BugSplat's SEH filter. `0xC0000374`
  (`STATUS_HEAP_CORRUPTION`) bypasses BugSplat → WerFault writes
  a clean dmp at `%LOCALAPPDATA%/CrashDumps/`. The bug applies
  only to the SEH-trappable case.
- BugSplat constructs its dmp filename from KCD2's app display
  name: `"Kingdom Come: Deliverance II"`. Colon is illegal on
  NTFS. The write fails / produces a zero-byte stub named
  `Kingdom Come` in `%LOCALAPPDATA%/Temp/`. BugSplat's own
  session log claims success.
- WHGame.dll contains both forms of the string:
  - `0x183aa3508` — colon form, 4 xrefs (3 display-name, 1
    BugSplat per agent's static analysis)
  - `0x183e18290` — no-colon form, 1 xref
- BugSplat reads its app-name string at DLL init, then caches it
  internally. Patching the LEA after init has no effect.
- kcdx's `crash_guard::UnhandledFilter` writes its own filtered
  minidump (~2-5MB) to `<kcdx-engine>/logs/kcdx_<sessionstamp>.dmp`,
  bundled by the watchdog into `crash_<ts>.zip`. This covers the
  kcdx diagnostic use case independently of whether BugSplat
  works.
- **The colon is in the FILENAME, not a directory name.** PROBE R
  caught the exact wide-string: `Kingdom Come: Deliverance IILB9D64F4.dmp`
  inside `%LOCALAPPDATA%/Temp/`. BugSplat concatenates app-name + session-id
  + `.dmp` directly with no separator. Windows interprets the colon
  as an alternate-data-stream specifier on the filename root
  `Kingdom Come` and the write fails / produces the 0-byte stub.
  (PROBE R, 2026-05-21)
- **The CreateFileW caller is `BugSplat64.dll`, not WHGame.dll.** PROBE
  R shows RVA `BugSplat64.dll+0x4757` for the broken path. Patching
  WHGame.dll's `.rdata` for the app-name string was the wrong binary
  — BugSplat keeps its own copy of the string, set when WHGame.dll
  called the `MiniDmpSender` constructor at init. (PROBE R, 2026-05-21)
- **BugSplat64.dll exports the `MiniDmpSender` C++ API**, including
  the constructor `MiniDmpSender::MiniDmpSender(wchar_t const*,
  wchar_t const*, wchar_t const*, wchar_t const*, unsigned long)` at
  export ordinal 3 / RVA `0xC914`. Standard BugSplat client API
  signature: `(szDatabase, szApp, szVersion, szUser, flags)`. Arg
  `szApp` is what propagates to every dmp filename. (PROBE R, 2026-05-21)
- **The ctor fires before kcdx's worker thread `hooks::Install`
  reaches our detour install code.** PROBE S installed cleanly at
  worker-thread time (BugSplat64.dll was mapped, GetProcAddress
  succeeded, MinHook reported MH_OK) but the detour never observed
  a call. The crash dmp at crash time still showed the broken path,
  so the ctor DID run — just earlier. Worker-thread install is
  too late. (PROBE S, 2026-05-21)

## Reframe 2026-05-21: wrong binary, hookable constructor

Two days of attempts patched WHGame.dll's `.rdata` based on a static-
analysis claim that LEA `0x1824599e7` fed BugSplat's filename
construction. PROBE R disproved this empirically: the dmp filename
comes from BugSplat64.dll's own cached copy of the app name, set
exactly once when WHGame.dll calls the `MiniDmpSender` constructor
at process init.

The right fix is a **MinHook detour on `MiniDmpSender::MiniDmpSender`**
in `BugSplat64.dll` (or its first-ctor variant — see exports list).
The detour inspects arg 2 (`szApp`); if it contains a `:`, substitute
a colon-free copy before calling the original. One hook, one
substitution, deterministic.

This also means the load-order `before_game` zone work shipped in
PR 2 isn't strictly required for this fix — the constructor runs in
WHGame.dll's normal init path, not its DllMain, so a hook installed
from kcdx's worker thread (after MinHook is up) lands well before
BugSplat sees the string. The before_game zone stays as
infrastructure for any future engine fix that genuinely needs DllMain
timing.

## Trail

| Date | Action | Result |
|------|--------|--------|
| 2026-05-20 | Observed zero-byte `Kingdom Come` file in `%LOCALAPPDATA%/Temp/` after `0xC0000005` crash with page heap on | Identified the colon-in-filename mode |
| 2026-05-20 | Shipped `crash_guard::WriteOwnMinidump` (commit `2ee3da6`) — in-process MiniDumpWriteDump to a path kcdx controls | Watchdog now bundles a usable dmp for any SEH-trappable kcdx crash |
| 2026-05-20 | Agent static analysis identified LEA at VA `0x1824599e7` as the BugSplat call site; same-length 4-byte disp32 rewrite from colon-string `0x183aa3508` → no-colon-string `0x183e18290` | Patch design captured |
| 2026-05-20 | Shipped patch as `kcdx-engine/builtin/bugsplat-filename-fix/` (commit `2bd9837`); engine-fix discovery walker shipped same commit | Builds clean, deploys, walker accepts it |
| 2026-05-20 | A/B run #1: rename folder to `.disabled`, crash game with page heap, check `%TEMP%` | Zero-byte `Kingdom Come` stub. Baseline confirmed. |
| 2026-05-20 | A/B run #2: rename `.disabled` off, crash game with page heap, check `%TEMP%` | Still zero-byte `Kingdom Come` stub. Patch did not change BugSplat's filename. |
| 2026-05-20 | Verified patch did apply: engine log shows `[bugsplat-filename-fix] applied successfully at 0x00007FFCEA3699EA: 1A 9B 64 01 -> A2 E8 9B 01` 22 seconds into the session | Patch lands but too late; BugSplat already cached the string |
| 2026-05-20 | Disabled patch in deployed game (`bugsplat-filename-fix.disabled`); source kept in repo for reference | Status set to working-around; no auto-fix shipped |
| 2026-05-20 | Built load-order zones (PR 1: schema + sort) + DllMain-time before_game path via `LdrRegisterDllNotification` (PR 2). Gated by `KCDX_BEFORE_GAME_ZONE=1` during bring-up. Patch now applies at DllMain time, BEFORE WHGame.dll's own DllMain runs | Mechanism verified — deferred-log buffer captures the apply line and the `ldr_notify: registered` confirmation pre-`log::Init`. Engine log: `applied 1 before_game patch(es) to already-loaded module(s) at DllMain time`. |
| 2026-05-20 | A/B run #3 with PR 2 + env-var-enabled DllMain-time apply: `kcdx_crash_now` on AV. Patch confirmed-applied at DllMain time per logs | **Still zero-byte `Kingdom Come` stub.** Conclusion: the LEA at VA `0x1824599e7` is not on BugSplat's filename construction path. The agent's static-analysis attribution was wrong. Patch site needs to be re-derived from a different angle. |
| 2026-05-20 | Re-disabled `bugsplat-filename-fix` in repo (renamed to `.disabled`); source retained as the reference template for the eventual correct fix. Load-order zone+priority hints stay intact | No-op for users (release zip ships `.disabled`); reopens investigation pending new RE evidence. |
| 2026-05-21 | Migrated to the new `enabled = false` toggle in `kcdx-engine/load_order.toml` (commit `4c0bcab`). Folder renamed back to `bugsplat-filename-fix/` (no more `.disabled` suffix). Shipped `load_order.toml` in the release zip carries the `enabled = false` row for this plugin | Single-mechanism disable. To re-enable once the correct patch site is identified, flip the flag in `load_order.toml` or delete the `[[plugin]]` row entirely (default is enabled). |
| 2026-05-21 | PROBE R: MinHook detour on `kernel32!CreateFileW`, filter wide-path `"Kingdom Come"` or `".dmp"`, log path + caller RIP | **Caller is `BugSplat64.dll`, not WHGame.dll**. Single match for the broken dmp: `path="C:\Users\Michael\AppData\Local\Temp\Kingdom Come: Deliverance IILB9D64F4.dmp"`, `rip=BugSplat64.dll+0x4757`, `access=READ\|WRITE disp=CREATE_ALWAYS`. The colon comes from BugSplat's cached app-name string, set when WHGame.dll called `MiniDmpSender::MiniDmpSender(db, app, version, user, flags)` at init. Every prior LEA-patching attempt was in the wrong binary. |
| 2026-05-21 | PROBE S: MinHook detour on `BugSplat64.dll!MiniDmpSender::MiniDmpSender` (export ordinal 3 / RVA 0xC914) installed from kcdx worker thread, log-only (does NOT mutate args). Tests whether worker-thread install timing catches the ctor call | **Hook installed cleanly at 09:41:35.347 (BugSplat64.dll already mapped, GetProcAddress + MinHook both succeeded) but `[BUGSPLAT_CTOR] fire` never logged across a full session including a crash.** Ctor ran before our worker-thread install. PROBE R confirmed the broken path was still written at crash time → **final fix requires before_game timing**: install the MinHook detour during DllMain via LdrRegisterDllNotification on BugSplat64.dll mapping, before WHGame.dll's init code calls the ctor. |
| 2026-05-21 | PROBE T: same hook as PROBE S but install moves to kcdx.dll DllMain (`RunBeforeGameZoneInDllMain` → `ArmLdrInstall`). If BugSplat64.dll is already mapped, install immediately; otherwise register an LDR notification for the BugSplat64.dll load event. Confirms whether DllMain-time install timing is in time for the ctor | **T1 CONFIRMED (live run 2026-05-26 09:01).** BugSplat64.dll was NOT mapped at kcdx DllMain → `PROBE T: LdrRegisterDllNotification armed`; it fired the instant BugSplat64.dll mapped → `ctor hook installed via LDR callback (pre-its-DllMain)`; the ctor then FIRED: `[BUGSPLAT_CTOR] fire szApp="Kingdom Come: Deliverance II" szDatabase="WarhorseStudiosDB" szVersion="..._release_1_5_1164953_841" szUser="(null)" flags=8212`. DllMain-via-LDR-notification timing catches the ctor that worker-thread (PROBE S) missed; the 5-arg mangled ctor variant is the one that runs; szApp carries the colon to rewrite. **The fix is viable + fully de-risked.** |

## Reframe 2026-05-26: the before_game-hook capability (bugsplat's real fix) is blocked by a zone_gate design flaw

The bugsplat fix became a `/feature`: an author-facing before_game-hook
capability, with bugsplat as its first consumer. Step 1 (PROBE BG1: is a
before_game plugin DLL mapped at kcdx DllMain, for the self-registration
mechanism) ran with a new before_game DLL fixture (`cap-43-before-game-hook`,
`default_position="before_game"`) and surfaced TWO findings:

- **PROBE BG1 read `before_game_plugins=0`** at kcdx DllMain (`09:34:13.699`)
  even though cap-43 IS before_game — because `kcdx::plugins::g_manifests` is
  NOT yet populated when `RunBeforeGameZoneInDllMain`'s probe loop runs
  (cap-43 was discovered at `.707`, after the probe). The probe's assumption
  that `LoadAllConfigs` filled `g_manifests` before the probe loop is wrong.
  Probe inconclusive on the mapping question. (PROBE BG1, 2026-05-26)
- **zone_gate REJECTS every before_game plugin** — the bigger finding.
  `zone_gate::Check` (zone_gate.cpp:109-123) iterates `kCapabilities` and
  rejects a plugin if its zone mismatches ANY row's `requireZone`, WITHOUT
  checking whether the plugin actually USES that capability (there is no
  per-plugin usage input to `Check` at all). The only non-`Either` row is the
  synthetic test exemplar `kcdx.zone_gate_test_after_only` (RequireZone::After,
  zone_gate.cpp:56). So EVERY before_game plugin trips it — the rejection
  message "declared zone='before_game' but calls kcdx.zone_gate_test_after_only"
  is FALSE (cap-43 never calls it). comp-13-zone-gate-observer masked this: it
  is DESIGNED to be the rejected before_game plugin, so the bug looked like
  correct behavior. cap-43 is the first LEGITIMATE before_game DLL plugin →
  it exposes that the synthetic After-row blocks ALL before_game plugins from
  loading. **This blocks the entire before_game-hook capability** until the
  gate's usage-detection (or the synthetic row) is fixed. (PROBE BG1, 2026-05-26)

## Patch reference (shipped disabled via load_order.toml; patch site proven wrong 2026-05-20)

Source: `kcdx-engine/builtin/bugsplat-filename-fix/kcdx.toml`

| Field | Value |
|---|---|
| Target | `WHGame.dll` (KCD2 1.5.1164953) |
| Instruction VA | `0x1824599e7` |
| AOB | `E8 48 CF 09 FE 4C 8B C0 48 8D 15 ?? ?? ?? ?? 48 8D 4C 24 30 E8 10 D4` |
| Patch offset (in AOB) | +11 |
| Original disp32 | `1A 9B 64 01` |
| Replacement disp32 | `A2 E8 9B 01` |

## Active diagnostic instrumentation

| File | Purpose | Lifecycle |
|---|---|---|
| `src/probes/createfilew_probe.{h,cpp}` (PROBE R) | MinHook detour on `kernel32!CreateFileW`. On entry, scans the `lpFileName` for the wide substrings `"Kingdom Come"` or `".dmp"`. On match, logs the full path + the return address read from `_AddressOfReturnAddress()`. Always calls the original. | Dev-mode-only install (gated by `kcdx::dev::IsEnabled()`). Deleted after the probe answers the question. |
| `src/probes/bugsplat_ctor_probe.{h,cpp}` (PROBE S + T) | MinHook detour on `BugSplat64.dll!MiniDmpSender::MiniDmpSender`. Logs the call timestamp + the wide-string passed as `szApp` (arg 2). Always calls the original; never mutates args. PROBE S install was worker-thread (too late); PROBE T install is DllMain-time via `ArmLdrInstall` (LDR notification) — T1 CONFIRMED it catches the ctor. | **KEEP through the fix (not reverted): the LDR-notification ctor-hook machinery IS the engine-internal half of the bugsplat fix.** The log-only `HookedCtor` becomes a `szApp`-rewrite once the before_game-hook capability design settles; `ArmLdrInstall` + the LDR-notification path are the proven install mechanism the fix reuses. Revert/relocate out of `src/probes/` into a permanent engine home when the fix lands. |

## Open questions

- **H3 (PROBE T, the gating timing unknown — resume 2026-05-26)**: does a
  DllMain-time install catch the `MiniDmpSender` ctor that the worker-thread
  install (PROBE S) missed? The probe is already built + committed + deployed:
  `bugsplat_ctor_probe::ArmLdrInstall()` is called from
  `RunBeforeGameZoneInDllMain()` (`src/dllmain.cpp:173`), which runs in kcdx.dll's
  DllMain BEFORE WHGame.dll's init code calls the ctor. It installs the log-only
  ctor hook immediately if `BugSplat64.dll` is already mapped at that point,
  else arms an `LdrRegisterDllNotification` to install the instant it maps.
  Probe is dev-gated (`kcdx::dev::IsEnabled()`); dev_mode is on. — **Outcome map:**
  - (T1) `[BUGSPLAT_CTOR] fire szApp="Kingdom Come: Deliverance II"` logs at
    crash/init time → DllMain-time install IS early enough → the fix is viable:
    move the log-only hook to a `szApp`-rewrite (strip the colon before calling
    original). Next: implement the fix on this install path.
  - (T2) the install lines log (`PROBE T: ... ctor hook installed`) but
    `[BUGSPLAT_CTOR] fire` NEVER logs (as in PROBE S) → even DllMain timing is
    too late, OR the ctor isn't the 5-arg mangled variant we hooked → re-observe:
    which BugSplat ctor/overload actually runs (widen to the other exported ctor
    variants), or whether `BugSplat64.dll` maps after the ctor-caller runs.
  - (T3) no `PROBE T: ...` install line at all → BugSplat64.dll not mapped at
    kcdx DllMain AND the LDR notification never fired for it → the dll loads via
    a path the notification misses; re-observe the load order.
  Each outcome is logged under the `BUGSPLAT_CTOR` / `PROBE T` category tags in
  `kcdx-dev.log`.


- **H1**: BugSplat opens its dmp via `CreateFileW(L"...\\Kingdom Come...\\<name>.dmp", ...)` somewhere in WHGame.dll's BugSplat integration code, with the literal colon-bearing path. — Probe: PROBE R. **Predicted:** at `kcdx_crash_now` time, exactly one log line of the form `[CREATEFILEW] path="...Kingdom Come..."  rip=0x... access=GENERIC_WRITE creation=CREATE_ALWAYS`. The RIP tells us the call site; we trace backward to the string-construction code.
- **H2**: BugSplat uses a different API (`CreateFileA`, `_wfopen`, `NtCreateFile`, `OpenFile`). — Probe R won't fire for the dmp. If observed: 0-byte stub still appears, no `CREATEFILEW` log lines matching `Kingdom Come` or `.dmp`. Next probe: PROBE S widens to `NtCreateFile` (the bottom of the kernel32 stack — every other API funnels through it).
- ~~Can kcdx apply this patch earlier than `LoadAllConfigs`'s
  deferred apply phase — early enough to beat BugSplat's
  string-read at DLL init?~~ **Answered 2026-05-20: yes.**
  PR 2 ships an `LdrRegisterDllNotification` path that applies
  before_game-zoned `[[patch]]` entries at DllMain time, before
  WHGame.dll's own DllMain runs. Verified end-to-end. The patch
  STILL didn't fix BugSplat — so this open question moves to the
  next item.
- ~~Is the colon-string LEA at `0x1824599e7` actually the one
  BugSplat reads?~~ **Answered 2026-05-20: no.** The patch lands
  at the correct timing and the zero-byte stub still appears.
  Static analysis mis-attributed the call site. **Next investigation
  step**: hook `MiniDumpWriteDump` (exported from `dbghelp.dll`)
  or `CreateFileW` filtered to `*.dmp` to log the actual filename
  BugSplat passes at crash time, then trace that string back to
  its construction site in WHGame.dll. The correct LEA / CVar /
  whatever-it-is can then be patched the same way (the
  before_game-zone mechanism is in place).
- BugSplat may also pass the colon-string to other Win32 APIs
  beyond the dmp filename (logging? telemetry attribute?).
  Worth checking if any of those break in less-visible ways once
  the dmp path itself is fixed.
