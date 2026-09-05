#pragma once

#include <windows.h>
#include <string>
#include <stdexcept>

namespace Orpheus {
namespace registry {

class error : public std::runtime_error
{
public:
    explicit error(const std::string& msg) : std::runtime_error(msg) {}
};

class key
{
public:
    key(HKEY parent, const std::wstring& name, REGSAM access_mask = KEY_READ, bool create = false)
    {
        const LONG result = create
            ? RegCreateKeyExW(parent, name.c_str(), 0, nullptr, 0, access_mask, nullptr, &handle_, nullptr)
            : RegOpenKeyExW(parent, name.c_str(), 0, access_mask, &handle_);

        if (result != ERROR_SUCCESS) {
            throw error("Unable to open/create a registry key");
        }
    }

    ~key()
    {
        if (handle_) {
            RegCloseKey(handle_);
        }
    }

    key(const key&) = delete;
    key& operator=(const key&) = delete;

    key(key&& other) noexcept : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    key& operator=(key&& other) noexcept
    {
        if (this != &other) {
            if (handle_) {
                RegCloseKey(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] operator HKEY() const noexcept
    {
        return handle_;
    }

    [[nodiscard]] std::wstring get(const std::wstring& name) const;

    [[nodiscard]] std::wstring get() const
    {
        return get(L"");
    }

    void set(const std::wstring& name, const std::wstring& value);

    void set(const std::wstring& value)
    {
        set(L"", value);
    }

private:
    HKEY handle_ = nullptr;
};

// Access needed to remove a subtree under `parent`.
constexpr REGSAM DELETE_ACCESS =
    DELETE | KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_SET_VALUE;

// Delete `name` and everything under it.
//
// Never use RegDeleteKeyW for this.  It refuses any key that still has
// subkeys, returning ERROR_ACCESS_DENIED, and a SAPI voice token always has an
// Attributes subkey - so a delete written that way silently does nothing and
// leaves a dead voice in the machine's shared voice list.  RegDeleteTreeW is
// the one that works.
//
// An empty `name` is refused: RegDeleteTreeW with an empty subkey empties the
// key the handle itself names, which for these callers would be the whole
// CLSID hive or the machine's entire voice list.
//
// Returns true if the key is gone afterwards, including when it was already
// absent.  Never throws.
[[nodiscard]] bool delete_subtree(HKEY parent, const std::wstring& name,
                                  REGSAM view = 0) noexcept;

// True when `parent\name` exists in the given view.
[[nodiscard]] bool key_exists(HKEY parent, const std::wstring& name,
                              REGSAM view = 0) noexcept;

}
}
