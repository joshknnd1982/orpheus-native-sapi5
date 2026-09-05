// Drives the product's own registration and de-registration code against a
// sandbox key under HKCU.
//
// This test exists because of a specific, shipped bug in a sibling project:
// DllUnregisterServer removed voice tokens with RegDeleteKeyW, which refuses
// any key that still has subkeys.  Every SAPI voice token has an Attributes
// subkey, so the delete always failed, the failure was swallowed by a
// catch (...), and uninstalling left dead voices in the machine's shared SAPI
// voice list - which stops other engines' voices working too.  The SAPI test
// harness missed it entirely because that harness cleaned up with a helper of
// its own: the test tidied up correctly while the product did not.
//
// So: call the real functions, and check the real outcomes.
//
// Usage: registry_test.exe        (exit code 0 on success)

#include <windows.h>
#include <cstdio>
#include <string>

#include "registry.hpp"
#include "sapi_registration.h"

namespace {

const wchar_t* const SANDBOX = L"Software\\OrpheusNativeSAPI\\registry_test";

int g_failures = 0;

void ok(bool condition, const char* what)
{
    printf("  %-62s %s\n", what, condition ? "ok" : "FAILED");
    if (!condition) {
        ++g_failures;
    }
}

[[nodiscard]] bool key_exists(HKEY root, const std::wstring& path)
{
    HKEY handle = nullptr;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &handle) != ERROR_SUCCESS) {
        return false;
    }
    RegCloseKey(handle);
    return true;
}

bool create_key(HKEY root, const std::wstring& path)
{
    HKEY handle = nullptr;
    const LONG result = RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0,
                                        KEY_ALL_ACCESS, nullptr, &handle, nullptr);
    if (result != ERROR_SUCCESS) {
        return false;
    }
    RegCloseKey(handle);
    return true;
}

bool set_value(HKEY root, const std::wstring& path, const wchar_t* name,
               const std::wstring& value)
{
    HKEY handle = nullptr;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr,
                        &handle, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const LONG result = RegSetValueExW(
        handle, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(handle);
    return result == ERROR_SUCCESS;
}

[[nodiscard]] bool value_exists(HKEY root, const std::wstring& path, const wchar_t* name)
{
    HKEY handle = nullptr;
    if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &handle) != ERROR_SUCCESS) {
        return false;
    }
    const LONG result = RegQueryValueExW(handle, name, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(handle);
    return result == ERROR_SUCCESS;
}

void cleanup()
{
    HKEY software = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\OrpheusNativeSAPI", 0,
                      Orpheus::registry::DELETE_ACCESS, &software) == ERROR_SUCCESS) {
        RegDeleteTreeW(software, nullptr);
        RegCloseKey(software);
    }
    HKEY cu = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software", 0,
                      Orpheus::registry::DELETE_ACCESS, &cu) == ERROR_SUCCESS) {
        RegDeleteKeyExW(cu, L"OrpheusNativeSAPI", 0, 0);
        RegCloseKey(cu);
    }
}

// The sandbox mirrors the real layout: SANDBOX plays the part of a registry
// root, so the code under test builds "<SANDBOX>\SOFTWARE\Microsoft\..." keys.
[[nodiscard]] HKEY open_sandbox_root()
{
    HKEY handle = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SANDBOX, 0, nullptr, 0, KEY_ALL_ACCESS,
                        nullptr, &handle, nullptr) != ERROR_SUCCESS) {
        return nullptr;
    }
    return handle;
}

}

