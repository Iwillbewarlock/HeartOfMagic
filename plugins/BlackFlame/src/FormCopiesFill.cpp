// =============================================================================
// FORM COPIES - per-type fill, link rewiring and the live compare (FormCopies.h)
// =============================================================================
// Data structs are copied byte for byte (std::memcpy of trivially copyable structs whose
// layouts CommonLibSSE-NG asserts: BGSProjectileData 0x88, BGSExplosionData 0x50,
// BGSHazardData 0x38, IMPACT_DATA_DATA 0x18; DecalData 0x24 is assigned), so the compare is byte
// for byte too. Components holding engine strings or owned arrays are never memcpy'd:
// TESFullName by BSFixedString assignment, the muzzle flash TESModel through SetModel.

#include "FormCopies.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <format>
#include <string_view>
#include <type_traits>
#include <utility>

namespace BlackFlame::CopyFill
{
    namespace
    {
        static_assert(std::is_trivially_copyable_v<RE::BGSProjectileData>);
        static_assert(std::is_trivially_copyable_v<RE::BGSExplosionData>);
        static_assert(std::is_trivially_copyable_v<RE::BGSHazardData>);
        static_assert(std::is_trivially_copyable_v<RE::BGSImpactData::IMPACT_DATA_DATA>);
        // DecalData holds a Color with its own copy members: assigned, not memcpy'd (no implicit padding:
        // DecalData.h asserts 0x24 with every byte a named member)
        static_assert(std::is_trivially_copyable_v<RE::TESBoundObject::BOUND_DATA>);

        template <class T>
        void CopyBytes(T& a_to, const T& a_from)
        {
            std::memcpy(&a_to, &a_from, sizeof(T));
        }

        template <class T>
        [[nodiscard]] bool SameBytes(const T& a_lhs, const T& a_rhs)
        {
            return std::memcmp(&a_lhs, &a_rhs, sizeof(T)) == 0;
        }

        // The link a copy should hold for a source link: its copy when one exists, else the source's own
        template <class T>
        [[nodiscard]] T* LinkFor(T* a_link)
        {
            auto* copy = CopyOf(a_link);
            return copy ? static_cast<T*>(copy) : a_link;
        }

        // BGSExplosionData::impactPlacedObject is declared TESObjectREFR* (BGSExplosion.h) but holds the
        // placed BASE object (a bound object). TESForm is TESObjectREFR's first base at offset 0, so the
        // pointer value is the form's; only form lookups by id are done on it.
        [[nodiscard]] RE::TESObjectREFR* PlacedLinkFor(RE::TESObjectREFR* a_link)
        {
            auto* copy = CopyOf(reinterpret_cast<RE::TESForm*>(a_link));
            return copy ? reinterpret_cast<RE::TESObjectREFR*>(copy) : a_link;
        }

        RE::BGSProjectileData ExpectedData(const RE::BGSProjectile& a_source)
        {
            RE::BGSProjectileData data;
            CopyBytes(data, a_source.data);
            data.explosionType = LinkFor(a_source.data.explosionType);
            return data;
        }

        RE::BGSExplosionData ExpectedData(const RE::BGSExplosion& a_source)
        {
            RE::BGSExplosionData data;
            CopyBytes(data, a_source.data);
            data.impactPlacedObject = PlacedLinkFor(a_source.data.impactPlacedObject);
            data.spawnProjectile = LinkFor(a_source.data.spawnProjectile);
            return data;
        }

        void FillProjectile(RE::BGSProjectile& a_copy, const RE::BGSProjectile& a_source)
        {
            CopyBytes(a_copy.data, a_source.data);
            CopyBytes(a_copy.boundData, a_source.boundData);
            a_copy.soundLevel = a_source.soundLevel;
            a_copy.fullName = a_source.fullName;
            a_copy.muzzleFlashModel.SetModel(a_source.muzzleFlashModel.GetModel());
        }

        void FillExplosion(RE::BGSExplosion& a_copy, const RE::BGSExplosion& a_source)
        {
            CopyBytes(a_copy.data, a_source.data);
            CopyBytes(a_copy.boundData, a_source.boundData);
            a_copy.formEnchanting = a_source.formEnchanting;
            a_copy.castingType = a_source.castingType;
            a_copy.amountofEnchantment = a_source.amountofEnchantment;
            a_copy.imageSpaceModifying = a_source.imageSpaceModifying;
            a_copy.fullName = a_source.fullName;
        }

