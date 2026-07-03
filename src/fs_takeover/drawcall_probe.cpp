// === DIAGNOSTIC (PROBE S) — KI-0028 command-list DRAW recording ground truth ===
// See drawcall_probe.h for WHY + the outcome->meaning map. NO-RESIDUE on retire.

#include "drawcall_probe.h"

#include <windows.h>
#include <d3d12.h>
#include <intrin.h>  // _ReturnAddress (HOP-3 caller attribution)

#include <atomic>
#include <cstdint>

#include "MinHook.h"
#include "../log.h"
#include "../pe_helpers.h"     // kcdx::pe::OpenModule (WHGame base for caller RVAs)
#include "draw_caller_tally.h" // HOP-3 Draw* caller attribution

namespace kcdx::fs_takeover {

namespace {

using KV = ::kcdx::log::KV;
constexpr const char* kCat = "DRAW_PROBE";

// Canonical D3D12 COM vtable slot indices (0-based, Microsoft SDK d3d12.h order —
// fixed by the interface spec, cross-checked against the documented method order;
// NOT a KCD2-specific vtable, so AP3 does not apply).
constexpr int kDevSlot_CreateCommandList = 12;     // ID3D12Device
// === DIAGNOSTIC (PROBE Z8 — KI-0028) — ID3D12Device resource-CREATION slots.
// SDK d3d12.h ID3D12DeviceVtbl 0-based order (same header + counting that fixes
// CreateCommandList=12 above — verified against the 10.0.26100.0 header's vtbl
// struct: ...25 GetResourceAllocationInfo, 26 GetCustomHeapProperties,
// 27 CreateCommittedResource, 28 CreateHeap, 29 CreatePlacedResource). NOT a
// KCD2 vtable — AP3 does not apply. The Z8 question (Reframe 14): the renderer
// runs (pass entered) but draw_indexed=0 — are world-GEOMETRY index buffers ever
// CREATED swap-ON? A geometry buffer is a BUFFER resource (Dimension==BUFFER) on
// the DEFAULT heap with no RT/DS/UAV flags. Counting them splits branch (a) "IB
// never made" from "IB made but never bound" (the existing ia_set_ib==0). ===
constexpr int kDevSlot_CreateCommittedResource = 27;
constexpr int kDevSlot_CreatePlacedResource    = 29;
constexpr int kCmdSlot_DrawInstanced        = 12;  // ID3D12GraphicsCommandList
constexpr int kCmdSlot_DrawIndexedInstanced = 13;
// SOURCE: d3d12.h ID3D12GraphicsCommandListVtbl member order, re-verified 0-based
// (the prior 47 was ClearDepthStencilView — its by-value DSV handle was deref'd as
// an RT-handle array → the AV at HookedOMSetRT+0x11 in dump kcdx_2026-06-22_16-35-19).
constexpr int kCmdSlot_OMSetRenderTargets   = 46;
// === DIAGNOSTIC (PROBE X) — IA-setup slots (SDK header-verified 0-based, same
// d3d12.h source as the slots above; the count cross-checks DrawInstanced=12 +
// OMSetRenderTargets=46). The discriminator: is ANY index buffer ever BOUND
// swap-ON? ia_set_ib==0 (vs >0 swap-OFF) => the indexed-geometry path is
// abandoned UPSTREAM of command-list recording (never bound); ia_set_ib>0 with
// draw_indexed==0 => bound but the draw is skipped (falsifies "geometry never
// created"). ===
constexpr int kCmdSlot_IASetPrimitiveTopology = 20;
constexpr int kCmdSlot_IASetIndexBuffer       = 43;
constexpr int kCmdSlot_IASetVertexBuffers     = 44;

std::atomic<bool> g_armed{false};
std::atomic<bool> g_cmdVtablePatched{false};  // patch the shared cmd-list vtable once

// Counters (relaxed — diagnostic tallies).
std::atomic<uint64_t> g_createCmdList{0};
std::atomic<uint64_t> g_drawInstanced{0};
std::atomic<uint64_t> g_drawIndexed{0};
std::atomic<uint64_t> g_omSetRT{0};
std::atomic<uint64_t> g_omSetRT_nullRT{0};   // OMSetRenderTargets with 0 RTs / null handle
// === PROBE X counters — IA index/vertex/topology binds ===
std::atomic<uint64_t> g_iaSetIB{0};   // IASetIndexBuffer calls (index buffer BOUND)
std::atomic<uint64_t> g_iaSetVB{0};   // IASetVertexBuffers calls
std::atomic<uint64_t> g_iaSetTopo{0}; // IASetPrimitiveTopology calls
// First-IB-view one-shot capture (the IB actually bound: GPU VA / format / size).
std::atomic<bool>     g_firstIBCaptured{false};
std::atomic<uint64_t> g_firstIB_va{0};
std::atomic<uint64_t> g_firstIB_size{0};
std::atomic<uint64_t> g_firstIB_fmt{0};
// === PROBE Z8 counters — resource CREATION (does the engine ever MAKE geometry
// buffers swap-ON?). buf_created = BUFFER-dimension resources; geo_buf = the
// geometry-class subset (DEFAULT heap, no RT/DS/UAV flag — the shape an index/
// vertex buffer takes); tex_created = non-buffer (textures/RTs). ===
std::atomic<uint64_t> g_bufCreated{0};
std::atomic<uint64_t> g_geoBufCreated{0};
std::atomic<uint64_t> g_texCreated{0};
std::atomic<uint64_t> g_geoBufBytes{0};   // total bytes of geometry-class buffers

// === HOP 3 (KI-0028) — Draw* caller-attribution tables (draw_caller_tally.h:
// _ReturnAddress names the engine path issuing the swap-ON non-indexed draws). ===
DrawCallerTable g_diCallers;  // DrawInstanced callers
DrawCallerTable g_dxCallers;  // DrawIndexedInstanced callers

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
    // HOP 3: name the path issuing non-indexed draws (first-seen resolves module).
    TallyDrawCaller(g_diCallers, "instanced", _ReturnAddress());
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
    // HOP 3: the indexed-draw caller set (the swap-OFF comparison arm).
    TallyDrawCaller(g_dxCallers, "indexed", _ReturnAddress());
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

// --- PROBE X: ID3D12GraphicsCommandList::IASetIndexBuffer (slot 43) ---
// The decisive discriminator. Pass-through + count; capture the first bound IB
// view once (no per-call logging/alloc — hot path). A null pView is a valid call
// (unbind) — counted, but it carries no view to capture.
using IASetIB_t = void(STDMETHODCALLTYPE*)(void* self,
                                           const D3D12_INDEX_BUFFER_VIEW* pView);
IASetIB_t g_origIASetIB = nullptr;
void STDMETHODCALLTYPE HookedIASetIB(void* self,
                                     const D3D12_INDEX_BUFFER_VIEW* pView) {
    g_iaSetIB.fetch_add(1);
    if (pView) {
        bool expected = false;
        if (g_firstIBCaptured.compare_exchange_strong(expected, true,
                                                       std::memory_order_acq_rel)) {
            g_firstIB_va.store(pView->BufferLocation, std::memory_order_relaxed);
            g_firstIB_size.store(pView->SizeInBytes, std::memory_order_relaxed);
            g_firstIB_fmt.store(static_cast<uint64_t>(pView->Format),
                                std::memory_order_relaxed);
        }
    }
    g_origIASetIB(self, pView);
}

// --- PROBE X: ID3D12GraphicsCommandList::IASetVertexBuffers (slot 44) ---
using IASetVB_t = void(STDMETHODCALLTYPE*)(
    void* self, UINT startSlot, UINT numViews,
    const D3D12_VERTEX_BUFFER_VIEW* pViews);
IASetVB_t g_origIASetVB = nullptr;
void STDMETHODCALLTYPE HookedIASetVB(void* self, UINT startSlot, UINT numViews,
                                     const D3D12_VERTEX_BUFFER_VIEW* pViews) {
    g_iaSetVB.fetch_add(1);
    g_origIASetVB(self, startSlot, numViews, pViews);
}

// --- PROBE X: ID3D12GraphicsCommandList::IASetPrimitiveTopology (slot 20) ---
using IASetTopo_t = void(STDMETHODCALLTYPE*)(void* self,
                                             D3D12_PRIMITIVE_TOPOLOGY topo);
IASetTopo_t g_origIASetTopo = nullptr;
void STDMETHODCALLTYPE HookedIASetTopo(void* self, D3D12_PRIMITIVE_TOPOLOGY topo) {
    g_iaSetTopo.fetch_add(1);
    g_origIASetTopo(self, topo);
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
    // === PROBE X — the IA-bind discriminator (is any index buffer ever bound?) ===
    any |= HookVtableSlot(cmdList, kCmdSlot_IASetIndexBuffer,
                          reinterpret_cast<void*>(&HookedIASetIB),
                          reinterpret_cast<void**>(&g_origIASetIB),
                          "IASetIndexBuffer");
    any |= HookVtableSlot(cmdList, kCmdSlot_IASetVertexBuffers,
                          reinterpret_cast<void*>(&HookedIASetVB),
                          reinterpret_cast<void**>(&g_origIASetVB),
                          "IASetVertexBuffers");
    any |= HookVtableSlot(cmdList, kCmdSlot_IASetPrimitiveTopology,
                          reinterpret_cast<void*>(&HookedIASetTopo),
                          reinterpret_cast<void**>(&g_origIASetTopo),
                          "IASetPrimitiveTopology");
    // === END PROBE X arm ===
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

// --- PROBE Z8: classify + tally a resource-creation call. Shared by both
// CreateCommittedResource and CreatePlacedResource (the desc + heap-props shape is
// what we read; the heap-management difference between them does not matter here).
// heapProps is null for CreatePlacedResource (the heap is a separate arg) — treat
// null as "not identifiably a readback/upload heap", i.e. still geometry-eligible.
void Z8ClassifyResource(const D3D12_HEAP_PROPERTIES* heapProps,
                        const D3D12_RESOURCE_DESC* desc) {
    if (!desc) return;
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
        g_bufCreated.fetch_add(1);
        // Geometry-class buffer: a DEFAULT-heap buffer with NO render-target,
        // depth-stencil, or unordered-access flag (index/vertex buffers are plain
        // DEFAULT buffers; RT/DS/UAV flags mark compute/render scratch, not mesh
        // geometry). heapProps null (placed) → still eligible (the flags decide).
        const bool defaultHeap =
            (heapProps == nullptr) ||
            (heapProps->Type == D3D12_HEAP_TYPE_DEFAULT);
        const bool noSpecialFlag =
            (desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) == 0;
        if (defaultHeap && noSpecialFlag) {
            g_geoBufCreated.fetch_add(1);
            g_geoBufBytes.fetch_add(static_cast<uint64_t>(desc->Width));
        }
    } else {
        g_texCreated.fetch_add(1);  // TEXTURE1D/2D/3D
    }
}

// --- ID3D12Device::CreateCommittedResource (slot 27) ---
using CreateCommitted_t = HRESULT(STDMETHODCALLTYPE*)(
    void* self, const D3D12_HEAP_PROPERTIES* heapProps, D3D12_HEAP_FLAGS heapFlags,
    const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES initState,
    const D3D12_CLEAR_VALUE* clear, REFIID riid, void** ppResource);
CreateCommitted_t g_origCreateCommitted = nullptr;
HRESULT STDMETHODCALLTYPE HookedCreateCommitted(
    void* self, const D3D12_HEAP_PROPERTIES* heapProps, D3D12_HEAP_FLAGS heapFlags,
    const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES initState,
    const D3D12_CLEAR_VALUE* clear, REFIID riid, void** ppResource) {
    Z8ClassifyResource(heapProps, desc);
    return g_origCreateCommitted(self, heapProps, heapFlags, desc, initState, clear,
                                 riid, ppResource);
}

// --- ID3D12Device::CreatePlacedResource (slot 29) — heap is a separate arg; no
// heap-props pointer, so classification reads the desc alone (null heapProps). ---
using CreatePlaced_t = HRESULT(STDMETHODCALLTYPE*)(
    void* self, void* heap, UINT64 heapOffset, const D3D12_RESOURCE_DESC* desc,
    D3D12_RESOURCE_STATES initState, const D3D12_CLEAR_VALUE* clear, REFIID riid,
    void** ppResource);
CreatePlaced_t g_origCreatePlaced = nullptr;
HRESULT STDMETHODCALLTYPE HookedCreatePlaced(
    void* self, void* heap, UINT64 heapOffset, const D3D12_RESOURCE_DESC* desc,
    D3D12_RESOURCE_STATES initState, const D3D12_CLEAR_VALUE* clear, REFIID riid,
    void** ppResource) {
    Z8ClassifyResource(nullptr, desc);
    return g_origCreatePlaced(self, heap, heapOffset, desc, initState, clear, riid,
                              ppResource);
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
            KV("om_null_rt",      g_omSetRT_nullRT.load()),
            // === PROBE X — the IA-bind discriminator. ia_set_ib==0 (vs >0
            // swap-OFF) => indexed path abandoned upstream of recording (never
            // bound); ia_set_ib>0 + draw_indexed==0 => bound but draw skipped;
            // first_ib_va==0 with ib>0 => bound to a null/invalid IB. ===
            KV("ia_set_ib",       g_iaSetIB.load()),
            KV("ia_set_vb",       g_iaSetVB.load()),
            KV("ia_set_topo",     g_iaSetTopo.load()),
            KV("first_ib_va",     g_firstIB_va.load()),
            KV("first_ib_size",   g_firstIB_size.load()),
            KV("first_ib_fmt",    g_firstIB_fmt.load()),
            // === PROBE Z8 — resource creation. geo_buf==0 => geometry buffers
            // NEVER created (branch a); geo_buf>0 + ia_set_ib==0 => created but
            // never bound (drop between create and bind). ===
            KV("buf_created",     g_bufCreated.load()),
            KV("geo_buf",         g_geoBufCreated.load()),
            KV("geo_buf_bytes",   g_geoBufBytes.load()),
            KV("tex_created",     g_texCreated.load()));
        // HOP 3: periodic caller-table dump so an early quit still captures the
        // per-caller draw attribution (the user never waits on the watcher clock).
        if (reads % 10 == 9) {
            DumpDrawCallers("instanced", g_diCallers);
            DumpDrawCallers("indexed",   g_dxCallers);
        }
    }
    // HOP 3: the per-caller draw attribution — the offline arm diff compares these
    // sets (which engine RVA issues the 21k swap-ON non-indexed draws).
    DumpDrawCallers("instanced", g_diCallers);
    DumpDrawCallers("indexed",   g_dxCallers);
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

uint64_t DrawcallProbeIndexedCount() {
    // PROBE Y reads the LIVE draw counter (incremented in HookedDrawIndexed),
    // NOT the SummaryMain watcher's cached value — the watcher self-terminates
    // after 40 reads (~2min) but this atomic advances for the whole process
    // life, so a stall reached AFTER the watcher exits is still observed.
    // Relaxed: the trigger tolerates one-sample lag (a real stall holds
    // draw_indexed==0 for many frames), no happens-before edge (concurrency.md,
    // same rationale as boot_watch g_lastMs).
    return g_drawIndexed.load(std::memory_order_relaxed);
}

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
    // HOP 3: WHGame base for caller-RVA attribution (unresolved => raw addresses).
    kcdx::pe::ModuleView whgame{};
    if (kcdx::pe::OpenModule(L"WHGame.dll", whgame) && whgame.base) {
        DrawCallerTallySetBase(reinterpret_cast<uintptr_t>(whgame.base));
    } else {
        LOG_WARN_KV(kCat, "whgame_base_unresolved", KV::BareStr("detail",
            "caller RVAs will log as raw addresses (WHGame.dll not resolvable yet)"));
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
    // === PROBE Z8 — patch the two resource-CREATION slots on the same device
    // (cold path, patched once here — NOT a hot-function hook). Answers: are
    // world-geometry index/vertex buffers ever CREATED swap-ON? ===
    bool okCommit = HookVtableSlot(device, kDevSlot_CreateCommittedResource,
                                   reinterpret_cast<void*>(&HookedCreateCommitted),
                                   reinterpret_cast<void**>(&g_origCreateCommitted),
                                   "CreateCommittedResource");
    bool okPlaced = HookVtableSlot(device, kDevSlot_CreatePlacedResource,
                                   reinterpret_cast<void*>(&HookedCreatePlaced),
                                   reinterpret_cast<void**>(&g_origCreatePlaced),
                                   "CreatePlacedResource");
    LOG_INFO_KV(kCat, "z8_resource_hooks_armed",
        KV("committed_ok", okCommit ? 1 : 0), KV("placed_ok", okPlaced ? 1 : 0),
        KV::BareStr("detail",
            "PROBE Z8: patched ID3D12Device::CreateCommittedResource (27) + "
            "CreatePlacedResource (29). geo_buf counts DEFAULT-heap buffers with no "
            "RT/DS/UAV flag (the index/vertex-buffer shape). geo_buf==0 swap-ON => "
            "world geometry buffers are NEVER CREATED (branch a); geo_buf>0 with "
            "ia_set_ib==0 => created but never bound (drop is between create + "
            "bind)."));
}

}  // namespace kcdx::fs_takeover
