#include "debug_log.h"

#include <windows.h>
#include <share.h>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string>

#include "settings.h"

namespace Orpheus {
namespace logging {

namespace {

constexpr long MAX_LOG_BYTES = 10 * 1024 * 1024;
constexpr ULONGLONG ENABLED_RECHECK_MS = 2000;

CRITICAL_SECTION g_cs;
FILE* g_file = nullptr;
bool g_init_done = false;
bool g_open_failed = false;
bool g_enabled = true;
ULONGLONG g_enabled_checked_at = 0;

void delete_old_logs(const std::wstring& log_dir)
{
    WIN32_FIND_DATAW find_data;
    HANDLE find = FindFirstFileW((log_dir + L"\\*.log").c_str(), &find_data);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    FILETIME now_ft;
    GetSystemTimeAsFileTime(&now_ft);
    ULARGE_INTEGER now;
    now.LowPart = now_ft.dwLowDateTime;
    now.HighPart = now_ft.dwHighDateTime;
    const ULONGLONG week = 7ull * 24 * 60 * 60 * 10000000ull;
    do {
        ULARGE_INTEGER written;
        written.LowPart = find_data.ftLastWriteTime.dwLowDateTime;
        written.HighPart = find_data.ftLastWriteTime.dwHighDateTime;
        if (now.QuadPart > written.QuadPart && now.QuadPart - written.QuadPart > week) {
            DeleteFileW((log_dir + L"\\" + find_data.cFileName).c_str());
        }
    } while (FindNextFileW(find, &find_data));
    FindClose(find);
}

void ensure_open()
{
    if (g_file || g_open_failed) {
        return;
    }
    const std::wstring dir = settings::settings_dir();
    if (dir.empty()) {
        g_open_failed = true;
        return;
    }
    const std::wstring log_dir = dir + L"\\logs";
    CreateDirectoryW(log_dir.c_str(), nullptr);
    delete_old_logs(log_dir);

#ifdef _WIN64
    const wchar_t* arch = L"x64";
#else
    const wchar_t* arch = L"x86";
#endif
    wchar_t exe_path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    const wchar_t* exe_name = wcsrchr(exe_path, L'\\');
    exe_name = exe_name ? exe_name + 1 : exe_path;
    std::wstring base_name = exe_name;
    const size_t dot = base_name.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        base_name.resize(dot);
    }

    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\%s_%s_%lu.log", log_dir.c_str(), base_name.c_str(), arch,
               GetCurrentProcessId());
    g_file = _wfsopen(path, L"a", _SH_DENYNO);
    if (!g_file) {
        g_open_failed = true;
    }
}

struct CsInit {
    CsInit() { InitializeCriticalSection(&g_cs); }
};
CsInit g_cs_init;

}

void log(const char* format, ...)
{
    EnterCriticalSection(&g_cs);

    const ULONGLONG now = GetTickCount64();
    if (!g_init_done || now - g_enabled_checked_at > ENABLED_RECHECK_MS) {
        g_enabled = settings::logging_enabled();
        g_enabled_checked_at = now;
        g_init_done = true;
    }
    if (!g_enabled) {
        LeaveCriticalSection(&g_cs);
        return;
    }

    ensure_open();
    if (g_file) {
        if (ftell(g_file) < MAX_LOG_BYTES) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            fprintf(g_file, "[%04u-%02u-%02u %02u:%02u:%02u.%03u] [%05lu] ",
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                    st.wMilliseconds, GetCurrentThreadId());

            va_list args;
            va_start(args, format);
            vfprintf(g_file, format, args);
            va_end(args);

            fputc('\n', g_file);
            fflush(g_file);
        }
    }
    LeaveCriticalSection(&g_cs);
}

}
}
