#include "Recolor.h"

#include "Config.h"
#include "Palette.h"
#include "ParticleColors.h"
#include "Tint.h"

#include <atomic>
#include <format>
#include <memory>

namespace BlackFlame
{
    namespace
    {
        constexpr int kMaxDescribedMaterials = 3;

        std::atomic<bool> g_ownershipLogged{ false };

        // BSShaderProperty::SetMaterial(material, true) gives the property its own
        // unshared copy and does not keep the pointer passed in (PapyrusExtenderSSE
        // ActorGraphics.cpp, Precision AttackTrail.cpp, HorizonFix HorizonBand.cpp); the
        // caller frees its temporary the way those plugins do: destructor, then RE::free
        // (Create() allocates on the game heap). Whether the property really holds
        // another pointer is checked and logged; if it ever held ours, ours is kept.
        struct MaterialDeleter
        {
            void operator()(RE::BSEffectShaderMaterial* a_material) const noexcept
            {
                a_material->~BSEffectShaderMaterial();
                RE::free(a_material);
            }
        };
        using MaterialPtr = std::unique_ptr<RE::BSEffectShaderMaterial, MaterialDeleter>;

        bool HasColorController(const RE::NiObjectNET* a_object)
        {
            static REL::Relocation<const RE::NiRTTI*> colorControllerRTTI{ RE::NiRTTI_BSEffectShaderPropertyColorController };
            for (auto* ctrl = a_object->GetControllers(); ctrl; ctrl = ctrl->GetNext()) {
                if (ctrl->GetRTTI() == colorControllerRTTI.get()) {
                    return true;
                }
            }
            return false;
        }

        void RecolorProperty(RE::BSEffectShaderProperty* a_prop, RecolorStats& a_stats)
        {
            ++a_stats.effectGeometries;
            if (HasColorController(a_prop)) {
                ++a_stats.colorControlled;
            }
            // The palette's alpha ramp then also drives the shape's alpha (README test item)
            if (a_prop->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kGrayscaleToPaletteAlpha)) {
                ++a_stats.paletteAlpha;
            }
            auto* palette = GetPalette();
            auto* source = a_prop->GetMaterial();
            // Both the texture and its path, never one alone: with the greyscale flags on, the
            // effect technique reads the texture pointer unchecked (HorizonFix notes)
            if (!palette || !GetPalettePath() || !source) {
                ++a_stats.failed;
                return;
            }
            // The property holds the engine's copy of ours: should it resolve the palette to another
            // texture object, the path still marks it (else the hand-art tick would recolour again)
            if (source->greyscaleTexture.get() == palette || source->greyscaleTexturePath == GetPalettePath()) {
                ++a_stats.alreadyOurs;
                return;
            }

            MaterialPtr copy{ static_cast<RE::BSEffectShaderMaterial*>(source->Create()) };
            if (!copy) {
                ++a_stats.failed;
                return;
            }
            copy->CopyMembers(source);
            copy->greyscaleTexture.reset(palette);
            copy->greyscaleTexturePath = GetPalettePath();
            (void)Desaturate(copy->baseColor, GetConfig().tintScale);

            a_prop->SetMaterial(copy.get(), true);
            const bool engineKeptOurs = a_prop->GetBaseMaterial() == copy.get();
            if (!g_ownershipLogged.exchange(true)) {
                logger::info("BlackFlame: SetMaterial(copy, true): the property holds {} (copy 0x{:X}, now 0x{:X})",
                    engineKeptOurs ? "OUR copy (kept alive)" : "the engine's own copy (ours freed)",
                    reinterpret_cast<std::uintptr_t>(copy.get()), reinterpret_cast<std::uintptr_t>(a_prop->GetBaseMaterial()));
            }
            if (engineKeptOurs) {
                (void)copy.release();  // the property owns it now
                ++a_stats.keptOurs;
            } else {
                ++a_stats.engineCopied;  // `copy` frees ours at scope end
            }
            // No SetupGeometry: effect material values are read live at draw time (Precision
            // changes baseColor every frame without one). If the palette does not show in
            // game, a missing SetupGeometry/FinishSetupGeometry is the first suspect (README).

