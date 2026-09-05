#include "orpheus_client.h" // must come first: winsock2.h before windows.h

#include <new>
#include <sapi.h>

#include "com.hpp"
#include "registry.hpp"
#include "sapi_registration.h"
#include "version.h"
#include "ISpTTSEngineImpl.hpp"
#include "IEnumSpObjectTokensImpl.hpp"
#include "debug_log.h"

namespace {

HINSTANCE g_dll_handle = nullptr;
Orpheus::com::class_object_factory g_cls_obj_factory;

[[nodiscard]] std::wstring clsid_to_string(const GUID& clsid)
{
    wchar_t buf[64];
    StringFromGUID2(clsid, buf, 64);
    return std::wstring(buf);
}

}

BOOL APIENTRY DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID /*lpReserved*/)
{
    if (dwReason == DLL_PROCESS_ATTACH) {
        g_dll_handle = hInstance;
        DisableThreadLibraryCalls(hInstance);

        try {
            g_cls_obj_factory.register_class<Orpheus::sapi::IEnumSpObjectTokensImpl>();
            g_cls_obj_factory.register_class<Orpheus::sapi::ISpTTSEngineImpl>();
        }
        catch (...) {
            return FALSE;
        }
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    return g_cls_obj_factory.create(rclsid, riid, ppv);
}

STDAPI DllCanUnloadNow()
{
    return Orpheus::com::object_counter::is_zero() ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    try {
        Orpheus::com::class_registrar r(g_dll_handle);
        r.register_class<Orpheus::sapi::IEnumSpObjectTokensImpl>();
        r.register_class<Orpheus::sapi::ISpTTSEngineImpl>();
        Orpheus::sapi::register_token_enumerator(
            HKEY_LOCAL_MACHINE,
            clsid_to_string(__uuidof(Orpheus::sapi::IEnumSpObjectTokensImpl)));
        ORPHEUS_LOG("DllRegisterServer: registered, version " ORPHEUS_VERSION_STRING);
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDAPI DllUnregisterServer()
{
    // Every step is independent and none can take the others down with it: a
    // half-finished uninstall that leaves a live CLSID pointing at a deleted
    // DLL is worse than one that leaves a file behind, because the damage
    // lands on the machine's shared SAPI voice list rather than on us.
    HRESULT result = S_OK;

    if (!Orpheus::sapi::remove_token_enumerator(HKEY_LOCAL_MACHINE)) {
        ORPHEUS_LOG("DllUnregisterServer: could not remove the TokenEnums key");
        result = E_FAIL;
    }
    Orpheus::sapi::clear_default_voice_if_ours(HKEY_LOCAL_MACHINE);
    Orpheus::sapi::clear_default_voice_if_ours(HKEY_CURRENT_USER);

    try {
        Orpheus::com::class_registrar r(g_dll_handle);
        r.unregister_class<Orpheus::sapi::IEnumSpObjectTokensImpl>();
    }
    catch (...) {
        ORPHEUS_LOG("DllUnregisterServer: could not remove the enumerator CLSID");
        result = E_FAIL;
    }
    try {
        Orpheus::com::class_registrar r(g_dll_handle);
        r.unregister_class<Orpheus::sapi::ISpTTSEngineImpl>();
    }
    catch (...) {
        ORPHEUS_LOG("DllUnregisterServer: could not remove the engine CLSID");
        result = E_FAIL;
    }

    ORPHEUS_LOG("DllUnregisterServer: %s", SUCCEEDED(result) ? "clean" : "incomplete");
    return result;
}
