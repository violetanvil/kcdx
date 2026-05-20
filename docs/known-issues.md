# Known external issues kcdx routes around

Bugs and quirks in the KCD2 game build, third-party libraries kcdx
ships against, and the Windows tooling around them. Each entry
documents:

- **Symptom** — what you'd see if kcdx weren't working around it.
- **Root cause** — what's actually wrong, with as much certainty as
  we have.
- **kcdx workaround** — what we ship that masks the symptom.
- **Long-term fix** — what we'd ideally do once we have capacity.

Entries graduate out of this doc when their root cause is fixed
upstream OR when kcdx's workaround is promoted to a permanent
documented design choice in `design.md`.

---

## 1. BugSplat dmp files don't reach disk for AV crashes

**Status:** working around it; in-process MiniDumpWriteDump fully
mitigates the symptom for our use case. Long-term fix not scoped.

### Symptom

When KCD2 crashes with a standard `STATUS_ACCESS_VIOLATION`
(`0xC0000005`) — the SEH-trappable kind — BugSplat's exception filter
runs first (before kcdx's `crash_guard::UnhandledFilter`), tries to
write its own minidump, and writes a zero-byte stub file. No usable
minidump appears on disk anywhere. The BugSplat UI dialog may show a
crash report, but the file BugSplat references in its session log
either doesn't exist or is empty.

For crashes that **bypass** SEH (heap-corruption fast-fails,
`0xC0000374`) this doesn't apply — BugSplat never runs, WerFault
handles the dump cleanly, and the file lands at
`%LOCALAPPDATA%/CrashDumps/KingdomCome.exe.<pid>.dmp`. So the bug
is specific to crash classes that route through BugSplat's filter.

Confirmed in the kcdx 13:32 session 2026-05-20: page heap enabled
turned a recurring `0xC0000374` into a `0xC0000005`. The previous
crash classes had produced 108MB WerFault dumps in
`%LOCALAPPDATA%/CrashDumps/`. The 13:32 crash routed through
BugSplat and left only a zero-byte file named `Kingdom Come`
(no extension, mtime matches crash time) in `%LOCALAPPDATA%/Temp/`.

### Root cause

BugSplat constructs the minidump filename from the configured app
display name. KCD2 ships BugSplat configured with app name
`"Kingdom Come: Deliverance II"` — the literal app name with the
colon. BugSplat then attempts to write to a path like:

```
C:\Users\<user>\AppData\Local\Temp\Kingdom Come: Deliverance II<id>.dmp
```

Colon is an illegal filename character on NTFS (it's the
alternate-data-stream separator). The `CreateFileW` call fails
or — depending on the exact call — produces a zero-byte stub
truncated at the colon. BugSplat's own session log claims the
write succeeded, but the file on disk is unusable.

BugSplat's session log evidence (text path quoted verbatim from
the log):

```
BugSplat.dll: 2026-05-20 13:32:45  Minidump file successfully saved
BugSplat.dll:                          C:\Users\{username}\AppData\Local\Temp\Kingdom Come: Deliverance IIIOI57IO6.dmp
```

The `{username}` placeholder is BugSplat's literal log formatting,
not a path substitution issue. The actual filename uses the real
username — and contains the colon character.

This is a bug in either KCD2's BugSplat integration (display name
shouldn't be used as a filename) or in BugSplat itself (filename
should sanitize illegal characters).

### kcdx workaround

`crash_guard::UnhandledFilter` calls `MiniDumpWriteDump` ourselves,
writing to a path we fully control:

```
<kcdx-engine>/logs/kcdx_<sessionstamp>.dmp
```

Filtered dump type (~2-5MB) rather than full memory (~100MB).
kcdx-watchdog scans this path first, falling back to WerFault's
`%LOCALAPPDATA%/CrashDumps/` for fast-fail crashes where our SEH
filter didn't run.

This means the watchdog now produces a dmp for **any** crash class
where dev mode is on:

| Crash class | Source of dmp |
|---|---|
| AV / SEH-trappable | Our in-process MiniDumpWriteDump |
| Heap corruption / fast-fail | WerFault dump |
| BugSplat-written | Best-effort fallback (kept for completeness, not relied on) |

See `crash_guard.cpp::WriteOwnMinidump` and the watchdog source
in `src/watchdog/main.cpp` (sections marked "a)", "b)", "c)").

### Planned fix (to ship as a kcdx builtin engine-fix plugin)

A clean, ready-to-ship patch exists. **Decision: ship it** as a
first-party kcdx engine-fix plugin under
`kcdx-engine/builtin/bugsplat-filename-fix/`. Not yet implemented
— this section documents the patch + the implementation plan.

**Patch summary.** Repoint a single `LEA` instruction in WHGame.dll
from the colon-bearing app-name string to a sibling string that's
filename-safe. WHGame.dll already contains both forms:

