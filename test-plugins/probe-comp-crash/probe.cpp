// PROBE-COMP-CRASH — replicate comp-02/comp-03 DLL flow with logging at
// every step, no actual hook/patch installed.
//
// Today the game crashes during kcdxMessage_InputLoaded broadcast when
// either comp-02 or comp-03 plugins are enabled. The shared pattern is:
//   1. QueryInterface(Messaging) + RegisterListener
//   2. On InputLoaded: scan WHGame.dll for an AOB
//   3. Compute target VA
//   4. GetConflictReport(target, ...)
//   5. ReportTestResult
//
// This probe replicates all 5 steps with a benign AOB (the COMP-02
// pattern, but we just OBSERVE — the probe's own kcdx.toml installs
// nothing). If the probe crashes, the bug is in the engine-side
// surface (messaging, scan-via-PE-walk, or GetConflictReport).
// If the probe survives, the bug is interaction with hooks/patches
// having been installed first.
//
// The probe writes directly to a file via Win32 CreateFileW +
// WriteFile + FlushFileBuffers. The kcdx per-plugin log subsystem
// is now reliable (every write fsyncs), but the probe keeps its
// independent file as a maximum-survivability sentinel — if the
// engine's log path itself ever regresses, this file still gets
// the trace. Lines are ALSO emitted through kcdxLogger so they
// land in the standard channels (engine log, plugin log, dev log).

#include <windows.h>
#include <psapi.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "kcdx/Interfaces.h"

namespace {

const char* kName = "ts_probe_comp_crash";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
kcdxLogger           gLog;
HANDLE               g_probelog = INVALID_HANDLE_VALUE;
bool                 g_input_loaded_seen = false;

void ProbeLog(const char* fmt, ...) {
    char buf[512];
    SYSTEMTIME st;
    GetLocalTime(&st);
    int n = snprintf(buf, sizeof(buf),
        "[%02u:%02u:%02u.%03u T:%lu] ",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        GetCurrentThreadId());
    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(buf + n, sizeof(buf) - n - 2, fmt, ap);
    va_end(ap);
    if (m < 0) m = 0;
    int total = n + m;
    if (total > (int)sizeof(buf) - 2) total = (int)sizeof(buf) - 2;
    buf[total]     = '\n';
    buf[total + 1] = 0;
    if (g_probelog != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_probelog, buf, (DWORD)(total + 1), &written, nullptr);
        FlushFileBuffers(g_probelog);
    }
}

void OpenProbeLog() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t* slash = wcsrchr(exe, L'\\');
    if (slash) *slash = 0;
    wchar_t path[MAX_PATH];
    _snwprintf_s(path, MAX_PATH, _TRUNCATE,
        L"%s\\plugins\\probe-comp-crash.log", exe);
    g_probelog = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ,
                             nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_probelog == INVALID_HANDLE_VALUE) {
        // Last resort: %TEMP%
        wchar_t tmp[MAX_PATH];
        DWORD n = GetTempPathW(MAX_PATH, tmp);
        if (n > 0 && n < MAX_PATH) {
            wchar_t tpath[MAX_PATH];
            _snwprintf_s(tpath, MAX_PATH, _TRUNCATE,
                L"%sprobe-comp-crash.log", tmp);
            g_probelog = CreateFileW(tpath, GENERIC_WRITE, FILE_SHARE_READ,
                                     nullptr, CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
        }
    }
}

// ----- AOB scan mimic of comp-02's OnInputLoaded ---------------------

struct PatBytes { std::vector<uint8_t> v; std::vector<uint8_t> mask; };
PatBytes ParsePattern(const char* s) {
    PatBytes p;
    for (const char* c = s; *c; ) {
        if (*c == ' ') { ++c; continue; }
        if (*c == '?') {
            p.v.push_back(0); p.mask.push_back(0);
            ++c; if (*c == '?') ++c; continue;
        }
        char buf[3] = { c[0], c[1], 0 };
        p.v.push_back(static_cast<uint8_t>(strtoul(buf, nullptr, 16)));
        p.mask.push_back(1);
        c += 2;
    }
    return p;
}
std::vector<const uint8_t*> FindAll(const uint8_t* base, size_t size, const PatBytes& p) {
    std::vector<const uint8_t*> hits;
    if (p.v.empty() || size < p.v.size()) return hits;
    size_t plen = p.v.size();
    for (size_t i = 0; i <= size - plen; ++i) {
        bool match = true;
        for (size_t j = 0; j < plen; ++j) {
            if (p.mask[j] && base[i + j] != p.v[j]) { match = false; break; }
        }
        if (match) hits.push_back(base + i);
    }
    return hits;
}

// ---------------------------------------------------------------------

