// kcdx-watchdog.exe — external crash-bundle helper.
//
// Spawned by kcdx.asi at startup with the game's PID + engine paths.
// Waits on the game's process handle (zero CPU; just blocks on a
// kernel handle). When the game dies, the watchdog wakes up, checks
// the exit code, and on non-zero exit collects:
//
//   - kcdx-engine/logs/kcdx_<sessionstamp>.log         (engine log)
//   - kcdx-engine/logs/kcdx-dev_<sessionstamp>.log     (dev log, if it exists)
//   - plugins/<X>/logs/<X>_<sessionstamp>.log          (ALL plugin logs)
//   - <game>/kcd.log                                   (game's own log)
//   - %LOCALAPPDATA%/CrashDumps/KingdomCome.exe.<pid>.dmp  (WerFault dump if any)
//   - %LOCALAPPDATA%/Temp/<recent>.xml                 (BugSplat XML reports)
//
// Files are zipped into:
//   kcdx-engine/logs/crash/crash_<sessionstamp>.zip
//
// On clean shutdown (game exit code 0), the watchdog exits silently —
// no bundle is created.
//
// Invocation:
//   kcdx-watchdog.exe <pid> <engine-dir> <plugins-dir> <session-stamp> <game-dir>
//
// All paths are wide-string-encoded as UTF-8 on the command line by
// kcdx.asi's launcher (kcdx::watchdog::Spawn in dllmain).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "miniz.h"

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------
// Self-log so the watchdog's own diagnostics are visible. Writes to
// <engine-dir>/logs/kcdx-watchdog_<sessionstamp>.log. Each line is
// flushed; on a fast-fail of the watchdog itself (unlikely but
// defense-in-depth), the disk has the last line.
// ---------------------------------------------------------------------
FILE* g_selflog = nullptr;

void SelfLog(const char* fmt, ...) {
    if (!g_selflog) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fprintf(g_selflog, "[%02u:%02u:%02u.%03u] ",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_selflog, fmt, ap);
    va_end(ap);
    fputc('\n', g_selflog);
    fflush(g_selflog);
}

void OpenSelfLog(const fs::path& engineDir, const std::string& stamp) {
    fs::path logDir = engineDir / "logs";
    std::error_code ec;
    fs::create_directories(logDir, ec);
    fs::path p = logDir / ("kcdx-watchdog_" + stamp + ".log");
    g_selflog = _wfopen(p.wstring().c_str(), L"wb");
}

// ---------------------------------------------------------------------
// Wide-string path helpers
// ---------------------------------------------------------------------
std::string WToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1,
                                nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(),
                            n, nullptr, nullptr);
    }
    return out;
}

std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) {
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    }
    return out;
}

// ---------------------------------------------------------------------
// Find files matching a predicate, with mtime newer than `since`.
// Returns absolute paths.
// ---------------------------------------------------------------------
template <typename Pred>
std::vector<fs::path> FindRecent(const fs::path& dir,
                                 fs::file_time_type since,
                                 Pred pred,
                                 bool recursive = false) {
    std::vector<fs::path> hits;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return hits;
    auto check = [&](const fs::path& p) {
        if (!fs::is_regular_file(p, ec)) return;
        auto mt = fs::last_write_time(p, ec);
        if (ec) return;
        if (mt < since) return;
        if (pred(p)) hits.push_back(p);
    };
    if (recursive) {
        for (auto& entry : fs::recursive_directory_iterator(dir, ec)) {
            if (ec) break;
            check(entry.path());
        }
    } else {
        for (auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            check(entry.path());
        }
    }
    return hits;
}

