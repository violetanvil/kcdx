#include "console.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "address_library.h"
#include "dev.h"
#include "log.h"

namespace kcdx::console {

namespace {

// CryEngine console command signature. Same as kcdxConsoleCommandCallback,
// but typed against IConsoleCmdArgs* directly — that's the opaque pointer
// CryEngine passes to our trampolines.
using CryConsoleCommandFunc = void(__fastcall*)(void* /*IConsoleCmdArgs*/);

// IConsole vtable accessors (live RVAs from _research/phase7-recon/
// address-library-seed.csv, ids 2000–2003 verified by the Phase 7 probe).
using AddCommandFn    = void(__fastcall*)(void* iconsole,
                                          const char* sCommand,
                                          CryConsoleCommandFunc func,
                                          int nFlags,
                                          const char* sHelp);
using RemoveCommandFn  = void(__fastcall*)(void* iconsole, const char* sName);
using ExecuteStringFn  = void(__fastcall*)(void* iconsole,
                                           const char* command,
                                           bool bSilentMode,
                                           bool bDeferExecution);

// Resolved IConsole state. Populated by Init() at the first update tick.
void*            g_iconsole       = nullptr;
AddCommandFn     g_AddCommand     = nullptr;
RemoveCommandFn  g_RemoveCommand  = nullptr;
ExecuteStringFn  g_ExecuteString  = nullptr;
std::atomic<bool> g_ready         {false};

// Per-slot registration record. We pre-allocate kMaxCommands slots and
// expose one templated thunk per slot, so CryEngine's
// (IConsoleCmdArgs*) callback can carry the slot identity via the
// function pointer itself (no closure needed).
constexpr size_t kMaxCommands = 256;

struct Slot {
    bool                       used = false;
    kcdxPluginHandle           owner = kcdxInvalidPluginHandle;
    std::string                name;   // command name (we own the storage so kcdx can validate
                                       // uniqueness; CryEngine's AddCommand will copy)
    std::string                help;
    kcdxConsoleCommandCallback callback = nullptr;
};

Slot      g_slots[kMaxCommands];
std::mutex g_slotsMutex;

// Dispatcher: called by every trampoline. Looks up the slot and forwards
// the IConsoleCmdArgs* to the plugin callback.
void DispatchSlot(size_t slotIdx, void* iConsoleCmdArgs) {
    Slot* s = nullptr;
    kcdxConsoleCommandCallback cb = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_slotsMutex);
        if (slotIdx >= kMaxCommands) return;
        s = &g_slots[slotIdx];
        if (!s->used || !s->callback) return;
        cb = s->callback;
    }
    KCDX_DEV("CONSOLE", "DISPATCH",
        kcdx::dev::KV("slot", static_cast<unsigned long long>(slotIdx)),
        kcdx::dev::KV("name", s->name.c_str()));

    cb(reinterpret_cast<const kcdxConsoleCmdArgs*>(iConsoleCmdArgs));
}

// Templated thunk: one C function per slot. CryEngine sees the thunk
// as a plain `void(IConsoleCmdArgs*)` callback; the thunk knows its
// own slot index from the template parameter.
template <size_t SlotIdx>
void __fastcall SlotThunk(void* args) {
    DispatchSlot(SlotIdx, args);
}

// Build a compile-time array of thunk function pointers, indexed by slot.
template <size_t... I>
constexpr auto MakeThunkArrayImpl(std::index_sequence<I...>) {
    return std::array<CryConsoleCommandFunc, kMaxCommands>{
        &SlotThunk<I>...
    };
}

const auto kSlotThunks =
    MakeThunkArrayImpl(std::make_index_sequence<kMaxCommands>{});

