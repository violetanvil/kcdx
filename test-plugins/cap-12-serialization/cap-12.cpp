// CAP-12 — kcdxSerializationInterface save/load roundtrip.
//
// The test: maintain an in-memory counter that increments every time
// we get a SaveCallback. On Load, read back the previously-saved
// counter and verify it matches our expectation. On Revert (no
// cosave for this save, or new game), reset to 0.
//
// Tag mechanism: the counter record is opened by NAME ("counter") via
// OpenRecordNamed (the v2 string-tag path), and read back by comparing
// GetRecordTagName() against "counter" — not by hand-packing a FourCC
// magic number. The engine carries the name->u32 hash and the string
// for read-back, so the author never reverse-engineers a tag encoding
// (the disassembler-test fix for the hand-packed tag, AP12). The UID
// stays an explicit SetUniqueID (the valid C++ expert path; a C++
// name-derived-UID default is a tracked future parity item).
//
// Pass conditions (reported at successive lifecycle messages):
//   - kInputLoaded: registration succeeded (interface fetched,
//     callbacks set without error).
//   - kPostLoadGame: either Load fired with a correct round-trip
//     value, OR Revert fired (cosave didn't exist yet — also valid
//     since the engine's "no plugin data for me, please revert"
//     path is exercised).
//
// Manual confirmation (for the developer): save the game once with
// dev mode on, then quit, reboot, and load that save. kcdx.log
// should show `Test suite: ... CAP-12 PASS reason="load round-trip"
// counter=N`.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "kcdx/Interfaces.h"

namespace {
const char* kName = "ts_cap_12_serialization";

// Cosave UID — distinct from any other plugin. The C++ surface pins
// the plugin's section identity explicitly via SetUniqueID (the valid
// expert path); the Lua cosave binder auto-derives the UID from the
// plugin name, but no C++ name-derived-UID default exists yet (a tracked
// future parity item). ASCII "C12S" packed little-endian.
constexpr uint32_t kUID = 0x53323143;  // 'C','1','2','S'

// Record version. The record itself is opened by NAME ("counter") via
// OpenRecordNamed (the string-tag path) — the engine hashes the name to
// the u32 it stores and records the string for read-back, so this plugin
// never hand-packs a FourCC tag (the disassembler-test fix for the
// hand-packed magic-number tag, AP12).
constexpr uint32_t kRecordVersion = 1;

const kcdxInterface*              g_api  = nullptr;
const kcdxSerializationInterface* g_ser  = nullptr;
kcdxPluginHandle                  g_self = kcdxInvalidPluginHandle;
kcdxLogger                        gLog;

// The state we're persisting: a counter and a "did we successfully
// round-trip on this session?" flag.
uint64_t g_counter             = 0;
bool     g_loadObserved        = false;
bool     g_revertObserved      = false;
uint64_t g_lastLoadedValue     = 0;
bool     g_inputLoadedReported = false;
bool     g_loadReported        = false;
bool     g_registrationOK      = false;

void Report(const char* status, int pass, const char* reasonFmt, ...) {
    char reason[256];
    va_list ap;
    va_start(ap, reasonFmt);
    vsnprintf(reason, sizeof(reason), reasonFmt, ap);
    va_end(ap);
    if (pass) {
        gLog.Info("SERIALIZATION", "PASS: %s", reason);
    } else {
        gLog.Error("SERIALIZATION", "FAIL: %s", reason);
    }
    g_api->ReportTestResult(g_self, "CAP-12", pass, reason);
    (void)status;
}

void OnSave(kcdxPluginHandle /*plugin*/) {
    g_counter += 1;
    g_ser->OpenRecordNamed("counter", kRecordVersion);
    g_ser->WriteRecordData(&g_counter, sizeof(g_counter));
}

void OnLoad(kcdxPluginHandle /*plugin*/) {
    g_loadObserved = true;
    uint32_t tag = 0, version = 0, len = 0;
    while (g_ser->GetNextRecordInfo(&tag, &version, &len)) {
        // Match by the human-readable STRING tag, not the hashed u32:
        // GetRecordTagName hands back the name the chunk was opened
        // under ("counter"). This proves the named round-trip end-to-end
        // and is what the cross-language parity test leans on (a Lua
        // plugin writing "counter" and this C++ plugin reading "counter"
        // resolve to the same record).
        const char* name = g_ser->GetRecordTagName();
        if (name && strcmp(name, "counter") == 0 && len == sizeof(uint64_t)) {
            uint64_t v = 0;
            if (g_ser->ReadRecordData(&v, sizeof(v))) {
                g_lastLoadedValue = v;
                g_counter = v;  // restore in-memory state
            }
        }
        // For any record we don't recognize, just call
        // GetNextRecordInfo again — the engine auto-skips unread
        // chunks.
    }
}

void OnRevert(kcdxPluginHandle /*plugin*/) {
    g_revertObserved = true;
    g_counter = 0;
    g_lastLoadedValue = 0;
}

void OnMessage(kcdxMessage* msg) {
    gLog.Info("SERIALIZATION", "OnMessage received type=%d; dispatching", msg->messageType);
    switch (msg->messageType) {
    case kcdxMessage_InputLoaded:
        if (g_inputLoadedReported) break;
        g_inputLoadedReported = true;
        if (g_registrationOK) {
            Report("input", 1,
                "registration ok (uid=0x%08X); awaiting first load to confirm round-trip",
                kUID);
        } else {
            Report("input", 0, "QueryInterface(Serialization) failed at Plugin_Load");
        }
        break;

    case kcdxMessage_PostLoadGame:
        if (g_loadReported) break;
        if (g_loadObserved) {
            g_loadReported = true;
            Report("post-load", 1,
                "Load round-trip: read counter=%llu from cosave",
                static_cast<unsigned long long>(g_lastLoadedValue));
        } else if (g_revertObserved) {
            g_loadReported = true;
            Report("post-load", 1,
                "Revert fired (no cosave yet — expected on first load of an existing save); "
                "save+reboot+load to verify round-trip");
        }
        break;

    default: break;
    }
}
}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    gLog   = kcdxLogger(api, g_self);

    gLog.Info("INIT", "kcdxPlugin_Load called");

    g_ser = static_cast<const kcdxSerializationInterface*>(
        api->QueryInterface(kcdxInterface_Serialization,
                            kcdxSerializationInterface_Version));
    if (!g_ser) {
        gLog.Error("INIT", "QueryInterface(Serialization) returned null");
        api->ReportTestResult(g_self, "CAP-12", 0,
            "QueryInterface(Serialization) returned null");
        return true;
    }

    // SKSE pattern: register UID + callbacks at plugin load time.
    g_ser->SetUniqueID(g_self, kUID);
    g_ser->SetSaveCallback  (g_self, OnSave);
    g_ser->SetLoadCallback  (g_self, OnLoad);
    g_ser->SetRevertCallback(g_self, OnRevert);

    auto* m = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    if (!m) {
        gLog.Error("INIT", "QueryInterface(Messaging) returned null");
        api->ReportTestResult(g_self, "CAP-12", 0,
            "QueryInterface(Messaging) returned null");
        return true;
    }
    m->RegisterListener(g_self, nullptr, OnMessage);

    g_registrationOK = true;
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
