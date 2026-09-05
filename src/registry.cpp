#include <vector>
#include "registry.hpp"

namespace Orpheus {
namespace registry {

std::wstring key::get(const std::wstring& name) const
{
    DWORD type = 0;
    DWORD size = 0;

    if (RegQueryValueExW(handle_, name.c_str(), nullptr, &type, nullptr, &size) == ERROR_SUCCESS) {
        if (type == REG_SZ) {
            std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
            if (RegQueryValueExW(handle_, name.c_str(), nullptr, &type,
                                  reinterpret_cast<BYTE*>(buffer.data()), &size) == ERROR_SUCCESS) {
                if (type == REG_SZ) {
                    return std::wstring(buffer.data());
                }
            }
        }
    }

    throw error("Unable to read a value from the registry");
}

void key::set(const std::wstring& name, const std::wstring& value)
{
    const DWORD size = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    if (RegSetValueExW(handle_, name.c_str(), 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(value.c_str()), size) != ERROR_SUCCESS) {
        throw error("Unable to write a value in the registry");
    }
}

bool delete_subtree(HKEY parent, const std::wstring& name, REGSAM view) noexcept
{
    // Refuse to be pointed at the parent itself; see the header.
    if (name.empty()) {
        return false;
    }

    HKEY handle = nullptr;
    LONG result = RegOpenKeyExW(parent, name.c_str(), 0, DELETE_ACCESS | view, &handle);
    if (result == ERROR_FILE_NOT_FOUND) {
        return true; // already gone
    }
    if (result != ERROR_SUCCESS) {
        return false;
    }

    // Empty the key first, then remove the (now childless) key itself.
    result = RegDeleteTreeW(handle, nullptr);
    RegCloseKey(handle);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
        return false;
    }

    HKEY parent_handle = nullptr;
    if (RegOpenKeyExW(parent, L"", 0, DELETE_ACCESS | view, &parent_handle) == ERROR_SUCCESS) {
        result = RegDeleteKeyExW(parent_handle, name.c_str(), view, 0);
        RegCloseKey(parent_handle);
    } else {
        result = RegDeleteKeyExW(parent, name.c_str(), view, 0);
    }
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

bool key_exists(HKEY parent, const std::wstring& name, REGSAM view) noexcept
{
    HKEY handle = nullptr;
    if (RegOpenKeyExW(parent, name.c_str(), 0, KEY_READ | view, &handle) != ERROR_SUCCESS) {
        return false;
    }
    RegCloseKey(handle);
    return true;
}

}
}
