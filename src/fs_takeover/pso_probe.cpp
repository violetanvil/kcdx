// === DIAGNOSTIC (PROBE P) — KI-0028 shader-blob -> PSO consumption ground truth ===
// See pso_probe.h for WHY + the outcome->meaning map. NO-RESIDUE on retire.

#include "pso_probe.h"

#include <windows.h>
#include <d3d12.h>

#include <atomic>
#include <cstdint>

#include "MinHook.h"
#include "../log.h"

namespace kcdx::fs_takeover {

namespace {

using KV = ::kcdx::log::KV;
constexpr const char* kCat = "PSO_PROBE";

// ID3D12Device vtable slot indices (0-based, VERIFIED from the Windows SDK
// d3d12.h ID3D12DeviceVtbl method order):
//   0 QueryInterface, 1 AddRef, 2 Release, ...,
//   10 CreateGraphicsPipelineState, 11 CreateComputePipelineState.
// We patch 10 + 11 on the live device's shared class vtable (the patch then
// catches every PSO creation the engine drives through that device).
constexpr int kSlot_CreateGraphicsPSO = 10;
constexpr int kSlot_CreateComputePSO = 11;

// DXBC container magic ('DXBC' little-endian) — the first 4 bytes of a valid
// compiled shader blob. DXIL blobs are ALSO wrapped in a DXBC container, so this
// magic holds for both. A served shader byte stream that is NOT this is malformed
// at the point D3D12 receives it (outcome O1).
constexpr uint32_t kDXBCMagic = 0x43425844;  // 'D''X''B''C'

std::atomic<bool> g_armed{false};
std::atomic<ID3D12Device*> g_device{nullptr};

// Per-call aggregate counters (relaxed — diagnostic tallies, no happens-before
// edge synchronized against). Flushed in the summary, not per call.
std::atomic<uint64_t> g_gfxCalls{0};
std::atomic<uint64_t> g_gfxFailed{0};      // hr failed OR null PSO out
std::atomic<uint64_t> g_gfxNullPso{0};
std::atomic<uint64_t> g_blobBadMagic{0};   // a VS/PS blob present but not DXBC
std::atomic<uint64_t> g_blobZeroLen{0};    // a VS/PS blob ptr present, len 0
std::atomic<uint64_t> g_compCalls{0};
std::atomic<uint64_t> g_compFailed{0};

// Per-call DEBUG lines are rate-bounded: log the first kLogFirst calls in full,
// and ALWAYS log a failure (O1/O2 evidence), but never flood the hot creation
// path (logging.md — count, don't log per iteration).
constexpr uint64_t kLogFirst = 24;

void** VtableOf(void* obj) { return *reinterpret_cast<void***>(obj); }

// Read a shader bytecode blob's len + container magic for the log. A null/zero
// blob is a normal "this stage unused in this PSO" — only counted when a pointer
// is present but the bytes are malformed.
struct BlobFacts {
    uint64_t len;
    uint32_t magic;     // first 4 bytes, or 0 if len < 4
    bool present;       // pShaderBytecode != null && len > 0
    bool dxbc;          // present && magic == kDXBCMagic
};
BlobFacts ReadBlob(const D3D12_SHADER_BYTECODE& bc) {
    BlobFacts f{};
    f.len = (uint64_t)bc.BytecodeLength;
    f.present = (bc.pShaderBytecode != nullptr && bc.BytecodeLength > 0);
    if (f.present) {
        if (bc.BytecodeLength >= 4) {
            f.magic = *reinterpret_cast<const uint32_t*>(bc.pShaderBytecode);
            f.dxbc = (f.magic == kDXBCMagic);
        }
        if (bc.BytecodeLength == 0) ++g_blobZeroLen;  // (unreachable given present, kept for symmetry)
        if (!f.dxbc) ++g_blobBadMagic;
    }
    return f;
}

// --- ID3D12Device::CreateGraphicsPipelineState detour (slot 10) ---
using CreateGfxPSO_t = HRESULT(STDMETHODCALLTYPE*)(
    void* self, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID riid,
    void** ppPSO);
CreateGfxPSO_t g_origCreateGfxPSO = nullptr;

HRESULT STDMETHODCALLTYPE HookedCreateGfxPSO(
    void* self, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID riid,
    void** ppPSO) {
    const uint64_t n = g_gfxCalls.fetch_add(1) + 1;

    BlobFacts vs{}, ps{};
    if (desc) {
        vs = ReadBlob(desc->VS);
        ps = ReadBlob(desc->PS);
    }

    HRESULT hr = g_origCreateGfxPSO(self, desc, riid, ppPSO);

    const bool nullPso = !(ppPSO && *ppPSO);
    const bool failed = FAILED(hr) || nullPso;
    if (failed) {
        ++g_gfxFailed;
        if (nullPso) ++g_gfxNullPso;
    }

    // Log the first N in full + ALWAYS log a failure or a malformed blob (the
    // O1/O2 evidence). Otherwise silent (the hot path is just counted).
    const bool malformed =
        (vs.present && !vs.dxbc) || (ps.present && !ps.dxbc);
    if (n <= kLogFirst || failed || malformed) {
        LOG_DEBUG_KV(kCat, "gfx_pso",
            KV("call_n",     (uint64_t)n),
            KV("hr",         (uint64_t)(uint32_t)hr),
            KV("null_pso",   nullPso ? 1 : 0),
            KV("vs_len",     vs.len),
            KV("vs_dxbc",    vs.present ? (vs.dxbc ? 1 : 0) : -1),
            KV("vs_magic",   (uint64_t)vs.magic),
            KV("ps_len",     ps.len),
            KV("ps_dxbc",    ps.present ? (ps.dxbc ? 1 : 0) : -1),
            KV("ps_magic",   (uint64_t)ps.magic));
    }
    return hr;
}

// --- ID3D12Device::CreateComputePipelineState detour (slot 11) ---
using CreateCompPSO_t = HRESULT(STDMETHODCALLTYPE*)(
    void* self, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, REFIID riid,
    void** ppPSO);
CreateCompPSO_t g_origCreateCompPSO = nullptr;

HRESULT STDMETHODCALLTYPE HookedCreateCompPSO(
    void* self, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, REFIID riid,
    void** ppPSO) {
    const uint64_t n = g_compCalls.fetch_add(1) + 1;
    BlobFacts cs{};
    if (desc) cs = ReadBlob(desc->CS);

    HRESULT hr = g_origCreateCompPSO(self, desc, riid, ppPSO);

    const bool nullPso = !(ppPSO && *ppPSO);
    const bool failed = FAILED(hr) || nullPso;
    if (failed) ++g_compFailed;

    const bool malformed = cs.present && !cs.dxbc;
    if (n <= kLogFirst || failed || malformed) {
        LOG_DEBUG_KV(kCat, "comp_pso",
            KV("call_n",   (uint64_t)n),
            KV("hr",       (uint64_t)(uint32_t)hr),
            KV("null_pso", nullPso ? 1 : 0),
            KV("cs_len",   cs.len),
            KV("cs_dxbc",  cs.present ? (cs.dxbc ? 1 : 0) : -1),
            KV("cs_magic", (uint64_t)cs.magic));
    }
    return hr;
}

bool HookVtableSlot(void* comObj, int slot, void* detour, void** origOut,
                    const char* label) {
    void* target = VtableOf(comObj)[slot];
    MH_STATUS s = MH_CreateHook(target, detour, origOut);
    if (s != MH_OK && s != MH_ERROR_ALREADY_CREATED) {
        LOG_ERROR_KV(kCat, "vtable_hook_create_failed",
            KV::BareStr("label", label), KV("mh_status", (uint64_t)s));
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        LOG_ERROR_KV(kCat, "vtable_hook_enable_failed",
            KV::BareStr("label", label));
        return false;
    }
    return true;
}

// A bounded summary watcher: log the running PSO tallies every few seconds so the
// outcome (O2/O3/O5) is visible without per-call flooding. Reuses the sanctioned
// boot_watch/present_probe diagnostic-poll shape (one dedicated thread, no thread
// suspended). Stops after ~2 min.
constexpr DWORD kSummaryMs = 3000;
std::atomic<bool> g_watcherStarted{false};

DWORD WINAPI SummaryMain(LPVOID) {
    LOG_INFO_KV(kCat, "watcher_started",
        KV::BareStr("detail",
            "KI-0028 PSO-creation watcher armed. gfx_failed>0 with blobs dxbc=1 "
            "=> O2 (bytes fine, PSO assembly fails). gfx_failed=0 + all dxbc=1 "
            "=> O3 (PSOs fine, black is downstream of PSO). any vs/ps_dxbc=0 "
            "=> O1 (malformed served bytes). gfx_calls low / a UI PSO absent "
            "=> O5 (resolution path diverted)."));
    for (int reads = 0; reads < 40; ++reads) {  // ~2 min
        Sleep(kSummaryMs);
        LOG_INFO_KV(kCat, "summary",
            KV("gfx_calls",     g_gfxCalls.load()),
            KV("gfx_failed",    g_gfxFailed.load()),
            KV("gfx_null_pso",  g_gfxNullPso.load()),
            KV("blob_bad_magic",g_blobBadMagic.load()),
            KV("blob_zero_len", g_blobZeroLen.load()),
            KV("comp_calls",    g_compCalls.load()),
            KV("comp_failed",   g_compFailed.load()));
    }
    LOG_INFO_KV(kCat, "watcher_done",
        KV::BareStr("detail", "40 summary reads taken; stopping the PSO watcher."));
    return 0;
}

void StartWatcherOnce() {
    bool expected = false;
    if (!g_watcherStarted.compare_exchange_strong(expected, true,
                                                  std::memory_order_acq_rel)) {
        return;
    }
    HANDLE h = CreateThread(nullptr, 0, SummaryMain, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    else g_watcherStarted.store(false, std::memory_order_release);
}

void CaptureDevice(ID3D12Device* dev) {
    ID3D12Device* expected = nullptr;
    if (!g_device.compare_exchange_strong(expected, dev,
                                          std::memory_order_acq_rel)) {
        return;  // already captured (a second device — keep the first)
    }
    LOG_INFO_KV(kCat, "device_captured", KV("device", (void*)dev));

    bool any = false;
    any |= HookVtableSlot(dev, kSlot_CreateGraphicsPSO,
                          reinterpret_cast<void*>(&HookedCreateGfxPSO),
                          reinterpret_cast<void**>(&g_origCreateGfxPSO),
                          "CreateGraphicsPipelineState");
    any |= HookVtableSlot(dev, kSlot_CreateComputePSO,
                          reinterpret_cast<void*>(&HookedCreateCompPSO),
                          reinterpret_cast<void**>(&g_origCreateCompPSO),
                          "CreateComputePipelineState");
    LOG_INFO_KV(kCat, "pso_hooks_armed", KV("ok", any ? 1 : 0),
        KV::BareStr("detail",
            "patched ID3D12Device CreateGraphicsPipelineState (slot 10) + "
            "CreateComputePipelineState (slot 11) on the shared class vtable."));
    StartWatcherOnce();
}

// --- d3d12!D3D12CreateDevice detour (one-shot device capture) ---
using D3D12CreateDevice_t = HRESULT(WINAPI*)(
    IUnknown* adapter, D3D_FEATURE_LEVEL minFeatureLevel, REFIID riid,
    void** ppDevice);
D3D12CreateDevice_t g_origD3D12CreateDevice = nullptr;

HRESULT WINAPI HookedD3D12CreateDevice(
    IUnknown* adapter, D3D_FEATURE_LEVEL minFeatureLevel, REFIID riid,
    void** ppDevice) {
    HRESULT hr = g_origD3D12CreateDevice(adapter, minFeatureLevel, riid, ppDevice);
    if (SUCCEEDED(hr) && ppDevice && *ppDevice) {
        // ppDevice holds an ID3D12Device-derived interface (the engine may ask
        // for a newer ID3D12Device{1..}); the vtable's first 12 slots are the
        // base ID3D12Device layout, so slot 10/11 are correct for any derived.
        CaptureDevice(reinterpret_cast<ID3D12Device*>(*ppDevice));
    }
    return hr;
}

void ArmDeviceHook() {
    HMODULE d3d12 = GetModuleHandleW(L"d3d12.dll");
    if (!d3d12) d3d12 = LoadLibraryW(L"d3d12.dll");
    if (!d3d12) {
        LOG_ERROR_KV(kCat, "d3d12_not_loaded", KV::BareStr("detail",
            "d3d12.dll not present — cannot arm the PSO probe."));
        return;
    }
    void* target =
        reinterpret_cast<void*>(GetProcAddress(d3d12, "D3D12CreateDevice"));
    if (!target) {
        LOG_ERROR_KV(kCat, "d3d12createdevice_not_found", KV::BareStr("detail",
            "D3D12CreateDevice export missing."));
        return;
    }
    MH_STATUS s = MH_CreateHook(
        target, reinterpret_cast<void*>(&HookedD3D12CreateDevice),
        reinterpret_cast<void**>(&g_origD3D12CreateDevice));
    if (s != MH_OK && s != MH_ERROR_ALREADY_CREATED) {
        LOG_ERROR_KV(kCat, "createdevice_hook_create_failed",
            KV("mh_status", (uint64_t)s));
        return;
    }
    if (MH_EnableHook(target) != MH_OK) {
        LOG_ERROR_KV(kCat, "createdevice_hook_enable_failed");
        return;
    }
    LOG_INFO_KV(kCat, "createdevice_hook_armed", KV::BareStr("detail",
        "patched d3d12!D3D12CreateDevice; the first device creation captures the "
        "device + patches its PSO-creation slots. If the device already exists, "
        "this never fires (outcome: device never captured)."));
}

}  // namespace

void PsoProbeStart() {
    bool expected = false;
    if (!g_armed.compare_exchange_strong(expected, true,
                                         std::memory_order_acq_rel)) {
        return;  // already armed
    }
    MH_STATUS mi = MH_Initialize();
    if (mi != MH_OK && mi != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_ERROR_KV(kCat, "mh_init_failed", KV("mh_status", (uint64_t)mi));
        g_armed.store(false, std::memory_order_release);
        return;
    }
    ArmDeviceHook();
}

}  // namespace kcdx::fs_takeover
