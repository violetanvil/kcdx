// COMP-03 — Two [[hook]] entries on the same function entry; first-wins.
//
// Subscribes to kInputLoaded. Re-resolves the target address (the sister
// IsInCombat wrapper at WHGame.dll RVA 0x566040) by scanning for the
// post-install AOB suffix — the first 5 bytes of the AOB were overwritten
// by plugin A's rel32 jmp on install, but bytes 5..29 survive intact and
// uniquely identify the site (the final 3C 01 disambiguates from the
// COMP-02 sister wrapper at FUN_1805605b8 which ends in 3C 02).
//
// Calls api->GetConflictReport(target) and asserts:
//   - exactly 2 entries returned
//   - one named "comp-03-A" with applied != 0
//   - one named "comp-03-B" with applied == 0
//
// On match, reports COMP-03 PASS. On mismatch, reports FAIL with a
// human-readable rundown of the entries actually returned.

#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include "kcdx/Interfaces.h"

namespace {

const char* kName = "kcdx.comp-03-B";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
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

    HMODULE whgame = GetModuleHandleW(L"WHGame.dll");
    if (!whgame) {
        g_api->ReportTestResult(g_self, "COMP-03", 0, "WHGame.dll not loaded");
        return;
    }
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), whgame, &mi, sizeof(mi))) {
        g_api->ReportTestResult(g_self, "COMP-03", 0, "GetModuleInformation failed");
        return;
    }
    auto* base = static_cast<const uint8_t*>(mi.lpBaseOfDll);
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);

    // Full AOB at target entry pre-install:
    //   48 83 EC 28 48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1
    //   60 0B 00 00 48 8B 01 FF 50 08 3C 01
    //
    // After plugin A installs MinHook's 5-byte rel32 jmp at offset 0,
    // MinHook RELOCATES whole instructions to its trampoline — not
    // just 5 bytes. The first AOB instruction `48 83 EC 28` is 4
    // bytes, the next `48 8B 41 08` is 4 bytes, so MinHook must
    // relocate 8 bytes of prologue (5 won't cover a full
    // instruction boundary). The first 5 bytes become the rel32
    // jmp `E9 ?? ?? ?? ??`; bytes [aob+5, aob+8) are leftover
    // garbage from the second instruction (now unreachable since
    // jmp diverts before they execute).
    //
    // Bytes [aob+8, aob+30) survive intact. That 22-byte suffix
    // starts `48 8B 88 90 00 00 00 ...` and ends `3C 01`. Unique
    // in .text (the COMP-02 sister at FUN_1805605b8 ends `3C 02`).
    PatBytes pat = ParsePattern(
        "48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 01");
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
            "post-install AOB suffix matches %zu times (need 1)", hits.size());
        g_api->ReportTestResult(g_self, "COMP-03", 0, r);
        return;
    }
    // hits[0] points at aob+8. Function entry = aob+0 = hits[0] - 8.
    uintptr_t target = reinterpret_cast<uintptr_t>(hits[0]) - 8;

    kcdxConflictEntry entries[8];
    uint32_t count = g_api->GetConflictReport(target, entries,
                                              sizeof(entries) / sizeof(entries[0]));
    if (count != 2) {
        char r[200];
        snprintf(r, sizeof(r),
            "GetConflictReport(0x%p) returned %u entries (expected 2: comp-03-A + comp-03-B)",
            (void*)target, count);
        g_api->ReportTestResult(g_self, "COMP-03", 0, r);
        return;
    }

    bool aWon = false, bLost = false;
    std::string names;
    for (uint32_t i = 0; i < count; ++i) {
        if (!names.empty()) names += ", ";
        names += entries[i].name;
        names += "(";
        names += (entries[i].kind == kcdxConflictEntryKind_Patch) ? "patch" : "hook";
        names += "=";
        names += entries[i].applied ? "OK" : "ABORTED";
        names += ")";
        if (std::string(entries[i].name) == "comp-03-A" && entries[i].applied)
            aWon = true;
        if (std::string(entries[i].name) == "comp-03-B" && !entries[i].applied)
            bLost = true;
    }

    char r[300];
    if (aWon && bLost) {
        snprintf(r, sizeof(r),
            "first-wins at 0x%p: %s", (void*)target, names.c_str());
        g_api->ReportTestResult(g_self, "COMP-03", 1, r);
    } else {
        snprintf(r, sizeof(r),
            "expected comp-03-A applied + comp-03-B aborted; got: %s",
            names.c_str());
        g_api->ReportTestResult(g_self, "COMP-03", 0, r);
    }
}

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    auto* m = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    if (!m) {
        api->ReportTestResult(g_self, "COMP-03", 0,
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
