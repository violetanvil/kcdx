#include "serialization.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "dev.h"
#include "log.h"
#include "messaging.h"
#include "plugin_loader.h"

namespace kcdx::serialization {

namespace {

// -----------------------------------------------------------------------------
// Cosave file format constants
// -----------------------------------------------------------------------------
//
// File layout:
//   [Header]
//     'K','C','D','X'      magic (4 bytes)
//     uint32 format_version (currently 1)
//     uint32 plugin_count
//   For each plugin:
//     uint32 uid
//     uint32 chunk_count
//     uint32 section_bytes        (the length of the chunks-block below)
//     For each chunk:
//       uint32 tag
//       uint32 version
//       uint32 chunk_bytes
//       <chunk_bytes> raw data
constexpr uint32_t kCosaveMagic        = 0x58444358;  // 'X','C','D','K' little-endian = "KCDX"
constexpr uint32_t kCosaveFormatVersion = 1;

// -----------------------------------------------------------------------------
// In-memory representation of cosave content
// -----------------------------------------------------------------------------

struct Chunk {
    uint32_t tag = 0;
    uint32_t version = 0;
    std::vector<uint8_t> data;
};

// Per-plugin state — registered callbacks plus working buffers used during
// save (write side) and load (read side).
struct PluginState {
    kcdxPluginHandle handle = kcdxInvalidPluginHandle;

    // Identity in the cosave: 0 means "plugin hasn't called SetUniqueID
    // and so produces nothing on save and consumes nothing on load."
    uint32_t uid = 0;

    // Registered callbacks. Any may be null.
    kcdxSerializationSaveCallback   saveCb   = nullptr;
    kcdxSerializationLoadCallback   loadCb   = nullptr;
    kcdxSerializationRevertCallback revertCb = nullptr;

    // -- Write-side state -----------------------------------------------
    //
    // Active when the engine is mid-SaveCallback for this plugin. The
    // plugin's OpenRecord / WriteRecordData calls mutate `pendingChunks`
    // (and `currentChunk` is a pointer into the back of that vector).
    std::vector<Chunk> pendingChunks;
    Chunk* currentChunk = nullptr;  // points into pendingChunks.back() while open

