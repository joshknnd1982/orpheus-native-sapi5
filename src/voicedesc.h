#pragma once

// Read and write the Orpheus engine's own voice table.
//
// Intonation, head size and voicing are not engine *parameters* - there is no
// id for them in CMD_GET_PARAMS.  They live in a binary blob the engine keeps
// at HKCU\Software\Dolphin\Orpheus210\Voices\{Installed,User Defined} under
// the value "VoiceDesc", and the engine reads them when a host starts.  So
// changing one means editing that blob and recycling the host processes.
//
// This is not a dependency on a pre-existing Dolphin installation: the engine
// builds the whole table from the Language\ folders the first time it runs, on
// a machine with no Dolphin registry keys at all (verified by deleting the
// branch and re-enumerating).  We only ever edit a table the engine authored.
//
// Record layout, 284 bytes each, recovered by decoding a live table:
//   0x00 u8   voice slot within the language (0 = synth.vcx, 1 = synth2.vcx)
//   0x01 u8   0x02, record-valid marker
//   0x02 u32  country code
//   0x14      UTF-16LE NUL-terminated relative path, e.g. "00044\synth.vcx"
//   0x68      UTF-16LE NUL-terminated short name, 0xA8 display name
//   0xF4 i32  default pitch in Hz (Synthetic Dave 100, Synthetic Andy 135)
//   0xF8 i32  intonation   0..100
//   0x104 i32 head size    -100..100
//   0x108 i32 voicing      0..100

#include <windows.h>
#include <string>
#include <vector>

namespace Orpheus {
namespace voicedesc {

struct Attributes {
    int intonation = 50;
    int head_size = 0;
    int voicing = 100;
    int default_pitch = 100;
};

// True when the engine's table exists and holds at least one record.
[[nodiscard]] bool available();

// Read one voice's attributes.  Returns false when the table or the record is
// missing; `out` is left at its defaults in that case.
[[nodiscard]] bool read(int country, int slot, Attributes& out);

// Write intonation, head size and voicing for one voice into both the
// "Installed" and "User Defined" tables (the engine consults both).  The
// default pitch field is never written - it is the voice's identity.
// Returns false if no record matched.
[[nodiscard]] bool write(int country, int slot, const Attributes& value);

// True when the stored record already holds exactly these three values, so
// callers can avoid a pointless write and host restart.
[[nodiscard]] bool matches(int country, int slot, const Attributes& value);

}
}
