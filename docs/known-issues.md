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

### Long-term fix (not scoped)

Options if we ever want to address the BugSplat side directly:

- **Hook `CreateFileW`** in WHGame.dll and sanitize `:` → `_` in
  paths matching the BugSplat pattern. Trivial implementation but
  affects every `CreateFileW` call in the process (most are not
  BugSplat-related), so it's a hot path.
- **Patch the static byte that produces the colon in BugSplat.dll**
  via mempatch. Need to RE BugSplat's filename-construction code
  to find the source of the colon. Cleanest at runtime; one-time
  AOB hunt up front.
- **Report to Warhorse / BugSplat upstream.** They control the
  display-name config; sanitizing it for filename use is their fix
  to make.

None of these are urgent because the in-process dmp workaround
fully covers our diagnostic need. Documented here so a future
investigation has the context.