    // -- Read-side state ------------------------------------------------
    //
    // Active when the engine is mid-LoadCallback for this plugin. The
    // plugin's GetNextRecordInfo / ReadRecordData calls walk this vector.
    std::vector<Chunk> loadedChunks;
    size_t loadCursor = 0;     // index into loadedChunks of the chunk being read
    size_t loadByteOffset = 0; // bytes consumed of the currently-info'd chunk
    bool   loadHasPending = false;  // true after GetNextRecordInfo returned ok,
                                    //   false after ReadRecordData consumed it
};

std::vector<std::unique_ptr<PluginState>> g_plugins;
std::mutex g_pluginsMutex;

// Currently-active write/read targets. Set by the engine before calling
// into a plugin's callback; the API thunks consult these. Since all
// dispatch happens on the main thread synchronously, plain pointers
// are sufficient.
PluginState* g_currentWriter = nullptr;
PluginState* g_currentReader = nullptr;

// Most-recent SaveGame full path. Captured from kcdxMessage_SaveGame
// listener so we know which directory to write the .kcdx file into.
std::string g_lastSaveFullPath;
std::mutex  g_lastSaveFullPathMutex;

// Most-recent load filename basename. Captured from
// kcdxMessage_LoadGameSelected so we know which .kcdx to read at
// PostLoadGame time. (LoadGameSelected fires earlier than
// PostLoadGame; we want to RUN load callbacks at PostLoadGame timing
// so the game world is hydrated when plugins call back into it.)
std::string g_pendingLoadBasename;
std::mutex  g_pendingLoadBasenameMutex;

// Most-recent load's playline index. Captured from the SlotResolver
// hook (which receives playline_idx as its second arg). Used by
// OnPostLoadGame to construct the cosave path with the correct
// `playline<N>` directory. -1 = unknown (cold load with no prior
// SlotResolver fire; falls back to playline0).
std::atomic<int32_t> g_pendingLoadPlayline {-1};

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

PluginState* FindOrCreatePluginState(kcdxPluginHandle handle) {
    if (handle == kcdxInvalidPluginHandle) return nullptr;
    std::lock_guard<std::mutex> lock(g_pluginsMutex);
    for (auto& p : g_plugins) {
        if (p->handle == handle) return p.get();
    }
    auto p = std::make_unique<PluginState>();
    p->handle = handle;
    PluginState* raw = p.get();
    g_plugins.push_back(std::move(p));
    return raw;
}

PluginState* FindPluginByHandle(kcdxPluginHandle handle) {
    std::lock_guard<std::mutex> lock(g_pluginsMutex);
    for (auto& p : g_plugins) {
        if (p->handle == handle) return p.get();
    }
    return nullptr;
}

// Resolve the directory containing the .whs from the most-recent
// SaveGame full path. The engine gives us paths like
// "%USER%/saves/playline0/save561.whs"; we strip the basename to get
// "%USER%/saves/playline0/".
std::string ExtractDirFromPath(const std::string& fullPath) {
    size_t lastSlash = fullPath.find_last_of("/\\");
    if (lastSlash == std::string::npos) return {};
    return fullPath.substr(0, lastSlash + 1);
}

// Swap a .whs basename for a .kcdx basename. If the input doesn't end
// in .whs, append .kcdx (so we can still find a sidecar for unusual
// save names).
std::string MakeCosaveBasename(const char* whsBasename) {
    if (!whsBasename) return {};
    std::string s(whsBasename);
    const std::string suffix = ".whs";
    if (s.size() >= suffix.size() &&
        s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0) {
        s.erase(s.size() - suffix.size());
    }
    s += ".kcdx";
    return s;
}

// UTF-8 -> wide string helper.
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

// Resolve the saves directory for a given playline. Returns
// "%USERPROFILE%\Saved Games\kingdomcome2\saves\playline<N>\" as a
// wide path. KCD2 stores saves under this convention (CryEngine
// sys_user_folder default). If the user has remapped sys_user_folder,
// this fallback path is wrong — but the SAVE side uses the engine's
// raw path verbatim (no reconstruction needed), so only LOAD-side
// lookups for cold loads are affected.
std::wstring SavesDirForPlayline(int32_t playline) {
    if (playline < 0) playline = 0;
    wchar_t userProfile[MAX_PATH] = {};
    if (SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr,
                         SHGFP_TYPE_CURRENT, userProfile) != S_OK) {
        return {};
    }
    std::wstring w = userProfile;
    wchar_t suffix[64] = {};
    swprintf_s(suffix, L"\\Saved Games\\kingdomcome2\\saves\\playline%d\\",
               static_cast<int>(playline));
    w += suffix;
    return w;
}

// Build a Windows wide-string path for the cosave matching a
// savegame basename. `playline` is the engine's playline index for
// the SAVE/LOAD operation; pass the value from g_pendingLoadPlayline
// for loads, or extract from the SaveGame raw path for saves.
std::wstring MakeCosavePath(int32_t playline, const std::string& basename) {
    std::wstring dir = SavesDirForPlayline(playline);
    if (dir.empty()) return {};
    return dir + Utf8ToWide(basename);
}

// SAVE-side path resolver: take the engine's raw SaveGame full path
// (e.g., "%USER%/saves/playline0/save561.whs"), swap the extension
// to .kcdx, and resolve "%USER%/" to %USERPROFILE%/Saved Games/
// kingdomcome2/. The playline directory is whatever the engine
// supplied verbatim — guaranteed to match.
std::wstring CosavePathFromSaveFullPath(const std::string& rawFullPath) {
    if (rawFullPath.empty()) return {};

    // Swap the trailing ".whs" for ".kcdx" first, on the raw string.
    std::string s = rawFullPath;
    const std::string whs = ".whs";
    if (s.size() >= whs.size() &&
        s.compare(s.size() - whs.size(), whs.size(), whs) == 0) {
        s.erase(s.size() - whs.size());
    }
    s += ".kcdx";

    // Resolve "%USER%/" prefix by expanding to
    // %USERPROFILE%\Saved Games\kingdomcome2\.
    constexpr const char* kUserPrefix = "%USER%/";
    std::string expanded;
    if (s.rfind(kUserPrefix, 0) == 0) {
        wchar_t userProfile[MAX_PATH] = {};
        if (SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr,
                             SHGFP_TYPE_CURRENT, userProfile) != S_OK) {
            return {};
        }
        char userProfileUtf8[MAX_PATH * 2] = {};
        WideCharToMultiByte(CP_UTF8, 0, userProfile, -1,
                            userProfileUtf8, sizeof(userProfileUtf8),
                            nullptr, nullptr);
        expanded = userProfileUtf8;
        expanded += "\\Saved Games\\kingdomcome2\\";
        expanded += s.substr(strlen(kUserPrefix));
    } else {
        expanded = s;
    }