| String | VA | Used for | xrefs |
|---|---|---|---|
| `"Kingdom Come: Deliverance II"` (colon) | `0x183aa3508` | display name (CVar `sys_game_name`, getter, etc.) + BugSplat dmp filename | 4 |
| `"Kingdom Come Deliverance II"`   (no colon) | `0x183e18290` | filename-safe sibling, single existing xref | 1 |

Of the 4 colon-form xrefs, three are legitimate display-name uses
(CVar, getter thunk, 29-byte hashing loop). The fourth, at VA
`0x1824599e7`, is the BugSplat call site — identified by "- Fatal
Error" string loaded immediately before and a vtable dispatch at
`[rax+0x170]` with `BS_MINIDUMP_TYPE` flags `0x12010`.

The patch rewrites that single LEA's `disp32` to point at the
filename-safe string instead. Same-length 4-byte rewrite; fits
the mempatch contract.

**Patch fields:**

| Field | Value |
|---|---|
| Target | `WHGame.dll` (KCD2 1.5.1164953) |
| Instruction VA | `0x1824599e7` |
| File offset | `0x2458de7` |
| LEA bytes | `48 8D 15 1A 9B 64 01` |
| Patch site offset | +3 from LEA start (the disp32) |
| Original 4 bytes | `1A 9B 64 01` (→ `0x183aa3508`, colon string) |
| Replacement 4 bytes | `A2 E8 9B 01` (→ `0x183e18290`, safe string) |
| AOB anchor (unique in `.text`) | `E8 48 CF 09 FE 4C 8B C0 48 8D 15 ?? ?? ?? ?? 48 8D 4C 24 30 E8 10 D4` |
| AOB patch offset | +13 |

**As a kcdx engine-fix plugin** (the planned ship vehicle —
`kcdx-engine/builtin/bugsplat-filename-fix/kcdx.toml`):

```toml
[plugin]
name        = "kcdx.bugsplat-filename-fix"
description = "Repoints WHGame.dll's BugSplat dmp-filename call site from \"Kingdom Come: Deliverance II\" (colon is illegal on NTFS) to the filename-safe sibling string already in the binary."
author      = "kcdx"
version     = "1.0.0"
compatible_game_versions = ["1.5.1164953"]

[[patch]]
name        = "bugsplat-filename-fix"
target      = "WHGame.dll"
pattern     = "E8 48 CF 09 FE 4C 8B C0 48 8D 15 ?? ?? ?? ?? 48 8D 4C 24 30 E8 10 D4"
offset      = 13
original    = "1A 9B 64 01"
replacement = "A2 E8 9B 01"
```

(The schema is mempatch-compatible by design — see kcdx
`CLAUDE.md` hard rule #11 — but mempatch is deprecated, so the
plugin ships exclusively as a kcdx engine-fix. See "How it ships"
below for the loader requirements.)

