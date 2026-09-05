#include "sapi_registration.h"

#include <vector>

#include "registry.hpp"

namespace Orpheus {
namespace sapi {

const wchar_t* const VOICES_PATH = L"SOFTWARE\\Microsoft\\Speech\\Voices";
const wchar_t* const TOKEN_ENUMS_PATH = L"SOFTWARE\\Microsoft\\Speech\\Voices\\TokenEnums";
const wchar_t* const TOKEN_ENUMS_NAME = L"OrpheusNative";

namespace {

[[nodiscard]] bool contains_ci(const std::wstring& haystack, const wchar_t* needle)
{
    const size_t n = wcslen(needle);
    if (n == 0 || haystack.size() < n) {
        return false;
    }
    for (size_t i = 0; i + n <= haystack.size(); ++i) {
        size_t j = 0;
        while (j < n && towlower(haystack[i + j]) == towlower(needle[j])) {
            ++j;
        }
        if (j == n) {
            return true;
        }
    }
    return false;
}

}

void register_token_enumerator(HKEY root, const std::wstring& clsid, REGSAM view)
{
    using namespace Orpheus::registry;

    key enums_key(root, TOKEN_ENUMS_PATH, KEY_CREATE_SUB_KEY | KEY_SET_VALUE | view, true);
    key enum_key(enums_key, TOKEN_ENUMS_NAME, KEY_SET_VALUE | view, true);

    enum_key.set(L"Orpheus Native Voices");
    enum_key.set(L"CLSID", clsid);
}

bool remove_token_enumerator(HKEY root, REGSAM view) noexcept
{
    HKEY enums = nullptr;
    const LONG opened = RegOpenKeyExW(root, TOKEN_ENUMS_PATH, 0,
                                      registry::DELETE_ACCESS | view, &enums);
    if (opened == ERROR_FILE_NOT_FOUND) {
        return true; // nothing of ours can be there
    }
    if (opened != ERROR_SUCCESS) {
        return false;
    }
    // Only our own subkey, by name. The TokenEnums key itself is shared with
    // every other engine that registers an enumerator and is never removed.
    const bool ok = registry::delete_subtree(enums, TOKEN_ENUMS_NAME, view);
    RegCloseKey(enums);
    return ok;
}

bool clear_default_voice_if_ours(HKEY root, REGSAM view) noexcept
{
    HKEY voices = nullptr;
    if (RegOpenKeyExW(root, VOICES_PATH, 0, KEY_QUERY_VALUE | KEY_SET_VALUE | view, &voices) !=
        ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD size = 0;
    bool cleared = false;
    if (RegQueryValueExW(voices, L"DefaultTokenId", nullptr, &type, nullptr, &size) ==
            ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && size >= sizeof(wchar_t)) {
        std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
        if (RegQueryValueExW(voices, L"DefaultTokenId", nullptr, &type,
                             reinterpret_cast<BYTE*>(buffer.data()), &size) == ERROR_SUCCESS) {
            const std::wstring value(buffer.data());
            if (contains_ci(value, L"TokenEnums\\OrpheusNative") ||
                contains_ci(value, L"TokenEnums/OrpheusNative")) {
                cleared = RegDeleteValueW(voices, L"DefaultTokenId") == ERROR_SUCCESS;
            }
        }
    }
    RegCloseKey(voices);
    return cleared;
}

}
}
