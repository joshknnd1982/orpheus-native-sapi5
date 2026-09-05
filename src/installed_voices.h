#pragma once

// Which languages and voices Setup actually installed.
//
// The wizard lets the user pick languages, and the second voice of each
// language separately, so the shipped voice list is a subset of the catalog.
// Setup records the choice in {app}\voices.ini:
//
//     [languages]
//     00001=2      ; both voice slots installed
//     00044=2
//     00049=1      ; first voice only
//                  ; a language that is absent was not installed at all
//
// The SAPI interfaces and the configuration utility read it so the Windows
// voice list only ever offers voices whose data files are present.
//
// If the file is missing - a developer build, or a tree copied by hand - every
// voice in the catalog counts as installed.

#include <string>

namespace Orpheus {
namespace voices {

// Directory holding orpheus-native-host.exe and orpheus\; empty if not found.
[[nodiscard]] std::wstring install_root();

// True when this language and voice slot were installed.
[[nodiscard]] bool installed(int country, int slot);

// Catalog index of the nth installed voice, or -1.  Used to map the position
// in a filtered list back to the catalog.
[[nodiscard]] int installed_voice_at(int position);

// How many catalog voices are installed.
[[nodiscard]] int installed_count();

}
}
