// outfit-swap-dll — C++ DLL plugin demonstrating runtime byte rewrites
// via kcdxMemoryInterface.
//
// Resolves the outfit-swap-in-combat AOB at Plugin_Load time using
// kcdxMemoryInterface::ScanPattern, then rewrites the 3-byte
// `mov r14b, al` (44 8A F0) at offset +13 to `xor r14d, r14d`
// (45 31 F6) — same fix as the mempatch / [[patch]] versions, but
// done as runtime C++ instead of declarative TOML.
//
// All logging goes through api->Log (per-plugin log at
// plugins/outfit-swap-dll/outfit-swap-dll.log).

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include "kcdx/Interfaces.h"

namespace {

const char* kName = "violetanvil.outfit-swap-dll";

// Tier-2 context pattern from the kcdx.toml [[patch]] version. 23 bytes
// total, ends with the original 3 bytes `44 8A F0` we want to rewrite.
const char* kPattern = "48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0";
const int   kPatchOffset = 20;  // bytes between pattern start and patch site
const uint8_t kReplacement[3] = { 0x45, 0x31, 0xF6 };  // xor r14d, r14d

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    kcdxPluginHandle self = api->GetPluginHandle(kName);

    auto* mem = static_cast<kcdxMemoryInterface*>(
        api->QueryInterface(kcdxInterface_Memory,
                            kcdxMemoryInterface_Version));
    if (!mem) {
        api->Log(self, kcdxLog_Error,
                 "QueryInterface(Memory) returned null - kcdx too old?");
        return false;
    }

    uintptr_t hit = mem->ScanPattern("WHGame.dll", kPattern);
    if (!hit) {
        api->Log(self, kcdxLog_Error,
                 "Could not resolve outfit-swap AOB in WHGame.dll - "
                 "AOB may have drifted; check kcdx.log for context");
        return false;
    }

    uintptr_t patchAddr = hit + kPatchOffset;

    // Read-before-write so we can verify the pre-patch state.
    uint8_t before[3] = { 0, 0, 0 };
    if (!mem->ReadBytes(patchAddr, before, sizeof(before))) {
        api->Log(self, kcdxLog_Error,
                 "ReadBytes pre-check failed at resolved address");
        return false;
    }

    char buf[256];
    if (before[0] == kReplacement[0] && before[1] == kReplacement[1]
        && before[2] == kReplacement[2]) {
        snprintf(buf, sizeof(buf),
            "site at 0x%p already patched (45 31 F6) - idempotent skip",
            (void*)patchAddr);
        api->Log(self, kcdxLog_Info, buf);
        return true;
    }
    if (before[0] != 0x44 || before[1] != 0x8A || before[2] != 0xF0) {
        snprintf(buf, sizeof(buf),
            "site at 0x%p has unexpected bytes %02X %02X %02X "
            "(expected vanilla 44 8A F0); refusing to write",
            (void*)patchAddr, before[0], before[1], before[2]);
        api->Log(self, kcdxLog_Error, buf);
        return false;
    }

    if (!mem->WriteBytes(patchAddr, kReplacement, sizeof(kReplacement))) {
        api->Log(self, kcdxLog_Error, "WriteBytes failed");
        return false;
    }

    snprintf(buf, sizeof(buf),
        "patched 0x%p: 44 8A F0 -> 45 31 F6 (xor r14d, r14d) via "
        "kcdxMemoryInterface", (void*)patchAddr);
    api->Log(self, kcdxLog_Info, buf);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
