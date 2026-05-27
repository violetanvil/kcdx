# third-party-ghidra

Reverse-engineering toolchain for resolving WHGame.dll offsets. See
[`.claude/rules/reverse-engineering.md`](../.claude/rules/reverse-engineering.md)
for methodology.

## What's tracked

- `ghidra_scripts/` — Java/Python analysis scripts (e.g. `FindIsInCombatSlot.java`).
- This README.

## What's git-ignored (supply locally)

These are multi-GB binaries kept out of source control:

- `ghidra_project/KCD2.gpr` (+ project data) — pre-analyzed WHGame.dll project.
  Cold analysis takes hours; use the pre-analyzed project.
- `ghidra_12.1_PUBLIC/` — Ghidra install. Ghidra 12.1 dropped Jython; write
  Java scripts (PyGhidra only if explicitly installed).
- `WHGame.dll` — the game binary under analysis.
- `x64dbg/`, `ninja/`, `*.fidb` — supporting tools.

A working checkout expects these already present on disk.

## Headless invocation

```
analyzeHeadless.bat "ghidra_project" KCD2 -process WHGame.dll \
  -postScript <Script> -noanalysis -readOnly
```