// IConsoleCmdArgs vtable accessors (live indices per
// console-command-abi.md). The recon agent documented:
//   slot 0: dtor
//   slot 1: int  GetArgCount() const
//   slot 2: const char* GetArg(int nIndex) const
//   slot 3: const char* GetCommandLine() const
//
// Calling these from C requires the standard MSVC member-function-via-
// vtable shape: cast the object to void***, deref vtable, call slot.
using CmdArgs_GetArgCount_t     = int(__fastcall*)(const void*);
using CmdArgs_GetArg_t          = const char*(__fastcall*)(const void*, int);
using CmdArgs_GetCommandLine_t  = const char*(__fastcall*)(const void*);

const void* const* CmdArgsVtable(const void* args) {
    return *reinterpret_cast<const void* const* const*>(args);
}

// -----------------------------------------------------------------
// kcdxConsoleInterface thunks
// -----------------------------------------------------------------

bool Thunk_RegisterCommand(kcdxPluginHandle owner,
                           const char* name,
                           const char* help,
                           kcdxConsoleCommandCallback cb) {
    if (!g_ready.load(std::memory_order_acquire)) {
        log::WarnF("[console] RegisterCommand('%s') refused: IConsole not ready",
                   name ? name : "<null>");
        return false;
    }
    if (!name || !*name || !cb) {
        log::Warn("[console] RegisterCommand: null name or callback");
        return false;
    }
    std::lock_guard<std::mutex> lock(g_slotsMutex);

    // Uniqueness check (kcdx-side, before involving CryEngine).
    for (size_t i = 0; i < kMaxCommands; ++i) {
        if (g_slots[i].used && g_slots[i].name == name) {
            log::WarnF("[console] RegisterCommand('%s') refused: name already "
                       "registered by another plugin (handle %u)",
                       name, g_slots[i].owner);
            return false;
        }
    }
    // Find an unused slot.
    size_t slotIdx = kMaxCommands;
    for (size_t i = 0; i < kMaxCommands; ++i) {
        if (!g_slots[i].used) { slotIdx = i; break; }
    }
    if (slotIdx == kMaxCommands) {
        log::WarnF("[console] RegisterCommand('%s') refused: no free slots "
                   "(max %zu commands across all plugins)",
                   name, kMaxCommands);
        return false;
    }
    g_slots[slotIdx].used     = true;
    g_slots[slotIdx].owner    = owner;
    g_slots[slotIdx].name     = name;
    g_slots[slotIdx].help     = help ? help : "";
    g_slots[slotIdx].callback = cb;

    // Register with CryEngine. We always pass VF_RESTRICTEDMODE
    // (0x00080000) — without this flag, CryEngine's in-game `~`
    // console silently refuses to dispatch the command (Scaleform
    // UI restricts non-devmode users to flagged commands only).
    // Live-discovered 2026-05-20 by typing `kcdx_test_cap13` in the
    // in-game console and observing the silent no-op.
    //
    // A future API revision could expose nFlags as a parameter for
    // plugins that want devmode-only commands, but the most-common
    // case is "I want my command to be callable from the in-game
    // console" — defaulting to RESTRICTEDMODE matches that.
    constexpr int kVF_RESTRICTEDMODE = 0x00080000;
    g_AddCommand(g_iconsole, g_slots[slotIdx].name.c_str(),
                 kSlotThunks[slotIdx], kVF_RESTRICTEDMODE,
                 g_slots[slotIdx].help.empty() ? nullptr
                                               : g_slots[slotIdx].help.c_str());

    log::InfoF("[console] registered '%s' (slot %zu) for handle %u",
               name, slotIdx, owner);
    return true;
}

int Thunk_GetArgCount(const kcdxConsoleCmdArgs* args) {
    if (!args) return 0;
    auto vt = CmdArgsVtable(args);
    auto fn = reinterpret_cast<CmdArgs_GetArgCount_t>(const_cast<void*>(vt[1]));
    return fn(args);
}