        void FillImpact(RE::BGSImpactData& a_copy, const RE::BGSImpactData& a_source)
        {
            CopyBytes(a_copy.data, a_source.data);
            a_copy.decalTextureSet = a_source.decalTextureSet;
            a_copy.decalTextureSet2 = a_source.decalTextureSet2;
            a_copy.sound1 = a_source.sound1;
            a_copy.sound2 = a_source.sound2;
            a_copy.hazard = a_source.hazard;
            a_copy.dData = a_source.dData;
        }

        void FillHazard(RE::BGSHazard& a_copy, const RE::BGSHazard& a_source)
        {
            CopyBytes(a_copy.data, a_source.data);
            CopyBytes(a_copy.boundData, a_source.boundData);
            a_copy.imageSpaceModifying = a_source.imageSpaceModifying;
            a_copy.fullName = a_source.fullName;
        }

        void Note(std::string& a_out, std::string_view a_what)
        {
            a_out += a_out.empty() ? "DIFFERS: " : ", ";
            a_out += a_what;
        }

        // C5: per-field compare. Each data struct as a table of its members (offset, size, how to print), so a
        // difference names the field with the expected value (the source's, or the copy for a link that has one)
        // and the copy's; bytes no member covers (padding) are reported as such.
        enum class Kind
        {
            kForm,   // a form pointer, printed as its form id
            kFloat,
            kUInt    // 1, 2 or 4 bytes, printed in hex
        };