    // Normalize forward slashes to backslashes for Windows file APIs.
    for (auto& c : expanded) {
        if (c == '/') c = '\\';
    }
    return Utf8ToWide(expanded);
}

// -----------------------------------------------------------------------------
// Cosave serialization (in-memory blob → bytes)
// -----------------------------------------------------------------------------

void AppendBytes(std::vector<uint8_t>& out, const void* src, size_t n) {
    if (n == 0) return;
    const uint8_t* p = static_cast<const uint8_t*>(src);
    out.insert(out.end(), p, p + n);
}

void AppendU32(std::vector<uint8_t>& out, uint32_t v) {
    AppendBytes(out, &v, sizeof(v));
}

bool ReadBytes(const uint8_t*& cursor, const uint8_t* end,
               void* dst, size_t n) {
    if (cursor + n > end) return false;
    std::memcpy(dst, cursor, n);
    cursor += n;
    return true;
}

bool ReadU32(const uint8_t*& cursor, const uint8_t* end, uint32_t& v) {
    return ReadBytes(cursor, end, &v, sizeof(v));
}

// -----------------------------------------------------------------------------
// Save flow
// -----------------------------------------------------------------------------

void RunSaveCallbacks() {
    // Snapshot the plugin list under the lock; release for the actual
    // callback dispatch so plugins can re-enter SetSaveCallback etc.
    // without deadlocking.
    std::vector<PluginState*> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_pluginsMutex);
        snapshot.reserve(g_plugins.size());
        for (auto& p : g_plugins) snapshot.push_back(p.get());
    }

    for (PluginState* p : snapshot) {
        if (!p->saveCb || p->uid == 0) continue;
        // Reset write-side state for this plugin.
        p->pendingChunks.clear();
        p->currentChunk = nullptr;

        g_currentWriter = p;
        p->saveCb(p->handle);
        g_currentWriter = nullptr;
    }
}