// ---------------------------------------------------------------------
// Add a single file to the zip under `archiveName`. Logs success/fail.
// ---------------------------------------------------------------------
bool AddFile(mz_zip_archive& zip, const fs::path& src,
             const std::string& archiveName) {
    std::error_code ec;
    if (!fs::exists(src, ec)) {
        SelfLog("  skip (not found): %s", WToUtf8(src.wstring()).c_str());
        return false;
    }
    // Read into memory — Windows file locking can interfere with
    // mz_zip_writer_add_file's open call, especially for the game's
    // own kcd.log which the game may still hold open. ReadFile with
    // FILE_SHARE_READ|WRITE works around that.
    HANDLE h = CreateFileW(src.wstring().c_str(),
                           GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        SelfLog("  skip (open failed err=%lu): %s",
                GetLastError(), WToUtf8(src.wstring()).c_str());
        return false;
    }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz)) {
        CloseHandle(h);
        SelfLog("  skip (size failed): %s", WToUtf8(src.wstring()).c_str());
        return false;
    }
    std::vector<uint8_t> blob(static_cast<size_t>(sz.QuadPart));
    DWORD got = 0;
    BOOL ok = ReadFile(h, blob.data(),
                       static_cast<DWORD>(blob.size()), &got, nullptr);
    CloseHandle(h);
    if (!ok || got != blob.size()) {
        SelfLog("  skip (read failed): %s", WToUtf8(src.wstring()).c_str());
        return false;
    }
    if (!mz_zip_writer_add_mem(&zip, archiveName.c_str(),
                               blob.data(), blob.size(),
                               MZ_DEFAULT_COMPRESSION)) {
        SelfLog("  zip add failed: %s -> %s",
                WToUtf8(src.wstring()).c_str(), archiveName.c_str());
        return false;
    }
    SelfLog("  bundled: %s (%llu bytes) as %s",
            WToUtf8(src.wstring()).c_str(),
            (unsigned long long)blob.size(),
            archiveName.c_str());
    return true;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // /SUBSYSTEM:WINDOWS doesn't give us argv directly. Get the
    // wide command line and split it ourselves.
    //
    // Expected: <exe> <pid> <engineDir> <pluginsDir> <stamp>
    //                <gameDir> <devMode 0|1>
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 7) {
        if (argv) LocalFree(argv);
        return 1;
    }

    DWORD pid = wcstoul(argv[1], nullptr, 10);
    fs::path engineDir = argv[2];
    fs::path pluginsDir = argv[3];
    std::wstring stampW = argv[4];
    std::string stamp = WToUtf8(stampW);
    fs::path gameDir = argv[5];
    bool devMode = (wcstoul(argv[6], nullptr, 10) != 0);
    LocalFree(argv);

    OpenSelfLog(engineDir, stamp);
    SelfLog("kcdx-watchdog started pid=%lu session=%s",
            pid, stamp.c_str());
    SelfLog("  engineDir=%s",   WToUtf8(engineDir.wstring()).c_str());
    SelfLog("  pluginsDir=%s",  WToUtf8(pluginsDir.wstring()).c_str());
    SelfLog("  gameDir=%s",     WToUtf8(gameDir.wstring()).c_str());
    SelfLog("  devMode=%s (dmp inclusion %s)",
            devMode ? "ON" : "OFF",
            devMode ? "enabled" : "skipped — set dev_mode = true in "
                                  "engine.toml to include the minidump");

    HANDLE hProc = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                               FALSE, pid);
    if (!hProc) {
        SelfLog("OpenProcess failed err=%lu — game must have exited "
                "before watchdog wired up", GetLastError());
        return 1;
    }

    // Capture wall-clock at watchdog start. Used as the lower bound
    // for crash-artifact mtime filtering so we don't bundle dumps
    // from earlier game runs.
    auto sessionStart = fs::file_time_type::clock::now()
                      - std::chrono::seconds(5);  // small grace window

    SelfLog("waiting on game process handle...");
    WaitForSingleObject(hProc, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(hProc, &exitCode);
    SelfLog("game exited with code=0x%lX (%lu)",
            exitCode, exitCode);
    CloseHandle(hProc);

    if (exitCode == 0) {
        SelfLog("clean exit; no bundle written");
        if (g_selflog) { fclose(g_selflog); g_selflog = nullptr; }
        return 0;
    }

    // Give BugSplat / WerFault a few seconds to finish writing
    // their dumps and XML reports before we scan.
    SelfLog("non-zero exit; waiting 5s for OS to flush crash artifacts");
    Sleep(5000);

    // Bundle location: kcdx-engine/logs/crash/crash_<stamp>.zip
    fs::path crashDir = engineDir / "logs" / "crash";
    std::error_code ec;
    fs::create_directories(crashDir, ec);
    fs::path zipPath = crashDir / ("crash_" + stamp + ".zip");
    SelfLog("opening zip: %s", WToUtf8(zipPath.wstring()).c_str());

    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, WToUtf8(zipPath.wstring()).c_str(),
                                 0)) {
        SelfLog("mz_zip_writer_init_file failed");
        if (g_selflog) { fclose(g_selflog); g_selflog = nullptr; }
        return 1;
    }

    SelfLog("collecting artifacts...");

    // Bundle layout is intentionally flat: kcdx/, plugins/, game/,
    // crash/. Each leaf is a single file. No nested directories per
    // plugin — plugin logs are flattened by filename, which is now
    // unique because kcdx::log writes plugin files as
    // "<manifest.name>_<ts>.log" (manifest names are required to be
    // unique). Authors and consumers don't need to navigate three
    // levels deep to find a specific plugin's log.

    // 1) kcdx engine log + dev log for this session, into kcdx/.
    AddFile(zip,
        engineDir / "logs" / ("kcdx_" + stamp + ".log"),
        "kcdx/kcdx_" + stamp + ".log");
    AddFile(zip,
        engineDir / "logs" / ("kcdx-dev_" + stamp + ".log"),
        "kcdx/kcdx-dev_" + stamp + ".log");

    // 2) ALL plugin logs for this session (recursive walk under
    //    plugins/ for files named "*_<stamp>.log"), flattened into
    //    plugins/<filename>. The filename already encodes the
    //    plugin's manifest name + session stamp, so there's no
    //    ambiguity in the flat layout.
    {
        std::error_code wec;
        std::string suffix = "_" + stamp + ".log";
        for (auto& entry : fs::recursive_directory_iterator(pluginsDir, wec)) {
            if (wec) break;
            if (!entry.is_regular_file(wec)) continue;
            std::string name = entry.path().filename().string();
            if (name.size() < suffix.size()) continue;
            if (name.compare(name.size() - suffix.size(),
                             suffix.size(), suffix) != 0) continue;
            std::string archive = "plugins/" + name;
            AddFile(zip, entry.path(), archive);
        }
    }

    // 3) Game's own kcd.log, into game/.
    AddFile(zip, gameDir / "kcd.log", "game/kcd.log");

    // 4) WerFault dumps under %LOCALAPPDATA%/CrashDumps/ for our PID,
    //    or any KingdomCome.exe dump newer than sessionStart.
    //
    // Gated on devMode. Dumps are ~100MB each; in non-dev sessions a
    // typical "plugin X faulted in its own callback" crash is fully
    // diagnosed from the logs alone (the GUARD line names the
    // plugin + module + offset). The dmp matters most for crashes
    // the logs can't see — fast-fails, kernel kills, game-side
    // faults — and those are exactly the cases an engine dev /
    // plugin author would be investigating with dev mode on.
    if (devMode) {
        wchar_t local[MAX_PATH];
        if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0,
                             local) == S_OK) {
            fs::path crashDumpsDir = fs::path(local) / "CrashDumps";
            auto dumps = FindRecent(crashDumpsDir, sessionStart,
                [](const fs::path& p) {
                    auto name = p.filename().string();
                    return name.rfind("KingdomCome.exe.", 0) == 0 &&
                           p.extension() == ".dmp";
                });
            // Bundle ALL matching (typically 1, possibly 2 if BugSplat + WerFault both wrote).
            for (auto& d : dumps) {
                std::string archive = "crash/" + d.filename().string();
                AddFile(zip, d, archive);
            }
            if (dumps.empty()) {
                SelfLog("  no WerFault dump found in %s",
                        WToUtf8(crashDumpsDir.wstring()).c_str());
            }
        }
    } else {
        SelfLog("  minidump skipped (dev mode off); enable dev_mode = true "
                "in engine.toml to include it");
    }

    // 5) BugSplat XML reports — these live in %LOCALAPPDATA%/Temp and have
    //    8-char random names. Filter by recency.
    {
        wchar_t local[MAX_PATH];
        if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0,
                             local) == S_OK) {
            fs::path tempDir = fs::path(local) / "Temp";
            auto xmls = FindRecent(tempDir, sessionStart,
                [](const fs::path& p) {
                    return p.extension() == ".xml";
                });
            // Filter further: only XMLs whose first line looks like a
            // BugSplat report header. Cheap inspection of first 64 bytes.
            for (auto& x : xmls) {
                HANDLE h = CreateFileW(x.wstring().c_str(),
                    GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h == INVALID_HANDLE_VALUE) continue;
                char buf[128] = {};
                DWORD got = 0;
                ReadFile(h, buf, sizeof(buf) - 1, &got, nullptr);
                CloseHandle(h);
                buf[got] = 0;
                if (strstr(buf, "BugReport") || strstr(buf, "BsSndRpt")) {
                    std::string archive = "crash/" + x.filename().string();
                    AddFile(zip, x, archive);
                }
            }
        }
    }

    // 6) BugSplat session log
    {
        wchar_t local[MAX_PATH];
        if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0,
                             local) == S_OK) {
            fs::path tempDir = fs::path(local) / "Temp";
            auto bslogs = FindRecent(tempDir, sessionStart,
                [](const fs::path& p) {
                    auto name = p.filename().string();
                    return name.rfind("bugsplat_", 0) == 0 &&
                           p.extension() == ".log";
                });
            for (auto& b : bslogs) {
                std::string archive = "crash/" + b.filename().string();
                AddFile(zip, b, archive);
            }
        }
    }

    if (!mz_zip_writer_finalize_archive(&zip)) {
        SelfLog("mz_zip_writer_finalize_archive failed");
    }
    mz_zip_writer_end(&zip);
    SelfLog("zip finalized: %s", WToUtf8(zipPath.wstring()).c_str());
    if (g_selflog) { fclose(g_selflog); g_selflog = nullptr; }
    return 0;
}
