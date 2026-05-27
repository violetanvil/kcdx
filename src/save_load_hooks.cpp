#include "save_load_hooks.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

#include "MinHook.h"
#include "dev.h"
#include "modification_inventory.h"
#include "log.h"
#include "messaging.h"
#include "patch_engine.h"
#include "pe_helpers.h"
#include "serialization.h"
#include "kcdx/Interfaces.h"

namespace kcdx::save_load_hooks {

namespace {

// Tier-2 40-byte AOB signatures from
// _research/phase6-save-load/SAVE-LOAD-CANDIDATES.md (Phase 6a) and
// _research/phase6b-recon/SAVE-SELECTION-HOOK.md (Phase 6b). All verified
// unique in WHGame.dll .text against KCD2 release_1_5_1164953_841.
//
// Arg ABIs come from _research/phase6-save-load/phase6_abi_walker.py (capstone-based
// body-wide stack-arg analysis). Earlier rounds derived arg lists from
// prologue-shape only — that's what produced the 3-arg-SaveGame bug
// that corrupted saves. Always use full-body analysis for new hook
// targets.

const char* SAVE_GAME_SIG =
    "4C 8B DC 49 89 5B 08 49 89 73 18 49 89 7B 20 55 41 54 41 55 41 56 41 57 "
    "48 8B EC 48 83 EC 50 40 8A 7D 58 48 8D 05 B2 A6";

const char* LOAD_GAME_WRAPPER_SIG =
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 41 8B D8 8B FA 48 8B F1 E8 "
    "C0 F1 FF FF 48 8D 8E C8 00 00 00 66 C7 86 C0 00";

const char* POST_LOAD_GAME_SIG =
    "48 89 5C 24 10 55 56 57 41 56 41 57 48 8D 6C 24 C9 48 81 EC 90 00 00 00 "
    "49 8B F8 8B DA 48 8B F1 E8 7F F1 35 FE 4C 8B B8";

const char* DELETE_SAVEGAME_SIG =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 40 "
    "48 63 EA 45 8B F8 8B D5 48 8B F1 E8 40 19 42 FF";

// Slot resolver @ 0x1819DDE78 (Phase 6b). 24-byte function:
//   movsxd rax, edx              ; sign-extend playline index
//   lea rdx, [rax + rax*8]       ; * 9
//   lea rcx, [rcx + rdx*8]       ; rcx += playline * 72 (struct stride)
//   mov edx, r8d                 ; pass slot to vector_get
//   add rcx, 8                   ; point at the vector
//   jmp 0x180703c0c              ; vector_get returns SaveGameRecord* in rax
//
// Fires inside LoadGame_wrapper's tail path on every load (multiple
// times per user-visible load, since the engine re-resolves the
// record at several stages). Returns the SaveGameRecord*; we read
// the filename basename from [record+0x80] (live-confirmed 2026-05-19
// with two distinct loads producing "exit.whs" and "save561.whs").
const char* SLOT_RESOLVER_SIG =
    "48 63 C2 48 8D 14 C0 48 8D 0C D1 41 8B D0 48 83 C1 08 E9 7D 5D D2 FE";

// Phase 6a function-pointer typedefs.
using save_game_t = char (__fastcall*)(
    void*       self,
    const char* filename,
    uint8_t     reason,
    uint8_t     flag_a,
    uint32_t    arg5,
    uint8_t     flag_b,
    const char* description);

using load_game_wrapper_t = char (__fastcall*)(
    void*    self,
    uint32_t playline,   // re-labeled from "arg2" after Phase 6b confirmed
    uint32_t slot);      // re-labeled from "reason" after Phase 6b confirmed

using post_load_game_t = char (__fastcall*)(
    void*    self,
    uint32_t arg2,
    void*    arg3);

using delete_savegame_t = char (__fastcall*)(
    void*    self,
    int32_t  slot,
    uint32_t flags);

// Phase 6b: slot resolver. Returns SaveGameRecord* in rax. ABI is
// (rcx = SaveGameMgr_sub_object, edx = playline_idx, r8d = slot_idx).
using slot_resolver_t = void* (__fastcall*)(
    void*   sub_object,
    int32_t playline_idx,
    int32_t slot_idx);

save_game_t         g_orig_save_game         = nullptr;
load_game_wrapper_t g_orig_load_game_wrapper = nullptr;
post_load_game_t    g_orig_post_load_game    = nullptr;
delete_savegame_t   g_orig_delete_savegame   = nullptr;
slot_resolver_t     g_orig_slot_resolver     = nullptr;

std::atomic<uint64_t> g_save_game_fires         {0};
std::atomic<uint64_t> g_load_game_wrapper_fires {0};
std::atomic<uint64_t> g_post_load_game_fires    {0};
std::atomic<uint64_t> g_delete_savegame_fires   {0};
std::atomic<uint64_t> g_slot_resolver_fires     {0};

// Dedup state for slot resolver: the engine calls the resolver several
// times per user-visible load (once during the load setup, again
// during commit). Track the last record we fired LoadGameSelected
// for and skip duplicates while the same record is being resolved.
// Cleared on PostLoadGame so the NEXT user load fires fresh.
std::atomic<void*> g_last_resolved_record {nullptr};

// -----------------------------------------------------------------
// Safe-read helpers (SEH-guarded so a bad pointer can't AV the game)
// -----------------------------------------------------------------

bool SafeReadByte(const void* src) {
    if (!src) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(src, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & PAGE_NOACCESS) return false;
    __try {
        volatile uint8_t b = *static_cast<const uint8_t*>(src);
        (void)b;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeReadPtr(const void* base, uintptr_t off, void*& out) {
    if (!base) return false;
    const uint8_t* p = static_cast<const uint8_t*>(base) + off;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & PAGE_NOACCESS) return false;
    __try {
        out = *reinterpret_cast<void* const*>(p);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Strip the "%USER%/saves/playline<N>/" prefix from a savegame path
// so plugin-facing messages all carry the same basename shape. The
// SaveGame hook receives full paths from the engine; the slot
// resolver returns basenames directly. Normalizing to basename for
// plugins (per design decision 2026-05-19).
const char* Basename(const char* path) {
    if (!path) return nullptr;
    const char* slash = nullptr;
    for (const char* p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') slash = p;
    }
    return slash ? slash + 1 : path;
}

// -----------------------------------------------------------------
// Hook bodies — Phase 6a
// -----------------------------------------------------------------

char __fastcall HookedSaveGame(void* self, const char* filename,
                               uint8_t reason, uint8_t flag_a,
                               uint32_t arg5, uint8_t flag_b,
                               const char* description) {
    uint64_t n = g_save_game_fires.fetch_add(1, std::memory_order_relaxed) + 1;

    const char* safePath = (filename && SafeReadByte(filename)) ? filename : nullptr;
    const char* base     = Basename(safePath);
    uint32_t baseLen     = base ? static_cast<uint32_t>(strlen(base)) + 1 : 0;

    LOG_INFO("SAVE_LOAD", "HookedSaveGame ENTER fire_n=%llu basename='%s'",
             (unsigned long long)n, base ? base : "<null>");

    KCDX_DEV("SAVE_LOAD", "FIRE",
        kcdx::dev::KV("name",        "SaveGame"),
        kcdx::dev::KV("fire_n",      static_cast<unsigned long long>(n)),
        kcdx::dev::KV("tid",         static_cast<unsigned long long>(GetCurrentThreadId())),
        kcdx::dev::KV("self",        self),
        kcdx::dev::KV("path",        safePath ? safePath : "<null>"),
        kcdx::dev::KV("basename",    base ? base : "<null>"),
        kcdx::dev::KV("reason",      static_cast<unsigned int>(reason)),
        kcdx::dev::KV("flag_a",      static_cast<unsigned int>(flag_a)),
        kcdx::dev::KV("arg5",        static_cast<unsigned int>(arg5)),
        kcdx::dev::KV("flag_b",      static_cast<unsigned int>(flag_b)),
        kcdx::dev::KV("description", description ? description : "<null>"));

    // Stash the full path for the serialization subsystem so its
    // kcdxMessage_SaveGame listener can write the cosave into the
    // matching directory. Must happen BEFORE FireEngineMessage so the
    // listener sees the latest value.
    if (safePath) {
        LOG_DEBUG("SAVE_LOAD", "  before SetLastSaveFullPath");
        kcdx::serialization::SetLastSaveFullPath(safePath);
        LOG_DEBUG("SAVE_LOAD", "  after  SetLastSaveFullPath");
    }

    LOG_DEBUG("SAVE_LOAD", "  before FireEngineMessage(SaveGame)");
    kcdx::messaging::FireEngineMessage(kcdxMessage_SaveGame, base, baseLen);
    LOG_DEBUG("SAVE_LOAD", "  after  FireEngineMessage(SaveGame)");

    LOG_DEBUG("SAVE_LOAD", "  before original SaveGame");
    char result = g_orig_save_game(self, filename, reason, flag_a,
                                   arg5, flag_b, description);
    LOG_INFO("SAVE_LOAD", "  after  original SaveGame (result=%d)", (int)result);

    LOG_INFO("SAVE_LOAD", "HookedSaveGame EXIT fire_n=%llu", (unsigned long long)n);
    return result;
}

char __fastcall HookedLoadGameWrapper(void* self, uint32_t playline,
                                      uint32_t slot) {
    uint64_t n = g_load_game_wrapper_fires.fetch_add(1, std::memory_order_relaxed) + 1;

    LOG_INFO("SAVE_LOAD",
        "HookedLoadGameWrapper ENTER fire_n=%llu playline=%u slot=%u",
        (unsigned long long)n, (unsigned)playline, (unsigned)slot);

    // Re-emit the engine-modification inventory at load-start. The 0xC8 load
    // crash (docs/known-issues/save-load crash 0xC8 raised from WHGame.md) was
    // invisible because nothing recorded what kcdx modifies on the load path;
    // this gives a build-to-build diffable fingerprint right before the load
    // that crashes. SUMMARY at Info (always-on), per-target DETAIL at Debug.
    // Also refreshes the cached summary the crash guard dumps from its SEH
    // handler, so a fault during this load reports the current hook set.
    kcdx::modification_inventory::LogInventory(kcdx::log::Level::Info);

    KCDX_DEV("SAVE_LOAD", "FIRE",
        kcdx::dev::KV("name",     "LoadGame_wrapper"),
        kcdx::dev::KV("fire_n",   static_cast<unsigned long long>(n)),
        kcdx::dev::KV("tid",      static_cast<unsigned long long>(GetCurrentThreadId())),
        kcdx::dev::KV("self",     self),
        kcdx::dev::KV("playline", static_cast<unsigned int>(playline)),
        kcdx::dev::KV("slot",     static_cast<unsigned int>(slot)));

    // Note: we do NOT reset g_last_resolved_record here. LoadGame_wrapper
    // fires twice per user-visible load (engine bootstraps the load
    // through this path before the actual deserialization pass), so
    // resetting on every wrapper-entry would re-fire LoadGameSelected
    // for the second pass. Reset happens in PostLoadGame instead —
    // one PostLoadGame per user-visible load is the right cadence.
    LOG_DEBUG("SAVE_LOAD", "  before FireEngineMessage(PreLoadGame)");
    kcdx::messaging::FireEngineMessage(kcdxMessage_PreLoadGame);
    LOG_DEBUG("SAVE_LOAD", "  after  FireEngineMessage(PreLoadGame)");

    // (The mid-hook JIT-buffer fingerprint scan that once ran here —
    // hook_engine::DumpMidHookFingerprints — was removed in the
    // apply-consolidation cut: it walked the now-deleted g_mid_hooks vector,
    // which had no populator since Phase 5. Live mid-hooks live in hook_chain,
    // which fingerprints them on its own path. Its return was discarded, so
    // removing the call drops only a Debug log line, not load control-flow.)

    LOG_DEBUG("SAVE_LOAD", "  before original LoadGame_wrapper");
    char result = g_orig_load_game_wrapper(self, playline, slot);
    LOG_INFO("SAVE_LOAD", "  after  original LoadGame_wrapper (result=%d)",
             (int)result);

    LOG_INFO("SAVE_LOAD", "HookedLoadGameWrapper EXIT fire_n=%llu",
             (unsigned long long)n);
    return result;
}

char __fastcall HookedPostLoadGame(void* self, uint32_t arg2, void* arg3) {
    uint64_t n = g_post_load_game_fires.fetch_add(1, std::memory_order_relaxed) + 1;

    LOG_INFO("SAVE_LOAD", "HookedPostLoadGame ENTER fire_n=%llu",
             (unsigned long long)n);

    KCDX_DEV("SAVE_LOAD", "FIRE",
        kcdx::dev::KV("name",   "PostLoadGame"),
        kcdx::dev::KV("fire_n", static_cast<unsigned long long>(n)),
        kcdx::dev::KV("tid",    static_cast<unsigned long long>(GetCurrentThreadId())),
        kcdx::dev::KV("self",   self),
        kcdx::dev::KV("arg2",   static_cast<unsigned int>(arg2)),
        kcdx::dev::KV("arg3",   arg3));

    // PostLoadGame is the "world is hydrated" signal. Clear dedup
    // state again here as belt-and-suspenders.
    g_last_resolved_record.store(nullptr, std::memory_order_release);

    LOG_DEBUG("SAVE_LOAD", "  before FireEngineMessage(PostLoadGame)");
    kcdx::messaging::FireEngineMessage(kcdxMessage_PostLoadGame);
    LOG_DEBUG("SAVE_LOAD", "  after  FireEngineMessage(PostLoadGame)");

    LOG_DEBUG("SAVE_LOAD", "  before original PostLoadGame");
    char result = g_orig_post_load_game(self, arg2, arg3);
    LOG_INFO("SAVE_LOAD", "  after  original PostLoadGame (result=%d)",
             (int)result);

    LOG_INFO("SAVE_LOAD", "HookedPostLoadGame EXIT fire_n=%llu",
             (unsigned long long)n);
    return result;
}

char __fastcall HookedDeleteSavegame(void* self, int32_t slot, uint32_t flags) {
    uint64_t n = g_delete_savegame_fires.fetch_add(1, std::memory_order_relaxed) + 1;

    KCDX_DEV("SAVE_LOAD", "FIRE",
        kcdx::dev::KV("name",   "DeleteSavegame"),
        kcdx::dev::KV("fire_n", static_cast<unsigned long long>(n)),
        kcdx::dev::KV("tid",    static_cast<unsigned long long>(GetCurrentThreadId())),
        kcdx::dev::KV("self",   self),
        kcdx::dev::KV("slot",   static_cast<long long>(slot)),
        kcdx::dev::KV("flags",  static_cast<unsigned int>(flags)));

    kcdx::messaging::FireEngineMessage(kcdxMessage_DeleteGame);

    return g_orig_delete_savegame(self, slot, flags);
}

// -----------------------------------------------------------------
// Phase 6b production hook — slot resolver
// -----------------------------------------------------------------
//
// Reads the SaveGameRecord pointer from the original's return value
// (rax), then dereferences [record+0x80] to get a const char* basename
// (live-confirmed 2026-05-19: "exit.whs", "save561.whs"). Dedups by
// record pointer so plugins see one kcdxMessage_LoadGameSelected per
// user-visible load even though the engine resolves the record
// multiple times per load.

void* __fastcall HookedSlotResolver(void* sub_object, int32_t playline_idx,
                                    int32_t slot_idx) {
    uint64_t n = g_slot_resolver_fires.fetch_add(1, std::memory_order_relaxed) + 1;

    LOG_INFO("SAVE_LOAD",
        "HookedSlotResolver ENTER fire_n=%llu playline=%d slot=%d",
        (unsigned long long)n, (int)playline_idx, (int)slot_idx);

    // Forward to original first — we need the SaveGameRecord pointer.
    LOG_DEBUG("SAVE_LOAD", "  before original SlotResolver");
    void* record = g_orig_slot_resolver(sub_object, playline_idx, slot_idx);
    LOG_DEBUG("SAVE_LOAD", "  after  original SlotResolver record=0x%p", record);

    // Dedup: if this is the same record we already fired for since
    // the most recent LoadGame_wrapper / PostLoadGame, skip.
    void* last = g_last_resolved_record.load(std::memory_order_acquire);
    bool isFirst = (record != nullptr) &&
                   (record != last) &&
                   g_last_resolved_record.compare_exchange_strong(
                       last, record, std::memory_order_acq_rel);

    const char* base = nullptr;
    if (record) {
        // [record + 0x80] is a const char* (single indirection) to the
        // savegame basename. Live-verified 2026-05-19 with two distinct
        // loads (record_off=0x80 deref → "exit.whs", "save561.whs").
        void* nameAddr = nullptr;
        if (SafeReadPtr(record, 0x80, nameAddr) && nameAddr &&
            SafeReadByte(nameAddr)) {
            base = static_cast<const char*>(nameAddr);
        }
    }

    KCDX_DEV("SAVE_LOAD", "FIRE",
        kcdx::dev::KV("name",     "SlotResolver"),
        kcdx::dev::KV("fire_n",   static_cast<unsigned long long>(n)),
        kcdx::dev::KV("tid",      static_cast<unsigned long long>(GetCurrentThreadId())),
        kcdx::dev::KV("playline", static_cast<long long>(playline_idx)),
        kcdx::dev::KV("slot",     static_cast<long long>(slot_idx)),
        kcdx::dev::KV("record",   record),
        kcdx::dev::KV("basename", base ? base : "<null>"),
        kcdx::dev::KV("dedup",    isFirst ? "first" : "skip"));

    // The original returned a real record but the [record+0x80] basename
    // deref failed (base stayed null). This is a corruption-risk path, not a
    // benign miss: with no basename we SUPPRESS kcdxMessage_LoadGameSelected
    // and do NOT stamp the pending-load playline (the if (isFirst && base)
    // block below is skipped), so the cosave subsystem falls back to a
    // stale/wrong playline for THIS load — the wrong cosave gets attached.
    // The only prior signal was the dev-gated KCDX_DEV FIRE line above
    // (invisible in production). Always-on Error: the [record+0x80] offset
    // was live-verified 2026-05-19, so a null deref off a non-null record
    // most likely means the offset MOVED on a game patch. Names the
    // consequence, not just the event (fail-state-logging.md). Off the
    // dedup-`first` path because EVERY resolve with a bad deref mis-serves.
    if (record != nullptr && base == nullptr) {
        LOG_ERROR("SAVE_LOAD",
            "SlotResolver: original returned record=0x%p but the "
            "[record+0x80] basename deref failed (base=null) — "
            "kcdxMessage_LoadGameSelected SUPPRESSED and the pending-load "
            "playline NOT stamped, so this load uses a stale/wrong cosave "
            "playline (the [record+0x80] offset likely moved on a game "
            "patch; was live-verified for release_1_5_1164953_841 on "
            "2026-05-19). fire_n=%llu playline=%d slot=%d",
            record, (unsigned long long)n, (int)playline_idx, (int)slot_idx);
    }

    if (isFirst && base) {
        // Stash the playline for the serialization layer's cosave-path
        // construction in OnPostLoadGame. This is what makes the
        // cosave subsystem playline-safe: the engine just told us
        // which playline the user is loading into, so we use that
        // exact value rather than guessing from session history.
        LOG_DEBUG("SAVE_LOAD",
            "  first resolve, base='%s', firing LoadGameSelected",
            base);
        kcdx::serialization::SetPendingLoadPlayline(playline_idx);

        uint32_t baseLen = static_cast<uint32_t>(strlen(base)) + 1;
        LOG_DEBUG("SAVE_LOAD", "  before FireEngineMessage(LoadGameSelected)");
        kcdx::messaging::FireEngineMessage(kcdxMessage_LoadGameSelected,
                                           base, baseLen);
        LOG_DEBUG("SAVE_LOAD", "  after  FireEngineMessage(LoadGameSelected)");
    }

    LOG_INFO("SAVE_LOAD", "HookedSlotResolver EXIT fire_n=%llu",
             (unsigned long long)n);
    return record;
}

// -----------------------------------------------------------------
// Install plumbing
// -----------------------------------------------------------------

uintptr_t FindUniqueSig(const pe::ModuleView& mod, const char* sig,
                        const char* label) {
    auto pat = kcdx::patch::ParsePattern(sig);
    auto sections = pe::ExecutableSections(mod);
    std::vector<uintptr_t> all;
    for (const auto& sec : sections) {
        auto offs = kcdx::patch::FindAllInBuffer(sec.data, sec.size, pat);
        for (size_t off : offs) {
            all.push_back(reinterpret_cast<uintptr_t>(sec.data + off));
        }
    }
    if (all.size() != 1) {
        log::WarnF("[phase6] %s sig: %zu matches (need exactly 1) — skipping hook",
                   label, all.size());
        return 0;
    }
    log::InfoF("[phase6] %s found at 0x%p", label,
               reinterpret_cast<void*>(all[0]));
    return all[0];
}

bool VerifyExecutable(void* p, const char* label) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) {
        log::WarnF("[phase6] %s VirtualQuery failed — skipping hook", label);
        return false;
    }
    if (mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
        log::WarnF("[phase6] %s not executable memory — skipping hook", label);
        return false;
    }
    return true;
}

bool InstallOne(uintptr_t target, void* detour, void** trampoline,
                const char* label) {
    if (!target) return false;
    if (!VerifyExecutable(reinterpret_cast<void*>(target), label)) return false;

    MH_STATUS st = MH_CreateHook(reinterpret_cast<LPVOID>(target),
                                 reinterpret_cast<LPVOID>(detour),
                                 trampoline);
    if (st != MH_OK) {
        log::WarnF("[phase6] MH_CreateHook(%s) failed: %d", label, (int)st);
        return false;
    }
    return true;
}

}  // namespace

bool Install() {
    pe::ModuleView whgame;
    if (!pe::OpenModule(L"WHGame.dll", whgame)) {
        log::Error("[phase6] WHGame.dll not loaded — refusing to install hooks");
        return false;
    }

    uintptr_t saveGame        = FindUniqueSig(whgame, SAVE_GAME_SIG,
                                              "SaveGame");
    uintptr_t loadGameWrapper = FindUniqueSig(whgame, LOAD_GAME_WRAPPER_SIG,
                                              "LoadGame(wrapper)");
    uintptr_t postLoadGame    = FindUniqueSig(whgame, POST_LOAD_GAME_SIG,
                                              "PostLoadGame");
    uintptr_t deleteSavegame  = FindUniqueSig(whgame, DELETE_SAVEGAME_SIG,
                                              "DeleteSavegame");
    uintptr_t slotResolver    = FindUniqueSig(whgame, SLOT_RESOLVER_SIG,
                                              "SlotResolver");

    MH_Initialize();

    // Register each lifecycle hook into the live modification inventory on
    // successful install (category "lifecycle"). The VA is the resolved
    // target the detour sits on.
    using kcdx::modification_inventory::RegisterModification;
    const auto kLifecycle = kcdx::modification_inventory::Category::Lifecycle;

    int installed = 0;
    if (InstallOne(saveGame,        (void*)&HookedSaveGame,
                   (void**)&g_orig_save_game,         "SaveGame"))           {
        RegisterModification(saveGame, kLifecycle, "SaveGame");        ++installed; }
    if (InstallOne(loadGameWrapper, (void*)&HookedLoadGameWrapper,
                   (void**)&g_orig_load_game_wrapper, "LoadGame(wrapper)"))  {
        RegisterModification(loadGameWrapper, kLifecycle, "LoadGame"); ++installed; }
    if (InstallOne(postLoadGame,    (void*)&HookedPostLoadGame,
                   (void**)&g_orig_post_load_game,    "PostLoadGame"))       {
        RegisterModification(postLoadGame, kLifecycle, "PostLoadGame"); ++installed; }
    if (InstallOne(deleteSavegame,  (void*)&HookedDeleteSavegame,
                   (void**)&g_orig_delete_savegame,   "DeleteSavegame"))     {
        RegisterModification(deleteSavegame, kLifecycle, "DeleteSavegame"); ++installed; }
    if (InstallOne(slotResolver,    (void*)&HookedSlotResolver,
                   (void**)&g_orig_slot_resolver,     "SlotResolver"))       {
        RegisterModification(slotResolver, kLifecycle, "SlotResolver"); ++installed; }

    if (installed > 0) {
        MH_STATUS st = MH_EnableHook(MH_ALL_HOOKS);
        if (st != MH_OK) {
            log::ErrorF("[phase6] MH_EnableHook failed: %d", (int)st);
            return false;
        }
    }

    log::InfoF("[phase6] save/load hooks installed: %d/5", installed);
    return installed > 0;
}

}  // namespace kcdx::save_load_hooks