void OnMessage(kcdxMessage* msg) {
    ProbeLog("OnMessage: enter, messageType=%u", msg ? msg->messageType : 0);
    if (!msg) { ProbeLog("OnMessage: null msg, returning"); return; }
    if (msg->messageType != kcdxMessage_InputLoaded) {
        ProbeLog("OnMessage: not InputLoaded (got %u), early return",
                 msg->messageType);
        return;
    }
    if (g_input_loaded_seen) {
        ProbeLog("OnMessage: InputLoaded already seen, skipping");
        return;
    }
    g_input_loaded_seen = true;

    ProbeLog("STEP 1: about to GetModuleHandleW(\"WHGame.dll\")");
    HMODULE whgame = GetModuleHandleW(L"WHGame.dll");
    ProbeLog("STEP 1: WHGame.dll handle = %p", whgame);
    if (!whgame) { ProbeLog("STEP 1: aborting, no WHGame"); return; }

    ProbeLog("STEP 2: about to GetModuleInformation");
    MODULEINFO mi{};
    BOOL ok = GetModuleInformation(GetCurrentProcess(), whgame, &mi, sizeof(mi));
    ProbeLog("STEP 2: GetModuleInformation -> %d, base=%p size=0x%X",
             (int)ok, mi.lpBaseOfDll, mi.SizeOfImage);
    if (!ok) { ProbeLog("STEP 2: aborting"); return; }

    ProbeLog("STEP 3: about to walk PE sections");
    auto* base = static_cast<const uint8_t*>(mi.lpBaseOfDll);
    auto* dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    ProbeLog("STEP 3a: DOS e_magic=0x%X e_lfanew=0x%X",
             (unsigned)dos->e_magic, (unsigned)dos->e_lfanew);
    auto* nt   = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    ProbeLog("STEP 3b: NT signature=0x%X, NumberOfSections=%u",
             (unsigned)nt->Signature, (unsigned)nt->FileHeader.NumberOfSections);
    auto* sec  = IMAGE_FIRST_SECTION(nt);

    ProbeLog("STEP 4: ParsePattern (the comp-02 post-install AOB suffix)");
    PatBytes pat = ParsePattern(
        "48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 02");
    ProbeLog("STEP 4: pattern len=%zu", pat.v.size());

    ProbeLog("STEP 5: scan executable sections");
    std::vector<const uint8_t*> hits;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& s = sec[i];
        if (!(s.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        ProbeLog("STEP 5: scanning section[%u] RVA=0x%X size=0x%X",
                 (unsigned)i, (unsigned)s.VirtualAddress,
                 (unsigned)s.Misc.VirtualSize);
        auto found = FindAll(base + s.VirtualAddress, s.Misc.VirtualSize, pat);
        ProbeLog("STEP 5:   -> %zu hits in this section", found.size());
        hits.insert(hits.end(), found.begin(), found.end());
    }
    ProbeLog("STEP 5: total hits = %zu", hits.size());
    if (hits.empty()) {
        ProbeLog("STEP 5: no hits, ending probe early (AOB didn't match)");
        ProbeLog("PROBE END: no crash through scan; engine messaging+scan healthy");
        return;
    }

    // Even if hits != 1, continue to exercise GetConflictReport — that's
    // where comp-02/comp-03's code would have aborted, but we want the
    // probe to reach the next API call so we can see if THAT crashes.
    ProbeLog("STEP 6: compute target VA (mimicking comp-02's offset arithmetic)");
    uintptr_t target = reinterpret_cast<uintptr_t>(hits[0]) - 4 - 4;
    ProbeLog("STEP 6: target = 0x%p (hits[0]=%p)",
             (void*)target, hits[0]);

    ProbeLog("STEP 7: about to call api->GetConflictReport");
    kcdxConflictEntry entries[8];
    uint32_t count = g_api->GetConflictReport(target, entries,
                                              sizeof(entries) / sizeof(entries[0]));
    ProbeLog("STEP 7: returned count = %u", count);

    ProbeLog("STEP 8: iterate entries");
    for (uint32_t i = 0; i < count && i < 8; ++i) {
        ProbeLog("STEP 8:   entry[%u] name=%s kind=%d applied=%d priority=%d",
                 (unsigned)i,
                 entries[i].name ? entries[i].name : "(null)",
                 (int)entries[i].kind,
                 (int)entries[i].applied,
                 (int)entries[i].priority);
    }

    ProbeLog("STEP 9: about to ReportTestResult");
    gLog.Info("PROBE", "PASS: all 9 steps completed without crashing");
    g_api->ReportTestResult(g_self, "PROBE-COMP-CRASH", 1,
        "all 9 steps completed without crashing");
    ProbeLog("PROBE END: all 9 steps complete, no crash");
}

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    OpenProbeLog();
    ProbeLog("kcdxPlugin_Load: enter");
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    gLog   = kcdxLogger(api, g_self);
    gLog.Info("INIT", "kcdxPlugin_Load called (probe sentinel; see also probe-comp-crash.log)");
    ProbeLog("kcdxPlugin_Load: handle = %u", (unsigned)g_self);

    ProbeLog("kcdxPlugin_Load: QueryInterface(Messaging)");
    auto* m = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    ProbeLog("kcdxPlugin_Load: Messaging interface = %p", m);
    if (!m) {
        ProbeLog("kcdxPlugin_Load: messaging null, returning true anyway");
        return true;
    }

    ProbeLog("kcdxPlugin_Load: RegisterListener");
    bool reg = m->RegisterListener(g_self, nullptr, OnMessage);
    ProbeLog("kcdxPlugin_Load: RegisterListener -> %d", (int)reg);
    ProbeLog("kcdxPlugin_Load: returning true");
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
