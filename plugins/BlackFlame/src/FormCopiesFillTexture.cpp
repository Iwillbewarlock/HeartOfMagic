// =============================================================================
// FORM COPIES - the decal texture set (TXST) fill, refill, compare and checks (FormCopies.h)
// =============================================================================
// A TXST copy takes its source's bounds, DNAM flags and decal data (DODT, colour greyed). Its texture slots are its own
// record's (the builder's grey copies of TX00 / TX03, the source's own bytes elsewhere): the fill never writes
// BGSTextureSet::textures[] (0x040) or textureFileIDs (0x0CC, RE/B/BGSTextureSet.h), nor calls BSTextureSet's
// SetTexturePath (vfunc 27). decalData is a pointer (0x0C0) the engine allocates for a record with DODT; the builder
// writes a DODT stub exactly when the source has one, and a copy whose presence differs is left out (never allocated
// here). AB1: slots are compared as BYTES - the map's "sourceTexturesRaw" (hex of the source's record string, as the
// request gave it) against the live strings - through DataPathRule::LowerSlashes (ASCII case and '/' folded, a
// double-byte character of the system code page kept), never through a code page conversion. A slot that differs after
// the start cannot be refilled (the record's string), so CheckCopies reports it and the next start leaves the copy out.

#include "FormCopies.h"
#include "FormCopiesBytes.h"
#include "FormCopiesState.h"

#include "DataPathRule.h"
#include "NifDump.h"
#include "Tint.h"

#include "EncodingUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
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
        constexpr const char* kDataFolder = "Data";

        [[nodiscard]] std::uint8_t GreyByte(const RE::Color& a_colour) noexcept
        {
            const float lum = a_colour.red * kLumaRed + a_colour.green * kLumaGreen + a_colour.blue * kLumaBlue;
            return static_cast<std::uint8_t>(std::clamp(std::lround(lum), 0L, static_cast<long>(kByteMax)));
        }

        // The slot's own bytes (the record's string as the engine keeps it)
        [[nodiscard]] std::string SlotBytes(const RE::BGSTextureSet& a_set, std::size_t a_slot)
        {
            const char* text = a_set.textures[a_slot].textureName.c_str();
            return text ? std::string(text) : std::string();
        }

        // For a log line only
        [[nodiscard]] std::string Shown(const std::string& a_bytes)
        {
            return EncodingUtils::SanitizeToUTF8(a_bytes);
        }

        [[nodiscard]] bool SameSlot(const std::string& a_lhs, const std::string& a_rhs)
        {
            return DataPathRule::LowerSlashes(a_lhs) == DataPathRule::LowerSlashes(a_rhs);
        }

        [[nodiscard]] bool SameDecal(const RE::DecalData& a_lhs, const RE::DecalData& a_rhs)
        {
            return std::memcmp(&a_lhs, &a_rhs, sizeof(a_lhs)) == 0;  // every byte a named member (RE/D/DecalData.h)
        }

        // The map entry's 8 slot strings under a_key (a_hex: each one hex of the bytes), or none when the key is missing,
        // not 8 strings or a string is no hex
        [[nodiscard]] std::optional<std::array<std::string, kTextureSlots>> Slots(const nlohmann::json& a_item, const char* a_key,
            bool a_hex)
        {
            if (!a_item.contains(a_key) || !a_item[a_key].is_array() || a_item[a_key].size() != kTextureSlots) {
                return std::nullopt;
            }
            std::array<std::string, kTextureSlots> out{};
            for (std::size_t i = 0; i < kTextureSlots; ++i) {
                if (!a_item[a_key][i].is_string()) {
                    return std::nullopt;
                }
                const auto text = a_item[a_key][i].get<std::string>();
                const auto bytes = a_hex ? DataPathRule::FromHex(text) : std::optional<std::string>(text);
                if (!bytes) {
                    return std::nullopt;
                }
                out[i] = *bytes;
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
                if (const auto own = SlotBytes(a_copy, i); !SameSlot(own, (*expected)[i])) {
                    note(std::format("TX0{}: expected '{}' copy '{}' (a slot cannot be refilled: rebuild)", i, Shown((*expected)[i]),
                        Shown(own)));
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
        const auto sourceSlots = Slots(a_item, "sourceTexturesRaw", true);
        const auto greySlots = Slots(a_item, "greyTextures", false);
        if (!sourceSlots || !greySlots) {
            return "the map entry has no sourceTexturesRaw / greyTextures (8 strings each): run the builder";
        }
        for (std::size_t i = 0; i < kTextureSlots; ++i) {
            const auto live = SlotBytes(*source, i);
            if (!SameSlot(live, (*sourceSlots)[i])) {
                return std::format("the source's TX0{} is '{}', the copy was built for '{}' (run the builder)", i, Shown(live),
                    Shown((*sourceSlots)[i]));
            }
            a_expected[i] = (*greySlots)[i].empty() ? (*sourceSlots)[i] : (*greySlots)[i];
            const auto own = SlotBytes(*copy, i);
            if (!SameSlot(own, a_expected[i])) {
                return std::format("the copy's TX0{} is '{}', the map says '{}' (the copies ESL and map are from different runs)", i,
                    Shown(own), Shown(a_expected[i]));
            }
            if ((*greySlots)[i].empty()) {
                continue;
            }
            const auto path = NifDump::NormalizeTexturePath(own);
            if (!path) {
                return std::format("the copy's TX0{} '{}' is refused as a texture path", i, Shown(own));
            }
            // Opens, which is all it says: whether the DDS decodes is the builder's risk check's (it decodes every copy)
            RE::BSResourceNiBinaryStream stream(path->c_str());
            if (!stream.good()) {
                a_reason = TextureSetReason::kOutputMissing;
                // AB4: on disk but not through the game's file system: a loose-file cache (CRDW and the like) out of date
                std::error_code ec;
                const bool onDisk = std::filesystem::exists(std::filesystem::path(kDataFolder) / *path, ec) && !ec;
                return std::format("the copy's texture {} does not open{}", Shown(*path), onDisk ?
                    " although it is on disk under Data (a loose-file cache such as CRDW out of date: rebuild it)" :
                    " (is the builder's output mod enabled with its textures, and nothing overwrites them?)");
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

    std::vector<std::string> GreyTexturePaths(const nlohmann::json& a_item)
    {
        std::vector<std::string> out;
        if (const auto grey = Slots(a_item, "greyTextures", false)) {
            for (const auto& slot : *grey) {
                if (const auto path = slot.empty() ? std::nullopt : NifDump::NormalizeTexturePath(slot)) {
                    out.push_back(*path);
                }
            }
        }
        return out;
    }
}
