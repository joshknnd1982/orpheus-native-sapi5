#pragma once

// Persistent per-voice settings for the Orpheus Native SAPI5 voices.
//
// Stored as a plain INI file under %APPDATA%\OrpheusNativeSAPI, so the 32-bit
// interface, the 64-bit interface and the configuration utility all share one
// copy without any registry involvement.  The SAPI DLLs re-read it whenever
// its timestamp changes, which is what makes a change in the configuration
// utility audible on the very next utterance.
//
// Sections are keyed by country code and voice slot ("[voice.00044.0]") rather
// than by the voice's display name: the Win32 profile API writes ANSI unless
// the file is UTF-16, and two voice names contain non-ASCII characters.

#include <windows.h>
#include <string>

namespace Orpheus {
namespace settings {

// "The user has not chosen a value for this." Used for the three attributes
// that live in the engine's own voice table: the engine ships per-voice values
// there - Cantonese's intonation is 80, not 50 - and installing this wrapper
// must not flatten them to one set of numbers. Nothing is written to the voice
// table until the user actually changes something.
constexpr int UNSET = -1000000;

struct VoiceSettings {
    // Inline engine parameters, applied to every utterance.
    int rate = 160;          // 10..700 wpm; SAPI's -10..10 modulates around this
    int pitch = 0;           // 50..500 Hz, or 0 to keep the voice's own pitch
    int volume = 100;        // 0..100 %
    int spelling = 0;        // 0..15, 0 = off
    int skim = 0;            // 0..8
    int pause = 10;          // 0..100
    int word_pause = 0;      // 0..1000 ms
    int phrase_pause = 250;  // 0..2000 ms
    int bass_lift = 0;       // -100..100 %
    int high_lift = 0;       // -100..100 %
    int exceptions = 1;      // 0/1
    int anomalies = 1;       // 0/1

    // Held in the engine's own voice table, not sent per utterance.
    // UNSET means "leave whatever the engine shipped for this voice".
    int intonation = UNSET;  // 0..100
    int head_size = UNSET;   // -100..100
    int voicing = UNSET;     // 0..100
};

[[nodiscard]] VoiceSettings clamp(const VoiceSettings& value);

// %APPDATA%\OrpheusNativeSAPI (created on demand); empty on failure.
[[nodiscard]] std::wstring settings_dir();
[[nodiscard]] std::wstring settings_path();

[[nodiscard]] VoiceSettings load_voice(int country, int slot);
bool save_voice(int country, int slot, const VoiceSettings& value);

// Copy one voice's settings onto every voice in the catalog.
bool apply_to_all_voices(const VoiceSettings& value);

[[nodiscard]] bool logging_enabled();
bool set_logging_enabled(bool enabled);

// Remembered by the configuration utility so it reopens where you left off.
[[nodiscard]] int last_voice_index();
bool set_last_voice_index(int index);

[[nodiscard]] std::wstring test_text();
bool set_test_text(const std::wstring& text);

// True when the settings file changed since the caller's last check.
// `last_write` is caller-owned state, zero-initialised before the first call.
bool changed_since(FILETIME& last_write);

}
}
