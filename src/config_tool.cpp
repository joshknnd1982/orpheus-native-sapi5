// Configuration utility for the Orpheus Native SAPI5 voices.
//
// Edits the per-voice settings in %APPDATA%\OrpheusNativeSAPI\settings.ini.
// Every change is saved as it is made and the SAPI engine re-reads the file
// whenever its timestamp moves, so adjustments take effect on the next
// utterance, including under a running screen reader.
//
// All controls are labelled, reachable with the Tab key and carry distinct
// access keys.  Numeric fields use edit boxes with spin buttons rather than
// trackbars on purpose: MSAA reports a trackbar's position as a percentage of
// its range, so a 0-to-8 slider sitting on 5 is announced as "62".

#include <windows.h>
#include <commctrl.h>
#include <sapi.h>
#include <string>
#include <vector>

#include "config_resource.h"
#include "version.h"
#include "installed_voices.h"
#include "orpheus_protocol.h"
#include "settings.h"
#include "voice_catalog.hpp"
#include "voicedesc.h"
#include "debug_log.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

using Orpheus::sapi::orpheus_voice_count;
using Orpheus::sapi::orpheus_voices;
namespace settings = Orpheus::settings;
namespace protocol = Orpheus::protocol;

constexpr wchar_t APP_TITLE[] = L"Orpheus Native Configuration";

// EN_CHANGE fires while the dialog is still being created - spin buddies set
// their buddy's text during creation, before WM_INITDIALOG - so the guard
// starts out true and is only cleared at the end of WM_INITDIALOG.  Without
// that, the dialog saves garbage over the user's settings on every launch.
bool g_loading = true;

ISpVoice* g_voice = nullptr;

// Catalog index of the voice being edited, and the catalog indices behind the
// drop-down list.  The two differ whenever Setup installed a subset.
int g_voice_index = 0;
std::vector<int> g_listed;

