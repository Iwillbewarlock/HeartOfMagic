#include "Recolor.h"

#include "Config.h"
#include "Palette.h"
#include "ParticleColors.h"
#include "LogLimit.h"
#include "Tint.h"

#include <atomic>
#include <format>
#include <memory>
#include <string_view>
#include <unordered_set>

namespace BlackFlame
{
    namespace
    {
        constexpr int kMaxDescribedMaterials = 3;

        std::atomic<bool> g_ownershipLogged{ false };

        // Properties whose recolour lost the palette texture (raw pointers as keys only; a new
        // property at a reused address would be left alone until the next load clears this).
        // Game thread only; heap-held and never freed, like the burn layer's lists.
        std::unordered_set<const RE::BSEffectShaderProperty*>& FailedProperties()
        {
            static auto* failed = std::make_unique<std::unordered_set<const RE::BSEffectShaderProperty*>>().release();
            return *failed;
        }

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

        bool HasColorControllerImpl(const RE::NiObjectNET* a_object)
        {
            static REL::Relocation<const RE::NiRTTI*> colorControllerRTTI{ RE::NiRTTI_BSEffectShaderPropertyColorController };
            for (auto* ctrl = a_object->GetControllers(); ctrl; ctrl = ctrl->GetNext()) {
                if (ctrl->GetRTTI() == colorControllerRTTI.get()) {
                    return true;
                }
            }
            return false;
        }

        void RecolorProperty(RE::BSEffectShaderProperty* a_prop, RecolorStats& a_stats, const RecolorObserver& a_observer)
        {
            ++a_stats.effectGeometries;
            if (HasColorControllerImpl(a_prop)) {
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
            using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
            using Flag8 = RE::BSShaderProperty::EShaderPropertyFlag8;
            // A property whose recolour lost the palette (remembered below), or one showing our path
            // with no texture behind it: terminal. Its palette flags stay off, and it is neither copied
            // nor desaturated again (a fresh copy every tick would compound the desaturation to black,
            // also for a copy that lost the path as well as the texture)
            if (FailedProperties().contains(a_prop) ||
                (IsPalettePath(source->greyscaleTexturePath) && !source->greyscaleTexture)) {
                if (a_prop->flags.any(Flag::kGrayscaleToPaletteColor, Flag::kGrayscaleToPaletteAlpha)) {
                    a_prop->SetFlags(Flag8::kGrayscaleToPaletteColor, false);
                    a_prop->SetFlags(Flag8::kGrayscaleToPaletteAlpha, false);
                }
                ++a_stats.failed;
                return;
            }
            // The property holds the engine's copy of ours: should it resolve the palette to another
            // texture object, the path still marks it (else the hand-art tick would recolour again);
            // the texture-less case was handled just above
            if (source->greyscaleTexture.get() == palette || IsPalettePath(source->greyscaleTexturePath)) {
                ++a_stats.alreadyOurs;
                return;
            }

            // What it was before (for a caller that will put it back, e.g. the readied hand flames)
            const PropertyOriginal original{ source->greyscaleTexture, source->greyscaleTexturePath, source->baseColor,
                a_prop->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kGrayscaleToPaletteColor),
                a_prop->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kGrayscaleToPaletteAlpha) };

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

