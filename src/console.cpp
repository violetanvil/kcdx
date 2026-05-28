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

#include "crash_guard.h"
#include "dev.h"
#include "log.h"
#include "plugin_loader.h"
#include "refdb.h"

namespace kcdx::console {

namespace {

// CryEngine console command signature. Same as kcdxConsoleCommandCallback,
// but typed against IConsoleCmdArgs* directly — that's the opaque pointer
// CryEngine passes to our trampolines.
using CryConsoleCommandFunc = void(__fastcall*)(void* /*IConsoleCmdArgs*/);

// IConsole vtable accessors (live RVAs from the Address Library,
// ids 2000–2003 verified against the binary).
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

// Deferred-registration queue (restructure-plan.md §"The deferred-registration
// pattern"). kcdx.command may be CALLED before Init() resolves IConsole — Lua
// plugin.lua runs (lua_plugin_loader::RunAll) BEFORE console::Init() in the
// same first-update-tick block (hooks.cpp). A RegisterCommand that arrives
// while g_ready==false is queued here (validated first), then flushed in FIFO
// order the instant Init() arms the surface. The author's call succeeds
// optimistically ("accepted-deferred"); the engine does the timing.
// Guarded by g_slotsMutex (the same lock the slot table uses).
struct PendingCommand {
    kcdxPluginHandle           owner;
    std::string                name;
    std::string                help;
    kcdxConsoleCommandCallback callback;
};
std::vector<PendingCommand> g_pendingCommands;

// Dispatcher: called by every trampoline. Looks up the slot and forwards
// the IConsoleCmdArgs* to the plugin callback.
void DispatchSlot(size_t slotIdx, void* iConsoleCmdArgs) {
    kcdxConsoleCommandCallback cb = nullptr;
    kcdxPluginHandle owner = kcdxInvalidPluginHandle;
    std::string slotName;
    {
        std::lock_guard<std::mutex> lock(g_slotsMutex);
        if (slotIdx >= kMaxCommands) return;
        Slot* s = &g_slots[slotIdx];
        if (!s->used || !s->callback) return;
        cb       = s->callback;
        owner    = s->owner;
        slotName = s->name;
    }
    KCDX_DEV("CONSOLE", "DISPATCH",
        kcdx::dev::KV("slot", static_cast<unsigned long long>(slotIdx)),
        kcdx::dev::KV("name", slotName.c_str()));

    // Resolve owner plugin name for the guard log line.
    const char* pluginName = nullptr;
    for (const auto& p : plugins::g_plugins) {
        if (p.handle == owner && !p.manifest.name.empty()) {
            pluginName = p.manifest.name.c_str();
            break;
        }
    }

    struct Ctx {
        kcdxConsoleCommandCallback cb;
        const kcdxConsoleCmdArgs*  args;
    };
    Ctx ctx{cb, reinterpret_cast<const kcdxConsoleCmdArgs*>(iConsoleCmdArgs)};
    guard::Call(
        "console.cmd",
        pluginName,
        [](void* ud) {
            Ctx* c = static_cast<Ctx*>(ud);
            c->cb(c->args);
        },
        &ctx);
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

// THE single registration code path: find a free slot, fill it, and call
// CryEngine's AddCommand. BOTH the immediate (g_ready==true) RegisterCommand
// path AND the deferred-queue flush in Init() call this — there is exactly
// ONE copy of the slot-find + AddCommand logic, never two.
//
// PRECONDITIONS (caller's responsibility — this helper asserts none of them):
//   * g_slotsMutex is HELD by the caller.
//   * g_ready is true / IConsole is armed (g_AddCommand non-null).
//   * name/cb already validated non-null; name already dup-checked.
// Returns false only on "no free slots" (the one failure this body owns).
bool RegisterCommandNow(kcdxPluginHandle owner,
                        const char* name,
                        const char* help,
                        kcdxConsoleCommandCallback cb) {
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

// True iff `name` is already taken by a live slot OR a queued pending command.
// Caller holds g_slotsMutex. The pending-queue arm closes the gap where two
// before-ready registrations of the same name would otherwise both queue.
bool NameTaken(const char* name, kcdxPluginHandle* outOwner) {
    for (size_t i = 0; i < kMaxCommands; ++i) {
        if (g_slots[i].used && g_slots[i].name == name) {
            if (outOwner) *outOwner = g_slots[i].owner;
            return true;
        }
    }
    for (const auto& pc : g_pendingCommands) {
        if (pc.name == name) {
            if (outOwner) *outOwner = pc.owner;
            return true;
        }
    }
    return false;
}

bool Thunk_RegisterCommand(kcdxPluginHandle owner,
                           const char* name,
                           const char* help,
                           kcdxConsoleCommandCallback cb) {
    // Cheap validations run FIRST, in BOTH the ready and not-ready paths —
    // a null/dup call is a genuinely bad call and must fail fast (and, when
    // not-ready, must NOT be queued).
    if (!name || !*name || !cb) {
        log::Warn("[console] RegisterCommand: null name or callback");
        return false;
    }

    std::lock_guard<std::mutex> lock(g_slotsMutex);

    // Uniqueness check (kcdx-side, before involving CryEngine). Looks in BOTH
    // the live slot table AND the pending queue, so a dup is refused whether
    // the prior registration already landed or is still queued.
    kcdxPluginHandle priorOwner = kcdxInvalidPluginHandle;
    if (NameTaken(name, &priorOwner)) {
        log::WarnF("[console] RegisterCommand('%s') refused: name already "
                   "registered by another plugin (handle %u)",
                   name, priorOwner);
        return false;
    }

    if (!g_ready.load(std::memory_order_acquire)) {
        // IConsole not up yet — accept-deferred. Queue the validated command;
        // Init() flushes it in FIFO order the instant the surface arms.
        // (restructure-plan.md §"The deferred-registration pattern".)
        g_pendingCommands.push_back(
            PendingCommand{owner, name, help ? help : "", cb});
        LOG_INFO_KV("CONSOLE", "register_deferred",
                    log::KV("name", name),
                    log::KV("owner", static_cast<unsigned long long>(owner)),
                    log::KV("queued", static_cast<unsigned long long>(
                                          g_pendingCommands.size())));
        return true;
    }

    // Surface is armed — register immediately through the shared path.
    return RegisterCommandNow(owner, name, help, cb);
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

// Drain g_pendingCommands through the ONE registration path (RegisterCommandNow)
// in FIFO order, then clear the queue. Caller holds g_slotsMutex; g_ready must
// already be true (the surface is armed). Called once, from Init(), right after
// g_ready flips.
void FlushPendingCommands() {
    if (g_pendingCommands.empty()) return;
    LOG_INFO_KV("CONSOLE", "flush_deferred",
                log::KV("count", static_cast<unsigned long long>(
                                     g_pendingCommands.size())));
    for (const auto& pc : g_pendingCommands) {
        // RegisterCommandNow may still refuse a single command (no free slots);
        // it logs its own loud reason. The rest of the queue still flushes.
        RegisterCommandNow(pc.owner, pc.name.c_str(),
                           pc.help.empty() ? "" : pc.help.c_str(),
                           pc.callback);
    }
    g_pendingCommands.clear();
}

// Init() failed terminally (Address Library ids didn't resolve — wrong game
// version / broken seed; NOT a transient timing issue, and Init is call-once
// per session, so these commands are genuinely undeliverable). Drop every
// queued command with a LOUD per-command ERROR that names the command, the
// owning plugin, and the reason — routed to the OWNING PLUGIN'S OWN log (via
// the by-handle plugin log stream) so the author sees it where they look
// (errors teach, in the author's terms; no silent
// orphan — fix the cause, never bury it). Caller holds g_slotsMutex; g_ready stays false.
void DropPendingWithError(const char* reason) {
    if (g_pendingCommands.empty()) return;
    for (const auto& pc : g_pendingCommands) {
        // Resolve owner handle -> plugin name for the message body.
        const char* pluginName = "<unknown/anonymous>";
        for (const auto& p : plugins::g_plugins) {
            if (p.handle == pc.owner && !p.manifest.name.empty()) {
                pluginName = p.manifest.name.c_str();
                break;
            }
        }
        // Per-plugin log (by handle) — the author's first stop. LOG_PLUGIN_*
        // also mirrors INFO/WARN/ERROR to the engine log, so users debugging
        // "why didn't my command register" see it there too.
        LOG_PLUGIN_ERROR_KV(pc.owner, "CONSOLE", "deferred_command_dropped",
                            log::KV("command", pc.name.c_str()),
                            log::KV("plugin", pluginName),
                            log::KV("reason", reason));
    }
    g_pendingCommands.clear();
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

    // Init() is call-once per session: hooks.cpp invokes it exactly once,
    // inside the first-update-tick `done` CAS latch, AFTER plugin.lua RunAll.
    // It is NOT tried-early-then-retried — so a failure here is TERMINAL, and
    // every queued command is genuinely undeliverable. At each failure return
    // we drain+drop the pending queue with a loud per-command error (routed to
    // the owning plugin's log) under g_slotsMutex; the queue never silently
    // orphans (no silent drop — fail loud).

    // Resolve gEnv->pConsole storage by canonical name.
    uintptr_t pConsole_storage = refdb::ResolveAddrByName("gEnv_pConsole");
    if (!pConsole_storage) {
        log::Warn("[console] Init: refdb name \"gEnv_pConsole\" did not "
                  "resolve — IConsole will be unavailable");
        std::lock_guard<std::mutex> lock(g_slotsMutex);
        DropPendingWithError("IConsole unavailable (refdb name "
                             "\"gEnv_pConsole\" did not resolve)");
        return false;
    }
    void* iconsole = *reinterpret_cast<void**>(pConsole_storage);
    if (!iconsole) {
        log::Warn("[console] Init: gEnv->pConsole is null — engine not "
                  "initialized yet?");
        std::lock_guard<std::mutex> lock(g_slotsMutex);
        DropPendingWithError("IConsole unavailable (gEnv->pConsole is null)");
        return false;
    }

    // Resolve AddCommand + RemoveCommand + ExecuteString by canonical name.
    // AddCommand maps to vtable[33] (the func-pointer overload); RVAs are
    // empirically probed against the binary.
    uintptr_t addCommandVA     = refdb::ResolveAddrByName("IConsole_AddCommand");
    uintptr_t removeCommandVA  = refdb::ResolveAddrByName("IConsole_RemoveCommand");
    uintptr_t executeStringVA  = refdb::ResolveAddrByName("IConsole_ExecuteString");
    if (!addCommandVA || !removeCommandVA || !executeStringVA) {
        log::Warn("[console] Init: refdb names IConsole_AddCommand / "
                  "IConsole_RemoveCommand / IConsole_ExecuteString did not "
                  "resolve");
        std::lock_guard<std::mutex> lock(g_slotsMutex);
        DropPendingWithError("IConsole unavailable (refdb names "
                             "IConsole_AddCommand / IConsole_RemoveCommand / "
                             "IConsole_ExecuteString did not resolve)");
        return false;
    }

    g_iconsole       = iconsole;
    g_AddCommand     = reinterpret_cast<AddCommandFn>(addCommandVA);
    g_RemoveCommand  = reinterpret_cast<RemoveCommandFn>(removeCommandVA);
    g_ExecuteString  = reinterpret_cast<ExecuteStringFn>(executeStringVA);

    {
        std::lock_guard<std::mutex> lock(g_slotsMutex);
        g_ready.store(true, std::memory_order_release);
        log::InfoF("[console] ready: IConsole=0x%p, AddCommand=0x%p, "
                   "RemoveCommand=0x%p, %zu slots available",
                   iconsole, reinterpret_cast<void*>(addCommandVA),
                   reinterpret_cast<void*>(removeCommandVA), kMaxCommands);
        // Drain the deferred-registration queue NOW that the surface is armed,
        // FIFO, through the single RegisterCommandNow path.
        FlushPendingCommands();
    }
    return true;
}

}  // namespace kcdx::console
