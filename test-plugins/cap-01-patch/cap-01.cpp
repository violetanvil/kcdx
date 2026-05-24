// CAP-01 — DLL companion for the [[patch]] test.
//
// Subscribes to kInputLoaded (fires after patches have applied on the
// first update tick). Then re-scans WHGame.dll for the same AOB the
// patch used, offsets by 13, and reads 3 bytes. If they equal the
// replacement (45 31 F6), reports pass; if they equal the original
// (44 8A F0), reports fail (patch didn't apply); otherwise reports
// fail with the observed bytes.

#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include "kcdx/Interfaces.h"

namespace {

const char* kName        = "cap_01_patch";
// Two full 23-byte patterns: pre-patch (vanilla, ends with the original
// mov r14b, al = 44 8A F0) and post-patch (kcdx replacement, ends with
// xor r14d, r14d = 45 31 F6). Each is unique in WHGame.dll's executable
// sections (the wider context disambiguates from coincidental matches
// of the shorter prefix). After kcdx applies, only the post-patch
// pattern matches → PASS. If the pre-patch pattern still matches →
// patch didn't apply → FAIL. Neither matches → unexpected state.
const char* kPrePatchPattern  =
    "48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0";
const char* kPostPatchPattern =
    "48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 45 31 F6";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
kcdxLogger           gLog;
bool                 g_reported = false;

// Trivial AOB scan: each "byte" is either two hex chars or "?". Returns
// every match in [base, base+size) as byte offsets.
struct PatBytes {
    std::vector<uint8_t> v;
    std::vector<uint8_t> mask;  // 1 = match exact, 0 = wildcard
};
PatBytes ParsePattern(const char* s) {
    PatBytes p;
    for (const char* c = s; *c; ) {
        if (*c == ' ') { ++c; continue; }
        if (*c == '?') {
            p.v.push_back(0);
            p.mask.push_back(0);
            ++c;
            if (*c == '?') ++c;
            continue;
        }
        char buf[3] = { c[0], c[1], 0 };
        p.v.push_back(static_cast<uint8_t>(strtoul(buf, nullptr, 16)));
        p.mask.push_back(1);
        c += 2;
    }
    return p;
}

// Find all matches and return them. Used so we can verify uniqueness.
std::vector<const uint8_t*> FindAll(const uint8_t* base, size_t size,
                                    const PatBytes& p) {
    std::vector<const uint8_t*> hits;
    if (p.v.empty() || size < p.v.size()) return hits;
    size_t plen = p.v.size();
    size_t last = size - plen;
    for (size_t i = 0; i <= last; ++i) {
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

    gLog.Info("VERIFY", "InputLoaded received; scanning WHGame.dll");

    HMODULE whgame = GetModuleHandleW(L"WHGame.dll");
    if (!whgame) {
        gLog.Error("VERIFY", "WHGame.dll not loaded");
        g_api->ReportTestResult(g_self, "CAP-01", 0,
            "WHGame.dll not loaded");
        return;
    }
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), whgame, &mi, sizeof(mi))) {
        g_api->ReportTestResult(g_self, "CAP-01", 0,
            "GetModuleInformation failed");
        return;
    }

    // Scan executable sections of WHGame.dll only. A naive scan of the
    // whole module (including .data, .rdata, .rsrc) finds coincidental
    // byte matches in non-code regions — exactly what kcdx avoids by
    // routing through pe::ExecutableSections.
    auto* base = static_cast<const uint8_t*>(mi.lpBaseOfDll);
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);

    auto scanAll = [&](const PatBytes& pat) {
        std::vector<const uint8_t*> hits;
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            const auto& s = sec[i];
            if (!(s.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            auto found = FindAll(base + s.VirtualAddress, s.Misc.VirtualSize, pat);
            hits.insert(hits.end(), found.begin(), found.end());
        }
        return hits;
    };

    PatBytes prePat  = ParsePattern(kPrePatchPattern);
    PatBytes postPat = ParsePattern(kPostPatchPattern);
    auto preHits  = scanAll(prePat);
    auto postHits = scanAll(postPat);

    char reason[200];
    if (postHits.size() == 1 && preHits.empty()) {
        snprintf(reason, sizeof(reason),
            "post-patch pattern unique at 0x%p; pre-patch absent (patch applied)",
            postHits[0]);
        gLog.Info("VERIFY", "PASS: %s", reason);
        g_api->ReportTestResult(g_self, "CAP-01", 1, reason);
    } else if (preHits.size() == 1 && postHits.empty()) {
        snprintf(reason, sizeof(reason),
            "pre-patch pattern still present at 0x%p (patch did NOT apply)",
            preHits[0]);
        gLog.Error("VERIFY", "FAIL: %s", reason);
        g_api->ReportTestResult(g_self, "CAP-01", 0, reason);
    } else {
        snprintf(reason, sizeof(reason),
            "unexpected state: pre-patch matches=%zu, post-patch matches=%zu",
            preHits.size(), postHits.size());
        gLog.Error("VERIFY", "FAIL: %s", reason);
        g_api->ReportTestResult(g_self, "CAP-01", 0, reason);
    }
}

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    gLog   = kcdxLogger(api, g_self);

    gLog.Info("INIT", "kcdxPlugin_Load called; waiting for InputLoaded to verify patch");

    auto* msg = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    if (!msg) {
        gLog.Error("INIT", "QueryInterface(Messaging) returned null");
        api->ReportTestResult(g_self, "CAP-01", 0,
            "QueryInterface(Messaging) returned null");
        return true;
    }
    msg->RegisterListener(g_self, /*sender=*/nullptr, OnInputLoaded);
    // Report happens in OnInputLoaded, after patches have applied.
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
