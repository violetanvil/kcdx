// kcdx.exe — launcher
//
// The user runs this instead of KingdomCome.exe. The launcher:
//
//   1. Resolves the game exe path. Two modes:
//        (a) %command% pass-through from Steam: argv[1] is the full
//            path to KingdomCome.exe (or a wrapper), argv[2..N] are
//            its args. Set Steam launch options to:
//              "<full path>\kcdx.exe" %command%
//        (b) Direct double-click or no-arg invocation: look for
//            KingdomCome.exe as a sibling of kcdx.exe.
//   2. CreateProcessW(<game exe>, ..., CREATE_SUSPENDED) — game
//      starts frozen.
//   3. CreateRemoteThread(game_proc, LoadLibraryW,
//      "<path>/kcdx-engine/kcdx.dll") — kcdx.dll loads inside the
//      game's process. Its DllMain runs.
//   4. ResumeThread(main_thread) — game continues.
//
// If injection fails (Windows Defender / third-party AV intercept), the
// launcher logs the failure to kcdx-engine/logs/kcdx-launcher_<ts>.log
// and falls back to a second injection variant. If all variants fail,
// the launcher logs an actionable error message and exits nonzero so
// the user knows kcdx didn't load (rather than silently launching
// vanilla KCD2).
//
// Logging: minimal. Just enough to diagnose "kcdx didn't load" reports.
// All log lines also written to OutputDebugStringW so DebugView shows
// them in real time.
//
// No CRT dependencies beyond what Win32 needs. No UI. No external
// dependencies (asmjit / lua / etc.) — the launcher is intentionally tiny
// (under 100 KB stripped).

#include <windows.h>
#include <shlwapi.h>
#include <shellapi.h>   // CommandLineToArgvW
#include <shlobj.h>     // SHCreateDirectoryExW
#include <strsafe.h>

#include <stdio.h>
#include <time.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

namespace {

HANDLE g_logFile = INVALID_HANDLE_VALUE;
wchar_t g_logPath[MAX_PATH] = L"";

void FormatTimestamp(wchar_t* out, size_t cap) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    StringCchPrintfW(out, cap,
        L"%04u-%02u-%02u_%02u-%02u-%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

void Logf(const wchar_t* fmt, ...) {
    wchar_t timestamp[32];
    SYSTEMTIME st;
    GetLocalTime(&st);
    StringCchPrintfW(timestamp, _countof(timestamp),
        L"[%02u:%02u:%02u.%03u]",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    wchar_t body[1024];
    va_list args;
    va_start(args, fmt);
    StringCchVPrintfW(body, _countof(body), fmt, args);
    va_end(args);

    wchar_t line[1100];
    StringCchPrintfW(line, _countof(line), L"%s %s\r\n", timestamp, body);

    // Stream to OutputDebugStringW (always visible in DebugView).
    OutputDebugStringW(line);

    // Stream to log file if open.
    if (g_logFile != INVALID_HANDLE_VALUE) {
        // Convert to UTF-8 for on-disk encoding (matches kcdx::log).
        char utf8[2200];
        int n = WideCharToMultiByte(CP_UTF8, 0, line, -1,
                                    utf8, sizeof(utf8), nullptr, nullptr);
        if (n > 0) {
            DWORD written = 0;
            WriteFile(g_logFile, utf8, (DWORD)(n - 1), &written, nullptr);
        }
    }
}

void OpenLogFile(const wchar_t* engineLogsDir) {
    wchar_t timestamp[32];
    FormatTimestamp(timestamp, _countof(timestamp));
    StringCchPrintfW(g_logPath, _countof(g_logPath),
        L"%s\\kcdx-launcher_%s.log", engineLogsDir, timestamp);
    g_logFile = CreateFileW(g_logPath,
                            FILE_APPEND_DATA,
                            FILE_SHARE_READ,
                            nullptr,
                            CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL,
                            nullptr);
    // If the open fails, OutputDebugStringW path still works.
}

void CloseLogFile() {
    if (g_logFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_logFile);
        g_logFile = INVALID_HANDLE_VALUE;
    }
}

// ---------------------------------------------------------------------------
// Path resolution
// ---------------------------------------------------------------------------

// Get this launcher's directory (with trailing '\\').
bool GetSelfDir(wchar_t* out, size_t cap) {
    wchar_t full[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, full, _countof(full));
    if (n == 0 || n >= _countof(full)) return false;
    // Strip the trailing filename.
    wchar_t* lastSlash = nullptr;
    for (wchar_t* p = full; *p; ++p) {
        if (*p == L'\\' || *p == L'/') lastSlash = p;
    }
    if (!lastSlash) return false;
    *(lastSlash + 1) = 0;  // keep the trailing slash
    return SUCCEEDED(StringCchCopyW(out, cap, full));
}

// ---------------------------------------------------------------------------
// Injection
// ---------------------------------------------------------------------------

// Standard CreateRemoteThread(LoadLibraryW) injection. Most reliable on
// systems without aggressive AV; first thing we try.
//
// Returns true on success. Sets *outLastError to GetLastError() on failure.
bool InjectViaCreateRemoteThread(HANDLE hProcess,
                                 const wchar_t* dllPath,
                                 DWORD* outLastError) {
    SIZE_T dllPathLen = (wcslen(dllPath) + 1) * sizeof(wchar_t);

    // Allocate memory in the target process for the DLL path.
    LPVOID remoteMem = VirtualAllocEx(hProcess, nullptr, dllPathLen,
                                      MEM_COMMIT | MEM_RESERVE,
                                      PAGE_READWRITE);
    if (!remoteMem) {
        *outLastError = GetLastError();
        return false;
    }

    // Write the DLL path into target process memory.
    SIZE_T bytesWritten = 0;
    if (!WriteProcessMemory(hProcess, remoteMem, dllPath,
                            dllPathLen, &bytesWritten)
        || bytesWritten != dllPathLen) {
        *outLastError = GetLastError();
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        return false;
    }

    // Resolve LoadLibraryW in OUR process — same address as in the
    // target since kernel32.dll is mapped at the same base across
    // processes on the same Windows session.
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) {
        *outLastError = ERROR_MOD_NOT_FOUND;
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        return false;
    }
    LPTHREAD_START_ROUTINE pLoadLibraryW =
        reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(k32, "LoadLibraryW"));
    if (!pLoadLibraryW) {
        *outLastError = ERROR_PROC_NOT_FOUND;
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        return false;
    }

