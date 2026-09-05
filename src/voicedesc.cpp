#include "voicedesc.h"

#include <cstdio>
#include <cstring>

#include "debug_log.h"

namespace Orpheus {
namespace voicedesc {

namespace {

constexpr size_t RECORD_SIZE = 284;
constexpr size_t OFF_PATH = 0x14;
constexpr size_t OFF_DEFAULT_PITCH = 0xF4;
constexpr size_t OFF_INTONATION = 0xF8;
constexpr size_t OFF_HEAD_SIZE = 0x104;
constexpr size_t OFF_VOICING = 0x108;

const wchar_t* const KEY_INSTALLED =
    L"Software\\Dolphin\\Orpheus210\\Voices\\Installed";
const wchar_t* const KEY_USER_DEFINED =
    L"Software\\Dolphin\\Orpheus210\\Voices\\User Defined";
const wchar_t* const VALUE_NAME = L"VoiceDesc";

// The engine is 32-bit.  HKCU\Software is not WOW64-redirected, so this flag
// is a no-op here, but it keeps us in the same view the engine asks for.
constexpr REGSAM VIEW = KEY_WOW64_32KEY;

[[nodiscard]] int clamp_int(int value, int lo, int hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

[[nodiscard]] bool load_table(const wchar_t* key_path, std::vector<BYTE>& out)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key_path, 0, KEY_QUERY_VALUE | VIEW, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD size = 0;
    LONG result = RegQueryValueExW(key, VALUE_NAME, nullptr, &type, nullptr, &size);
    if (result != ERROR_SUCCESS || type != REG_BINARY || size < RECORD_SIZE) {
        RegCloseKey(key);
        return false;
    }
    out.resize(size);
    result = RegQueryValueExW(key, VALUE_NAME, nullptr, &type, out.data(), &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        out.clear();
        return false;
    }
    out.resize(size);
    return true;
}

[[nodiscard]] bool store_table(const wchar_t* key_path, const std::vector<BYTE>& data)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key_path, 0, KEY_SET_VALUE | VIEW, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    const LONG result = RegSetValueExW(key, VALUE_NAME, 0, REG_BINARY, data.data(),
                                       static_cast<DWORD>(data.size()));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

// The path field is a UTF-16 string embedded in the record; read it back
// without assuming it is NUL-terminated within the record.
[[nodiscard]] std::wstring record_path(const std::vector<BYTE>& table, size_t offset)
{
    std::wstring path;
    for (size_t pos = offset + OFF_PATH; pos + 1 < offset + RECORD_SIZE; pos += 2) {
        wchar_t ch = 0;
        memcpy(&ch, table.data() + pos, sizeof(ch));
        if (ch == 0) {
            break;
        }
        path.push_back(ch);
        if (path.size() > 64) {
            break;
        }
    }
    return path;
}

// Offset of the record for one language/voice slot, or SIZE_MAX.
[[nodiscard]] size_t find_record(const std::vector<BYTE>& table, int country, int slot)
{
    wchar_t wanted[32];
    swprintf_s(wanted, L"%05d\\synth%s.vcx", country, slot ? L"2" : L"");
    const size_t count = table.size() / RECORD_SIZE;
    for (size_t i = 0; i < count; ++i) {
        const size_t offset = i * RECORD_SIZE;
        if (_wcsicmp(record_path(table, offset).c_str(), wanted) == 0) {
            return offset;
        }
    }
    return SIZE_MAX;
}

[[nodiscard]] int read_i32(const std::vector<BYTE>& table, size_t offset)
{
    int32_t value = 0;
    memcpy(&value, table.data() + offset, sizeof(value));
    return static_cast<int>(value);
}

void write_i32(std::vector<BYTE>& table, size_t offset, int value)
{
    const int32_t stored = static_cast<int32_t>(value);
    memcpy(table.data() + offset, &stored, sizeof(stored));
}

}

bool available()
{
    std::vector<BYTE> table;
    return load_table(KEY_INSTALLED, table) && table.size() >= RECORD_SIZE;
}

bool read(int country, int slot, Attributes& out)
{
    std::vector<BYTE> table;
    if (!load_table(KEY_INSTALLED, table) && !load_table(KEY_USER_DEFINED, table)) {
        return false;
    }
    const size_t offset = find_record(table, country, slot);
    if (offset == SIZE_MAX) {
        return false;
    }
    out.intonation = clamp_int(read_i32(table, offset + OFF_INTONATION), 0, 100);
    out.head_size = clamp_int(read_i32(table, offset + OFF_HEAD_SIZE), -100, 100);
    out.voicing = clamp_int(read_i32(table, offset + OFF_VOICING), 0, 100);
    out.default_pitch = clamp_int(read_i32(table, offset + OFF_DEFAULT_PITCH), 50, 500);
    return true;
}

bool matches(int country, int slot, const Attributes& value)
{
    Attributes stored;
    if (!read(country, slot, stored)) {
        return false;
    }
    return stored.intonation == clamp_int(value.intonation, 0, 100) &&
           stored.head_size == clamp_int(value.head_size, -100, 100) &&
           stored.voicing == clamp_int(value.voicing, 0, 100);
}

bool write(int country, int slot, const Attributes& value)
{
    bool wrote_any = false;
    for (const wchar_t* key_path : { KEY_INSTALLED, KEY_USER_DEFINED }) {
        std::vector<BYTE> table;
        if (!load_table(key_path, table)) {
            continue;
        }
        const size_t offset = find_record(table, country, slot);
        if (offset == SIZE_MAX) {
            continue;
        }
        write_i32(table, offset + OFF_INTONATION, clamp_int(value.intonation, 0, 100));
        write_i32(table, offset + OFF_HEAD_SIZE, clamp_int(value.head_size, -100, 100));
        write_i32(table, offset + OFF_VOICING, clamp_int(value.voicing, 0, 100));
        if (store_table(key_path, table)) {
            wrote_any = true;
        }
    }
    if (!wrote_any) {
        ORPHEUS_LOG("VoiceDesc: no record for country %d slot %d", country, slot);
    }
    return wrote_any;
}

}
}
