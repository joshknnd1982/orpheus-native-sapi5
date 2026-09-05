#pragma once

// The complete Orpheus Native voice set: 25 languages, 48 voices.
//
// The engine reports its languages through CMD_GET_LANGS as an ordered list,
// and PARAM_LANGUAGE selects one by *index into that list*, not by country
// code.  The order below is that list, verified against the engine on a full
// install, so `lang_index` can be used directly.
//
// Every language ships two voice slots (synth.vcx and synth2.vcx) except
// Chinese Putonghua and Cantonese: their second .vcx files exist but the
// engine refuses to load them and clamps slot 1 back to slot 0, which was
// confirmed by rendering both slots and comparing the audio.

#include <cstddef>

namespace Orpheus {
namespace sapi {

struct language_info {
    int lang_index;         // index into the engine's language list (PARAM_LANGUAGE)
    int country;            // Dolphin country code, and the Language\ folder name
    const wchar_t* name;    // display name used in SAPI token names
    const wchar_t* lcid;    // SAPI "Language" attribute, hex LCID
    const wchar_t* tag;     // BCP-47-ish tag, used in documentation and logs
};

inline constexpr language_info orpheus_languages[] = {
    {  0,     1, L"US English",              L"409", L"en-US" },
    {  1,    30, L"Greek",                   L"408", L"el-GR" },
    {  2,    31, L"Dutch",                   L"413", L"nl-NL" },
    {  3,    33, L"French",                  L"40C", L"fr-FR" },
    {  4,    34, L"Castilian Spanish",       L"C0A", L"es-ES" },
    {  5,    36, L"Hungarian",               L"40E", L"hu-HU" },
    {  6,    38, L"Croatian",                L"41A", L"hr-HR" },
    {  7,    39, L"Italian",                 L"410", L"it-IT" },
    {  8,    40, L"Romanian",                L"418", L"ro-RO" },
    {  9,    42, L"Czech",                   L"405", L"cs-CZ" },
    { 10,    44, L"UK English",              L"809", L"en-GB" },
    { 11,    45, L"Danish",                  L"406", L"da-DK" },
    { 12,    46, L"Swedish",                 L"41D", L"sv-SE" },
    { 13,    47, L"Norwegian",               L"414", L"nb-NO" },
    { 14,    48, L"Polish",                  L"415", L"pl-PL" },
    { 15,    49, L"German",                  L"407", L"de-DE" },
    { 16,    52, L"Latin American Spanish",  L"80A", L"es-MX" },
    { 17,    55, L"Brazilian Portuguese",    L"416", L"pt-BR" },
    { 18,    60, L"Malay",                   L"43E", L"ms-MY" },
    { 19,    86, L"Chinese Putonghua",       L"804", L"zh-CN" },
    { 20,   351, L"Portuguese",              L"816", L"pt-PT" },
    { 21,   358, L"Finnish",                 L"40B", L"fi-FI" },
    { 22,   370, L"Lithuanian",              L"427", L"lt-LT" },
    { 23, 10044, L"Welsh",                   L"452", L"cy-GB" },
    { 24, 10086, L"Cantonese",               L"C04", L"zh-HK" },
};

inline constexpr int orpheus_language_count =
    static_cast<int>(sizeof(orpheus_languages) / sizeof(orpheus_languages[0]));

struct voice_entry {
    const wchar_t* token_name;  // SAPI token name, and the settings.ini section
    const wchar_t* voice_name;  // the engine's own name for this voice
    int lang_index;             // PARAM_LANGUAGE
    int country;
    int slot;                   // PARAM_VOICE, 0 = synth.vcx, 1 = synth2.vcx
    bool female;
};

// Non-ASCII names are written as escapes so the table does not depend on the
// source file's encoding.
inline constexpr voice_entry orpheus_voices[] = {
    { L"Orpheus US English - Synthetic Dave",             L"Synthetic Dave",  0,     1, 0, false },
    { L"Orpheus US English - Synthetic Andy",             L"Synthetic Andy",  0,     1, 1, false },
    { L"Orpheus Greek - Synthetic Dave",                  L"Synthetic Dave",  1,    30, 0, false },
    { L"Orpheus Greek - Synthetic Andy",                  L"Synthetic Andy",  1,    30, 1, false },
    { L"Orpheus Dutch - Jan",                             L"Jan",             2,    31, 0, false },
    { L"Orpheus Dutch - Hendrick",                        L"Hendrick",        2,    31, 1, false },
    { L"Orpheus French - Jean",                           L"Jean",            3,    33, 0, false },
    { L"Orpheus French - Pierre",                         L"Pierre",          3,    33, 1, false },
    { L"Orpheus Castilian Spanish - David",               L"David",           4,    34, 0, false },
    { L"Orpheus Castilian Spanish - Andr\u00E9s",         L"Andr\u00E9s",     4,    34, 1, false },
    { L"Orpheus Hungarian - Istvan",                      L"Istvan",          5,    36, 0, false },
    { L"Orpheus Hungarian - Marcus",                      L"Marcus",          5,    36, 1, false },
    { L"Orpheus Croatian - Stjepan",                      L"Stjepan",         6,    38, 0, false },
    { L"Orpheus Croatian - Marija",                       L"Marija",          6,    38, 1, true  },
    { L"Orpheus Italian - Davide",                        L"Davide",          7,    39, 0, false },
    { L"Orpheus Italian - Andrea",                        L"Andrea",          7,    39, 1, false },
    { L"Orpheus Romanian - David",                        L"David",           8,    40, 0, false },
    { L"Orpheus Romanian - Andrei",                       L"Andrei",          8,    40, 1, false },
    { L"Orpheus Czech - Honza",                           L"Honza",           9,    42, 0, false },
    { L"Orpheus Czech - Katka",                           L"Katka",           9,    42, 1, true  },
    { L"Orpheus UK English - Synthetic Dave",             L"Synthetic Dave", 10,    44, 0, false },
    { L"Orpheus UK English - Synthetic Andy",             L"Synthetic Andy", 10,    44, 1, false },
    { L"Orpheus Danish - Thomas",                         L"Thomas",         11,    45, 0, false },
    { L"Orpheus Danish - Lasse",                          L"Lasse",          11,    45, 1, false },
    { L"Orpheus Swedish - Tomas",                         L"Tomas",          12,    46, 0, false },
    { L"Orpheus Swedish - Lasse",                         L"Lasse",          12,    46, 1, false },
    { L"Orpheus Norwegian - Knut",                        L"Knut",           13,    47, 0, false },
    { L"Orpheus Norwegian - Andreas",                     L"Andreas",        13,    47, 1, false },
    { L"Orpheus Polish - Synthetic Dave",                 L"Synthetic Dave", 14,    48, 0, false },
    { L"Orpheus Polish - Synthetic Andy",                 L"Synthetic Andy", 14,    48, 1, false },
    { L"Orpheus German - Klaus",                          L"Klaus",          15,    49, 0, false },
    { L"Orpheus German - Andreas",                        L"Andreas",        15,    49, 1, false },
    { L"Orpheus Latin American Spanish - David",          L"David",          16,    52, 0, false },
    { L"Orpheus Latin American Spanish - Andr\u00E9s",    L"Andr\u00E9s",    16,    52, 1, false },
    { L"Orpheus Brazilian Portuguese - Jo\u00E3o",        L"Jo\u00E3o",      17,    55, 0, false },
    { L"Orpheus Brazilian Portuguese - Isabel",           L"Isabel",         17,    55, 1, true  },
    { L"Orpheus Malay - David",                           L"David",          18,    60, 0, false },
    { L"Orpheus Malay - Anne",                            L"Anne",           18,    60, 1, true  },
    { L"Orpheus Chinese Putonghua - Dave",                L"Dave",           19,    86, 0, false },
    { L"Orpheus Portuguese - Jo\u00E3o",                  L"Jo\u00E3o",      20,   351, 0, false },
    { L"Orpheus Portuguese - Isabel",                     L"Isabel",         20,   351, 1, true  },
    { L"Orpheus Finnish - Dave",                          L"Dave",           21,   358, 0, false },
    { L"Orpheus Finnish - Andy",                          L"Andy",           21,   358, 1, false },
    { L"Orpheus Lithuanian - Jonas",                      L"Jonas",          22,   370, 0, false },
    { L"Orpheus Lithuanian - Petras",                     L"Petras",         22,   370, 1, false },
    { L"Orpheus Welsh - David",                           L"David",          23, 10044, 0, false },
    { L"Orpheus Welsh - Megan",                           L"Megan",          23, 10044, 1, true  },
    { L"Orpheus Cantonese - John",                        L"John",           24, 10086, 0, false },
};

inline constexpr int orpheus_voice_count =
    static_cast<int>(sizeof(orpheus_voices) / sizeof(orpheus_voices[0]));

[[nodiscard]] inline const language_info& language_for_index(int lang_index)
{
    for (const auto& lang : orpheus_languages) {
        if (lang.lang_index == lang_index) {
            return lang;
        }
    }
    return orpheus_languages[10]; // UK English
}

[[nodiscard]] inline const language_info* language_for_country(int country)
{
    for (const auto& lang : orpheus_languages) {
        if (lang.country == country) {
            return &lang;
        }
    }
    return nullptr;
}

// First voice whose SAPI Language attribute matches `lcid`, or -1.
[[nodiscard]] inline int voice_for_lcid(const wchar_t* lcid)
{
    if (!lcid) {
        return -1;
    }
    for (int i = 0; i < orpheus_voice_count; ++i) {
        const language_info& lang = language_for_index(orpheus_voices[i].lang_index);
        const wchar_t* a = lang.lcid;
        const wchar_t* b = lcid;
        while (*a && *b) {
            wchar_t ca = (*a >= L'a' && *a <= L'z') ? static_cast<wchar_t>(*a - 32) : *a;
            wchar_t cb = (*b >= L'a' && *b <= L'z') ? static_cast<wchar_t>(*b - 32) : *b;
            if (ca != cb) {
                break;
            }
            ++a; ++b;
        }
        if (!*a && !*b) {
            return i;
        }
    }
    return -1;
}

}
}
