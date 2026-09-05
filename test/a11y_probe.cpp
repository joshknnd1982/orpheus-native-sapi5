// Reads the live configuration dialog back through MSAA.
//
// A screen reader user cannot use a control that has no accessible name or
// cannot be reached with Tab, and two controls sharing an access key means one
// of them is unreachable by keyboard shortcut. None of that is visible by
// looking at the .rc file, so this walks the real window.
//
// MSAA (oleacc) rather than UI Automation on purpose: the stock UIA client
// reports every classic Win32 control as a generic "Pane", which makes it
// useless for checking that an edit box is announced as an edit box.
//
// Usage: a11y_probe.exe <path-to-OrpheusNativeConfig.exe>

#define INITGUID
#include <windows.h>
#include <oleacc.h>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "oleacc.lib")

namespace {

int g_failures = 0;

void fail(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    printf("  FAIL: ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
    ++g_failures;
}

struct FoundWindow {
    DWORD pid;
    HWND hwnd;
};

BOOL CALLBACK find_dialog(HWND hwnd, LPARAM param)
{
    auto* found = reinterpret_cast<FoundWindow*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != found->pid || !IsWindowVisible(hwnd)) {
        return TRUE;
    }
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, 64);
    if (wcscmp(cls, L"#32770") == 0) { // dialog
        found->hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

std::wstring acc_name(IAccessible* acc, VARIANT child)
{
    BSTR name = nullptr;
    std::wstring out;
    if (SUCCEEDED(acc->get_accName(child, &name)) && name) {
        out = name;
        SysFreeString(name);
    }
    return out;
}

long acc_role(IAccessible* acc, VARIANT child)
{
    VARIANT role;
    VariantInit(&role);
    long value = 0;
    if (SUCCEEDED(acc->get_accRole(child, &role)) && role.vt == VT_I4) {
        value = role.lVal;
    }
    VariantClear(&role);
    return value;
}

const char* role_name(long role)
{
    switch (role) {
    case ROLE_SYSTEM_PUSHBUTTON: return "button";
    case ROLE_SYSTEM_CHECKBUTTON: return "check box";
    case ROLE_SYSTEM_COMBOBOX: return "combo box";
    case ROLE_SYSTEM_TEXT: return "edit";
    case ROLE_SYSTEM_STATICTEXT: return "static text";
    case ROLE_SYSTEM_SPINBUTTON: return "spin button";
    case ROLE_SYSTEM_CLIENT: return "client";
    case ROLE_SYSTEM_WINDOW: return "window";
    default: return "other";
    }
}

// The character after '&' in a label, lower-cased; 0 when there is none.
wchar_t access_key(const std::wstring& text)
{
    for (size_t i = 0; i + 1 < text.size(); ++i) {
        if (text[i] == L'&' && text[i + 1] != L'&') {
            return towlower(text[i + 1]);
        }
    }
    return 0;
}

}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) {
        printf("usage: a11y_probe <config exe>\n");
        return 1;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = argv[1];
    if (!CreateProcessW(argv[1], cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                        &si, &pi)) {
        printf("FAIL: cannot start %ls (error %lu)\n", argv[1], GetLastError());
        return 1;
    }
    WaitForInputIdle(pi.hProcess, 10000);

    FoundWindow found = { pi.dwProcessId, nullptr };
    for (int attempt = 0; attempt < 50 && !found.hwnd; ++attempt) {
        EnumWindows(find_dialog, reinterpret_cast<LPARAM>(&found));
        if (!found.hwnd) {
            Sleep(100);
        }
    }
    if (!found.hwnd) {
        printf("FAIL: the configuration dialog never appeared\n");
        TerminateProcess(pi.hProcess, 0);
        return 1;
    }

    wchar_t caption[256] = {};
    GetWindowTextW(found.hwnd, caption, 256);
    printf("dialog: \"%ls\"\n\n", caption);

    // Walk the child windows in Z-order, which for a dialog is tab order.
    std::map<wchar_t, std::wstring> keys;
    int tab_stops = 0;
    int controls = 0;
    std::wstring last_label;

    for (HWND child = GetWindow(found.hwnd, GW_CHILD); child;
         child = GetWindow(child, GW_HWNDNEXT)) {
        wchar_t cls[64] = {};
        GetClassNameW(child, cls, 64);
        wchar_t text[256] = {};
        GetWindowTextW(child, text, 256);
        const LONG style = GetWindowLongW(child, GWL_STYLE);
        const bool is_tab_stop = (style & WS_TABSTOP) != 0;
        const bool is_static = (_wcsicmp(cls, L"Static") == 0);

        IAccessible* acc = nullptr;
        if (FAILED(AccessibleObjectFromWindow(child, OBJID_CLIENT, IID_IAccessible,
                                              reinterpret_cast<void**>(&acc))) ||
            !acc) {
            printf("  [%-14ls] no IAccessible\n", cls);
            ++g_failures;
            continue;
        }
        VARIANT self;
        self.vt = VT_I4;
        self.lVal = CHILDID_SELF;
        const std::wstring name = acc_name(acc, self);
        const long role = acc_role(acc, self);
        acc->Release();

        ++controls;
        if (is_tab_stop) {
            ++tab_stops;
        }
        printf("  %-14ls %-12s tab=%d name=\"%ls\"\n", cls, role_name(role),
               is_tab_stop ? 1 : 0, name.c_str());

        // Every interactive control must report a name. Windows derives an
        // edit box's name from the static text immediately before it, which is
        // exactly why the .rc puts each label directly ahead of its control.
        if (is_tab_stop && name.empty()) {
            fail("control of class %ls is a tab stop with no accessible name", cls);
        }

        const wchar_t key = access_key(is_static ? text : text);
        if (key) {
            auto it = keys.find(key);
            if (it != keys.end()) {
                fail("access key '%lc' is used twice: \"%ls\" and \"%ls\"", key,
                     it->second.c_str(), text);
            } else {
                keys[key] = text;
            }
        }
        if (is_static) {
            last_label = text;
        }
    }

    printf("\n%d controls, %d tab stops, %d distinct access keys\n",
           controls, tab_stops, static_cast<int>(keys.size()));

    if (tab_stops < 18) {
        fail("only %d tab stops; every setting should be reachable with Tab", tab_stops);
    }

    PostMessageW(found.hwnd, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(pi.hProcess, 5000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 0);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    printf(g_failures ? "RESULT: %d failure(s)\n" : "RESULT: all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
