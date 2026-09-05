#pragma once

// Wire protocol of orpheus-native-host.exe (Dolphin Orpheus 2.10, "OA3").
//
// Transport: the client opens a TCP listener on 127.0.0.1 and spawns the host
// with "--address 127.0.0.1:<port>"; the host connects back.  Every frame is
// <u32 length><payload>.  Command payloads are
// <u8 FRAME_COMMAND><u32 msg_id><u16 command_id><data>, responses are
// <u8 FRAME_RESPONSE><u32 msg_id><u32 status><data> and events are
// <u8 FRAME_EVENT><u16 event_id><data>.
//
// The host is a 32-bit process, so 32-bit and 64-bit callers use exactly the
// same client code - the loopback socket is the architecture boundary.

#include <stdint.h>

namespace Orpheus {
namespace protocol {

constexpr uint8_t FRAME_COMMAND = 1;
constexpr uint8_t FRAME_RESPONSE = 2;
constexpr uint8_t FRAME_EVENT = 3;

constexpr uint16_t CMD_INITIALIZE = 1;   // u32 len + utf-8 path of the orpheus data dir
constexpr uint16_t CMD_APPEND = 2;       // u32 params_len, u32 text_len, params, utf-16le text
constexpr uint16_t CMD_SPEAK_APPEND = 3; // no data; starts rendering appended text
constexpr uint16_t CMD_MUTE = 4;         // u32 value (3 = stop speech)
constexpr uint16_t CMD_CONFIG = 5;
constexpr uint16_t CMD_GET_PARAMS = 6;   // -> u32 count + {i32 min,max,current + str name}
constexpr uint16_t CMD_GET_LANGS = 7;    // -> u32 count + {u32 country + str name}
constexpr uint16_t CMD_GET_VOICES = 8;   // u32 country -> u32 count + {str name}
constexpr uint16_t CMD_CLOSE = 9;

constexpr uint16_t EV_AUDIO = 1;         // u32 audio_len, pcm, u32 controls_len, {u32 pos,type,value}*

constexpr uint32_t STATUS_OK = 0;

// A control value with this bit set marks the end of the utterance.
constexpr uint32_t CONTROL_FINAL_FLAG = 0x80000000u;

// Parameter ids, as reported by CMD_GET_PARAMS on a full 25-language install.
// A parameter is applied at a character offset into the appended text, so the
// same id can change value part-way through an utterance.
//
// Every id below was verified by rendering the same sentence at its low and
// high values and comparing the audio, except where noted.
constexpr int PARAM_RATE = 0;           // 10..700 wpm
constexpr int PARAM_PITCH = 1;          // 50..500 Hz; default is per-voice
constexpr int PARAM_SPELLING = 2;       // 0..15, 0 = off
constexpr int PARAM_SKIM = 3;           // 0..8 skim reading level
constexpr int PARAM_PAUSE = 4;          // 0..100 general pause scaling
constexpr int PARAM_WORD_PAUSE = 5;     // 0..1000 ms
constexpr int PARAM_PHRASE_PAUSE = 6;   // 0..2000 ms
constexpr int PARAM_VOICE = 7;          // voice slot within the language, 0..63
constexpr int PARAM_VOLUME = 8;         // 0..100 %
constexpr int PARAM_BALANCE = 9;        // -100..100; inert, the engine renders mono
constexpr int PARAM_BASS_LIFT = 10;     // -100..100 %
constexpr int PARAM_HIGH_LIFT = 11;     // -100..100 %
constexpr int PARAM_LANGUAGE = 12;      // index into the CMD_GET_LANGS list, not a country code
constexpr int PARAM_SOUND_EFFECT = 13;  // 0..30, index into orpheus\sfx
constexpr int PARAM_INDEX = 14;         // index marker (only the final one is delivered)
constexpr int PARAM_EXCEPTIONS = 15;    // 0/1, pronunciation exceptions from orpheus\settings\*.exc
constexpr int PARAM_ANOMALIES = 16;     // 0/1; no audible effect found in testing

constexpr int PARAM_COUNT = 17;

// Ranges, mirroring CMD_GET_PARAMS.  Kept here so the configuration utility
// and the engine agree without having to spawn a host.
constexpr int RATE_MIN = 10, RATE_MAX = 700, RATE_DEFAULT = 160;
constexpr int PITCH_MIN = 50, PITCH_MAX = 500;
constexpr int SPELLING_MIN = 0, SPELLING_MAX = 15;
constexpr int SKIM_MIN = 0, SKIM_MAX = 8;
constexpr int PAUSE_MIN = 0, PAUSE_MAX = 100, PAUSE_DEFAULT = 10;
constexpr int WORD_PAUSE_MIN = 0, WORD_PAUSE_MAX = 1000;
constexpr int PHRASE_PAUSE_MIN = 0, PHRASE_PAUSE_MAX = 2000, PHRASE_PAUSE_DEFAULT = 250;
constexpr int VOLUME_MIN = 0, VOLUME_MAX = 100;
constexpr int LIFT_MIN = -100, LIFT_MAX = 100;
constexpr int SOUND_EFFECT_MIN = 0, SOUND_EFFECT_MAX = 30;

// Per-voice attributes.  These do not exist as parameters: they live in the
// engine's own voice table (see voicedesc.h) and are read when a host starts.
constexpr int INTONATION_MIN = 0, INTONATION_MAX = 100, INTONATION_DEFAULT = 50;
constexpr int HEAD_SIZE_MIN = -100, HEAD_SIZE_MAX = 100, HEAD_SIZE_DEFAULT = 0;
constexpr int VOICING_MIN = 0, VOICING_MAX = 100, VOICING_DEFAULT = 100;

// The host streams 16-bit PCM at 22050 Hz, one channel.  (Orpheus Classic
// emitted duplicated stereo; this engine does not - do not downmix.)
constexpr uint32_t HOST_SAMPLE_RATE = 22050;
constexpr uint32_t HOST_CHANNELS = 1;
constexpr uint32_t HOST_BITS = 16;

// One parameter application: `value` takes effect at character `offset` of the
// appended text.  Sent as four little-endian 32-bit words; `value` is signed
// two's complement, which is how negative lifts and the 0x80000000 final
// marker both travel over the same field.
struct EngineParam {
    uint32_t offset;
    uint32_t type;
    int32_t id;
    int32_t value;
};

}
}
