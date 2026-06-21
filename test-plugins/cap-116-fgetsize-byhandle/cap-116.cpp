// CAP-116 — slot 46 (CCryPak::FGetSize, by-handle) returns the file's byte SIZE,
// not a fileno (the KI-0026 residual fix).
//
// KI-0026's first fix (cap-115) made the engine config file RESOLVE + SERVE
// correctly from the pak (the index/alias layer). But the 0xC8 moved to
// "served-but-rejected": the engine read the right bytes, then sized its read
// wrong. Root cause (BODY-VERIFIED, _research/ki0026-fs-takeover-slot46-recon/):
// WHGame CCryPak slot 46 (+0x170, FUN_180460c08, RVA 0x460C08) is FGetSize — it
// returns the file's byte SIZE. The engine's FRead OS arm (slot 40, +0x140) calls
// slot 46, STORES the return as the size, and reads that many bytes. kcdx had
// slot 46 implemented as `kcdx_Fileno` (returned the raw fd; -1 for a pak handle)
// — so the engine computed size = -1 (a multi-GB count) for the pak-served config
// → CSystem::FatalError "couldn't get length for Config file" → 0xC8.
//
// The fix: slot 46 is now `kcdx_FGetSize`, backed by the pool op
// `FileSize(KcdxHandle)`. This plugin exercises that pool op directly (the cap-112
// shape: compile the engine TU under test — file_handle.cpp — into the plugin DLL,
// against a log stub, never linking the engine). It runs THREE assertions at boot:
//
//   (a) PAK SIZE: mint a Pak handle over a known N-byte buffer, assert
//       FileSize(handle) == N (NOT -1, NOT 0). This is the exact failing path —
//       before the fix the slot returned -1 for a pak handle. FALSIFIES the bug:
//       if FileSize still returned the fd (-1) or 0, this FAILS.
//   (b) BAD-HANDLE FAILS LOUD WITH 0, NOT -1 (AP14): query FileSize on a closed
//       handle, assert it returns 0 — never -1. The engine reads the return as a
//       size; a -1 here is the very multi-GB-reject bug. This pins the fail-loud
//       direction (0 = a loud short read the engine survives; -1 = the crash).
//   (c) PAK SIZE IS POSITION-INDEPENDENT: read some bytes (advance the cursor),
//       then assert FileSize still == N. Size is a property of the file, not the
//       cursor — proves the by-handle size query does not depend on / corrupt the
//       read position (the engine queries size mid-read).
//
// (a)+(b)+(c) are the load-bearing FALSIFIABLE claim. A regression that returned
// the fd again → (a) gets -1 → FAIL. One that returned -1 on a bad handle →
// (b) FAIL. One that derived size from the cursor → (c) FAIL. This is a DIRECT
// assertion on the size contract the engine's read path consumes — the live boot
// (no 0xC8) is the end-to-end proof; this row guards the primitive.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "kcdx/Interfaces.h"

#include "file_handle.h"

namespace {

// Manifest bare name — must match [plugin].name in kcdx.toml.
const char* kName = "cap_116_fgetsize_byhandle";
const char* kRow  = "cap-116-fgetsize-byhandle";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
kcdxLogger           g_log;

namespace fst = kcdx::fs_takeover;

void Report(bool pass, const char* reason) {
    if (pass) g_log.Info ("CAP116", "PASS %s: %s", kRow, reason);
    else      g_log.Error("CAP116", "FAIL %s: %s", kRow, reason);
    g_api->ReportTestResult(g_self, kRow, pass ? 1 : 0, reason);
}

}  // namespace

// === kcdxPlugin_Load ==================================================
//
// All assertions are pool ops on synthetic handles — no file I/O, no game
// lifecycle dependency. The row self-checks and reports here, at load.

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    g_log  = kcdxLogger(api, g_self);
    g_log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)", api->kcdxVersion);

    char reason[700];

    // The known byte count — the size the served thread-config has (20096), used
    // as a concrete N so the assertion mirrors the real failing file. Any N works;
    // this one ties the test to the KI-0026 file's actual size.
    const size_t kN = 20096;
    std::vector<uint8_t> bytes(kN, 0xAB);

    // --- (a) PAK SIZE: FileSize(pak handle) == N (NOT -1, NOT 0). -----------
    fst::KcdxHandle hPak = fst::MintPak(std::move(bytes), "cap-116-test-pak");
    if (hPak == 0) {
        Report(false, "(a) MintPak returned 0 (pool mint failed) — cannot test "
                      "the FGetSize contract");
        return true;
    }
    const long long sizePak = fst::FileSize(hPak);
    if (sizePak != static_cast<long long>(kN)) {
        std::snprintf(reason, sizeof(reason),
            "(a) FileSize(pak handle) returned %lld, expected %zu. This is the "
            "KI-0026 residual bug: if it returned -1 the slot is still the old "
            "fileno impl (a pak handle has no fd) → the engine reads size=-1 (a "
            "multi-GB count) → 'couldn't get length' → 0xC8. FGetSize must return "
            "the pak buffer's byte size",
            sizePak, kN);
        Report(false, reason);
        return true;
    }

    // --- (c) POSITION-INDEPENDENT: advance the cursor, size unchanged. ------
    // (Run before close, while the handle is live.) Read 100 bytes to move the
    // cursor, then re-query: the size is a file property, not a cursor offset.
    uint8_t scratch[100];
    bool readOk = false;
    fst::Read(hPak, scratch, sizeof(scratch), readOk);
    const long long sizeAfterRead = fst::FileSize(hPak);
    if (sizeAfterRead != static_cast<long long>(kN)) {
        std::snprintf(reason, sizeof(reason),
            "(c) after reading 100 bytes, FileSize returned %lld, expected %zu — "
            "the by-handle size must be the FILE's size, independent of the read "
            "cursor (the engine queries size mid-read). A size derived from the "
            "cursor/remaining-bytes is wrong",
            sizeAfterRead, kN);
        Report(false, reason);
        return true;
    }

    // --- (b) BAD HANDLE → 0, NEVER -1 (AP14 fail-loud direction). -----------
    // Close the handle, then query FileSize on the now-dead handle. It must
    // return 0 (a loud short read the engine survives), never -1 (the crash).
    fst::Close(hPak);
    const long long sizeClosed = fst::FileSize(hPak);
    if (sizeClosed != 0) {
        std::snprintf(reason, sizeof(reason),
            "(b) FileSize(closed handle) returned %lld, expected 0. A bad/closed "
            "handle must fail LOUD with 0 (the engine reads it as a 0-byte size — "
            "a loud short read it survives), NEVER -1 (which the engine reads as a "
            "multi-GB size → the 0xC8 reject this fix removes)",
            sizeClosed);
        Report(false, reason);
        return true;
    }

    std::snprintf(reason, sizeof(reason),
        "kcdx slot 46 FGetSize-by-handle PASS — (a) FileSize(pak handle) returns "
        "the buffer's byte size (%zu), not the fd (-1) the old fileno impl returned "
        "(the KI-0026 residual: engine read size=-1 → 0xC8); (b) a closed handle "
        "returns 0 not -1 (fail-loud direction — the engine survives a 0-size read, "
        "crashes on -1); (c) the size is position-independent (unchanged after a "
        "100-byte read — a file property, not the cursor). The size the engine's "
        "read path stores + reads against is now correct for a pak-served config",
        kN);
    Report(true, reason);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