// Position of a catalog index within the drop-down list, or -1.
int list_position_of(int catalog_index)
{
    for (size_t i = 0; i < g_listed.size(); ++i) {
        if (g_listed[i] == catalog_index) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

struct SpinBinding {
    int edit_id;
    int spin_id;
    int min_value;
    int max_value;
};

const SpinBinding SPINS[] = {
    { IDC_RATE,        IDC_RATE_SPIN,        protocol::RATE_MIN,         protocol::RATE_MAX },
    // 0 is allowed as "keep the voice's own pitch", hence the lower bound.
    { IDC_PITCH,       IDC_PITCH_SPIN,       0,                          protocol::PITCH_MAX },
    { IDC_VOLUME,      IDC_VOLUME_SPIN,      protocol::VOLUME_MIN,       protocol::VOLUME_MAX },
    { IDC_INTONATION,  IDC_INTONATION_SPIN,  protocol::INTONATION_MIN,   protocol::INTONATION_MAX },
    { IDC_HEADSIZE,    IDC_HEADSIZE_SPIN,    protocol::HEAD_SIZE_MIN,    protocol::HEAD_SIZE_MAX },
    { IDC_VOICING,     IDC_VOICING_SPIN,     protocol::VOICING_MIN,      protocol::VOICING_MAX },
    { IDC_SKIM,        IDC_SKIM_SPIN,        protocol::SKIM_MIN,         protocol::SKIM_MAX },
    { IDC_PAUSE,       IDC_PAUSE_SPIN,       protocol::PAUSE_MIN,        protocol::PAUSE_MAX },
    { IDC_WORDPAUSE,   IDC_WORDPAUSE_SPIN,   protocol::WORD_PAUSE_MIN,   protocol::WORD_PAUSE_MAX },
    { IDC_PHRASEPAUSE, IDC_PHRASEPAUSE_SPIN, protocol::PHRASE_PAUSE_MIN, protocol::PHRASE_PAUSE_MAX },
    { IDC_BASSLIFT,    IDC_BASSLIFT_SPIN,    protocol::LIFT_MIN,         protocol::LIFT_MAX },
    { IDC_HIGHLIFT,    IDC_HIGHLIFT_SPIN,    protocol::LIFT_MIN,         protocol::LIFT_MAX },
    { IDC_SPELLING,    IDC_SPELLING_SPIN,    protocol::SPELLING_MIN,     protocol::SPELLING_MAX },
};
constexpr int SPIN_COUNT = sizeof(SPINS) / sizeof(SPINS[0]);

int get_edit_int(HWND dialog, int control_id)
{
    // GetDlgItemInt refuses a lone "-" and an empty box; treat both as 0 so a
    // half-typed negative number does not throw the field back at the user.
    BOOL translated = FALSE;
    const int value = static_cast<int>(GetDlgItemInt(dialog, control_id, &translated, TRUE));
    return translated ? value : 0;
}

void load_settings_into_dialog(HWND dialog)
{
    const bool was_loading = g_loading;
    g_loading = true;

    const Orpheus::sapi::voice_entry& voice = orpheus_voices[g_voice_index];
    settings::VoiceSettings v = settings::load_voice(voice.country, voice.slot);

    // Intonation, head size and voicing default to "not set", meaning the
    // engine's own per-voice value is in force. Show that value rather than a
    // blanket default, so the boxes tell the truth about what will be spoken.
    if (v.intonation == settings::UNSET || v.head_size == settings::UNSET ||
        v.voicing == settings::UNSET) {
        Orpheus::voicedesc::Attributes stored;
        const bool have = Orpheus::voicedesc::read(voice.country, voice.slot, stored);
        if (v.intonation == settings::UNSET) {
            v.intonation = have ? stored.intonation : protocol::INTONATION_DEFAULT;
        }
        if (v.head_size == settings::UNSET) {
            v.head_size = have ? stored.head_size : protocol::HEAD_SIZE_DEFAULT;
        }
        if (v.voicing == settings::UNSET) {
            v.voicing = have ? stored.voicing : protocol::VOICING_DEFAULT;
        }
    }

    SendDlgItemMessageW(dialog, IDC_VOICE, CB_SETCURSEL, list_position_of(g_voice_index), 0);
    SetDlgItemInt(dialog, IDC_RATE, v.rate, TRUE);
    SetDlgItemInt(dialog, IDC_PITCH, v.pitch, TRUE);
    SetDlgItemInt(dialog, IDC_VOLUME, v.volume, TRUE);
    SetDlgItemInt(dialog, IDC_INTONATION, v.intonation, TRUE);
    SetDlgItemInt(dialog, IDC_HEADSIZE, v.head_size, TRUE);
    SetDlgItemInt(dialog, IDC_VOICING, v.voicing, TRUE);
    SetDlgItemInt(dialog, IDC_SKIM, v.skim, TRUE);
    SetDlgItemInt(dialog, IDC_PAUSE, v.pause, TRUE);
    SetDlgItemInt(dialog, IDC_WORDPAUSE, v.word_pause, TRUE);
    SetDlgItemInt(dialog, IDC_PHRASEPAUSE, v.phrase_pause, TRUE);
    SetDlgItemInt(dialog, IDC_BASSLIFT, v.bass_lift, TRUE);
    SetDlgItemInt(dialog, IDC_HIGHLIFT, v.high_lift, TRUE);
    SetDlgItemInt(dialog, IDC_SPELLING, v.spelling, TRUE);
    CheckDlgButton(dialog, IDC_EXCEPTIONS, v.exceptions ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog, IDC_ANOMALIES, v.anomalies ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog, IDC_LOGGING,
                   settings::logging_enabled() ? BST_CHECKED : BST_UNCHECKED);

    g_loading = was_loading;
}

settings::VoiceSettings read_dialog(HWND dialog)
{
    settings::VoiceSettings v;
    v.rate = get_edit_int(dialog, IDC_RATE);
    v.pitch = get_edit_int(dialog, IDC_PITCH);
    v.volume = get_edit_int(dialog, IDC_VOLUME);
    v.intonation = get_edit_int(dialog, IDC_INTONATION);
    v.head_size = get_edit_int(dialog, IDC_HEADSIZE);
    v.voicing = get_edit_int(dialog, IDC_VOICING);
    v.skim = get_edit_int(dialog, IDC_SKIM);
    v.pause = get_edit_int(dialog, IDC_PAUSE);
    v.word_pause = get_edit_int(dialog, IDC_WORDPAUSE);
    v.phrase_pause = get_edit_int(dialog, IDC_PHRASEPAUSE);
    v.bass_lift = get_edit_int(dialog, IDC_BASSLIFT);
    v.high_lift = get_edit_int(dialog, IDC_HIGHLIFT);
    v.spelling = get_edit_int(dialog, IDC_SPELLING);
    v.exceptions = IsDlgButtonChecked(dialog, IDC_EXCEPTIONS) == BST_CHECKED ? 1 : 0;
    v.anomalies = IsDlgButtonChecked(dialog, IDC_ANOMALIES) == BST_CHECKED ? 1 : 0;
    return v;
}

void save_settings_from_dialog(HWND dialog)
{
    const Orpheus::sapi::voice_entry& voice = orpheus_voices[g_voice_index];
    settings::save_voice(voice.country, voice.slot, read_dialog(dialog));
    settings::set_logging_enabled(IsDlgButtonChecked(dialog, IDC_LOGGING) == BST_CHECKED);
}

void clamp_edit(HWND dialog, int control_id)
{
    for (int i = 0; i < SPIN_COUNT; ++i) {
        const SpinBinding& spin = SPINS[i];
        if (spin.edit_id != control_id) {
            continue;
        }
        const int value = get_edit_int(dialog, control_id);
        int clamped = value;
        if (clamped < spin.min_value) clamped = spin.min_value;
        if (clamped > spin.max_value) clamped = spin.max_value;
        if (clamped != value) {
            SetDlgItemInt(dialog, control_id, clamped, TRUE);
        }
        return;
    }
}

bool find_voice_token(const wchar_t* name, ISpObjectToken** token_out)
{
    *token_out = nullptr;
    ISpObjectTokenCategory* category = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_ISpObjectTokenCategory,
                                reinterpret_cast<void**>(&category)))) {
        return false;
    }
    bool found = false;
    if (SUCCEEDED(category->SetId(SPCAT_VOICES, FALSE))) {
        IEnumSpObjectTokens* tokens = nullptr;
        const std::wstring required = std::wstring(L"Name=") + name;
        if (SUCCEEDED(category->EnumTokens(required.c_str(), nullptr, &tokens)) && tokens) {
            ISpObjectToken* token = nullptr;
            ULONG fetched = 0;
            if (tokens->Next(1, &token, &fetched) == S_OK && fetched == 1) {
                *token_out = token;
                found = true;
            }
            tokens->Release();
        }
    }
    category->Release();
    return found;
}

