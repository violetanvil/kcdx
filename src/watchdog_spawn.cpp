// watchdog_spawn — see watchdog_spawn.h.

#include "watchdog_spawn.h"

#include <windows.h>

#include <filesystem>
#include <string>

#include "log.h"
#include "paths.h"

namespace fs = std::filesystem;

namespace kcdx::watchdog {

namespace {

// Resolve the game-root directory by walking up from kcdx.dll's
// install location: <game>/Bin/Win64MasterMasterSteamPGO/kcdx-engine/.
// Two parent_path() calls land us at <game>/Bin/Win64...; one more
// gets us to <game>/Bin; one more to <game>.
std::wstring DeriveGameDir() {
    fs::path engineDir(paths::EngineDataDir());
    if (engineDir.empty()) return {};
    // engineDir has a trailing slash; strip it via parent_path twice
    // (once removes the slash, second hops into Win64....)
    fs::path winDir  = engineDir.parent_path().parent_path();
    fs::path binDir  = winDir.parent_path();
    fs::path gameDir = binDir.parent_path();
    return gameDir.wstring();
}

}  // namespace

bool Spawn() {
    fs::path engineDir(paths::EngineDataDir());
    fs::path watchdogExe = engineDir / L"kcdx-watchdog.exe";

    std::error_code ec;
    if (!fs::exists(watchdogExe, ec)) {
        LOG_WARN("WATCHDOG",
            "kcdx-watchdog.exe not found at %s; crash-bundle will not "
            "auto-run. The release zip ships kcdx-watchdog.exe inside "
            "kcdx-engine/; check that folder.",
            watchdogExe.string().c_str());
        return false;
    }

    std::wstring engineDirW = paths::EngineDataDir();
    std::wstring pluginsDirW(paths::PluginsDir());
    std::wstring stampW;
    {
        const std::string& s = log::SessionStamp();
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        if (n > 0) {
            stampW.resize(n - 1);
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, stampW.data(), n);
        }
    }
    std::wstring gameDir = DeriveGameDir();

    // Strip trailing slashes from path args (CreateProcessW gets
    // confused by trailing-backslash + quoted args).
    auto strip = [](std::wstring& s) {
        while (!s.empty() && (s.back() == L'\\' || s.back() == L'/'))
            s.pop_back();
    };
    strip(engineDirW);
    strip(pluginsDirW);

    // Build command line:
    //   <exe> <pid> "<engine>" "<plugins>" <stamp> "<game>" <devMode>
    // Each path argument is double-quoted to survive spaces.
    // devMode is "1" if log::IsDevModeEnabled() at spawn time, "0"
    // otherwise. Dev mode gates the inclusion of the ~100MB
    // minidump in the crash bundle (consumer bundles stay ~500KB).
    wchar_t cmd[2048];
    DWORD myPid = GetCurrentProcessId();
    unsigned devMode = log::IsDevModeEnabled() ? 1u : 0u;
    int written = swprintf(cmd, _countof(cmd),
        L"\"%ls\" %lu \"%ls\" \"%ls\" %ls \"%ls\" %u",
        watchdogExe.wstring().c_str(),
        myPid,
        engineDirW.c_str(),
        pluginsDirW.c_str(),
        stampW.c_str(),
        gameDir.c_str(),
        devMode);
    if (written <= 0) {
        LOG_WARN("WATCHDOG", "command line too long; not spawning");
        return false;
    }

    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    // CREATE_NO_WINDOW + DETACHED_PROCESS: no console, no parent
    // attachment. CREATE_BREAKAWAY_FROM_JOB so the watchdog survives
    // if the game is part of a Steam job object that terminates on
    // game death — without this, the watchdog might be killed along
    // with the game by the job, defeating the entire point.
    DWORD flags = CREATE_NO_WINDOW | DETACHED_PROCESS |
                  CREATE_BREAKAWAY_FROM_JOB;

    BOOL ok = CreateProcessW(
        nullptr,        // app name (we put it in the command line)
        cmd,
        nullptr,        // process security
        nullptr,        // thread security
        FALSE,          // don't inherit handles
        flags,
        nullptr,        // inherit environment
        engineDir.wstring().c_str(),  // working dir (where the exe lives)
        &si,
        &pi);

    if (!ok) {
        DWORD err = GetLastError();
        // CREATE_BREAKAWAY_FROM_JOB can fail if the job doesn't allow
        // breakaway. Retry without it — the watchdog might still die
        // with the game, but at least it'll start.
        if (err == ERROR_ACCESS_DENIED && (flags & CREATE_BREAKAWAY_FROM_JOB)) {
            LOG_WARN("WATCHDOG",
                "CreateProcessW denied with BREAKAWAY_FROM_JOB; retrying without");
            flags &= ~CREATE_BREAKAWAY_FROM_JOB;
            ok = CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE,
                                flags, nullptr,
                                engineDir.wstring().c_str(), &si, &pi);
            if (!ok) err = GetLastError();
        }
    }

    if (!ok) {
        LOG_WARN("WATCHDOG",
            "CreateProcessW failed err=%lu; crash-bundle disabled this "
            "session", GetLastError());
        return false;
    }

    // Close our handles; the watchdog runs detached.
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    LOG_INFO("WATCHDOG",
        "spawned kcdx-watchdog.exe pid=%lu (parent_pid=%lu)",
        pi.dwProcessId, myPid);
    return true;
}

}  // namespace kcdx::watchdog