bool BuildAndWriteCosave(const std::wstring& path) {
    // Gather plugins that actually produced data.
    struct Section {
        uint32_t uid;
        std::vector<Chunk>* chunks;
    };
    std::vector<Section> sections;
    {
        std::lock_guard<std::mutex> lock(g_pluginsMutex);
        for (auto& p : g_plugins) {
            if (p->uid != 0 && !p->pendingChunks.empty()) {
                sections.push_back({p->uid, &p->pendingChunks});
            }
        }
    }

    if (sections.empty()) {
        KCDX_DEV("SERIALIZATION", "SAVE",
            kcdx::dev::KV("status", "no-plugin-data"));
        return true;  // nothing to write; not an error
    }

    std::vector<uint8_t> blob;
    blob.reserve(4096);
    AppendU32(blob, kCosaveMagic);
    AppendU32(blob, kCosaveFormatVersion);
    AppendU32(blob, static_cast<uint32_t>(sections.size()));

    for (const auto& s : sections) {
        AppendU32(blob, s.uid);
        AppendU32(blob, static_cast<uint32_t>(s.chunks->size()));

        // Compute section bytes (the chunks region length).
        uint32_t sectionBytes = 0;
        for (const auto& c : *s.chunks) {
            sectionBytes += 4 + 4 + 4 + static_cast<uint32_t>(c.data.size());
        }
        AppendU32(blob, sectionBytes);

        for (const auto& c : *s.chunks) {
            AppendU32(blob, c.tag);
            AppendU32(blob, c.version);
            AppendU32(blob, static_cast<uint32_t>(c.data.size()));
            AppendBytes(blob, c.data.data(), c.data.size());
        }
    }

    // Ensure the parent directory exists. On the FIRST save of a fresh
    // playline (e.g. a new game), kcdx's SaveGame hook fires before
    // the engine's save body runs, so `saves/playline<N>/` may not
    // exist yet — the engine creates it as a side effect of writing
    // the .whs. Without this, the cosave write loses the race.
    {
        std::wstring parent = path;
        size_t lastSep = parent.find_last_of(L"\\/");
        if (lastSep != std::wstring::npos) {
            parent.resize(lastSep);
            int r = SHCreateDirectoryExW(nullptr, parent.c_str(), nullptr);
            if (r != ERROR_SUCCESS && r != ERROR_ALREADY_EXISTS &&
                r != ERROR_FILE_EXISTS) {
                log::WarnF("[serialization] SHCreateDirectoryExW returned %d "
                           "for cosave parent dir; continuing optimistically",
                           r);
            }
        }
    }

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        log::ErrorF("[serialization] CreateFileW failed for cosave (err=%lu)",
                    GetLastError());
        return false;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(h, blob.data(),
                        static_cast<DWORD>(blob.size()), &written, nullptr);
    CloseHandle(h);
    if (!ok || written != blob.size()) {
        log::ErrorF("[serialization] WriteFile failed for cosave "
                    "(written=%lu, expected=%zu)",
                    static_cast<unsigned long>(written), blob.size());
        return false;
    }

    KCDX_DEV("SERIALIZATION", "SAVE",
        kcdx::dev::KV("plugins",   static_cast<unsigned long long>(sections.size())),
        kcdx::dev::KV("bytes",     static_cast<unsigned long long>(blob.size())));
    return true;
}

// -----------------------------------------------------------------------------
// Load flow
// -----------------------------------------------------------------------------

