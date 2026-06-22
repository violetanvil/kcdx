// === DIAGNOSTIC (PROBE S) — KI-0028 command-list DRAW recording ground truth ===
// See drawcall_probe.h for WHY + the outcome->meaning map. NO-RESIDUE on retire.

#include "drawcall_probe.h"

#include <windows.h>
#include <d3d12.h>

#include <atomic>
#include <cstdint>

#include "MinHook.h"
#include "../log.h"

namespace kcdx::fs_takeover {

namespace {

using KV = ::kcdx::log::KV;
constexpr const char* kCat = "DRAW_PROBE";

// Canonical D3D12 COM vtable slot indices (0-based, Microsoft SDK d3d12.h order —
// fixed by the interface spec, cross-checked against the documented method order;
// NOT a KCD2-specific vtable, so AP3 does not apply).
constexpr int kDevSlot_CreateCommandList = 12;     // ID3D12Device
constexpr int kCmdSlot_DrawInstanced        = 12;  // ID3D12GraphicsCommandList
constexpr int kCmdSlot_DrawIndexedInstanced = 13;
// SOURCE: d3d12.h ID3D12GraphicsCommandListVtbl member order, re-verified 0-based
// (the prior 47 was ClearDepthStencilView — its by-value DSV handle was deref'd as
// an RT-handle array → the AV at HookedOMSetRT+0x11 in dump kcdx_2026-06-22_16-35-19).
constexpr int kCmdSlot_OMSetRenderTargets   = 46;

std::atomic<bool> g_armed{false};
std::atomic<bool> g_cmdVtablePatched{false};  // patch the shared cmd-list vtable once

// Counters (relaxed — diagnostic tallies).
std::atomic<uint64_t> g_createCmdList{0};
std::atomic<uint64_t> g_drawInstanced{0};
std::atomic<uint64_t> g_drawIndexed{0};
std::atomic<uint64_t> g_omSetRT{0};
std::atomic<uint64_t> g_omSetRT_nullRT{0};   // OMSetRenderTargets with 0 RTs / null handle

void** VtableOf(void* obj) { return *reinterpret_cast<void***>(obj); }

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
        LOG_ERROR_KV(kCat, "vtable_hook_enable_failed", KV::BareStr("label", label));
        return false;
    }
    return true;
}

// --- ID3D12GraphicsCommandList::DrawInstanced (slot 12) ---
using DrawInstanced_t = void(STDMETHODCALLTYPE*)(
    void* self, UINT vtxPerInstance, UINT instanceCount, UINT startVtx,
    UINT startInstance);
DrawInstanced_t g_origDrawInstanced = nullptr;
void STDMETHODCALLTYPE HookedDrawInstanced(
    void* self, UINT vtxPerInstance, UINT instanceCount, UINT startVtx,
    UINT startInstance) {
    g_drawInstanced.fetch_add(1);
    g_origDrawInstanced(self, vtxPerInstance, instanceCount, startVtx, startInstance);
}

// --- ID3D12GraphicsCommandList::DrawIndexedInstanced (slot 13) ---
using DrawIndexed_t = void(STDMETHODCALLTYPE*)(
    void* self, UINT idxPerInstance, UINT instanceCount, UINT startIdx,
    INT baseVtx, UINT startInstance);
DrawIndexed_t g_origDrawIndexed = nullptr;
void STDMETHODCALLTYPE HookedDrawIndexed(
    void* self, UINT idxPerInstance, UINT instanceCount, UINT startIdx,
    INT baseVtx, UINT startInstance) {
    g_drawIndexed.fetch_add(1);
    g_origDrawIndexed(self, idxPerInstance, instanceCount, startIdx, baseVtx,
                      startInstance);
}

// --- ID3D12GraphicsCommandList::OMSetRenderTargets (slot 47) ---
using OMSetRT_t = void(STDMETHODCALLTYPE*)(
    void* self, UINT numRTs, const D3D12_CPU_DESCRIPTOR_HANDLE* rtHandles,
    BOOL singleHandle, const D3D12_CPU_DESCRIPTOR_HANDLE* dsHandle);
OMSetRT_t g_origOMSetRT = nullptr;
void STDMETHODCALLTYPE HookedOMSetRT(
    void* self, UINT numRTs, const D3D12_CPU_DESCRIPTOR_HANDLE* rtHandles,
    BOOL singleHandle, const D3D12_CPU_DESCRIPTOR_HANDLE* dsHandle) {
    g_omSetRT.fetch_add(1);
    // A bind of 0 render targets (or a null handle array) is the "no color target
    // bound" shape — draws would land nowhere visible.
    const bool nullRT = (numRTs == 0) ||
                        (rtHandles == nullptr) ||
                        (rtHandles[0].ptr == 0);
    if (nullRT) g_omSetRT_nullRT.fetch_add(1);
    g_origOMSetRT(self, numRTs, rtHandles, singleHandle, dsHandle);
}