**Verification recipe** (required before any commit that ships
this — kcdx's standard live-verify policy):

1. Enable Application Verifier page heap on `KingdomCome.exe`
   (the setup that turned the recurring `0xC0000374` into a
   `0xC0000005` in the 13:32 session).
2. Apply the patch via mempatch (or kcdx).
3. Boot the game with the test-suite plugins active and trigger
   the same save-load crash that produced the 13:32 fault.
4. Check `%LOCALAPPDATA%/Temp/` for the dmp file. Expected:
   `Kingdom Come Deliverance II<id>.dmp` (no colon). It should
   be a real non-zero-byte file.
5. Also confirm display-name UI surfaces (e.g. game title bar,
   `sys_game_name` CVar via console) still show
   `"Kingdom Come: Deliverance II"` — those use the three
   un-touched xrefs.

The agent's analysis was static-only; a first-run live check is
non-negotiable before this leaves draft state.

### Why ship it

For *kcdx users alone*, this patch is redundant — `crash_guard::WriteOwnMinidump` (commit `2ee3da6`) already gives us a usable
dmp at `<kcdx-engine>/logs/kcdx_<sessionstamp>.dmp` for every
SEH-trappable crash.

But **for Warhorse**, this is significant:

- BugSplat's transmission pipeline almost certainly reads from
  the dmp file on disk before uploading. With the file empty
  (the zero-byte `Kingdom Come` stub case), Warhorse receives
  either nothing usable or a corrupt upload for **every
  SEH-trappable crash on every KCD2 player's machine**.
- The patch is one same-length 4-byte LEA disp32 rewrite. Tiny
  maintenance footprint relative to the diagnostic value it
  restores to Warhorse's whole telemetry pipeline.
- A more stable KCD2 benefits every modder and every kcdx user
  transitively — the win is non-zero for us, just indirect.

The "kcdx is redundant for itself" framing missed the
externality. Shipping costs us an AOB to maintain on KCD2
updates and gives every KCD2 player (kcdx or otherwise) usable
BugSplat telemetry.

### How it ships

**Ship vehicle: a first-party kcdx engine-fix plugin** at
`kcdx-engine/builtin/bugsplat-filename-fix/kcdx.toml`.

This is a new plugin category — see `docs/loader-architecture.md`
§"Engine-fix plugins" for the design — distinct from user-installed
plugins under `plugins/`. Properties:

- **Ships in the kcdx release zip** alongside `kcdx.asi` and
  `kcdx-watchdog.exe`. Users get the fix automatically when
  they install kcdx.
- **Loaded by the same kcdx discovery pipeline** that walks
  `plugins/`, just rooted at `kcdx-engine/builtin/` instead.
- **Loaded before user plugins** so cross-plugin conflicts at
  the same address resolve in the engine fix's favor.
- **User-disable-able via `.disabled` suffix.** Same convention
  as user plugins under `plugins/`: rename
  `kcdx-engine/builtin/<fix>/` to `<fix>.disabled/` to opt out
  of a specific engine fix without uninstalling all of kcdx.
  Useful safety valve if a fix turns out to cause regressions
  on someone's machine.
- **Uses the same `[[patch]]` schema** that user plugins use.
  No special engine-fix-only syntax. The only differences from
  a user plugin are the on-disk location and the discovery
  precedence.

Why **not** ship as a mempatch plugin under
`mempatch-plugins/bugsplat-filename-fix/`: **mempatch is
deprecated.** All byte-rewrite patches now ship through kcdx.
The two engines used to coexist (CLAUDE.md hard rule #3 etc.);
that's no longer the case as of this design.

### Implementation plan

When ready to ship, the work is:

1. **Loader change:** extend `kcdx::config::LoadAllConfigs` to
   walk `kcdx-engine/builtin/` in addition to `plugins/`. Two
   roots, single result set, engine-fix root walked first so
   its patches land at the front of the conflict-engine's
   `g_applyOrder`. See [`src/config.cpp`](../src/config.cpp)'s
   `WalkForTomls` for the walker to extend.
2. **Discovery semantics:** engine-fix plugins honor the
   `.disabled` suffix the same way user plugins do — a user
   can opt out of a specific fix by renaming its folder
   (e.g. `bugsplat-filename-fix.disabled/`) without
   uninstalling kcdx. Discovery logs tag each line with
   `source=engine|user` so the funnel summary distinguishes
   user vs engine-fix counts.
3. **Conflict resolution:** if a user plugin tries to patch the
   same address as an engine-fix plugin, the engine fix wins
   and the user plugin's `[[patch]]` aborts with a clear
   `MANIFEST.reject` line naming the conflict.
4. **Plugin authoring:** create
   `kcdx-engine/builtin/bugsplat-filename-fix/kcdx.toml` with
   the TOML shown above.
5. **Live-verification** (kcdx policy: no patch ships without
   live verification):
   - Enable Application Verifier page heap on `KingdomCome.exe`
     (the setup that turned the recurring `0xC0000374` into
     a `0xC0000005` in the 13:32 session).
   - Build + deploy kcdx with the patch loaded.
   - Trigger the same save-load crash that produced the 13:32
     fault.
   - Check `%LOCALAPPDATA%/Temp/` for the dmp file. Expected:
     `Kingdom Come Deliverance II<id>.dmp` (no colon). Should
     be a real non-zero-byte file.
   - Confirm the display-name UI surfaces (game title bar,
     `sys_game_name` CVar via console) still show
     `"Kingdom Come: Deliverance II"` — those use the three
     un-touched colon-string xrefs.
6. **Release packaging:** the kcdx release zip needs to include
   `kcdx-engine/builtin/bugsplat-filename-fix/` so the fix
   ships with the engine. See `package-release.ps1`.
7. **Docs:** graduate this section from "planned" to "shipped";
   add a brief note in `docs/loader-architecture.md` about the
   `builtin/` location.

### What we did in the interim

Until the engine-fix plugin lands, kcdx still produces a usable
dmp for its own diagnostic purposes via the in-process workaround
shipped in commit `2ee3da6` (`crash_guard::WriteOwnMinidump`).
That dmp lands at `<kcdx-engine>/logs/kcdx_<sessionstamp>.dmp`
and is bundled by the watchdog into
`<kcdx-engine>/logs/crash/crash_<ts>.zip`. See
[`logging.md`](logging.md) §"Crash bundles".

The BugSplat-side dmp scan in the watchdog
(`src/watchdog/main.cpp`, section "c)") will continue to find
nothing useful for AV crashes until the engine-fix plugin
ships. After it ships, BugSplat's dmp will appear in
`%LOCALAPPDATA%/Temp/Kingdom Come Deliverance II<id>.dmp` and
the watchdog's existing "Kingdom" substring filter will
catch it without modification.