// Returns true if the cosave file was read and parsed; false (with
// loadedChunks empty for every plugin) if the file doesn't exist or
// is malformed. In the false case, RunRevertCallbacks fires for every
// registered plugin so they reset to a fresh-game state.
bool ReadAndParseCosave(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) {
            KCDX_DEV("SERIALIZATION", "LOAD",
                kcdx::dev::KV("status", "no-cosave-file"));
        } else {
            log::WarnF("[serialization] CreateFileW failed for cosave (err=%lu)",
                       e);
        }
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 ||
        size.QuadPart > (64 * 1024 * 1024)) {
        CloseHandle(h);
        log::WarnF("[serialization] cosave size suspect (%lld)",
                   static_cast<long long>(size.QuadPart));
        return false;
    }
    std::vector<uint8_t> blob(static_cast<size_t>(size.QuadPart));
    DWORD got = 0;
    BOOL ok = ReadFile(h, blob.data(), static_cast<DWORD>(blob.size()),
                       &got, nullptr);
    CloseHandle(h);
    if (!ok || got != blob.size()) {
        log::Warn("[serialization] ReadFile short on cosave");
        return false;
    }

    const uint8_t* cursor = blob.data();
    const uint8_t* end    = cursor + blob.size();

    uint32_t magic = 0, fmtVer = 0, pluginCount = 0;
    if (!ReadU32(cursor, end, magic) || magic != kCosaveMagic) {
        log::Warn("[serialization] cosave magic mismatch");
        return false;
    }
    if (!ReadU32(cursor, end, fmtVer) || fmtVer != kCosaveFormatVersion) {
        log::WarnF("[serialization] cosave format version %u (expected %u)",
                   fmtVer, kCosaveFormatVersion);
        return false;
    }
    if (!ReadU32(cursor, end, pluginCount)) {
        log::Warn("[serialization] cosave header truncated");
        return false;
    }

    // Build a transient UID → chunks map; then assign into registered
    // PluginStates by matching UIDs.
    std::unordered_map<uint32_t, std::vector<Chunk>> uidToChunks;

    for (uint32_t i = 0; i < pluginCount; ++i) {
        uint32_t uid = 0, chunkCount = 0, sectionBytes = 0;
        if (!ReadU32(cursor, end, uid) ||
            !ReadU32(cursor, end, chunkCount) ||
            !ReadU32(cursor, end, sectionBytes)) {
            log::Warn("[serialization] cosave plugin header truncated");
            return false;
        }
        if (cursor + sectionBytes > end) {
            log::Warn("[serialization] cosave section overflows file");
            return false;
        }
        const uint8_t* sectionEnd = cursor + sectionBytes;

        std::vector<Chunk> chunks;
        chunks.reserve(chunkCount);
        for (uint32_t c = 0; c < chunkCount; ++c) {
            uint32_t tag = 0, version = 0, chunkBytes = 0;
            if (!ReadU32(cursor, sectionEnd, tag) ||
                !ReadU32(cursor, sectionEnd, version) ||
                !ReadU32(cursor, sectionEnd, chunkBytes)) {
                log::Warn("[serialization] cosave chunk header truncated");
                return false;
            }
            Chunk ck;
            ck.tag = tag;
            ck.version = version;
            ck.data.resize(chunkBytes);
            if (!ReadBytes(cursor, sectionEnd, ck.data.data(), chunkBytes)) {
                log::Warn("[serialization] cosave chunk data truncated");
                return false;
            }
            chunks.push_back(std::move(ck));
        }
        if (cursor != sectionEnd) {
            // Honor section_bytes — skip any trailing bytes we didn't
            // consume (forwards-compat slack).
            cursor = sectionEnd;
        }
        uidToChunks[uid] = std::move(chunks);
    }

    // Push parsed chunks into registered PluginStates.
    std::lock_guard<std::mutex> lock(g_pluginsMutex);
    for (auto& p : g_plugins) {
        if (p->uid == 0) continue;
        auto it = uidToChunks.find(p->uid);
        if (it != uidToChunks.end()) {
            p->loadedChunks = std::move(it->second);
        } else {
            p->loadedChunks.clear();
        }
        p->loadCursor = 0;
        p->loadByteOffset = 0;
        p->loadHasPending = false;
    }

    KCDX_DEV("SERIALIZATION", "LOAD",
        kcdx::dev::KV("plugins", static_cast<unsigned long long>(uidToChunks.size())),
        kcdx::dev::KV("bytes",   static_cast<unsigned long long>(blob.size())));
    return true;
}

void RunLoadCallbacks() {
    std::vector<PluginState*> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_pluginsMutex);
        snapshot.reserve(g_plugins.size());
        for (auto& p : g_plugins) snapshot.push_back(p.get());
    }
    for (PluginState* p : snapshot) {
        if (!p->loadCb || p->uid == 0) continue;
        if (p->loadedChunks.empty()) continue;  // RevertCallback handles that

        p->loadCursor = 0;
        p->loadByteOffset = 0;
        p->loadHasPending = false;

        g_currentReader = p;
        p->loadCb(p->handle);
        g_currentReader = nullptr;

        // Clear after callback so we don't keep the data around longer
        // than needed (plugins copy what they want out during the
        // callback).
        p->loadedChunks.clear();
    }
}

void RunRevertCallbacks() {
    std::vector<PluginState*> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_pluginsMutex);
        snapshot.reserve(g_plugins.size());
        for (auto& p : g_plugins) snapshot.push_back(p.get());
    }
    for (PluginState* p : snapshot) {
        if (!p->revertCb) continue;
        // Revert fires for plugins that:
        //  - have no UID set (their loaded section, if any, would be 0
        //    which collides; revert keeps them in fresh state); OR
        //  - have a UID but no chunks in the cosave for that UID.
        // Engine callers (PostLoadGame when no cosave exists) call
        // this for every plugin regardless.
        p->revertCb(p->handle);
    }
}