void PatchCommandListVtableOnce(void* cmdList) {
    bool expected = false;
    if (!g_cmdVtablePatched.compare_exchange_strong(expected, true,
                                                    std::memory_order_acq_rel)) {
        return;  // the shared class vtable is already patched
    }
    bool any = false;
    any |= HookVtableSlot(cmdList, kCmdSlot_DrawInstanced,
                          reinterpret_cast<void*>(&HookedDrawInstanced),
                          reinterpret_cast<void**>(&g_origDrawInstanced),
                          "DrawInstanced");
    any |= HookVtableSlot(cmdList, kCmdSlot_DrawIndexedInstanced,
                          reinterpret_cast<void*>(&HookedDrawIndexed),
                          reinterpret_cast<void**>(&g_origDrawIndexed),
                          "DrawIndexedInstanced");
    any |= HookVtableSlot(cmdList, kCmdSlot_OMSetRenderTargets,
                          reinterpret_cast<void*>(&HookedOMSetRT),
                          reinterpret_cast<void**>(&g_origOMSetRT),
                          "OMSetRenderTargets");
    LOG_INFO_KV(kCat, "cmdlist_hooks_armed", KV("ok", any ? 1 : 0),
        KV::BareStr("detail",
            "patched the shared ID3D12GraphicsCommandList vtable: DrawInstanced "
            "(12), DrawIndexedInstanced (13), OMSetRenderTargets (47). Draw counts "
            "are the KI-0028 ground truth — black = few/zero real draws recorded."));
}

// --- ID3D12Device::CreateCommandList (slot 12) detour — capture each new list ---
using CreateCmdList_t = HRESULT(STDMETHODCALLTYPE*)(
    void* self, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, void* allocator,
    void* initialState, REFIID riid, void** ppCmdList);
CreateCmdList_t g_origCreateCmdList = nullptr;
HRESULT STDMETHODCALLTYPE HookedCreateCmdList(
    void* self, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, void* allocator,
    void* initialState, REFIID riid, void** ppCmdList) {
    HRESULT hr = g_origCreateCmdList(self, nodeMask, type, allocator, initialState,
                                     riid, ppCmdList);
    if (SUCCEEDED(hr) && ppCmdList && *ppCmdList) {
        g_createCmdList.fetch_add(1);
        // Only DIRECT/BUNDLE lists carry the Draw* methods; patch on the first one.
        if (type == D3D12_COMMAND_LIST_TYPE_DIRECT ||
            type == D3D12_COMMAND_LIST_TYPE_BUNDLE) {
            PatchCommandListVtableOnce(*ppCmdList);
        }
    }
    return hr;
}

// --- device handed over by PROBE P (MinHook allows ONE hook per target, so PROBE
// S does NOT re-hook D3D12CreateDevice — PROBE P owns it and calls us). ---
std::atomic<void*> g_device{nullptr};

// Bounded summary watcher (same sanctioned diagnostic-poll shape as PROBE P/K).
constexpr DWORD kSummaryMs = 3000;
std::atomic<bool> g_watcherStarted{false};

DWORD WINAPI SummaryMain(LPVOID) {
    LOG_INFO_KV(kCat, "watcher_started",
        KV::BareStr("detail",
            "KI-0028 draw-recording watcher armed. draws HIGH swap-OFF + ~ZERO "
            "swap-ON => the render loop records no scene/UI draws (wedge upstream "
            "in render-submission). draws ~EQUAL both + still black => draws "
            "recorded but composite black (RT/resource/state — read om_null_rt)."));
    for (int reads = 0; reads < 40; ++reads) {  // ~2 min
        Sleep(kSummaryMs);
        LOG_INFO_KV(kCat, "summary",
            KV("create_cmdlist",  g_createCmdList.load()),
            KV("draw_instanced",  g_drawInstanced.load()),
            KV("draw_indexed",    g_drawIndexed.load()),
            KV("om_set_rt",       g_omSetRT.load()),
            KV("om_null_rt",      g_omSetRT_nullRT.load()));
    }
    LOG_INFO_KV(kCat, "watcher_done",
        KV::BareStr("detail", "40 summary reads taken; stopping the draw watcher."));
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

}  // namespace

void DrawcallProbeStart() {
    bool expected = false;
    if (!g_armed.compare_exchange_strong(expected, true,
                                         std::memory_order_acq_rel)) {
        return;
    }
    MH_STATUS mi = MH_Initialize();
    if (mi != MH_OK && mi != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_ERROR_KV(kCat, "mh_init_failed", KV("mh_status", (uint64_t)mi));
        g_armed.store(false, std::memory_order_release);
        return;
    }
    // The device is NOT captured here — PROBE P owns the single d3d12!D3D12Create
    // Device hook and hands us the device via DrawcallProbeOnDeviceCaptured. Start
    // the watcher now so the summary runs regardless of when the device arrives.
    StartWatcherOnce();
    LOG_INFO_KV(kCat, "armed_waiting_for_device", KV::BareStr("detail",
        "PROBE S init complete; awaiting PROBE P's device-capture callback to "
        "patch CreateCommandList (one device hook shared, MinHook is 1-per-target)."));
}

void DrawcallProbeOnDeviceCaptured(void* device) {
    if (!device) return;
    void* expected = nullptr;
    if (!g_device.compare_exchange_strong(expected, device,
                                          std::memory_order_acq_rel)) {
        return;  // first device wins
    }
    LOG_INFO_KV(kCat, "device_captured", KV("device", device));
    bool ok = HookVtableSlot(device, kDevSlot_CreateCommandList,
                             reinterpret_cast<void*>(&HookedCreateCmdList),
                             reinterpret_cast<void**>(&g_origCreateCmdList),
                             "CreateCommandList");
    LOG_INFO_KV(kCat, "createcmdlist_hook_armed", KV("ok", ok ? 1 : 0),
        KV::BareStr("detail",
            "patched ID3D12Device::CreateCommandList (slot 12) on the device PROBE "
            "P handed over; the first DIRECT/BUNDLE list patches the shared "
            "command-list vtable for the Draw* + OMSetRenderTargets counters."));
}

}  // namespace kcdx::fs_takeover