        struct Field
        {
            const char* name;
            std::size_t offset;
            std::size_t size;
            Kind kind;
        };

#define BF_FIELD(T, member, kind) Field{ #member, offsetof(T, member), sizeof(T::member), kind }
        constexpr std::array kProjectileFields{
            BF_FIELD(RE::BGSProjectileData, flags, Kind::kUInt), BF_FIELD(RE::BGSProjectileData, types, Kind::kUInt),
            BF_FIELD(RE::BGSProjectileData, gravity, Kind::kFloat), BF_FIELD(RE::BGSProjectileData, speed, Kind::kFloat),
            BF_FIELD(RE::BGSProjectileData, range, Kind::kFloat), BF_FIELD(RE::BGSProjectileData, light, Kind::kForm),
            BF_FIELD(RE::BGSProjectileData, muzzleFlashLight, Kind::kForm),
            BF_FIELD(RE::BGSProjectileData, tracerChance, Kind::kFloat),
            BF_FIELD(RE::BGSProjectileData, explosionProximity, Kind::kFloat),
            BF_FIELD(RE::BGSProjectileData, explosionTimer, Kind::kFloat), BF_FIELD(RE::BGSProjectileData, pad2C, Kind::kUInt),
            BF_FIELD(RE::BGSProjectileData, explosionType, Kind::kForm),
            BF_FIELD(RE::BGSProjectileData, activeSoundLoop, Kind::kForm),
            BF_FIELD(RE::BGSProjectileData, muzzleFlashDuration, Kind::kFloat),
            BF_FIELD(RE::BGSProjectileData, fadeOutTime, Kind::kFloat), BF_FIELD(RE::BGSProjectileData, force, Kind::kFloat),
            BF_FIELD(RE::BGSProjectileData, pad4C, Kind::kUInt), BF_FIELD(RE::BGSProjectileData, countdownSound, Kind::kForm),
            BF_FIELD(RE::BGSProjectileData, deactivateSound, Kind::kForm),
            BF_FIELD(RE::BGSProjectileData, defaultWeaponSource, Kind::kForm),
            BF_FIELD(RE::BGSProjectileData, coneSpread, Kind::kFloat),
            BF_FIELD(RE::BGSProjectileData, collisionRadius, Kind::kFloat),
            BF_FIELD(RE::BGSProjectileData, lifetime, Kind::kFloat),
            BF_FIELD(RE::BGSProjectileData, relaunchInterval, Kind::kFloat),
            BF_FIELD(RE::BGSProjectileData, decalData, Kind::kForm), BF_FIELD(RE::BGSProjectileData, collisionLayer, Kind::kForm)
        };
        constexpr std::array kExplosionFields{
            BF_FIELD(RE::BGSExplosionData, light, Kind::kForm), BF_FIELD(RE::BGSExplosionData, sound1, Kind::kForm),
            BF_FIELD(RE::BGSExplosionData, sound2, Kind::kForm), BF_FIELD(RE::BGSExplosionData, impactDataSet, Kind::kForm),
            BF_FIELD(RE::BGSExplosionData, impactPlacedObject, Kind::kForm),
            BF_FIELD(RE::BGSExplosionData, spawnProjectile, Kind::kForm), BF_FIELD(RE::BGSExplosionData, force, Kind::kFloat),
            BF_FIELD(RE::BGSExplosionData, damage, Kind::kFloat), BF_FIELD(RE::BGSExplosionData, radius, Kind::kFloat),
            BF_FIELD(RE::BGSExplosionData, imageSpaceRadius, Kind::kFloat),
            BF_FIELD(RE::BGSExplosionData, verticalOffsetMult, Kind::kFloat), BF_FIELD(RE::BGSExplosionData, flags, Kind::kUInt),
            BF_FIELD(RE::BGSExplosionData, eSoundLevel, Kind::kUInt), BF_FIELD(RE::BGSExplosionData, pad4C, Kind::kUInt)
        };
        constexpr std::array kHazardFields{
            BF_FIELD(RE::BGSHazardData, limit, Kind::kUInt), BF_FIELD(RE::BGSHazardData, radius, Kind::kFloat),
            BF_FIELD(RE::BGSHazardData, lifetime, Kind::kFloat), BF_FIELD(RE::BGSHazardData, imageSpaceRadius, Kind::kFloat),
            BF_FIELD(RE::BGSHazardData, targetInterval, Kind::kFloat), BF_FIELD(RE::BGSHazardData, flags, Kind::kUInt),
            BF_FIELD(RE::BGSHazardData, spell, Kind::kForm), BF_FIELD(RE::BGSHazardData, light, Kind::kForm),
            BF_FIELD(RE::BGSHazardData, impactDataSet, Kind::kForm), BF_FIELD(RE::BGSHazardData, sound, Kind::kForm)
        };
        using ImpactData = RE::BGSImpactData::IMPACT_DATA_DATA;
        constexpr std::array kImpactFields{
            BF_FIELD(ImpactData, effectDuration, Kind::kFloat), BF_FIELD(ImpactData, orient, Kind::kUInt),
            BF_FIELD(ImpactData, angleThreshold, Kind::kFloat), BF_FIELD(ImpactData, placementRadius, Kind::kFloat),
            BF_FIELD(ImpactData, soundLevel, Kind::kUInt), BF_FIELD(ImpactData, flags, Kind::kUInt),
            BF_FIELD(ImpactData, resultOverride, Kind::kUInt), BF_FIELD(ImpactData, unk16, Kind::kUInt)
        };
#undef BF_FIELD

        std::string FieldValue(const void* a_struct, const Field& a_field)
        {
            const auto* at = static_cast<const std::byte*>(a_struct) + a_field.offset;
            switch (a_field.kind) {
            case Kind::kForm:
                {
                    const RE::TESForm* form = nullptr;
                    std::memcpy(&form, at, sizeof(form));
                    return form ? std::format("{:08X}", form->GetFormID()) : std::string("null");
                }
            case Kind::kFloat:
                {
                    float value = 0.0f;
                    std::memcpy(&value, at, sizeof(value));
                    return std::format("{}", value);
                }
            default:
                {
                    std::uint32_t value = 0;
                    std::memcpy(&value, at, std::min(a_field.size, sizeof(value)));
                    return std::format("0x{:X}", value);
                }
            }
        }

        // a_expected: what a fill would give now (links to copies); a_source: the source's own bytes
        template <class T, std::size_t N>
        void DiffFields(std::string& a_out, const char* a_group, const T& a_copy, const T& a_expected, const T& a_source,
            const std::array<Field, N>& a_fields)
        {
            if (SameBytes(a_copy, a_expected)) {
                return;
            }
            std::size_t covered = 0;
            bool named = false;
            for (const auto& field : a_fields) {
                covered += field.size;
                const auto* copyAt = reinterpret_cast<const std::byte*>(&a_copy) + field.offset;
                const auto* expectedAt = reinterpret_cast<const std::byte*>(&a_expected) + field.offset;
                if (std::memcmp(copyAt, expectedAt, field.size) == 0) {
                    continue;
                }
                named = true;
                const auto expected = FieldValue(&a_expected, field);
                const auto own = FieldValue(&a_source, field);
                Note(a_out, std::format("{}.{}: source {}{} copy {}", a_group, field.name, expected,
                    own != expected ? " (link to its copy; the source's own " + own + ")" : std::string(),
                    FieldValue(&a_copy, field)));
            }
            if (!named) {
                Note(a_out, std::format("{}: bytes no member covers ({} of {} bytes are named members)", a_group, covered,
                    sizeof(T)));
            }
        }

