# Archived scan probes — PROBE SCAN_ARGV (×2) + PROBE KI2-RESOLVE

Three inline `#if 0` blocks across two files, all from one investigation thread
(`kcdx_scan` reporting 0 matches at a site verified to resolve to 1). Extracted here;
`console_commands_scan.cpp::Callback` and `scan_engine.cpp::ResolveScan` returned to
pure production logic.

**Shared verdict:** the kcdx_scan argv + parse path is byte-clean and CORRECT. The
0-match was a TEST-FIXTURE defect, not a kcdx_scan bug.
**Shared root cause:** the cap-70/KI-0002 fixture scanned a site whose leading bytes a
co-resident before-mode `kcdx.hook` (cap-33-author-targets, target=`openlibs_by_pattern`)
detours at the apply pass — by `input_loaded` the AOB's first 5 prologue bytes are a
`JMP rel32` (`48 89 5C 24 08 → E9 0E 75 BA FE`). The scan correctly reported the AOB
was gone. Fixture repointed to luaL_openlibs' un-rewritten, un-hooked `.text`-unique entry AOB.
**Backlink:** `docs/known-issues/closed/KI-0002-scan-zero-matches-at-input-loaded.md` §Resolution.
**Revival hint:** re-add the matching block to re-observe argv/parse/section-coverage if
a scan ever reports an unexpected 0 again.

---

## PROBE SCAN_ARGV — block 1: raw argv dump (in `Callback`, after `int argc = ...`)

**Finding:** `argc==3`, `arg2` == the full `"48 81 ... F0"` quoted token (quotes
stripped, kept whole); the pattern arrives INTACT (Outcome A).

```cpp
{
    LOG_DEBUG_KV("SCAN_ARGV", "raw",
                 log::KV("argc", static_cast<long long>(argc)));
    for (int i = 0; i < argc; ++i) {
        const char* a = iface->GetArg(args, i);
        LOG_DEBUG_KV("SCAN_ARGV", "arg",
                     log::KV("i", static_cast<long long>(i)),
                     log::KV("val", a ? a : "<null>"));
    }
}
```

## PROBE SCAN_ARGV — block 2: parsed-pattern dump (in `Callback`, after `ParsePattern`)

**Finding:** `pattern.bytes.size()==16` AND `entry.module.size()==10` — both inputs
byte-clean (no hidden CR / NBSP / trailing space) (Outcome A). The 0-match cause is
NOT the inputs.

```cpp
LOG_DEBUG_KV("SCAN_ARGV", "parsed",
             log::KV("pattern_bytes",
                     static_cast<long long>(entry.pattern.bytes.size())),
             log::KV("module_len",
                     static_cast<long long>(entry.module.size())),
             log::KV("module", entry.module.c_str()));
```

---

## PROBE KI2-RESOLVE — section-coverage + target-bytes dump (in `ResolveScan`, after `ScanAll`)

**Finding:** Outcome B — the bytes at `base+0x1449600` are overwritten by
`input_loaded` (first 5 bytes `48 89 5C 24 08 → E9 0E 75 BA FE`, a JMP rel32 detour);
section enumeration + module base byte-identical at both timings. The scan is CORRECT;
the AOB's leading bytes are genuinely gone (clobbered by the co-resident hook).

Name-gated to the `ki2_openlibs` scan cells to avoid noise. `kProbeTargetRva =
0x1449600` is luaL_openlibs' entry (seed id 115). `WindowReadable` /
`ExecutableSections` / `pe::ModuleView` are existing `scan_engine.cpp` helpers.

```cpp
if (s.name.rfind("ki2_openlibs", 0) == 0) {
    constexpr uint32_t kProbeTargetRva = 0x1449600;  // luaL_openlibs entry (seed id 115)
    uintptr_t modBaseDbg = reinterpret_cast<uintptr_t>(mv.baseBytes);
    LOG_DEBUG_KV("KI2RESOLVE", "module",
                 log::KV("scan", s.name.c_str()),
                 log::KV("base", modBaseDbg),
                 log::KV("size_of_image", static_cast<unsigned long long>(mv.size)),
                 log::KV("count", static_cast<unsigned long long>(hits.size())));
    auto secsDbg = pe::ExecutableSections(mv);
    LOG_DEBUG_KV("KI2RESOLVE", "exec_sections",
                 log::KV("scan", s.name.c_str()),
                 log::KV("n", static_cast<unsigned long long>(secsDbg.size())));
    for (const auto& sec : secsDbg) {
        uint32_t secEndRva = sec.rva + static_cast<uint32_t>(sec.size);
        bool covers = (kProbeTargetRva >= sec.rva) && (kProbeTargetRva < secEndRva);
        LOG_DEBUG_KV("KI2RESOLVE", "section",
                     log::KV("scan", s.name.c_str()),
                     log::KV("name", sec.name.c_str()),
                     log::KV("rva", static_cast<unsigned long long>(sec.rva)),
                     log::KV("vsize", static_cast<unsigned long long>(sec.size)),
                     log::KV("end_rva", static_cast<unsigned long long>(secEndRva)),
                     log::KV("covers_target", covers));
    }
    uintptr_t targetVA = modBaseDbg + kProbeTargetRva;
    if (WindowReadable(targetVA, 16)) {
        LOG_DEBUG_KV("KI2RESOLVE", "target_bytes",
                     log::KV("scan", s.name.c_str()),
                     log::KV("target_rva", static_cast<unsigned long long>(kProbeTargetRva)),
                     log::KV::Bytes("bytes",
                                    reinterpret_cast<const uint8_t*>(targetVA), 16));
    } else {
        LOG_DEBUG_KV("KI2RESOLVE", "target_bytes",
                     log::KV("scan", s.name.c_str()),
                     log::KV("target_rva", static_cast<unsigned long long>(kProbeTargetRva)),
                     log::KV::BareStr("bytes", "UNREADABLE"));
    }
}
```