            if (a_prop->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kGrayscaleToPaletteColor)) {
                ++a_stats.paletteSwapped;
            } else if (GetConfig().forcePalette) {
                // Re-read: the property holds the engine's copy now, and the flag must never be on
                // without a palette texture behind it
                const auto* now = a_prop->GetMaterial();
                if (!now || !now->greyscaleTexture) {
                    ++a_stats.failed;
                    return;
                }
                // Set only now, after the material with both palette pointer and path is in
                // place: the effect technique gates on the path and reads the pointer unchecked.
                // The property is the instance's own (cloned per instance; the material is the
                // pooled part), and the material check log shows its pointer per instance.
                a_prop->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kGrayscaleToPaletteColor, true);
                ++a_stats.forcedPalette;
            } else {
                ++a_stats.baseColorOnly;
            }
        }

        RE::BSEffectShaderProperty* EffectPropertyOf(RE::BSGeometry* a_geometry)
        {
            return netimmerse_cast<RE::BSEffectShaderProperty*>(a_geometry->GetGeometryRuntimeData().shaderProperty.get());
        }

        // NiTriShape and other NiGeometry types do not derive from BSGeometry in this
        // CommonLib (RE/N/NiGeometry.h), so BSVisit::TraverseScenegraphGeometries skips them
        RE::BSEffectShaderProperty* EffectPropertyOf(RE::NiGeometry* a_geometry)
        {
            auto& data = a_geometry->GetRuntimeData();
            if (auto* prop = netimmerse_cast<RE::BSEffectShaderProperty*>(data.spEffectState.get())) {
                return prop;
            }
            return netimmerse_cast<RE::BSEffectShaderProperty*>(data.spPropertyState.get());
        }

        template <class Visit>
        void ForEachEffectProperty(RE::NiAVObject* a_root, Visit&& a_visit)
        {
            RE::BSVisit::TraverseScenegraphObjects(a_root, [&](RE::NiAVObject* a_object) {
                if (auto* geometry = a_object->AsGeometry()) {
                    if (auto* prop = EffectPropertyOf(geometry)) {
                        a_visit(prop, geometry, false);
                    }
                } else if (auto* legacy = netimmerse_cast<RE::NiGeometry*>(a_object)) {
                    if (auto* prop = EffectPropertyOf(legacy)) {
                        a_visit(prop, nullptr, true);
                    }
                }
                return RE::BSVisit::BSVisitControl::kContinue;
            });
        }
    }

    void RecolorStats::Add(const RecolorStats& a_other) noexcept
    {
        effectGeometries += a_other.effectGeometries;
        paletteSwapped += a_other.paletteSwapped;
        forcedPalette += a_other.forcedPalette;
        baseColorOnly += a_other.baseColorOnly;
        alreadyOurs += a_other.alreadyOurs;
        colorControlled += a_other.colorControlled;
        engineCopied += a_other.engineCopied;
        keptOurs += a_other.keptOurs;
        legacyGeometries += a_other.legacyGeometries;
        paletteAlpha += a_other.paletteAlpha;
        failed += a_other.failed;
        colorModifiers += a_other.colorModifiers;
    }

    std::string RecolorStats::Describe() const
    {
        return std::format("{} effect geometries: {} palette swapped, {} palette forced (flag added), "
                           "{} base colour only (no palette flag), {} already grey, {} with a colour controller, "
                           "{} with palette alpha, {} legacy NiGeometry, {} failed; material copies: {} engine-copied, "
                           "{} ours kept; {} particle colour modifiers desaturated{}",
            effectGeometries, paletteSwapped, forcedPalette, baseColorOnly, alreadyOurs, colorControlled, paletteAlpha,
            legacyGeometries, failed, engineCopied, keptOurs, colorModifiers,
            GetConfig().particleColors ? "" : " (particleColors off)");
    }

    RecolorStats RecolorTree(RE::NiAVObject* a_root)
    {
        RecolorStats stats;
        if (!a_root || !PaletteReady()) {
            return stats;
        }
        ForEachEffectProperty(a_root, [&](RE::BSEffectShaderProperty* a_prop, RE::BSGeometry* a_geometry, bool a_legacy) {
            if (a_legacy) {
                ++stats.legacyGeometries;
            }
            RecolorProperty(a_prop, stats);
            if (GetConfig().particleColors && a_geometry) {
                if (auto* particles = netimmerse_cast<RE::NiParticleSystem*>(a_geometry)) {
                    stats.colorModifiers += DesaturateColorModifiers(particles);
                }
            }
        });
        return stats;
    }

    bool TintLight(RE::NiLight* a_light)
    {
        if (!a_light) {
            return false;
        }
        a_light->GetLightRuntimeData().diffuse = GetConfig().lightColor;
        return true;
    }

    std::string DescribeMaterials(RE::NiAVObject* a_root)
    {
        std::string out;
        int count = 0;
        int total = 0;
        if (!a_root) {
            return "no 3D";
        }
        int grey = 0;
        ForEachEffectProperty(a_root, [&](RE::BSEffectShaderProperty* a_prop, RE::BSGeometry*, bool) {
            ++total;
            const bool isGrey = HasGreyMaterial(a_prop);
            grey += isGrey ? 1 : 0;
            if (count++ < kMaxDescribedMaterials) {
                out += std::format("{}prop=0x{:X} mat=0x{:X} grey={} paletteFlag={}", out.empty() ? "" : ", ",
                    reinterpret_cast<std::uintptr_t>(a_prop), reinterpret_cast<std::uintptr_t>(a_prop->GetBaseMaterial()),
                    isGrey ? "yes" : "no",
                    a_prop->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kGrayscaleToPaletteColor) ? "yes" : "no");
            }
        });
        return std::format("{} effect properties, {} grey{}{}", total, grey, total ? ": " : "", out);
    }

    bool HasGreyMaterial(RE::BSEffectShaderProperty* a_prop)
    {
        const auto* material = a_prop ? a_prop->GetMaterial() : nullptr;
        const auto* palette = GetPalette();
        const char* path = GetPalettePath();
        return material && ((palette && material->greyscaleTexture.get() == palette) ||
                               (path && material->greyscaleTexturePath == path));
    }
}