        std::string Quoted(const char* a_text)
        {
            return a_text ? std::format("\"{}\"", a_text) : std::string("null");
        }

        RE::FormID IdOf(const RE::TESForm* a_form)
        {
            return a_form ? a_form->GetFormID() : 0;
        }

        void DiffName(std::string& a_out, const RE::TESFullName& a_copy, const RE::TESFullName& a_source)
        {
            if (a_copy.fullName != a_source.fullName) {
                Note(a_out, std::format("fullName: source {} copy {}", Quoted(a_source.fullName.c_str()),
                    Quoted(a_copy.fullName.c_str())));
            }
        }

        std::string CompareProjectile(const RE::BGSProjectile& a_copy, const RE::BGSProjectile& a_source)
        {
            std::string out;
            DiffFields(out, "data", a_copy.data, ExpectedData(a_source), a_source.data, kProjectileFields);
            if (a_copy.soundLevel != a_source.soundLevel) {
                Note(out, std::format("soundLevel: source {} copy {}", std::to_underlying(a_source.soundLevel),
                    std::to_underlying(a_copy.soundLevel)));
            }
            DiffName(out, a_copy, a_source);
            if (std::strcmp(a_copy.muzzleFlashModel.GetModel(), a_source.muzzleFlashModel.GetModel()) != 0) {
                Note(out, std::format("muzzleFlashModel: source {} copy {}", Quoted(a_source.muzzleFlashModel.GetModel()),
                    Quoted(a_copy.muzzleFlashModel.GetModel())));
            }
            if (!SameBytes(a_copy.boundData, a_source.boundData)) {
                Note(out, "boundData");
            }
            return out;
        }

        std::string CompareExplosion(const RE::BGSExplosion& a_copy, const RE::BGSExplosion& a_source)
        {
            std::string out;
            DiffFields(out, "data", a_copy.data, ExpectedData(a_source), a_source.data, kExplosionFields);
            if (a_copy.formEnchanting != a_source.formEnchanting || a_copy.castingType != a_source.castingType ||
                a_copy.amountofEnchantment != a_source.amountofEnchantment) {
                Note(out, std::format("enchantment: source {:08X}/{}/{} copy {:08X}/{}/{}", IdOf(a_source.formEnchanting),
                    a_source.castingType.underlying(), a_source.amountofEnchantment, IdOf(a_copy.formEnchanting),
                    a_copy.castingType.underlying(), a_copy.amountofEnchantment));
            }
            if (a_copy.imageSpaceModifying != a_source.imageSpaceModifying) {
                Note(out, std::format("imageSpaceModifying: source {:08X} copy {:08X}", IdOf(a_source.imageSpaceModifying),
                    IdOf(a_copy.imageSpaceModifying)));
            }
            DiffName(out, a_copy, a_source);
            if (!SameBytes(a_copy.boundData, a_source.boundData)) {
                Note(out, "boundData");
            }
            return out;
        }

        std::string CompareImpact(const RE::BGSImpactData& a_copy, const RE::BGSImpactData& a_source)
        {
            std::string out;
            DiffFields(out, "data", a_copy.data, a_source.data, a_source.data, kImpactFields);
            if (!SameBytes(a_copy.dData, a_source.dData)) {
                Note(out, "decal data");
            }
            if (a_copy.decalTextureSet != a_source.decalTextureSet || a_copy.decalTextureSet2 != a_source.decalTextureSet2 ||
                a_copy.sound1 != a_source.sound1 || a_copy.sound2 != a_source.sound2) {
                Note(out, std::format("texture sets or sounds: source {:08X} {:08X} {:08X} {:08X} copy {:08X} {:08X} {:08X} "
                    "{:08X}", IdOf(a_source.decalTextureSet), IdOf(a_source.decalTextureSet2), IdOf(a_source.sound1),
                    IdOf(a_source.sound2), IdOf(a_copy.decalTextureSet), IdOf(a_copy.decalTextureSet2), IdOf(a_copy.sound1),
                    IdOf(a_copy.sound2)));
            }
            if (a_copy.hazard != LinkFor(a_source.hazard)) {
                Note(out, std::format("hazard link: expected {:08X} (the source's own {:08X}) copy {:08X}",
                    IdOf(LinkFor(a_source.hazard)), IdOf(a_source.hazard), IdOf(a_copy.hazard)));
            }
            return out;
        }