int main()
{
    using namespace Orpheus;
    setvbuf(stdout, nullptr, _IONBF, 0);
    cleanup();

    const std::wstring enums_path = std::wstring(SANDBOX) + L"\\" + sapi::TOKEN_ENUMS_PATH;
    const std::wstring ours_path = enums_path + L"\\" + sapi::TOKEN_ENUMS_NAME;
    const std::wstring other_path = enums_path + L"\\SomeOtherVendor";
    const std::wstring voices_path = std::wstring(SANDBOX) + L"\\" + sapi::VOICES_PATH;

    HKEY root = open_sandbox_root();
    if (!root) {
        printf("FAIL: cannot create the sandbox key\n");
        return 1;
    }

    printf("delete_subtree\n");
    {
        // A key with children: RegDeleteKeyW refuses this shape, which is
        // exactly the shape every SAPI voice token has.
        create_key(HKEY_CURRENT_USER, std::wstring(SANDBOX) + L"\\parent\\child\\grandchild");
        set_value(HKEY_CURRENT_USER, std::wstring(SANDBOX) + L"\\parent\\child", L"v", L"x");
        create_key(HKEY_CURRENT_USER, std::wstring(SANDBOX) + L"\\sibling");

        ok(RegDeleteKeyW(root, L"parent") != ERROR_SUCCESS,
           "RegDeleteKeyW refuses a key with subkeys (the trap)");
        ok(registry::delete_subtree(root, L"parent"),
           "delete_subtree removes a key that has subkeys");
        ok(!key_exists(HKEY_CURRENT_USER, std::wstring(SANDBOX) + L"\\parent"),
           "the subtree is really gone");
        ok(key_exists(HKEY_CURRENT_USER, std::wstring(SANDBOX) + L"\\sibling"),
           "a sibling key is untouched");
        ok(registry::delete_subtree(root, L"parent"),
           "deleting an absent key reports success");
        ok(!registry::delete_subtree(root, L""),
           "an empty name is refused (it would empty the parent)");
        ok(key_exists(HKEY_CURRENT_USER, std::wstring(SANDBOX) + L"\\sibling"),
           "...and the refusal really left the parent alone");
    }

    printf("register / remove token enumerator\n");
    {
        create_key(HKEY_CURRENT_USER, other_path);
        set_value(HKEY_CURRENT_USER, other_path, L"CLSID", L"{00000000-0000-0000-0000-000000000000}");

        sapi::register_token_enumerator(root, L"{e7077968-c442-45ac-bf26-9c5b3648be5a}");
        ok(key_exists(HKEY_CURRENT_USER, ours_path), "registration creates our key");
        ok(value_exists(HKEY_CURRENT_USER, ours_path, L"CLSID"), "...with a CLSID value");

        // A voice token always has an Attributes subkey; make sure removal
        // copes with one.
        create_key(HKEY_CURRENT_USER, ours_path + L"\\Attributes");

        ok(sapi::remove_token_enumerator(root), "removal reports success");
        ok(!key_exists(HKEY_CURRENT_USER, ours_path), "our key is gone");
        ok(key_exists(HKEY_CURRENT_USER, enums_path),
           "the shared TokenEnums key survives");
        ok(key_exists(HKEY_CURRENT_USER, other_path),
           "another vendor's enumerator survives");
        ok(value_exists(HKEY_CURRENT_USER, other_path, L"CLSID"),
           "...with its value intact");
        ok(sapi::remove_token_enumerator(root),
           "removing again is harmless");
    }

    printf("default voice pointer\n");
    {
        set_value(HKEY_CURRENT_USER, voices_path, L"DefaultTokenId",
                  L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices\\TokenEnums\\"
                  L"OrpheusNative\\Orpheus UK English - Synthetic Dave");
        ok(sapi::clear_default_voice_if_ours(root),
           "a default pointing at one of our voices is cleared");
        ok(!value_exists(HKEY_CURRENT_USER, voices_path, L"DefaultTokenId"),
           "...the value is really gone");

        set_value(HKEY_CURRENT_USER, voices_path, L"DefaultTokenId",
                  L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens\\MSSAM");
        ok(!sapi::clear_default_voice_if_ours(root),
           "another vendor's default voice is left alone");
        ok(value_exists(HKEY_CURRENT_USER, voices_path, L"DefaultTokenId"),
           "...and really is still there");
    }

    RegCloseKey(root);
    cleanup();
    ok(!key_exists(HKEY_CURRENT_USER, SANDBOX), "the sandbox cleaned itself up");

    printf(g_failures ? "\nRESULT: %d failure(s)\n" : "\nRESULT: all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
