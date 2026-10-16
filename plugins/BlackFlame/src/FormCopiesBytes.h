#pragma once

#include <cstring>
#include <type_traits>

// =============================================================================
// FORM COPIES - byte copy and compare of trivially copyable data structs (FormCopiesFill*.cpp)
// =============================================================================

namespace BlackFlame::CopyFill::Bytes
{
    template <class T>
    void Copy(T& a_to, const T& a_from)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        std::memcpy(&a_to, &a_from, sizeof(T));
    }

    template <class T>
    [[nodiscard]] bool Same(const T& a_lhs, const T& a_rhs)
    {
        return std::memcmp(&a_lhs, &a_rhs, sizeof(T)) == 0;
    }

    /// D3 (R7): a_expected written in one memcpy, and only when it differs (a copy never points at an original in between)
    template <class T>
    bool WriteIfDiffers(T& a_to, const T& a_expected)
    {
        if (Same(a_to, a_expected)) {
            return false;
        }
        Copy(a_to, a_expected);
        return true;
    }
}
