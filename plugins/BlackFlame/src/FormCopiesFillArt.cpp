// =============================================================================
// FORM COPIES - the ARTO fill, refill and compare (FormCopies.h)
// =============================================================================
// An art object copy takes its source's art type (DNAM, BGSArtObject::data at 0x68) and object bounds. Its
// TESModelTextureSwap is never touched: the copy keeps the grey model the copies ESL gave it. Alternate textures
// (MODS: TESModelTextureSwap::alternateTextures / numAlternateTextures, 0x28 / 0x30 in the art's model component)
// are engine-heap arrays of BSFixedStrings; no fire art in the measured load order has any (houseCARL 2026-10-16:
// 0 of 100), so a source with them is refused and left out of the map rather than copied.

#include "FormCopies.h"

#include <cstring>
#include <format>
#include <type_traits>

namespace BlackFlame::CopyFill
{
    namespace
    {
        static_assert(std::is_trivially_copyable_v<RE::BGSArtObject::Data>);
        static_assert(std::is_trivially_copyable_v<RE::TESBoundObject::BOUND_DATA>);

        template <class T>
        [[nodiscard]] bool SameBytes(const T& a_lhs, const T& a_rhs)
        {
            return std::memcmp(&a_lhs, &a_rhs, sizeof(T)) == 0;
        }

        template <class T>
        bool WriteIfDiffers(T& a_to, const T& a_from)
        {
            if (SameBytes(a_to, a_from)) {
                return false;
            }
            std::memcpy(&a_to, &a_from, sizeof(T));
            return true;
        }
    }

    std::string Refusal(const RE::TESForm* a_source)
    {
        const auto* art = a_source ? a_source->As<RE::BGSArtObject>() : nullptr;
        if (art && art->numAlternateTextures != 0) {
            return std::format("alternate textures not copied (the source art has {})", art->numAlternateTextures);
        }
        return {};
    }

    void FillArt(RE::BGSArtObject& a_copy, const RE::BGSArtObject& a_source)
    {
        std::memcpy(&a_copy.data, &a_source.data, sizeof(a_copy.data));
        std::memcpy(&a_copy.boundData, &a_source.boundData, sizeof(a_copy.boundData));
    }

    bool RefillArt(RE::BGSArtObject& a_copy, const RE::BGSArtObject& a_source)
    {
        bool changed = WriteIfDiffers(a_copy.data, a_source.data);
        changed |= WriteIfDiffers(a_copy.boundData, a_source.boundData);
        return changed;
    }

    std::string CompareArt(const RE::BGSArtObject& a_copy, const RE::BGSArtObject& a_source)
    {
        std::string out;
        if (!SameBytes(a_copy.data, a_source.data)) {
            out = std::format("DIFFERS: artType: source {} copy {}", a_source.data.artType.underlying(),
                a_copy.data.artType.underlying());
        }
        if (!SameBytes(a_copy.boundData, a_source.boundData)) {
            out += out.empty() ? "DIFFERS: boundData" : ", boundData";
        }
        return out;
    }
}