        std::string CompareHazard(const RE::BGSHazard& a_copy, const RE::BGSHazard& a_source)
        {
            std::string out;
            DiffFields(out, "data", a_copy.data, a_source.data, a_source.data, kHazardFields);
            if (a_copy.imageSpaceModifying != a_source.imageSpaceModifying) {
                Note(out, std::format("imageSpaceModifying: source {:08X} copy {:08X}", IdOf(a_source.imageSpaceModifying),
                    IdOf(a_copy.imageSpaceModifying)));
            }
            DiffName(out, a_copy, a_source);
            if (!SameBytes(a_copy.boundData, a_source.boundData)) {
                Note(out, "boundData");
            }
            return out;
        }
    }

    bool FillData(RE::TESForm* a_copy, const RE::TESForm* a_source)
    {
        if (!a_copy || !a_source || a_copy->GetFormType() != a_source->GetFormType()) {
            return false;
        }
        switch (a_source->GetFormType()) {
        case RE::FormType::Projectile:
            FillProjectile(*a_copy->As<RE::BGSProjectile>(), *a_source->As<RE::BGSProjectile>());
            return true;
        case RE::FormType::Explosion:
            FillExplosion(*a_copy->As<RE::BGSExplosion>(), *a_source->As<RE::BGSExplosion>());
            return true;
        case RE::FormType::Impact:
            FillImpact(*a_copy->As<RE::BGSImpactData>(), *a_source->As<RE::BGSImpactData>());
            return true;
        case RE::FormType::Hazard:
            FillHazard(*a_copy->As<RE::BGSHazard>(), *a_source->As<RE::BGSHazard>());
            return true;
        default:
            return false;
        }
    }

    int RewireLinks(RE::TESForm* a_copy)
    {
        int rewired = 0;
        const auto set = [&rewired](auto*& a_slot, auto* a_value) {
            rewired += a_slot != a_value ? 1 : 0;
            a_slot = a_value;
        };
        if (auto* projectile = a_copy->As<RE::BGSProjectile>()) {
            set(projectile->data.explosionType, LinkFor(projectile->data.explosionType));
        } else if (auto* explosion = a_copy->As<RE::BGSExplosion>()) {
            set(explosion->data.impactPlacedObject, PlacedLinkFor(explosion->data.impactPlacedObject));
            set(explosion->data.spawnProjectile, LinkFor(explosion->data.spawnProjectile));
        } else if (auto* impact = a_copy->As<RE::BGSImpactData>()) {
            set(impact->hazard, LinkFor(impact->hazard));
        }
        return rewired;
    }

    std::string Compare(const RE::TESForm* a_copy, const RE::TESForm* a_source)
    {
        if (!a_copy || !a_source || a_copy->GetFormType() != a_source->GetFormType()) {
            return "DIFFERS: form type";
        }
        switch (a_source->GetFormType()) {
        case RE::FormType::Projectile:
            return CompareProjectile(*a_copy->As<RE::BGSProjectile>(), *a_source->As<RE::BGSProjectile>());
        case RE::FormType::Explosion:
            return CompareExplosion(*a_copy->As<RE::BGSExplosion>(), *a_source->As<RE::BGSExplosion>());
        case RE::FormType::Impact:
            return CompareImpact(*a_copy->As<RE::BGSImpactData>(), *a_source->As<RE::BGSImpactData>());
        case RE::FormType::Hazard:
            return CompareHazard(*a_copy->As<RE::BGSHazard>(), *a_source->As<RE::BGSHazard>());
        default:
            return "DIFFERS: not a copied type";
        }
    }
}
