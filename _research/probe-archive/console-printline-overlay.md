# `src/console.cpp` archived probe — PROBE PRINTLINE_OVERLAY

Lived as an inline `#if 0` block inside the console-ready path (after
`FlushPendingCommands()`). Extracted here; the ready path returned to pure
production logic.

**Verdict:** CONFIRMED — `IConsole::PrintLine` paints the VISIBLE `~` console overlay.
The marker `KCDX_PRINTLINE_OVERLAY_PROBE_OK` appeared on the overlay as a clean plain
line (no prefix, no syntax wrapper), and the call fired (log:
`PRINTLINE_PROBE called rva=...8DFF08`). The `this+0x18` ring-buffer IS the rendered
overlay buffer. So `kcdx.console.print` wraps this method directly.
**Root cause:** N/A — a capability-confirmation probe (which channel paints the
overlay), not a bug investigation.
**Backlink:** Phase 9.2 `kcdx.console.print` surface (backed by the verified
`IConsole::PrintLine` overlay channel).
**Revival hint:** re-add the block below to re-confirm overlay-paint after a game
update.

### Wiring (in `console::<ready path>`, inside the `g_slotsMutex` lock after `FlushPendingCommands()`)

```cpp
// IConsole::PrintLine ABI: this (g_iconsole) in rcx, const char* line in rdx —
// the verified 2-arg __thiscall/fastcall shape (vtable[26], verified against the binary).
using PrintLineFn = void(__fastcall*)(void* iconsole, const char* s);
uintptr_t printLineVA = refdb::ResolveAddrByName("IConsole_PrintLine");
if (!printLineVA) {
    LOG_INFO_KV("PRINTLINE_PROBE", "unresolved",
                log::KV("reason", "refdb name \"IConsole_PrintLine\" "
                                  "did not resolve"));
} else {
    auto printLineFn = reinterpret_cast<PrintLineFn>(printLineVA);
    printLineFn(g_iconsole, "KCDX_PRINTLINE_OVERLAY_PROBE_OK");
    LOG_INFO_KV("PRINTLINE_PROBE", "called",
                log::KV("rva", static_cast<unsigned long long>(printLineVA)),
                log::KV("marker", "KCDX_PRINTLINE_OVERLAY_PROBE_OK"));
}
```