const char* Thunk_GetArg(const kcdxConsoleCmdArgs* args, int nIndex) {
    if (!args) return nullptr;
    auto vt = CmdArgsVtable(args);
    auto fn = reinterpret_cast<CmdArgs_GetArg_t>(const_cast<void*>(vt[2]));
    return fn(args, nIndex);
}

const char* Thunk_GetCommandLine(const kcdxConsoleCmdArgs* args) {
    if (!args) return nullptr;
    auto vt = CmdArgsVtable(args);
    auto fn = reinterpret_cast<CmdArgs_GetCommandLine_t>(const_cast<void*>(vt[3]));
    return fn(args);
}

bool Thunk_ExecuteString(const char* commandLine) {
    if (!g_ready.load(std::memory_order_acquire)) {
        log::WarnF("[console] ExecuteString refused: IConsole not ready "
                   "(call later than kcdxMessage_InputLoaded)");
        return false;
    }
    if (!commandLine || !*commandLine) return false;
    // Live-call CryEngine's IConsole::ExecuteString. bSilentMode=false so
    // any warnings (e.g. "Unknown command") echo to the in-game console.
    // bDeferExecution=false runs synchronously on this thread — required
    // for self-test patterns where the caller wants the callback to have
    // fired by the time ExecuteString returns.
    g_ExecuteString(g_iconsole, commandLine, false, false);
    return true;
}

kcdxConsoleInterface g_iface = {
    /*RegisterCommand=*/  Thunk_RegisterCommand,
    /*GetArgCount=*/      Thunk_GetArgCount,
    /*GetArg=*/           Thunk_GetArg,
    /*GetCommandLine=*/   Thunk_GetCommandLine,
    /*ExecuteString=*/    Thunk_ExecuteString,
};

}  // namespace

const kcdxConsoleInterface* GetInterface() {
    return &g_iface;
}

bool Init() {
    if (g_ready.load(std::memory_order_acquire)) return true;

    // Resolve gEnv->pConsole storage via Address Library id 1009.
    uintptr_t pConsole_storage = address_library::Resolve(1009);
    if (!pConsole_storage) {
        log::Warn("[console] Init: Address Library id 1009 (gEnv-pConsole-ptr) "
                  "did not resolve — IConsole will be unavailable");
        return false;
    }
    void* iconsole = *reinterpret_cast<void**>(pConsole_storage);
    if (!iconsole) {
        log::Warn("[console] Init: gEnv->pConsole is null — engine not "
                  "initialized yet?");
        return false;
    }

    // Resolve AddCommand + RemoveCommand + ExecuteString via Address
    // Library ids 2000 + 2001 + 2002 (the canonical-RVAs published by
    // the Phase 7 probe, with id 2000 corrected to vtable[33] after
    // the DISPATCH-INVESTIGATION).
    uintptr_t addCommandVA     = address_library::Resolve(2000);
    uintptr_t removeCommandVA  = address_library::Resolve(2001);
    uintptr_t executeStringVA  = address_library::Resolve(2002);
    if (!addCommandVA || !removeCommandVA || !executeStringVA) {
        log::Warn("[console] Init: Address Library ids 2000/2001/2002 "
                  "(AddCommand/RemoveCommand/ExecuteString) did not resolve");
        return false;
    }

    g_iconsole       = iconsole;
    g_AddCommand     = reinterpret_cast<AddCommandFn>(addCommandVA);
    g_RemoveCommand  = reinterpret_cast<RemoveCommandFn>(removeCommandVA);
    g_ExecuteString  = reinterpret_cast<ExecuteStringFn>(executeStringVA);

    g_ready.store(true, std::memory_order_release);
    log::InfoF("[console] ready: IConsole=0x%p, AddCommand=0x%p, "
               "RemoveCommand=0x%p, %zu slots available",
               iconsole, reinterpret_cast<void*>(addCommandVA),
               reinterpret_cast<void*>(removeCommandVA), kMaxCommands);
    return true;
}

}  // namespace kcdx::console
