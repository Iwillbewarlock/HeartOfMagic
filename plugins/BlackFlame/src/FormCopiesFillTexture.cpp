// =============================================================================
// FORM COPIES - the decal texture set (TXST) fill, refill, compare and checks (FormCopies.h)
// =============================================================================
// A TXST copy takes its source's bounds, DNAM flags and decal data (DODT, colour greyed). Its texture slots are its own
// record's (the builder's grey copies of TX00 / TX03, the source's text elsewhere): the fill never writes
// BGSTextureSet::textures[] (0x040) or textureFileIDs (0x0CC, RE/B/BGSTextureSet.h), nor calls BSTextureSet's
// SetTexturePath (vfunc 27). decalData is a pointer (0x0C0) the engine allocates for a record with DODT; the builder
// writes a DODT stub exactly when the source has one, and a copy whose presence differs is left out (never allocated
// here). Slots are compared as the map gives them (UTF-8, from the request's SanitizeToUTF8), ASCII letters and slashes
// folded; a slot that differs after the start cannot be refilled (the record's string), so CheckCopies reports it and
// the next start leaves the copy out.

#include "FormCopies.h"
#include "FormCopiesBytes.h"
#include "FormCopiesState.h"

#include "NifDump.h"
#include "Tint.h"

#include "EncodingUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <optional>
#include <type_traits>

namespace BlackFlame::CopyFill
{
    namespace
    {
        static_assert(std::is_trivially_copyable_v<RE::TESBoundObject::BOUND_DATA>);
        static_assert(sizeof(RE::DecalData) == 0x24 && offsetof(RE::DECAL_DATA_DATA, color) == 0x20,
            "DODT: the 36 bytes the builder's stub holds (tools/blackflame-greybuild GreyForms.h kDodtSize)");
        static_assert(kTextureSlots == 8, "TX00..TX07");

        constexpr float kByteMax = 255.0f;
        constexpr std::string_view kReplacement = "\xEF\xBF\xBD";  // U+FFFD in UTF-8 (SanitizeToUTF8 of bytes that are no text)

        [[nodiscard]] std::uint8_t GreyByte(const RE::Color& a_colour) noexcept
        {
            const float lum = a_colour.red * kLumaRed + a_colour.green * kLumaGreen + a_colour.blue * kLumaBlue;
            return static_cast<std::uint8_t>(std::clamp(std::lround(lum), 0L, static_cast<long>(kByteMax)));
        }

        // The slot's text as the request wrote it (UTF-8), ASCII letters lower case and '/' as '\'
        [[nodiscard]] std::string Folded(std::string a_text)
        {
            for (auto& c : a_text) {
                const auto byte = static_cast<unsigned char>(c);
                c = c == '/' ? '\\' : static_cast<char>(byte < 0x80 ? std::tolower(byte) : byte);
            }
            return a_text;
        }

        [[nodiscard]] std::string SlotText(const RE::BGSTextureSet& a_set, std::size_t a_slot)
        {
            const char* text = a_set.textures[a_slot].textureName.c_str();
            return text ? EncodingUtils::SanitizeToUTF8(std::string(text)) : std::string();
        }

        [[nodiscard]] bool SameDecal(const RE::DecalData& a_lhs, const RE::DecalData& a_rhs)
        {
            return std::memcmp(&a_lhs, &a_rhs, sizeof(a_lhs)) == 0;  // every byte a named member (RE/D/DecalData.h)
        }

        // The map entry's 8 slot strings under a_key, or none when the key is missing or not 8 strings
        [[nodiscard]] std::optional<std::array<std::string, kTextureSlots>> Slots(const nlohmann::json& a_item, const char* a_key)
        {
            if (!a_item.contains(a_key) || !a_item[a_key].is_array() || a_item[a_key].size() != kTextureSlots) {
                return std::nullopt;
            }
            std::array<std::string, kTextureSlots> out{};
            for (std::size_t i = 0; i < kTextureSlots; ++i) {
                if (!a_item[a_key][i].is_string()) {
                    return std::nullopt;
                }
                out[i] = a_item[a_key][i].get<std::string>();
            }
            return out;
        }
    }

    RE::DecalData ExpectedDecal(const RE::DecalData& a_decal) noexcept
    {
        RE::DecalData out = a_decal;
        const std::uint8_t grey = GreyByte(a_decal.data.color);
        out.data.color.red = grey;
        out.data.color.green = grey;
        out.data.color.blue = grey;
        return out;
    }

    void FillTextureSet(RE::BGSTextureSet& a_copy, const RE::BGSTextureSet& a_source)
    {
        Bytes::Copy(a_copy.boundData, a_source.boundData);
        a_copy.flags = a_source.flags;
        if (a_copy.decalData && a_source.decalData) {
            *a_copy.decalData = ExpectedDecal(*a_source.decalData);
        }
    }