void speak_test(HWND dialog)
{
    if (!g_voice) {
        if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_ISpVoice, reinterpret_cast<void**>(&g_voice)))) {
            MessageBoxW(dialog, L"Could not create a SAPI voice object.", APP_TITLE,
                        MB_OK | MB_ICONERROR);
            return;
        }
    }

    const wchar_t* name = orpheus_voices[g_voice_index].token_name;
    ISpObjectToken* token = nullptr;
    if (!find_voice_token(name, &token)) {
        MessageBoxW(dialog,
                    L"That voice is not registered with SAPI yet. "
                    L"Reinstall or repair Orpheus Native SAPI5 and try again.",
                    APP_TITLE, MB_OK | MB_ICONWARNING);
        return;
    }
    g_voice->SetVoice(token);
    token->Release();

    wchar_t text[512] = {};
    GetDlgItemTextW(dialog, IDC_TESTTEXT, text, 512);
    if (!text[0]) {
        wcscpy_s(text, L"The quick brown fox jumps over the lazy dog.");
    }
    settings::set_test_text(text);

    const HRESULT hr =
        g_voice->Speak(text, SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML, nullptr);
    if (FAILED(hr)) {
        MessageBoxW(dialog, L"The test speech request failed.", APP_TITLE,
                    MB_OK | MB_ICONERROR);
    }
}

