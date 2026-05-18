#include "log.h"

#include <windows.h>
#include <shellapi.h>

#include <ctime>
#include <fstream>
#include <mutex>

namespace kcdx::log {
namespace {

std::mutex g_mutex;
std::ofstream g_file;
HANDLE g_consoleOut = nullptr;
bool g_consoleEnabled = false;

bool HasConsoleArg() {
    LPWSTR cmdLine = GetCommandLineW();
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);
    bool found = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"-console") == 0) {
                found = true;
                break;
            }
        }
        LocalFree(argv);
    }
    return found;
}

void Write(const char* level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_mutex);

    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%H:%M:%S", &tm);

    char line[1280];
    int n = snprintf(line, sizeof(line), "[%s][%s] %s\n", ts, level, msg.c_str());
    if (n < 0) return;

    if (g_file.is_open()) {
        g_file.write(line, n);
        g_file.flush();
    }
    if (g_consoleEnabled && g_consoleOut) {
        DWORD written = 0;
        WriteConsoleA(g_consoleOut, line, static_cast<DWORD>(n), &written, nullptr);
    }
}

}  // namespace

void Init(const std::wstring& moduleDir) {
    std::wstring logPath = moduleDir + L"kcdx.log";
    g_file.open(logPath, std::ios_base::out | std::ios_base::trunc);

    if (HasConsoleArg()) {
        g_consoleEnabled = true;
        AllocConsole();
        g_consoleOut = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTitleA("kcdx.asi");
        COORD bufSize{120, 9000};
        SetConsoleScreenBufferSize(g_consoleOut, bufSize);
    }
}

void Info(const std::string& msg)  { Write("INFO",  msg); }
void Warn(const std::string& msg)  { Write("WARN",  msg); }
void Error(const std::string& msg) { Write("ERROR", msg); }
void Debug(const std::string& msg) { Write("DEBUG", msg); }

}  // namespace kcdx::log