    bool RefillTextureSet(RE::BGSTextureSet& a_copy, const RE::BGSTextureSet& a_source)
    {
        bool changed = Bytes::WriteIfDiffers(a_copy.boundData, a_source.boundData);
        if (a_copy.flags.underlying() != a_source.flags.underlying()) {
            a_copy.flags = a_source.flags;
            changed = true;
        }
        if (a_copy.decalData && a_source.decalData) {
            const auto expected = ExpectedDecal(*a_source.decalData);
            if (!SameDecal(*a_copy.decalData, expected)) {
                *a_copy.decalData = expected;
                changed = true;
            }
        }
        return changed;
    }

    std::string CompareTextureSet(const RE::BGSTextureSet& a_copy, const RE::BGSTextureSet& a_source)
    {
        std::string out;
        const auto note = [&](const std::string& a_what) {
            out += out.empty() ? "DIFFERS: " : ", ";
            out += a_what;
        };
        if (!Bytes::Same(a_copy.boundData, a_source.boundData)) {
            note("boundData");
        }
        if (a_copy.flags.underlying() != a_source.flags.underlying()) {
            note(std::format("flags: source 0x{:X} copy 0x{:X}", a_source.flags.underlying(), a_copy.flags.underlying()));
        }
        if (!a_copy.decalData != !a_source.decalData) {
            note(std::format("decal data: source {} copy {} (rebuild)", a_source.decalData ? "has one" : "none",
                a_copy.decalData ? "has one" : "none"));
        } else if (a_copy.decalData && !SameDecal(*a_copy.decalData, ExpectedDecal(*a_source.decalData))) {
            note("decal data (DODT)");
        }
        if (const auto* expected = ExpectedTexturesOf(&a_copy)) {
            for (std::size_t i = 0; i < kTextureSlots; ++i) {
                if (Folded(SlotText(a_copy, i)) != Folded((*expected)[i])) {
                    note(std::format("TX0{}: expected '{}' copy '{}' (a slot cannot be refilled: rebuild)", i, (*expected)[i],
                        SlotText(a_copy, i)));
                }
            }
        }
        return out;
    }

    std::string TextureSetProblem(const RE::TESForm* a_copy, const RE::TESForm* a_source, const nlohmann::json& a_item,
        std::array<std::string, kTextureSlots>& a_expected, TextureSetReason& a_reason)
    {
        a_reason = TextureSetReason::kStale;
        const auto* copy = a_copy ? a_copy->As<RE::BGSTextureSet>() : nullptr;
        const auto* source = a_source ? a_source->As<RE::BGSTextureSet>() : nullptr;
        if (!copy || !source) {
            return "not a texture set";
        }
        if (!copy->decalData != !source->decalData) {
            return std::format("the source {} decal data (DODT) and the copy {}: built for another load order (run the builder)",
                source->decalData ? "has" : "has no", copy->decalData ? "has" : "has none");
        }
        const auto sourceSlots = Slots(a_item, "sourceTextures");
        const auto greySlots = Slots(a_item, "greyTextures");
        if (!sourceSlots || !greySlots) {
            return "the map entry has no sourceTextures / greyTextures (8 strings each): run the builder";
        }
        for (std::size_t i = 0; i < kTextureSlots; ++i) {
            const auto live = SlotText(*source, i);
            if (live.find(kReplacement) != std::string::npos) {
                a_reason = TextureSetReason::kUnusable;  // AA2: every comparison would agree on U+FFFD and name another file
                return std::format("the source's TX0{} holds bytes that are no text in this code page: not copied", i);
            }
            if (Folded(live) != Folded((*sourceSlots)[i])) {
                return std::format("the source's TX0{} is '{}', the copy was built for '{}' (run the builder)", i, live, (*sourceSlots)[i]);
            }
            a_expected[i] = (*greySlots)[i].empty() ? (*sourceSlots)[i] : (*greySlots)[i];
            const auto own = SlotText(*copy, i);
            if (Folded(own) != Folded(a_expected[i])) {
                return std::format("the copy's TX0{} is '{}', the map says '{}' (the copies ESL and map are from different runs)", i,
                    own, a_expected[i]);
            }
            if ((*greySlots)[i].empty()) {
                continue;
            }
            const char* raw = copy->textures[i].textureName.c_str();
            const auto path = raw ? NifDump::NormalizeTexturePath(raw) : std::nullopt;
            if (!path) {
                return std::format("the copy's TX0{} '{}' is refused as a texture path", i, own);
            }
            // Opens, which is all it says: whether the DDS decodes is the builder's risk check's (it decodes every copy)
            RE::BSResourceNiBinaryStream stream(path->c_str());
            if (!stream.good()) {
                a_reason = TextureSetReason::kOutputMissing;
                return std::format("the copy's texture {} does not open (is the builder's output mod enabled with its textures?)",
                    EncodingUtils::SanitizeToUTF8(*path));
            }
        }
        a_reason = TextureSetReason::kNone;
        return {};
    }

    const std::array<std::string, kTextureSlots>* ExpectedTexturesOf(const RE::TESForm* a_copy) noexcept
    {
        if (!a_copy) {
            return nullptr;
        }
        const auto& map = CopiesState::TheCopies().textureSets;
        const auto it = map.find(a_copy->GetFormID());
        return it != map.end() ? &it->second : nullptr;
    }
}