            const bool hasFlag = a_prop->flags.all(Flag::kGrayscaleToPaletteColor);
            const bool hasAlphaFlag = a_prop->flags.all(Flag::kGrayscaleToPaletteAlpha);
            if (hasFlag || hasAlphaFlag || GetConfig().forcePalette) {
                // Re-read: the property holds the engine's copy now, and no palette flag (already
                // on, or about to be) may stay on without a palette texture behind it
                const auto* now = a_prop->GetMaterial();
                if (!now || !now->greyscaleTexture) {
                    // The property is the instance's own: its palette flags go off, so the effect
                    // technique never reads the missing texture (the shape keeps its texture's colours
                    // with the base colour already desaturated); later passes treat it as terminal
                    a_prop->SetFlags(Flag8::kGrayscaleToPaletteColor, false);
                    a_prop->SetFlags(Flag8::kGrayscaleToPaletteAlpha, false);
                    FailedProperties().insert(a_prop);  // terminal from now on (see the top)
                    ++a_stats.failed;  // counted only here: not also as a palette swap
                    int suppressed = 0;
                    if (LogLimit::Allow("palette lost", 0, suppressed)) {
                        logger::warn("BlackFlame: property 0x{:X}: the engine's material copy has no greyscale texture "
                                     "after SetMaterial - its palette flags were turned off{}",
                            reinterpret_cast<std::uintptr_t>(a_prop),
                            suppressed ? std::format(" ({} similar lines held back)", suppressed) : "");
                    }
                    return;
                }
            }
            if (hasFlag || (hasAlphaFlag && !GetConfig().forcePalette)) {
                ++a_stats.paletteSwapped;  // an alpha-only shape maps through the palette too
            } else if (GetConfig().forcePalette) {
                // Set only now, after the material with both palette pointer and path is in
                // place: the effect technique gates on the path and reads the pointer unchecked.
                // The property is the instance's own (cloned per instance; the material is the
                // pooled part), and the material check log shows its pointer per instance.
                a_prop->SetFlags(Flag8::kGrayscaleToPaletteColor, true);
                ++a_stats.forcedPalette;
            } else {
                ++a_stats.baseColorOnly;
            }
            if (a_observer) {
                a_observer(a_prop, original);
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

        constexpr std::string_view kAddOnNodePrefix = "AddOnNode";

        // Below a BSMasterParticleSystem: its particles are shared by every emitter of that kind in the
        // cell (an NPC's fire too), so no recolour touches them
        bool UnderMasterParticleSystem(RE::NiAVObject* a_object, const RE::NiAVObject* a_root)
        {
            for (RE::NiAVObject* node = a_object; node; node = node->parent) {
                if (netimmerse_cast<RE::BSMasterParticleSystem*>(node)) {
                    return true;
                }
                if (node == a_root) {
                    break;
                }
            }
            return false;
        }

        // An "AddOnNode" names an addon (BGSAddonNode) that emits into a shared master particle system
        // elsewhere in the cell: nothing of it is under this root to recolour (counted for the log)
        bool IsAddOnNode(const RE::NiAVObject* a_object)
        {
            const char* name = a_object->name.c_str();
            return name && std::string_view(name).starts_with(kAddOnNodePrefix);
        }

        // a_shared (recolours only): master particle subtrees are skipped and they and AddOn nodes counted
        template <class Visit>
        void ForEachEffectPropertyImpl(RE::NiAVObject* a_root, Visit&& a_visit, RecolorStats* a_shared = nullptr)
        {
            RE::BSVisit::TraverseScenegraphObjects(a_root, [&](RE::NiAVObject* a_object) {
                if (a_shared) {
                    const bool geometryLike = a_object->AsGeometry() || netimmerse_cast<RE::NiGeometry*>(a_object);
                    if (!geometryLike && IsAddOnNode(a_object)) {
                        ++a_shared->addOnNodes;
                    }
                    if (geometryLike && UnderMasterParticleSystem(a_object, a_root)) {
                        ++a_shared->masterSkipped;
                        return RE::BSVisit::BSVisitControl::kContinue;
                    }
                }
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
        masterSkipped += a_other.masterSkipped;
        addOnNodes += a_other.addOnNodes;
    }

    std::string RecolorStats::Describe() const
    {
        return std::format("{} effect geometries: {} palette swapped, {} palette forced (flag added), "
                           "{} base colour only (no palette flag), {} already grey, {} with a colour controller, "
                           "{} with palette alpha, {} legacy NiGeometry, {} failed; material copies: {} engine-copied, "
                           "{} ours kept; {} particle colour modifiers desaturated{}{}",
            effectGeometries, paletteSwapped, forcedPalette, baseColorOnly, alreadyOurs, colorControlled, paletteAlpha,
            legacyGeometries, failed, engineCopied, keptOurs, colorModifiers,
            GetConfig().particleColors ? "" : " (particleColors off)",
            masterSkipped + addOnNodes == 0 ? "" :
                std::format("; shared particles left alone: {} geometries under a BSMasterParticleSystem, {} AddOn "
                            "nodes", masterSkipped, addOnNodes));
    }

    RecolorStats RecolorTree(RE::NiAVObject* a_root)
    {
        return RecolorTree(a_root, {});
    }

    RecolorStats RecolorTree(RE::NiAVObject* a_root, const RecolorObserver& a_observer)
    {
        RecolorStats stats;
        if (!a_root || !PaletteReady()) {
            return stats;
        }
        ForEachEffectPropertyImpl(a_root, [&](RE::BSEffectShaderProperty* a_prop, RE::BSGeometry* a_geometry, bool a_legacy) {
            if (a_legacy) {
                ++stats.legacyGeometries;
            }
            RecolorProperty(a_prop, stats, a_observer);
            if (GetConfig().particleColors && a_geometry) {
                if (auto* particles = netimmerse_cast<RE::NiParticleSystem*>(a_geometry)) {
                    stats.colorModifiers += DesaturateColorModifiers(particles);
                }
            }
        }, &stats);
        return stats;
    }

    bool RestoreProperty(RE::BSEffectShaderProperty* a_prop, const PropertyOriginal& a_original)
    {
        auto* current = a_prop ? a_prop->GetMaterial() : nullptr;
        if (!current) {
            return false;
        }
        using Flag8 = RE::BSShaderProperty::EShaderPropertyFlag8;
        MaterialPtr copy{ static_cast<RE::BSEffectShaderMaterial*>(current->Create()) };
        if (!copy) {
            return false;
        }
        copy->CopyMembers(current);
        copy->greyscaleTexture = a_original.greyscaleTexture;
        copy->greyscaleTexturePath = a_original.greyscaleTexturePath;
        copy->baseColor = a_original.baseColor;
        // Flags off before a texture may go (a flag never stands without its texture), back on after
        if (!a_original.paletteColor) {
            a_prop->SetFlags(Flag8::kGrayscaleToPaletteColor, false);
        }
        if (!a_original.paletteAlpha) {
            a_prop->SetFlags(Flag8::kGrayscaleToPaletteAlpha, false);
        }
        a_prop->SetMaterial(copy.get(), true);
        if (a_prop->GetBaseMaterial() == copy.get()) {
            (void)copy.release();  // the property kept ours (see RecolorProperty)
        }
        const auto* now = a_prop->GetMaterial();
        const bool hasTexture = now && now->greyscaleTexture;
        a_prop->SetFlags(Flag8::kGrayscaleToPaletteColor, a_original.paletteColor && hasTexture);
        a_prop->SetFlags(Flag8::kGrayscaleToPaletteAlpha, a_original.paletteAlpha && hasTexture);
        FailedProperties().erase(a_prop);
        return true;
    }

    RE::NiLight* AsNiLight(RE::NiRefObject* a_raw, const char*& a_kind)
    {
        a_kind = "neither BSLight nor NiLight";
        if (!a_raw) {
            a_kind = "null";
            return nullptr;
        }
        if (auto* bsLight = skyrim_cast<RE::BSLight*>(a_raw)) {
            a_kind = "BSLight";
            return bsLight->light.get();
        }
        if (auto* niLight = skyrim_cast<RE::NiLight*>(a_raw)) {
            a_kind = "NiLight";
            return niLight;
        }
        return nullptr;
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
        ForEachEffectPropertyImpl(a_root, [&](RE::BSEffectShaderProperty* a_prop, RE::BSGeometry*, bool) {
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

    void ResetRecolorState()
    {
        FailedProperties().clear();
    }

    void ForEachEffectProperty(RE::NiAVObject* a_root, const EffectPropertyVisitor& a_visit)
    {
        if (a_root) {
            ForEachEffectPropertyImpl(a_root, a_visit);
        }
    }

    bool HasColorController(const RE::BSEffectShaderProperty* a_prop)
    {
        return a_prop && HasColorControllerImpl(a_prop);
    }

    bool HasGreyMaterial(RE::BSEffectShaderProperty* a_prop)
    {
        const auto* material = a_prop ? a_prop->GetMaterial() : nullptr;
        const auto* palette = GetPalette();
        // Our path counts only with a texture behind it (a broken copy is not "grey")
        return material && ((palette && material->greyscaleTexture.get() == palette) ||
                               (IsPalettePath(material->greyscaleTexturePath) && material->greyscaleTexture));
    }
}
