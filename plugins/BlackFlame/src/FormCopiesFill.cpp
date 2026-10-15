// =============================================================================
// FORM COPIES - per-type fill, link rewiring and the live compare (FormCopies.h)
// =============================================================================
// Data structs are copied byte for byte (std::memcpy of trivially copyable structs whose
// layouts CommonLibSSE-NG asserts: BGSProjectileData 0x88, BGSExplosionData 0x50,
// BGSHazardData 0x38, IMPACT_DATA_DATA 0x18; DecalData 0x24 is assigned), so the compare is byte
// for byte too. Components holding engine strings or owned arrays are never memcpy'd:
// TESFullName by BSFixedString assignment, the muzzle flash TESModel through SetModel.

#include "FormCopies.h"

#include <cstring>
#include <format>
#include <type_traits>

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

        void Note(std::string& a_out, const char* a_what)
        {
            a_out += a_out.empty() ? "DIFFERS: " : ", ";
            a_out += a_what;
        }

        std::string CompareProjectile(const RE::BGSProjectile& a_copy, const RE::BGSProjectile& a_source)
        {
            std::string out;
            if (!SameBytes(a_copy.data, ExpectedData(a_source))) {
                Note(out, "data (scalars or links)");
            }
            if (a_copy.soundLevel != a_source.soundLevel) {
                Note(out, "soundLevel");
            }
            if (a_copy.fullName != a_source.fullName) {
                Note(out, "fullName");
            }
            if (std::strcmp(a_copy.muzzleFlashModel.GetModel(), a_source.muzzleFlashModel.GetModel()) != 0) {
                Note(out, "muzzleFlashModel");
            }
            if (!SameBytes(a_copy.boundData, a_source.boundData)) {
                Note(out, "boundData");
            }
            return out;
        }

        std::string CompareExplosion(const RE::BGSExplosion& a_copy, const RE::BGSExplosion& a_source)
        {
            std::string out;
            if (!SameBytes(a_copy.data, ExpectedData(a_source))) {
                Note(out, "data (scalars or links)");
            }
            if (a_copy.formEnchanting != a_source.formEnchanting || a_copy.castingType != a_source.castingType ||
                a_copy.amountofEnchantment != a_source.amountofEnchantment) {
                Note(out, "enchantment");
            }
            if (a_copy.imageSpaceModifying != a_source.imageSpaceModifying) {
                Note(out, "imageSpaceModifying");
            }
            if (a_copy.fullName != a_source.fullName) {
                Note(out, "fullName");
            }
            if (!SameBytes(a_copy.boundData, a_source.boundData)) {
                Note(out, "boundData");
            }
            return out;
        }

        std::string CompareImpact(const RE::BGSImpactData& a_copy, const RE::BGSImpactData& a_source)
        {
            std::string out;
            if (!SameBytes(a_copy.data, a_source.data) || !SameBytes(a_copy.dData, a_source.dData)) {
                Note(out, "data or decal data");
            }
            if (a_copy.decalTextureSet != a_source.decalTextureSet || a_copy.decalTextureSet2 != a_source.decalTextureSet2 ||
                a_copy.sound1 != a_source.sound1 || a_copy.sound2 != a_source.sound2) {
                Note(out, "texture sets or sounds");
            }
            if (a_copy.hazard != LinkFor(a_source.hazard)) {
                Note(out, "hazard link");
            }
            return out;
        }

        std::string CompareHazard(const RE::BGSHazard& a_copy, const RE::BGSHazard& a_source)
        {
            std::string out;
            if (!SameBytes(a_copy.data, a_source.data)) {
                Note(out, "data (scalars or links)");
            }
            if (a_copy.imageSpaceModifying != a_source.imageSpaceModifying) {
                Note(out, "imageSpaceModifying");
            }
            if (a_copy.fullName != a_source.fullName) {
                Note(out, "fullName");
            }
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
