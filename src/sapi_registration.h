#pragma once

// Registering and - just as importantly - completely unregistering the
// Orpheus Native voices with SAPI 5.
//
// SAPI's voice list is one shared, machine-wide registry key. An installer
// that leaves a broken entry there is not leaving a mess of its own: it is
// handing every other speech engine on the machine a voice that cannot be
// created, and clients that remember their voice by token path (NVDA does)
// then fail to start their SAPI5 synthesiser at all.
//
// So the removal path is written to be independently correct, and it is driven
// by test/registry_test.cpp against a sandbox key rather than being assumed to
// work. Everything is parameterised by `root` for exactly that reason.

#include <windows.h>
#include <string>

namespace Orpheus {
namespace sapi {

// "SOFTWARE\Microsoft\Speech\Voices"
extern const wchar_t* const VOICES_PATH;
// "SOFTWARE\Microsoft\Speech\Voices\TokenEnums"
extern const wchar_t* const TOKEN_ENUMS_PATH;
// Our subkey under TokenEnums.
extern const wchar_t* const TOKEN_ENUMS_NAME;

// Create root\TokenEnums\OrpheusNative naming `clsid`.
void register_token_enumerator(HKEY root, const std::wstring& clsid, REGSAM view = 0);

// Remove root\TokenEnums\OrpheusNative and nothing else. Returns true when
// the key is gone afterwards, including when it never existed. Never throws,
// and never touches the TokenEnums key itself or any other vendor's subkey.
[[nodiscard]] bool remove_token_enumerator(HKEY root, REGSAM view = 0) noexcept;

// If the default-voice pointer names one of our tokens, remove the value.
// Leaving it behind points SAPI at a voice that no longer exists.
// Returns true when a stale pointer was cleared.
bool clear_default_voice_if_ours(HKEY root, REGSAM view = 0) noexcept;

}
}
