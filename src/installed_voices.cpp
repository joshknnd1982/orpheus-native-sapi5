#include "installed_voices.h"

#include <windows.h>
#include <shlwapi.h>
#include <cstdio>
#include <vector>

#include "voice_catalog.hpp"

#pragma comment(lib, "shlwapi.lib")

namespace Orpheus {
namespace voices {

namespace {

// Slots installed per country, indexed the same as the catalog's language
// table; -1 until the manifest has been read.
struct Manifest {
    bool loaded = false;
    bool have_file = false;
    int slots[sapi::orpheus_language_count] = {};
};

Manifest g_manifest;
CRITICAL_SECTION* g_guard = nullptr;
INIT_ONCE g_guard_once = INIT_ONCE_STATIC_INIT;

BOOL CALLBACK init_guard(PINIT_ONCE, PVOID, PVOID*)
{
    g_guard = new CRITICAL_SECTION();
    InitializeCriticalSection(g_guard);
    return TRUE;
}

CRITICAL_SECTION* guard()
{
    InitOnceExecuteOnce(&g_guard_once, init_guard, nullptr, nullptr);
    return g_guard;
}

HMODULE current_module()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&current_module), &module);
    return module;
}

[[nodiscard]] bool looks_like_install_root(const std::wstring& base)
{
    const std::wstring exe = base + L"\\orpheus-native-host.exe";
    const std::wstring data = base + L"\\orpheus";
    const DWORD data_attributes = GetFileAttributesW(data.c_str());
    return GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES &&
           data_attributes != INVALID_FILE_ATTRIBUTES &&
           (data_attributes & FILE_ATTRIBUTE_DIRECTORY);
}

void load_manifest_locked()
{
    if (g_manifest.loaded) {
        return;
    }
    g_manifest.loaded = true;

    const std::wstring root = install_root();
    if (root.empty()) {
        return;
    }
    const std::wstring path = root + L"\\voices.ini";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return; // no manifest: treat the whole catalog as installed
    }
    g_manifest.have_file = true;

    for (int i = 0; i < sapi::orpheus_language_count; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"%05d", sapi::orpheus_languages[i].country);
        g_manifest.slots[i] =
            static_cast<int>(GetPrivateProfileIntW(L"languages", key, 0, path.c_str()));
    }
}

[[nodiscard]] int language_slot_count(int country)
{
    EnterCriticalSection(guard());
    load_manifest_locked();
    const bool have_file = g_manifest.have_file;
    int slots = 2;
    if (have_file) {
        slots = 0;
        for (int i = 0; i < sapi::orpheus_language_count; ++i) {
            if (sapi::orpheus_languages[i].country == country) {
                slots = g_manifest.slots[i];
                break;
            }
        }
    }
    LeaveCriticalSection(guard());
    return slots;
}

}

std::wstring install_root()
{
    wchar_t env_home[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"ORPHEUS_NATIVE_SAPI_HOME", env_home, MAX_PATH) > 0) {
        if (looks_like_install_root(env_home)) {
            return env_home;
        }
    }

    wchar_t module_path[MAX_PATH] = {};
    if (HMODULE module = current_module()) {
        if (GetModuleFileNameW(module, module_path, MAX_PATH) > 0) {
            PathRemoveFileSpecW(module_path);
            if (looks_like_install_root(module_path)) {
                return module_path;
            }
            // The 64-bit interface lives in {app}\x64.
            PathRemoveFileSpecW(module_path);
            if (looks_like_install_root(module_path)) {
                return module_path;
            }
        }
    }
    return std::wstring();
}

bool installed(int country, int slot)
{
    if (slot < 0) {
        return false;
    }
    return slot < language_slot_count(country);
}

int installed_voice_at(int position)
{
    if (position < 0) {
        return -1;
    }
    int seen = 0;
    for (int i = 0; i < sapi::orpheus_voice_count; ++i) {
        const sapi::voice_entry& voice = sapi::orpheus_voices[i];
        if (!installed(voice.country, voice.slot)) {
            continue;
        }
        if (seen == position) {
            return i;
        }
        ++seen;
    }
    return -1;
}

int installed_count()
{
    int total = 0;
    for (int i = 0; i < sapi::orpheus_voice_count; ++i) {
        const sapi::voice_entry& voice = sapi::orpheus_voices[i];
        if (installed(voice.country, voice.slot)) {
            ++total;
        }
    }
    return total;
}

}
}