INT_PTR CALLBACK dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM /*lparam*/)
{
    switch (message) {
    case WM_INITDIALOG: {
        // Only list voices Setup installed, and remember which catalog entry
        // each row stands for.
        g_listed.clear();
        for (int i = 0; i < orpheus_voice_count; ++i) {
            const Orpheus::sapi::voice_entry& voice = orpheus_voices[i];
            if (!Orpheus::voices::installed(voice.country, voice.slot)) {
                continue;
            }
            g_listed.push_back(i);
            SendDlgItemMessageW(dialog, IDC_VOICE, CB_ADDSTRING, 0,
                                reinterpret_cast<LPARAM>(voice.token_name));
        }
        for (int i = 0; i < SPIN_COUNT; ++i) {
            SendDlgItemMessageW(dialog, SPINS[i].spin_id, UDM_SETRANGE32,
                                SPINS[i].min_value, SPINS[i].max_value);
        }
        SendDlgItemMessageW(dialog, IDC_TESTTEXT, EM_SETLIMITTEXT, 500, 0);
        SetDlgItemTextW(dialog, IDC_TESTTEXT, settings::test_text().c_str());

        g_voice_index = settings::last_voice_index();
        // The remembered voice may not be one of the installed ones - after a
        // reinstall with fewer languages, for instance.
        if (list_position_of(g_voice_index) < 0) {
            g_voice_index = g_listed.empty() ? 0 : g_listed[0];
        }
        load_settings_into_dialog(dialog);
        g_loading = false;
        return TRUE;
    }
    case WM_COMMAND: {
        const int control_id = LOWORD(wparam);
        const int notification = HIWORD(wparam);
        switch (control_id) {
        case IDOK:
        case IDCANCEL:
            EndDialog(dialog, 0);
            return TRUE;

        case IDC_VOICE:
            if (notification == CBN_SELCHANGE && !g_loading) {
                const int row =
                    static_cast<int>(SendDlgItemMessageW(dialog, IDC_VOICE, CB_GETCURSEL, 0, 0));
                if (row >= 0 && row < static_cast<int>(g_listed.size())) {
                    g_voice_index = g_listed[row];
                    settings::set_last_voice_index(g_voice_index);
                    load_settings_into_dialog(dialog);
                }
                return TRUE;
            }
            break;

        case IDC_TEST:
            if (notification == BN_CLICKED) {
                speak_test(dialog);
                return TRUE;
            }
            break;

        case IDC_APPLYALL:
            if (notification == BN_CLICKED) {
                const settings::VoiceSettings v = read_dialog(dialog);
                settings::apply_to_all_voices(v);
                wchar_t message[128];
                swprintf_s(message,
                           L"These settings now apply to all %d installed Orpheus voices.",
                           Orpheus::voices::installed_count());
                MessageBoxW(dialog, message, APP_TITLE, MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            break;

        case IDC_DEFAULTS:
            if (notification == BN_CLICKED) {
                const Orpheus::sapi::voice_entry& voice = orpheus_voices[g_voice_index];
                settings::save_voice(voice.country, voice.slot, settings::VoiceSettings());
                settings::set_logging_enabled(true);
                load_settings_into_dialog(dialog);
                return TRUE;
            }
            break;

        case IDC_EXCEPTIONS:
        case IDC_ANOMALIES:
        case IDC_LOGGING:
            if (notification == BN_CLICKED && !g_loading) {
                save_settings_from_dialog(dialog);
                return TRUE;
            }
            break;

        case IDC_RATE:
        case IDC_PITCH:
        case IDC_VOLUME:
        case IDC_INTONATION:
        case IDC_HEADSIZE:
        case IDC_VOICING:
        case IDC_SKIM:
        case IDC_PAUSE:
        case IDC_WORDPAUSE:
        case IDC_PHRASEPAUSE:
        case IDC_BASSLIFT:
        case IDC_HIGHLIFT:
        case IDC_SPELLING:
            if (notification == EN_CHANGE && !g_loading) {
                save_settings_from_dialog(dialog);
                return TRUE;
            }
            if (notification == EN_KILLFOCUS && !g_loading) {
                clamp_edit(dialog, control_id);
                save_settings_from_dialog(dialog);
                return TRUE;
            }
            break;

        case IDC_TESTTEXT:
            if (notification == EN_KILLFOCUS && !g_loading) {
                wchar_t text[512] = {};
                GetDlgItemTextW(dialog, IDC_TESTTEXT, text, 512);
                settings::set_test_text(text);
                return TRUE;
            }
            break;

        default:
            break;
        }
        break;
    }
    case WM_CLOSE:
        EndDialog(dialog, 0);
        return TRUE;
    default:
        break;
    }
    return FALSE;
}

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE /*prev*/, LPWSTR /*cmdline*/, int /*show*/)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ORPHEUS_LOG("Config: utility started, version " ORPHEUS_VERSION_STRING);
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_CONFIG), nullptr, dialog_proc, 0);
    ORPHEUS_LOG("Config: utility closed");

    if (g_voice) {
        g_voice->Release();
        g_voice = nullptr;
    }
    CoUninitialize();
    return 0;
}
