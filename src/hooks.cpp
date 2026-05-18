#include "hooks.h"

#include <windows.h>
#include <psapi.h>

#include <atomic>
#include <cstdint>
#include <vector>

#include "MinHook.h"
#include "log.h"
#include "lua_bind.h"
#include "messaging.h"
#include "patch_engine.h"
#include "pe_helpers.h"
#include "task.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lstate.h"
}

namespace kcdx::hooks {

namespace {

// Signatures derived from yobson1/kcd2lua (last AOB update 2025-10-03 against
// KCD2 1.3). These may need refresh if the game updates significantly.
const char* PCALL_SIG  = "48 89 5C 24 ? 57 48 83 EC 40 33 C0 41 8B F8";
const char* UPDATE_SIG = "48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70 ? 0F 29 78 ? 44 0F 29 40 ? 44 0F 29 48 ? 44 0F 29 50 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B F1";

std::atomic<lua_State*> g_L{nullptr};

using lua_pcall_t = int(__cdecl*)(lua_State*, int, int, int);
lua_pcall_t g_orig_lua_pcall = nullptr;

using update_t = void(__cdecl*)(long long*, uint32_t, DWORD);
update_t g_orig_update = nullptr;

int __cdecl HookedLuaPcall(lua_State* L, int nargs, int nresults, int errfunc) {
    g_L.store(L, std::memory_order_relaxed);
    return g_orig_lua_pcall(L, nargs, nresults, errfunc);
}

void __cdecl HookedUpdate(long long* p1, uint32_t p2, DWORD p3) {
    static std::atomic<bool> done{false};
    if (!done.load(std::memory_order_acquire)) {
        lua_State* L = g_L.load(std::memory_order_acquire);
        if (L) {
            bool expected = false;
            if (done.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
                log::Info("First update tick with live lua_State — registering KCDX + applying patches");
                kcdx::lua_bind::RegisterKcdxTable(L);
                kcdx::patch::ApplyAll();

                // Lifecycle: input subsystem is alive by the time the first
                // update tick fires (Lua VM is up). Closest analogue to
                // SKSE's kInputLoaded message.
                log::Info("Firing kcdxMessage_InputLoaded...");
                kcdx::messaging::FireEngineMessage(kcdxMessage_InputLoaded);
            }
        }
    }

    // Drain the task queue every tick. Plugins that called AddTask from
    // any thread get their tasks executed here, on the main thread.
    kcdx::task::DrainQueue();

    g_orig_update(p1, p2, p3);
}

uintptr_t FindUniqueSig(const pe::ModuleView& mod, const char* sig, const char* label) {
    auto pat = kcdx::patch::ParsePattern(sig);
    auto sections = pe::ExecutableSections(mod);
    std::vector<uintptr_t> all;
    for (const auto& sec : sections) {
        auto offs = kcdx::patch::FindAllInBuffer(sec.data, sec.size, pat);
        for (size_t off : offs) all.push_back(reinterpret_cast<uintptr_t>(sec.data + off));
    }
    if (all.size() != 1) {
        log::ErrorF("%s sig: %zu matches (need exactly 1)", label, all.size());
        return 0;
    }
    log::InfoF("%s found at 0x%p", label, reinterpret_cast<void*>(all[0]));
    return all[0];
}

bool VerifyExecutable(void* p, const char* label) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) {
        log::ErrorF("%s VirtualQuery failed", label);
        return false;
    }
    if (mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
        log::ErrorF("%s not executable memory", label);
        return false;
    }
    return true;
}

}  // namespace

lua_State* CurrentLuaState() {
    return g_L.load(std::memory_order_acquire);
}

bool Install() {
    pe::ModuleView whgame;
    if (!pe::OpenModule(L"WHGame.dll", whgame)) {
        log::Error("WHGame.dll not loaded — refusing to install hooks");
        return false;
    }
    log::InfoF("WHGame.dll base 0x%p size 0x%zx",
               reinterpret_cast<const void*>(whgame.baseBytes), whgame.size);

    uintptr_t pcallAddr  = FindUniqueSig(whgame, PCALL_SIG,  "lua_pcall");
    uintptr_t updateAddr = FindUniqueSig(whgame, UPDATE_SIG, "update");
    if (!pcallAddr || !updateAddr) {
        log::Error("aborting hook install — required sigs not unique");
        return false;
    }
    if (!VerifyExecutable(reinterpret_cast<void*>(pcallAddr), "lua_pcall") ||
        !VerifyExecutable(reinterpret_cast<void*>(updateAddr), "update")) {
        return false;
    }

    if (MH_Initialize() != MH_OK) {
        log::Error("MH_Initialize failed");
        return false;
    }

    if (MH_CreateHook(reinterpret_cast<LPVOID>(pcallAddr),
                      reinterpret_cast<LPVOID>(&HookedLuaPcall),
                      reinterpret_cast<LPVOID*>(&g_orig_lua_pcall)) != MH_OK) {
        log::Error("MH_CreateHook(lua_pcall) failed");
        return false;
    }
    if (MH_CreateHook(reinterpret_cast<LPVOID>(updateAddr),
                      reinterpret_cast<LPVOID>(&HookedUpdate),
                      reinterpret_cast<LPVOID*>(&g_orig_update)) != MH_OK) {
        log::Error("MH_CreateHook(update) failed");
        return false;
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        log::Error("MH_EnableHook failed");
        return false;
    }

    log::Info("Hooks installed: lua_pcall + update");
    return true;
}

}  // namespace kcdx::hooks