// -----------------------------------------------------------------------------
// Message listeners
// -----------------------------------------------------------------------------

void OnSaveGame(kcdxMessage* msg) {
    if (!msg || msg->messageType != kcdxMessage_SaveGame) return;
    const char* basename = static_cast<const char*>(msg->data);
    if (!basename || !*basename) {
        log::Warn("[serialization] kcdxMessage_SaveGame fired with empty basename — skipping cosave write");
        return;
    }

    // For SAVE, derive the cosave path from the engine's raw full
    // SaveGame path (captured by save_load_hooks::HookedSaveGame
    // before basename normalization). The path's playline directory
    // matches the engine's view exactly — no risk of guessing the
    // wrong playline.
    std::string rawPath;
    {
        std::lock_guard<std::mutex> lock(g_lastSaveFullPathMutex);
        rawPath = g_lastSaveFullPath;
    }
    std::wstring fullPath = CosavePathFromSaveFullPath(rawPath);
    if (fullPath.empty()) {
        log::Warn("[serialization] cannot resolve saves dir for cosave write "
                  "(no raw SaveGame full path captured)");
        return;
    }

    // Run save callbacks (fills each plugin's pendingChunks), then
    // serialize to disk.
    RunSaveCallbacks();
    BuildAndWriteCosave(fullPath);

    // Reset write buffers after flush so the same plugin can save
    // again on the next user save action.
    {
        std::lock_guard<std::mutex> lock(g_pluginsMutex);
        for (auto& p : g_plugins) {
            p->pendingChunks.clear();
            p->currentChunk = nullptr;
        }
    }
}

void OnLoadGameSelected(kcdxMessage* msg) {
    if (!msg || msg->messageType != kcdxMessage_LoadGameSelected) return;
    const char* basename = static_cast<const char*>(msg->data);
    if (!basename || !*basename) return;
    std::lock_guard<std::mutex> lock(g_pendingLoadBasenameMutex);
    g_pendingLoadBasename = basename;
}

void OnPostLoadGame(kcdxMessage* msg) {
    if (!msg || msg->messageType != kcdxMessage_PostLoadGame) return;

    std::string basename;
    {
        std::lock_guard<std::mutex> lock(g_pendingLoadBasenameMutex);
        basename = std::move(g_pendingLoadBasename);
        g_pendingLoadBasename.clear();
    }
    // Grab and clear the playline alongside the basename so subsequent
    // loads start with a clean slate.
    int32_t playline = g_pendingLoadPlayline.exchange(-1, std::memory_order_acq_rel);
    if (basename.empty()) {
        // No LoadGameSelected for this load (engine-internal load?).
        // Fire Revert so plugins reset to fresh state — matches SKSE
        // behavior when a save doesn't have plugin data.
        RunRevertCallbacks();
        return;
    }

    std::string cosaveBasename = MakeCosaveBasename(basename.c_str());
    std::wstring fullPath = MakeCosavePath(playline, cosaveBasename);
    if (fullPath.empty()) {
        RunRevertCallbacks();
        return;
    }

    bool parsed = ReadAndParseCosave(fullPath);
    if (parsed) {
        RunLoadCallbacks();
        // After loadCb dispatch, also fire Revert for plugins that
        // didn't get any chunks (their loadedChunks was empty, so
        // RunLoadCallbacks skipped them).
        std::vector<PluginState*> snapshot;
        {
            std::lock_guard<std::mutex> lock(g_pluginsMutex);
            snapshot.reserve(g_plugins.size());
            for (auto& p : g_plugins) snapshot.push_back(p.get());
        }
        for (PluginState* p : snapshot) {
            if (p->revertCb && p->uid != 0 && p->loadedChunks.empty()) {
                p->revertCb(p->handle);
            }
        }
    } else {
        // No cosave file or malformed — every plugin reverts.
        RunRevertCallbacks();
    }
}

