// === DIAGNOSTIC (PROBE K) — KI-0028 present-count delta (NO present hook) ===
// See present_probe.h for WHY + the outcome->meaning map. NO-RESIDUE on retire.

#include "present_probe.h"

#include <windows.h>
#include <dxgi1_2.h>  // IDXGIFactory2::CreateSwapChainForHwnd, IDXGISwapChain1

#include <atomic>

#include "MinHook.h"
#include "../log.h"

namespace kcdx::fs_takeover {

namespace {

using KV = ::kcdx::log::KV;
constexpr const char* kCat = "PRESENT_PROBE";

// IDXGISwapChain vtable slot indices (stable COM contract). FULL layout:
//   IUnknown 0-2 (QueryInterface/AddRef/Release)
//   IDXGIObject 3-6 (SetPrivateData/SetPrivateDataInterface/GetPrivateData/GetParent)
//   IDXGIDeviceSubObject 7 (GetDevice)
//   IDXGISwapChain: 8=Present, 9=GetBuffer, 10=SetFullscreenState,
//     11=GetFullscreenState, 12=GetDesc, 13=ResizeBuffers, 14=ResizeTarget,
//     15=GetContainingOutput, 16=GetFrameStatistics, 17=GetLastPresentCount
// We CALL these by index off the captured pointer — never patch slot 8 (no
// present hook).
//
// BUG FIX (KI-0028, PROBE K run 1): the prior constants were 18/13 — WRONG.
// Slot 13 is ResizeBuffers (calling it as GetLastPresentCount returned
// E_INVALIDARG=0x80070057 every read), slot 18 is past IDXGISwapChain (it
// happened to land on a IDXGISwapChain1 method, returning a stale value read as
// "frame stats"). The frozen 3840/2160 + the E_INVALIDARG were the slot bug, not
// ground truth. Corrected to 16/17 per the documented layout above.
constexpr int kSlot_GetFrameStatistics = 16;
constexpr int kSlot_GetLastPresentCount = 17;

// IDXGIFactory vtable: CreateSwapChain is slot 10. IDXGIFactory2 extends it;
// CreateSwapChainForHwnd is slot 15. We patch BOTH so either creation path is
// captured (modern CryEngine uses CreateSwapChainForHwnd; older uses CreateSwapChain).
constexpr int kSlot_CreateSwapChain = 10;
constexpr int kSlot_CreateSwapChainForHwnd = 15;

std::atomic<IDXGISwapChain*> g_swapchain{nullptr};
std::atomic<bool> g_armed{false};
std::atomic<bool> g_watcherStarted{false};

constexpr DWORD kPollMs = 1000;  // 1s read cadence (diagnostic — the boot_watch shape)

// Generic vtable call helpers (index off the COM object's first slot = vtable ptr).
void** VtableOf(void* obj) { return *reinterpret_cast<void***>(obj); }

HRESULT CallGetLastPresentCount(IDXGISwapChain* sc, UINT* out) {
    using Fn = HRESULT(STDMETHODCALLTYPE*)(void*, UINT*);
    auto fn = reinterpret_cast<Fn>(VtableOf(sc)[kSlot_GetLastPresentCount]);
    return fn(sc, out);
}
HRESULT CallGetFrameStatistics(IDXGISwapChain* sc, DXGI_FRAME_STATISTICS* out) {
    using Fn = HRESULT(STDMETHODCALLTYPE*)(void*, DXGI_FRAME_STATISTICS*);
    auto fn = reinterpret_cast<Fn>(VtableOf(sc)[kSlot_GetFrameStatistics]);
    return fn(sc, out);
}

// K2 — read the captured swapchain's present counters every kPollMs and log the
// per-interval delta. Event-light: it is a dedicated diagnostic thread, the one
// sanctioned diagnostic poll (boot_watch shape). No thread is suspended here.
DWORD WINAPI WatcherMain(LPVOID) {
    IDXGISwapChain* sc = g_swapchain.load(std::memory_order_acquire);
    if (!sc) return 0;

    UINT prevPresent = 0;
    UINT prevRefresh = 0;
    bool havePrev = false;
    int reads = 0;

    LOG_INFO_KV(kCat, "watcher_started",
        KV::BareStr("detail",
            "KI-0028 present-count watcher armed — reads GetLastPresentCount + "
            "GetFrameStatistics every 1s. present delta ~0 => loop never reaches "
            "present (wedge UPSTREAM, FALSIFIES present-failure); present>0 + "
            "refresh~0 => present called but no GPU scanout; both advance => "
            "frames presented (surface/compositor issue, not present)."));

    for (;;) {
        Sleep(kPollMs);
        sc = g_swapchain.load(std::memory_order_acquire);
        if (!sc) continue;

        UINT lastPresent = 0;
        HRESULT hrP = CallGetLastPresentCount(sc, &lastPresent);

        DXGI_FRAME_STATISTICS fs{};
        HRESULT hrF = CallGetFrameStatistics(sc, &fs);

        if (havePrev) {
            const long long dPresent =
                (long long)lastPresent - (long long)prevPresent;
            const long long dRefresh =
                (long long)fs.PresentRefreshCount - (long long)prevRefresh;
            // One line per interval — a state read, not a hot-path log.
            LOG_INFO_KV(kCat, "present_delta",
                KV("d_present",      dPresent),
                KV("d_refresh",      dRefresh),
                KV("last_present",   (uint64_t)lastPresent),
                KV("present_count",  (uint64_t)fs.PresentCount),
                KV("refresh_count",  (uint64_t)fs.PresentRefreshCount),
                KV("hr_present",     (uint64_t)(uint32_t)hrP),
                KV("hr_framestats",  (uint64_t)(uint32_t)hrF));
        }
        prevPresent = lastPresent;
        prevRefresh = fs.PresentRefreshCount;
        havePrev = true;

        if (++reads >= 120) {  // ~2 min of 1s reads, then stop (bounded diagnostic)
            LOG_INFO_KV(kCat, "watcher_done",
                KV::BareStr("detail", "120 reads taken; stopping the present watcher."));
            return 0;
        }
    }
}

void StartWatcherOnce() {
    bool expected = false;
    if (!g_watcherStarted.compare_exchange_strong(expected, true,
                                                  std::memory_order_acq_rel)) {
        return;
    }
    HANDLE h = CreateThread(nullptr, 0, WatcherMain, nullptr, 0, nullptr);
    if (h) {
        CloseHandle(h);
    } else {
        g_watcherStarted.store(false, std::memory_order_release);
        LOG_ERROR_KV(kCat, "watcher_start_failed",
            KV("win32_err", (uint64_t)GetLastError()));
    }
}

// Capture the swapchain the moment a creation call returns one, then start K2.
void CaptureSwapchain(IDXGISwapChain* sc, const char* via) {
    IDXGISwapChain* expected = nullptr;
    if (g_swapchain.compare_exchange_strong(expected, sc,
                                            std::memory_order_acq_rel)) {
        LOG_INFO_KV(kCat, "swapchain_captured",
            KV::BareStr("via", via),
            KV("swapchain", (void*)sc));
        StartWatcherOnce();
    }
}

// --- IDXGIFactory::CreateSwapChain detour (slot 10) ---
using CreateSwapChain_t = HRESULT(STDMETHODCALLTYPE*)(
    void* self, IUnknown* dev, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** out);
CreateSwapChain_t g_origCreateSwapChain = nullptr;

HRESULT STDMETHODCALLTYPE HookedCreateSwapChain(
    void* self, IUnknown* dev, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** out) {
    HRESULT hr = g_origCreateSwapChain(self, dev, desc, out);
    if (SUCCEEDED(hr) && out && *out) CaptureSwapchain(*out, "CreateSwapChain");
    return hr;
}

// --- IDXGIFactory2::CreateSwapChainForHwnd detour (slot 15) ---
using CreateSwapChainForHwnd_t = HRESULT(STDMETHODCALLTYPE*)(
    void* self, IUnknown* dev, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fsdesc, IDXGIOutput* restrict_out,
    IDXGISwapChain1** out);
CreateSwapChainForHwnd_t g_origCreateSwapChainForHwnd = nullptr;

HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForHwnd(
    void* self, IUnknown* dev, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fsdesc, IDXGIOutput* restrict_out,
    IDXGISwapChain1** out) {
    HRESULT hr = g_origCreateSwapChainForHwnd(
        self, dev, hwnd, desc, fsdesc, restrict_out, out);
    if (SUCCEEDED(hr) && out && *out)
        CaptureSwapchain(reinterpret_cast<IDXGISwapChain*>(*out),
                         "CreateSwapChainForHwnd");
    return hr;
}

// Patch one vtable slot on a live COM object via MinHook. Returns true on hook
// install. `origOut` receives the trampoline to the original.
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

// Create a throwaway DXGI factory to read its vtable, patch the two creation
// slots, then release it (the patch lives on the shared vtable, so it catches
// the engine's OWN factory's creation calls too — COM vtables are per-class).
void ArmFactoryHooks() {
    HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
    if (!dxgi) dxgi = LoadLibraryW(L"dxgi.dll");
    if (!dxgi) {
        LOG_ERROR_KV(kCat, "dxgi_not_loaded", KV::BareStr("detail",
            "dxgi.dll not present — cannot arm the present probe."));
        return;
    }
    using CreateFactory2_t = HRESULT(WINAPI*)(UINT, REFIID, void**);
    auto pCreateFactory2 = reinterpret_cast<CreateFactory2_t>(
        GetProcAddress(dxgi, "CreateDXGIFactory2"));
    using CreateFactory1_t = HRESULT(WINAPI*)(REFIID, void**);
    auto pCreateFactory1 = reinterpret_cast<CreateFactory1_t>(
        GetProcAddress(dxgi, "CreateDXGIFactory1"));

    IDXGIFactory2* factory = nullptr;
    HRESULT hr = E_FAIL;
    if (pCreateFactory2) {
        hr = pCreateFactory2(0, __uuidof(IDXGIFactory2),
                             reinterpret_cast<void**>(&factory));
    }
    if (FAILED(hr) && pCreateFactory1) {
        hr = pCreateFactory1(__uuidof(IDXGIFactory2),
                             reinterpret_cast<void**>(&factory));
    }
    if (FAILED(hr) || !factory) {
        LOG_ERROR_KV(kCat, "factory_create_failed",
            KV("hr", (uint64_t)(uint32_t)hr));
        return;
    }

    bool any = false;
    any |= HookVtableSlot(factory, kSlot_CreateSwapChain,
                          reinterpret_cast<void*>(&HookedCreateSwapChain),
                          reinterpret_cast<void**>(&g_origCreateSwapChain),
                          "CreateSwapChain");
    any |= HookVtableSlot(factory, kSlot_CreateSwapChainForHwnd,
                          reinterpret_cast<void*>(&HookedCreateSwapChainForHwnd),
                          reinterpret_cast<void**>(&g_origCreateSwapChainForHwnd),
                          "CreateSwapChainForHwnd");

    factory->Release();  // the patch persists on the shared class vtable

    LOG_INFO_KV(kCat, "factory_hooks_armed",
        KV("ok", any ? 1 : 0),
        KV::BareStr("detail",
            "patched IDXGIFactory CreateSwapChain (slot 10) + "
            "CreateSwapChainForHwnd (slot 15) on the shared class vtable; the "
            "engine's own factory creation will be captured. NO present (slot 8) "
            "hook."));
}

}  // namespace

void PresentProbeStart() {
    bool expected = false;
    if (!g_armed.compare_exchange_strong(expected, true,
                                         std::memory_order_acq_rel)) {
        return;  // already armed
    }
    // MinHook may already be initialized by the engine-hook install path.
    MH_STATUS mi = MH_Initialize();
    if (mi != MH_OK && mi != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_ERROR_KV(kCat, "mh_init_failed", KV("mh_status", (uint64_t)mi));
        g_armed.store(false, std::memory_order_release);
        return;
    }
    ArmFactoryHooks();
}

}  // namespace kcdx::fs_takeover
