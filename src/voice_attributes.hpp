#pragma once

// SAPI-facing view of one entry in the voice catalog.

#include <string>

#include "utils.hpp"
#include "voice_catalog.hpp"

namespace Orpheus {
namespace sapi {

class voice_attributes
{
public:
    explicit voice_attributes(int voice_index = 0) noexcept
        : index_(voice_index)
    {
        if (index_ < 0 || index_ >= orpheus_voice_count) {
            index_ = 0;
        }
    }

    [[nodiscard]] const voice_entry& entry() const noexcept
    {
        return orpheus_voices[index_];
    }

    [[nodiscard]] std::wstring get_name() const
    {
        return entry().token_name;
    }

    [[nodiscard]] int get_index() const noexcept
    {
        return index_;
    }

    [[nodiscard]] int get_country() const noexcept
    {
        return entry().country;
    }

    [[nodiscard]] int get_lang_index() const noexcept
    {
        return entry().lang_index;
    }

    [[nodiscard]] int get_slot() const noexcept
    {
        return entry().slot;
    }

    [[nodiscard]] std::wstring get_age() const
    {
        return L"Adult";
    }

    [[nodiscard]] std::wstring get_gender() const
    {
        return entry().female ? L"Female" : L"Male";
    }

    [[nodiscard]] std::wstring get_language() const
    {
        return language_for_index(entry().lang_index).lcid;
    }

    [[nodiscard]] std::wstring get_vendor() const
    {
        return L"Dolphin";
    }

private:
    int index_;
};

}
}