// Listener that watches kcdxMessage_SaveGame to capture the FULL path
// (the SaveGame hook's basename normalization is for plugin messages,
// but we need the engine's full path here to know which directory to
// write the cosave into). For now, save_load_hooks publishes a
// separate "raw path" field via the existing message infrastructure
// — and serialization snoops g_lastSaveFullPath via the
// save_load_hooks API. Plumbed below in Init().
//
// To avoid an additional message, we'll just have save_load_hooks
// stash the full path in a module-internal variable that this
// serialization module reads. That keeps the message contract
// (basename only) clean for plugin authors.

// -----------------------------------------------------------------------------
// Public API thunks
// -----------------------------------------------------------------------------

void Thunk_SetUniqueID(kcdxPluginHandle plugin, uint32_t uid) {
    PluginState* p = FindOrCreatePluginState(plugin);
    if (!p) {
        log::Warn("[serialization] SetUniqueID: invalid plugin handle");
        return;
    }
    p->uid = uid;
}

void Thunk_SetSaveCallback(kcdxPluginHandle plugin,
                           kcdxSerializationSaveCallback cb) {
    PluginState* p = FindOrCreatePluginState(plugin);
    if (!p) return;
    p->saveCb = cb;
}

void Thunk_SetLoadCallback(kcdxPluginHandle plugin,
                           kcdxSerializationLoadCallback cb) {
    PluginState* p = FindOrCreatePluginState(plugin);
    if (!p) return;
    p->loadCb = cb;
}

void Thunk_SetRevertCallback(kcdxPluginHandle plugin,
                             kcdxSerializationRevertCallback cb) {
    PluginState* p = FindOrCreatePluginState(plugin);
    if (!p) return;
    p->revertCb = cb;
}

bool Thunk_OpenRecord(uint32_t tag, uint32_t version) {
    PluginState* p = g_currentWriter;
    if (!p) {
        log::Warn("[serialization] OpenRecord called outside a SaveCallback");
        return false;
    }
    if (p->uid == 0) {
        log::Warn("[serialization] OpenRecord requires SetUniqueID first");
        return false;
    }
    p->pendingChunks.push_back(Chunk{tag, version, {}});
    p->currentChunk = &p->pendingChunks.back();
    return true;
}

bool Thunk_WriteRecordData(const void* buf, uint32_t len) {
    PluginState* p = g_currentWriter;
    if (!p || !p->currentChunk) {
        log::Warn("[serialization] WriteRecordData called without an open record");
        return false;
    }
    if (len == 0) return true;
    if (!buf) return false;
    auto& data = p->currentChunk->data;
    size_t oldSize = data.size();
    data.resize(oldSize + len);
    std::memcpy(data.data() + oldSize, buf, len);
    return true;
}

bool Thunk_GetNextRecordInfo(uint32_t* outTag, uint32_t* outVersion,
                             uint32_t* outLen) {
    PluginState* p = g_currentReader;
    if (!p) {
        log::Warn("[serialization] GetNextRecordInfo called outside a LoadCallback");
        return false;
    }
    // If the previous Info had a pending unread chunk, skip past it.
    if (p->loadHasPending) {
        p->loadCursor += 1;
        p->loadByteOffset = 0;
        p->loadHasPending = false;
    }
    if (p->loadCursor >= p->loadedChunks.size()) return false;
    const Chunk& c = p->loadedChunks[p->loadCursor];
    if (outTag)     *outTag     = c.tag;
    if (outVersion) *outVersion = c.version;
    if (outLen)     *outLen     = static_cast<uint32_t>(c.data.size());
    p->loadByteOffset = 0;
    p->loadHasPending = true;
    return true;
}

