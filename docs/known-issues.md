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

### Known patch (not shipped)

A clean, ready-to-ship patch exists. We've chosen **not** to ship
it — see "Why we don't ship this" below — but documented here in
full so a future maintainer has everything needed to flip the
decision.

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

**As a mempatch plugin** (if ever needed):

```toml
[mempatch]
schema = 1

[[patch]]
name = "bugsplat-filename-fix"
target = "WHGame.dll"
pattern = "E8 48 CF 09 FE 4C 8B C0 48 8D 15 ?? ?? ?? ?? 48 8D 4C 24 30 E8 10 D4"
offset = 13
original    = "1A 9B 64 01"
replacement = "A2 E8 9B 01"
game_version = "1.5.1164953"
```

kcdx supports the full mempatch `[[patch]]` schema (see kcdx
`CLAUDE.md` hard rule #11), so the same TOML works inside any
`kcdx.toml` if shipped as a kcdx-only patch instead.

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

### Why we don't ship this

kcdx's `crash_guard::UnhandledFilter` already calls
`MiniDumpWriteDump` itself, writing to a path we fully control at
`<kcdx-engine>/logs/kcdx_<sessionstamp>.dmp`. That dmp contains
the same diagnostic content (crashing thread's stack, registers,
modules, indirect-referenced memory) that BugSplat's dmp would
contain. From a kcdx diagnostic perspective, **the BugSplat dmp
is redundant**.

The fix would only have value:

- **For non-kcdx KCD2 users** if shipped as a standalone mempatch
  plugin. Real ecosystem-citizenship value, but kcdx isn't the
  right vehicle for delivering it.
- **As a regression check** — defence-in-depth if kcdx's
  in-process dmp ever breaks. Real but small.

Against that, every patched AOB is maintenance debt — needs
re-derivation on each KCD2 update. We've decided that's not
worth carrying for a fix whose only customer is "us, redundantly."

Other options considered and rejected:

- **Hook `CreateFileW`** to sanitize colon-bearing paths at runtime.
  Affects every `CreateFileW` call in the process (thousands per
  session); the patch above is targeted at one instruction instead.
- **Patch `BugSplat64.dll`** itself. Wrong target — BugSplat just
  consumes a wstring that WHGame.dll constructs. The colon
  literal isn't in `BugSplat64.dll`.
- **Report upstream to Warhorse.** Still worthwhile in parallel
  but on Warhorse's release cadence; not actionable from kcdx.

### What we did instead

Commit `2ee3da6` added `crash_guard::WriteOwnMinidump`. Every
SEH-trappable crash now produces a usable dmp at
`<kcdx-engine>/logs/kcdx_<sessionstamp>.dmp`. The watchdog
prioritizes that path over BugSplat's unreliable output (see
`src/watchdog/main.cpp`, sections marked "a)/b)/c)"). For
crashes that bypass SEH entirely (fast-fail, kernel kill), the
watchdog falls back to WerFault's `%LOCALAPPDATA%/CrashDumps/`
dumps.

The BugSplat-side scan is still in the watchdog as a third
fallback, but it's expected to find nothing useful until the
upstream bug is fixed or this patch ships.
