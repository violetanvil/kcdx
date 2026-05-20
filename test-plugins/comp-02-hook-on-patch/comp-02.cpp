// COMP-02 — Hook + patch overlap, both should apply.
//
// Subscribes to kInputLoaded. Re-resolves the IsInCombat AOB to find
// the function entry, then calls GetConflictReport(target) to enumerate
// the plugin's own patch + hook entries that target it. Asserts:
//   - exactly 2 entries returned (the patch + the hook)
//   - both have appliedOK = true
//   - both have name starting with "comp-02-"

#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include "kcdx/Interfaces.h"

namespace {

const char* kName        = "kcdx.comp-02-hook-on-patch";
const char* kPatternStr  = "48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 02";
const int   kEntryOffset = -4;  // function entry relative to AOB hit

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
kcdxLogger           gLog;
bool                 g_reported = false;

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

void OnInputLoaded(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    if (g_reported) return;
    g_reported = true;

    gLog.Info("CONFLICT", "InputLoaded received; resolving target and querying conflict report");

    HMODULE whgame = GetModuleHandleW(L"WHGame.dll");
    if (!whgame) {
        gLog.Error("CONFLICT", "FAIL: WHGame.dll not loaded");
        g_api->ReportTestResult(g_self, "COMP-02", 0, "WHGame.dll not loaded");
        return;
    }
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), whgame, &mi, sizeof(mi))) {
        gLog.Error("CONFLICT", "FAIL: GetModuleInformation failed");
        g_api->ReportTestResult(g_self, "COMP-02", 0, "GetModuleInformation failed");
        return;
    }
    auto* base = static_cast<const uint8_t*>(mi.lpBaseOfDll);
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);

    // Resolve target address. With the patch + hook installed at
    // [aob-4, aob+1), the original IMAGE_SCN_MEM_EXECUTE bytes have
    // been overwritten. The 27-byte AOB without the entry's leading
    // 4 bytes still matches uniquely though, because the hook only
    // wrote a 5-byte rel32 jmp starting at aob-4 (the first 4 bytes
    // of the 5-byte jmp overlap aob-4..aob-1, and byte aob+0 = E9
    // jmp opcode? actually no — the JMP starts at aob-4, so bytes
    // aob-4..aob+0 are E9 ?? ?? ?? ??. So the AOB starting at aob+1
    // onward survives. Skip past the rewritten prefix in pattern.)
    //
    // The AOB starts at the function's `mov rax, [rcx+8]` (48 8B 41 08).
    // After hook install, those bytes are gone (E9 jmp). The remaining
    // AOB suffix starting from `48 8B 88 90 00 00 00` (the second `mov
    // rcx, [rax+0x90]`) at AOB offset 4 IS still intact (the hook only
    // overwrites bytes [aob-4, aob+1)). Scan for the suffix.
    PatBytes pat = ParsePattern(
        "48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 02");
    std::vector<const uint8_t*> hits;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& s = sec[i];
        if (!(s.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        auto found = FindAll(base + s.VirtualAddress, s.Misc.VirtualSize, pat);
        hits.insert(hits.end(), found.begin(), found.end());
    }
    if (hits.size() != 1) {
        char r[160];
        snprintf(r, sizeof(r),
            "post-hook AOB suffix matches %zu times (need 1)", hits.size());
        gLog.Error("CONFLICT", "FAIL: %s", r);
        g_api->ReportTestResult(g_self, "COMP-02", 0, r);
        return;
    }
    // hits[0] = aob+4 in the original AOB. Function entry = (aob+4) - 4 - 4 = aob+4-8.
    // Actually offset -4 from the full AOB start, so target = aob - 4.
    // hits[0] - 4 (back to aob start) - 4 (kEntryOffset) = hits[0] - 8.
    uintptr_t target = reinterpret_cast<uintptr_t>(hits[0]) - 4 + kEntryOffset;

    // Query the conflict report
    kcdxConflictEntry entries[8];
    uint32_t count = g_api->GetConflictReport(target, entries,
                                              sizeof(entries) / sizeof(entries[0]));
    if (count == 0) {
        char r[160];
        snprintf(r, sizeof(r),
            "GetConflictReport(0x%p) returned 0 entries", (void*)target);
        gLog.Error("CONFLICT", "FAIL: %s", r);
        g_api->ReportTestResult(g_self, "COMP-02", 0, r);
        return;
    }
    if (count != 2) {
        char r[160];
        snprintf(r, sizeof(r),
            "GetConflictReport returned %u entries (expected 2: comp-02-patch + comp-02-hook)",
            count);
        gLog.Error("CONFLICT", "FAIL: %s", r);
        g_api->ReportTestResult(g_self, "COMP-02", 0, r);
        return;
    }

    bool patchOk = false, hookOk = false;
    std::string names;
    for (uint32_t i = 0; i < count; ++i) {
        if (!names.empty()) names += ", ";
        names += entries[i].name;
        names += "(";
        names += (entries[i].kind == kcdxConflictEntryKind_Patch) ? "patch" : "hook";
        names += "=";
        names += entries[i].applied ? "OK" : "FAIL";
        names += ")";
        if (std::string(entries[i].name) == "comp-02-patch" && entries[i].applied)
            patchOk = true;
        if (std::string(entries[i].name) == "comp-02-hook"  && entries[i].applied)
            hookOk = true;
    }

    char r[300];
    if (patchOk && hookOk) {
        snprintf(r, sizeof(r),
            "both entries applied at 0x%p: %s", (void*)target, names.c_str());
        gLog.Info("CONFLICT", "PASS: %s", r);
        g_api->ReportTestResult(g_self, "COMP-02", 1, r);
    } else {
        snprintf(r, sizeof(r),
            "expected both comp-02-patch + comp-02-hook applied; got: %s",
            names.c_str());
        gLog.Error("CONFLICT", "FAIL: %s", r);
        g_api->ReportTestResult(g_self, "COMP-02", 0, r);
    }
}

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    gLog   = kcdxLogger(api, g_self);

    gLog.Info("INIT", "kcdxPlugin_Load called");

    auto* m = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    if (!m) {
        gLog.Error("INIT", "QueryInterface(Messaging) returned null");
        api->ReportTestResult(g_self, "COMP-02", 0,
            "QueryInterface(Messaging) returned null");
        return true;
    }
    m->RegisterListener(g_self, nullptr, OnInputLoaded);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