bool Thunk_ReadRecordData(void* buf, uint32_t len) {
    PluginState* p = g_currentReader;
    if (!p || !p->loadHasPending) {
        log::Warn("[serialization] ReadRecordData called without GetNextRecordInfo");
        return false;
    }
    if (len == 0) return true;
    if (!buf) return false;
    const Chunk& c = p->loadedChunks[p->loadCursor];
    if (p->loadByteOffset + len > c.data.size()) {
        log::WarnF("[serialization] ReadRecordData over-reads (requested %u, "
                   "chunk len %zu, already-read %zu)",
                   len, c.data.size(), p->loadByteOffset);
        return false;
    }
    std::memcpy(buf, c.data.data() + p->loadByteOffset, len);
    p->loadByteOffset += len;
    // If the plugin has consumed the whole chunk, advance to the next
    // record on the next GetNextRecordInfo.
    if (p->loadByteOffset >= c.data.size()) {
        p->loadCursor += 1;
        p->loadByteOffset = 0;
        p->loadHasPending = false;
    }
    return true;
}

kcdxSerializationInterface g_iface = {
    /*SetUniqueID=*/        Thunk_SetUniqueID,
    /*SetSaveCallback=*/    Thunk_SetSaveCallback,
    /*SetLoadCallback=*/    Thunk_SetLoadCallback,
    /*SetRevertCallback=*/  Thunk_SetRevertCallback,
    /*OpenRecord=*/         Thunk_OpenRecord,
    /*WriteRecordData=*/    Thunk_WriteRecordData,
    /*GetNextRecordInfo=*/  Thunk_GetNextRecordInfo,
    /*ReadRecordData=*/     Thunk_ReadRecordData,
};

}  // namespace

const kcdxSerializationInterface* GetInterface() {
    return &g_iface;
}

void Init() {
    static bool s_inited = false;
    if (s_inited) return;
    s_inited = true;

    // Subscribe to the lifecycle messages. We listen as "engine"
    // (sender = nullptr) — these are engine-fired messages.
    // RegisterListener requires a plugin handle for the LISTENER —
    // serialization is engine-side, so we register with a sentinel
    // engine handle. Use kcdxInvalidPluginHandle and patch the
    // dispatcher to recognize it... actually, simpler: we don't go
    // through kcdxMessagingInterface at all. messaging::FireEngineMessage
    // delivers to subscribed listeners, but the engine's own
    // listeners need a different path. Hook in via a direct call
    // from messaging.cpp's FireEngineMessage — see Init() below.
    //
    // For Phase 6b implementation: just call the listeners
    // synchronously from a wrapper around FireEngineMessage. To
    // avoid touching messaging.cpp's API surface, we expose
    // serialization_internal::OnEngineMessage and call it from
    // FireEngineMessage when the messageType matches.

    KCDX_DEV("SERIALIZATION", "INIT", kcdx::dev::KV("status", "ready"));
}

// Engine-internal hook called by messaging::FireEngineMessage for
// every engine-fired message. Routes save/load lifecycle messages to
// the appropriate handlers above.
void OnEngineMessage(kcdxMessage* msg) {
    if (!msg) return;
    switch (msg->messageType) {
    case kcdxMessage_SaveGame:         OnSaveGame(msg); break;
    case kcdxMessage_LoadGameSelected: OnLoadGameSelected(msg); break;
    case kcdxMessage_PostLoadGame:     OnPostLoadGame(msg); break;
    default: break;
    }
}

// Engine-internal API for save_load_hooks to stash the full
// SaveGame path before basename normalization. Called from
// HookedSaveGame in save_load_hooks.cpp.
void SetLastSaveFullPath(const char* fullPath) {
    if (!fullPath) return;
    std::lock_guard<std::mutex> lock(g_lastSaveFullPathMutex);
    g_lastSaveFullPath = fullPath;
}

// Engine-internal API for save_load_hooks to stash the playline
// index from the SlotResolver hook. The next PostLoadGame will read
// this to construct the cosave path with the correct `playline<N>`
// directory.
void SetPendingLoadPlayline(int32_t playline) {
    g_pendingLoadPlayline.store(playline, std::memory_order_release);
}

}  // namespace kcdx::serialization