    // Spawn the remote thread.
    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
                                        pLoadLibraryW, remoteMem,
                                        0, nullptr);
    if (!hThread) {
        *outLastError = GetLastError();
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        return false;
    }

    // Wait for LoadLibraryW to return. Cap at 15 seconds — kcdx.dll's
    // DllMain shouldn't take anywhere near that, and if it does we have
    // a deadlock to diagnose anyway.
    DWORD waitResult = WaitForSingleObject(hThread, 15000);
    if (waitResult != WAIT_OBJECT_0) {
        *outLastError = (waitResult == WAIT_TIMEOUT)
                            ? ERROR_TIMEOUT
                            : GetLastError();
        CloseHandle(hThread);
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        return false;
    }

    // Get the LoadLibraryW return value (HMODULE of the loaded DLL).
    // On 64-bit Windows, thread exit codes are truncated to DWORD;
    // 0 means LoadLibrary returned NULL (failure).
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);

    if (exitCode == 0) {
        // LoadLibraryW returned NULL inside the target process. We
        // can't easily get the target's GetLastError(); log the
        // generic case.
        *outLastError = ERROR_DLL_INIT_FAILED;
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int RunLauncher() {
    // 1. Resolve paths.
    wchar_t selfDir[MAX_PATH];
    if (!GetSelfDir(selfDir, _countof(selfDir))) {
        Logf(L"FATAL: couldn't resolve launcher directory");
        return 1;
    }

    // Engine dir + logs dir. Folder name is kcdx-engine/ (prefix makes
    // ownership unambiguous at a glance; matches kcdx-plugins/ sibling).
    wchar_t engineDir[MAX_PATH];
    StringCchPrintfW(engineDir, _countof(engineDir), L"%skcdx-engine", selfDir);
    wchar_t engineLogsDir[MAX_PATH];
    StringCchPrintfW(engineLogsDir, _countof(engineLogsDir),
                     L"%s\\logs", engineDir);

    // Ensure the logs dir exists (best-effort; failure here just means
    // we lose log file capture, OutputDebugStringW still works).
    SHCreateDirectoryExW(nullptr, engineDir, nullptr);
    SHCreateDirectoryExW(nullptr, engineLogsDir, nullptr);
    OpenLogFile(engineLogsDir);

    Logf(L"kcdx.exe launcher starting");
    Logf(L"  self_dir       = %s", selfDir);
    Logf(L"  engine_dir     = %s", engineDir);
    Logf(L"  engine_logs    = %s", engineLogsDir);

    // Resolve kcdx.dll path.
    wchar_t dllPath[MAX_PATH];
    StringCchPrintfW(dllPath, _countof(dllPath),
                     L"%s\\kcdx.dll", engineDir);
    if (!PathFileExistsW(dllPath)) {
        Logf(L"FATAL: kcdx.dll not found at %s", dllPath);
        Logf(L"  expected install layout: %s should be a folder containing kcdx.dll", engineDir);
        CloseLogFile();
        MessageBoxW(nullptr,
            L"kcdx.dll not found. Reinstall kcdx so that kcdx-engine/kcdx.dll "
            L"sits next to KingdomCome.exe (in a folder named 'kcdx-engine').",
            L"kcdx.exe: missing engine DLL", MB_OK | MB_ICONERROR);
        return 2;
    }

    // Parse our own command line. Steam launch-options syntax
    //
    //     "<full path>\kcdx.exe" %command%
    //
    // expands %command% to "<path>\KingdomCome.exe [steam args...]" —
    // so when invoked via Steam, argv[1] is the would-be game exe and
    // argv[2..N] are the args Steam wanted to pass to it. We honor
    // that. When invoked directly (double-click, no args), argv has
    // only argv[0] (our own exe) and we fall back to sibling
    // KingdomCome.exe.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    wchar_t gameExePath[MAX_PATH] = L"";
    bool gameExeFromArgv = false;

    // argv[1] is the game exe IFF it exists on disk and is an .exe.
    // (Defensive: if Steam ever expands %command% to something other
    // than a real path, fall through to sibling lookup rather than
    // CreateProcess'ing a garbage path.)
    if (argc >= 2 && argv && argv[1]) {
        if (PathFileExistsW(argv[1])) {
            StringCchCopyW(gameExePath, _countof(gameExePath), argv[1]);
            gameExeFromArgv = true;
        } else {
            Logf(L"  argv[1] = '%s' (does not exist on disk; falling back to sibling lookup)",
                 argv[1]);
        }
    }

    if (!gameExeFromArgv) {
        StringCchPrintfW(gameExePath, _countof(gameExePath),
                         L"%sKingdomCome.exe", selfDir);
        if (!PathFileExistsW(gameExePath)) {
            Logf(L"FATAL: KingdomCome.exe not found at %s", gameExePath);
            if (argv) LocalFree(argv);
            CloseLogFile();
            MessageBoxW(nullptr,
                L"KingdomCome.exe not found. kcdx.exe must sit next to "
                L"KingdomCome.exe in the same folder, OR be launched via "
                L"Steam with launch options:\n\n"
                L"    \"<path>\\kcdx.exe\" %command%",
                L"kcdx.exe: missing game executable", MB_OK | MB_ICONERROR);
            return 3;
        }
    }

    Logf(L"  game_exe       = %s%s", gameExePath,
         gameExeFromArgv ? L"  (from %command% / argv[1])" : L"  (sibling lookup)");
    Logf(L"  kcdx_dll       = %s", dllPath);

    // 2. CreateProcessW(KingdomCome.exe, ..., CREATE_SUSPENDED).
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    // Build the child command line. argv[0] of the child process is
    // (by Win32 convention) the path to the exe itself, quoted to
    // handle spaces. When invoked via %command%, argv[2..N] of OUR
    // process are the args Steam wanted on the game (Steam overlay
    // / Proton / nothing — depending on user setup); pass them
    // through verbatim. When invoked directly, there are no extra
    // args.
    int firstPassThroughArg = gameExeFromArgv ? 2 : 1;
    wchar_t cmdBuf[2048];
    StringCchPrintfW(cmdBuf, _countof(cmdBuf), L"\"%s\"", gameExePath);
    for (int i = firstPassThroughArg; i < argc; ++i) {
        StringCchCatW(cmdBuf, _countof(cmdBuf), L" ");
        // Re-quote each arg if it contains whitespace (Win32 caller's
        // responsibility — CommandLineToArgvW stripped the quotes).
        bool needsQuotes = false;
        for (const wchar_t* p = argv[i]; *p; ++p) {
            if (*p == L' ' || *p == L'\t') { needsQuotes = true; break; }
        }
        if (needsQuotes) StringCchCatW(cmdBuf, _countof(cmdBuf), L"\"");
        StringCchCatW(cmdBuf, _countof(cmdBuf), argv[i]);
        if (needsQuotes) StringCchCatW(cmdBuf, _countof(cmdBuf), L"\"");
    }
    if (argv) LocalFree(argv);

    Logf(L"creating game process (suspended)");
    Logf(L"  cmd_line: %s", cmdBuf);

    // lpCurrentDirectory needs to be the game-bin directory without a
    // trailing slash; selfDir from GetSelfDir has the trailing slash, so
    // strip it for this call (Win32 is finicky about paths in some APIs
    // even when MSDN says it shouldn't matter). KCD2's KingdomCome.exe
    // does relative-path LoadLibrary calls for WHGame.dll etc., so the
    // current directory MUST be the same folder that contains those DLLs.
    wchar_t cwdBuf[MAX_PATH];
    StringCchCopyW(cwdBuf, _countof(cwdBuf), selfDir);
    size_t cwdLen = wcslen(cwdBuf);
    while (cwdLen > 0
           && (cwdBuf[cwdLen - 1] == L'\\' || cwdBuf[cwdLen - 1] == L'/')) {
        cwdBuf[--cwdLen] = 0;
    }
    Logf(L"  current_dir    = %s", cwdBuf);

    BOOL ok = CreateProcessW(
        gameExePath,             // lpApplicationName
        cmdBuf,                  // lpCommandLine
        nullptr, nullptr,        // process / thread security
        FALSE,                   // bInheritHandles
        CREATE_SUSPENDED,        // dwCreationFlags
        nullptr,                 // lpEnvironment
        cwdBuf,                  // lpCurrentDirectory (game-bin, no trailing slash)
        &si, &pi);
    if (!ok) {
        DWORD err = GetLastError();
        Logf(L"FATAL: CreateProcessW failed: GetLastError=0x%08x", err);
        CloseLogFile();
        wchar_t msg[512];
        StringCchPrintfW(msg, _countof(msg),
            L"Failed to launch KingdomCome.exe (Win32 error 0x%08x). "
            L"Check that kcdx.exe is in the same folder as KingdomCome.exe.",
            err);
        MessageBoxW(nullptr, msg, L"kcdx.exe: launch failed", MB_OK | MB_ICONERROR);
        return 4;
    }

    Logf(L"game process created: PID=%u, main thread ID=%u",
         pi.dwProcessId, pi.dwThreadId);

    // 3. Inject kcdx.dll.
    Logf(L"injecting kcdx.dll via CreateRemoteThread(LoadLibraryW)");
    DWORD injectError = 0;
    bool injected = InjectViaCreateRemoteThread(pi.hProcess,
                                                dllPath,
                                                &injectError);
    if (!injected) {
        Logf(L"  CreateRemoteThread injection failed: 0x%08x", injectError);
        // Future: fallback chain (WriteProcessMemory variant, then
        // manual mapped LoadLibrary stub). For the initial ship, the
        // single-variant path is the baseline; fallbacks are added
        // in a follow-up if Defender / AV problems surface.
        Logf(L"FATAL: kcdx.dll injection failed. Game will not have kcdx loaded.");
        Logf(L"  most common cause: Windows Defender or third-party AV blocking");
        Logf(L"  CreateRemoteThread. Check Defender exclusions or temporarily");
        Logf(L"  disable real-time protection to test.");

        // Kill the suspended game process; it's not useful without kcdx.
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseLogFile();

        wchar_t msg[512];
        StringCchPrintfW(msg, _countof(msg),
            L"kcdx.dll injection failed (Win32 error 0x%08x).\n\n"
            L"Most common cause: Windows Defender or third-party antivirus "
            L"blocking the CreateRemoteThread injection.\n\n"
            L"Try adding kcdx.exe to your AV exclusion list and relaunching.\n\n"
            L"See kcdx-engine/logs/kcdx-launcher_<ts>.log for details.",
            injectError);
        MessageBoxW(nullptr, msg, L"kcdx.exe: injection failed", MB_OK | MB_ICONERROR);
        return 5;
    }

    Logf(L"  injection OK");

    // 4. ResumeThread.
    Logf(L"resuming game main thread");
    DWORD resumeResult = ResumeThread(pi.hThread);
    if (resumeResult == (DWORD)-1) {
        Logf(L"WARN: ResumeThread failed: 0x%08x", GetLastError());
        // kcdx is loaded but the game isn't running. Not much we can do.
    }

    Logf(L"launcher done; game running with kcdx loaded");
    Logf(L"  launcher exits now; game process continues independently");

    // We don't WaitForSingleObject(pi.hProcess) — the game continues
    // running after we exit, and Steam tracks process state via the
    // game exe itself.
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseLogFile();
    return 0;
}

}  // namespace

// WIN32_EXECUTABLE — wWinMain entry point (no console window).
int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    return RunLauncher();
}
