#include "settings.h"

#include <shlobj.h>
#include <cstdio>

#include "orpheus_protocol.h"
#include "voice_catalog.hpp"

namespace Orpheus {
namespace settings {

namespace {

constexpr wchar_t SECTION_DIAG[] = L"diagnostics";
constexpr wchar_t SECTION_UI[] = L"ui";

int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

void section_name(int country, int slot, wchar_t (&buffer)[32])
{
    swprintf_s(buffer, L"voice.%05d.%d", country, slot);
}

bool write_int(const wchar_t* section, const wchar_t* name, int value,
               const std::wstring& path)
{
    wchar_t buffer[32];
    swprintf_s(buffer, L"%d", value);
    return WritePrivateProfileStringW(section, name, buffer, path.c_str()) != FALSE;
}

int read_int(const wchar_t* section, const wchar_t* name, int fallback,
             const std::wstring& path)
{
    // GetPrivateProfileIntW cannot express a negative value, so the two lift
    // settings are read as text.
    wchar_t buffer[32] = {};
    GetPrivateProfileStringW(section, name, L"", buffer, 32, path.c_str());
    if (buffer[0] == L'\0') {
        return fallback;
    }
    wchar_t* end = nullptr;
    const long value = wcstol(buffer, &end, 10);
    if (end == buffer) {
        return fallback;
    }
    return static_cast<int>(value);
}

}

VoiceSettings clamp(const VoiceSettings& value)
{
    using namespace protocol;
    VoiceSettings r = value;
    r.rate = clamp_int(r.rate, RATE_MIN, RATE_MAX);
    r.pitch = (r.pitch == 0) ? 0 : clamp_int(r.pitch, PITCH_MIN, PITCH_MAX);
    r.volume = clamp_int(r.volume, VOLUME_MIN, VOLUME_MAX);
    r.spelling = clamp_int(r.spelling, SPELLING_MIN, SPELLING_MAX);
    r.skim = clamp_int(r.skim, SKIM_MIN, SKIM_MAX);
    r.pause = clamp_int(r.pause, PAUSE_MIN, PAUSE_MAX);
    r.word_pause = clamp_int(r.word_pause, WORD_PAUSE_MIN, WORD_PAUSE_MAX);
    r.phrase_pause = clamp_int(r.phrase_pause, PHRASE_PAUSE_MIN, PHRASE_PAUSE_MAX);
    r.bass_lift = clamp_int(r.bass_lift, LIFT_MIN, LIFT_MAX);
    r.high_lift = clamp_int(r.high_lift, LIFT_MIN, LIFT_MAX);
    r.exceptions = clamp_int(r.exceptions, 0, 1);
    r.anomalies = clamp_int(r.anomalies, 0, 1);
    r.intonation = clamp_int(r.intonation, INTONATION_MIN, INTONATION_MAX);
    r.head_size = clamp_int(r.head_size, HEAD_SIZE_MIN, HEAD_SIZE_MAX);
    r.voicing = clamp_int(r.voicing, VOICING_MIN, VOICING_MAX);
    return r;
}

std::wstring settings_dir()
{
    wchar_t appdata[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT,
                                appdata))) {
        return std::wstring();
    }
    std::wstring dir = std::wstring(appdata) + L"\\OrpheusNativeSAPI";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring settings_path()
{
    const std::wstring dir = settings_dir();
    if (dir.empty()) {
        return std::wstring();
    }
    return dir + L"\\settings.ini";
}

VoiceSettings load_voice(int country, int slot)
{
    const VoiceSettings defaults;
    const std::wstring path = settings_path();
    if (path.empty()) {
        return defaults;
    }
    wchar_t section[32];
    section_name(country, slot, section);

    VoiceSettings v;
    v.rate = read_int(section, L"rate", defaults.rate, path);
    v.pitch = read_int(section, L"pitch", defaults.pitch, path);
    v.volume = read_int(section, L"volume", defaults.volume, path);
    v.spelling = read_int(section, L"spelling", defaults.spelling, path);
    v.skim = read_int(section, L"skim", defaults.skim, path);
    v.pause = read_int(section, L"pause", defaults.pause, path);
    v.word_pause = read_int(section, L"wordPause", defaults.word_pause, path);
    v.phrase_pause = read_int(section, L"phrasePause", defaults.phrase_pause, path);
    v.bass_lift = read_int(section, L"bassLift", defaults.bass_lift, path);
    v.high_lift = read_int(section, L"highLift", defaults.high_lift, path);
    v.exceptions = read_int(section, L"exceptions", defaults.exceptions, path);
    v.anomalies = read_int(section, L"anomalies", defaults.anomalies, path);
    v.intonation = read_int(section, L"intonation", defaults.intonation, path);
    v.head_size = read_int(section, L"headSize", defaults.head_size, path);
    v.voicing = read_int(section, L"voicing", defaults.voicing, path);
    return clamp(v);
}

bool save_voice(int country, int slot, const VoiceSettings& raw)
{
    const std::wstring path = settings_path();
    if (path.empty()) {
        return false;
    }
    wchar_t section[32];
    section_name(country, slot, section);
    const VoiceSettings v = clamp(raw);

    bool ok = true;
    ok &= write_int(section, L"rate", v.rate, path);
    ok &= write_int(section, L"pitch", v.pitch, path);
    ok &= write_int(section, L"volume", v.volume, path);
    ok &= write_int(section, L"spelling", v.spelling, path);
    ok &= write_int(section, L"skim", v.skim, path);
    ok &= write_int(section, L"pause", v.pause, path);
    ok &= write_int(section, L"wordPause", v.word_pause, path);
    ok &= write_int(section, L"phrasePause", v.phrase_pause, path);
    ok &= write_int(section, L"bassLift", v.bass_lift, path);
    ok &= write_int(section, L"highLift", v.high_lift, path);
    ok &= write_int(section, L"exceptions", v.exceptions, path);
    ok &= write_int(section, L"anomalies", v.anomalies, path);
    ok &= write_int(section, L"intonation", v.intonation, path);
    ok &= write_int(section, L"headSize", v.head_size, path);
    ok &= write_int(section, L"voicing", v.voicing, path);
    return ok;
}

bool apply_to_all_voices(const VoiceSettings& value)
{
    bool ok = true;
    for (int i = 0; i < sapi::orpheus_voice_count; ++i) {
        const sapi::voice_entry& voice = sapi::orpheus_voices[i];
        ok &= save_voice(voice.country, voice.slot, value);
    }
    return ok;
}

bool logging_enabled()
{
    const std::wstring path = settings_path();
    if (path.empty()) {
        return true;
    }
    return GetPrivateProfileIntW(SECTION_DIAG, L"logging", 1, path.c_str()) != 0;
}

bool set_logging_enabled(bool enabled)
{
    const std::wstring path = settings_path();
    if (path.empty()) {
        return false;
    }
    return write_int(SECTION_DIAG, L"logging", enabled ? 1 : 0, path);
}

int last_voice_index()
{
    const std::wstring path = settings_path();
    if (path.empty()) {
        return 0;
    }
    const int index = static_cast<int>(
        GetPrivateProfileIntW(SECTION_UI, L"lastVoice", 20, path.c_str()));
    if (index < 0 || index >= sapi::orpheus_voice_count) {
        return 0;
    }
    return index;
}

bool set_last_voice_index(int index)
{
    const std::wstring path = settings_path();
    if (path.empty()) {
        return false;
    }
    return write_int(SECTION_UI, L"lastVoice", index, path);
}

std::wstring test_text()
{
    const std::wstring path = settings_path();
    const wchar_t* fallback = L"The quick brown fox jumps over the lazy dog.";
    if (path.empty()) {
        return fallback;
    }
    wchar_t buffer[512] = {};
    GetPrivateProfileStringW(SECTION_UI, L"testText", fallback, buffer, 512, path.c_str());
    return buffer[0] ? std::wstring(buffer) : std::wstring(fallback);
}

bool set_test_text(const std::wstring& text)
{
    const std::wstring path = settings_path();
    if (path.empty()) {
        return false;
    }
    return WritePrivateProfileStringW(SECTION_UI, L"testText", text.c_str(),
                                      path.c_str()) != FALSE;
}

bool changed_since(FILETIME& last_write)
{
    const std::wstring path = settings_path();
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (path.empty() ||
        !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        // A missing file counts as "changed" once, so defaults get applied.
        const bool first = (last_write.dwLowDateTime == 0 && last_write.dwHighDateTime == 0);
        last_write.dwLowDateTime = 1;
        last_write.dwHighDateTime = 0;
        return first;
    }
    if (CompareFileTime(&data.ftLastWriteTime, &last_write) != 0) {
        last_write = data.ftLastWriteTime;
        return true;
    }
    return false;
}

}
}
