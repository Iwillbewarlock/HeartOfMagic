#include "ParticleColors.h"

namespace BlackFlame
{
    namespace
    {
        // Rec. 709 luma weights (as Recolor.cpp)
        constexpr float kLumaRed = 0.2126f;
        constexpr float kLumaGreen = 0.7152f;
        constexpr float kLumaBlue = 0.0722f;
        constexpr std::size_t kColorKeys = 3;

        // Layout from Jonahex/CommonLibSSE include/RE/B/BSPSysSimpleColorModifier.h
        // (commit 2bc2543c), not in this CommonLib, unverified on AE. Never
        // constructed: only a modifier the engine made, matched by its NiRTTI, is
        // viewed through it.
        struct SimpleColorModifierLayout : RE::NiPSysModifier
        {
            RE::NiColorA colors[kColorKeys];  // 30
            float fadeInPercent;              // 60
            float fadeOutPercent;             // 64
            float color1EndPercent;           // 68
            float color2StartPercent;         // 6C
            float color2EndPercent;           // 70
            float color3StartPercent;         // 74
            bool isMeshParticle;              // 78
            std::uint8_t pad79[7];            // 79
        };
        static_assert(sizeof(RE::NiPSysModifier) == 0x30);
        static_assert(offsetof(SimpleColorModifierLayout, colors) == 0x30);
        static_assert(sizeof(SimpleColorModifierLayout) == 0x80);

        bool Desaturate(RE::NiColorA& a_color)
        {
            const float grey = a_color.red * kLumaRed + a_color.green * kLumaGreen + a_color.blue * kLumaBlue;
            const bool changed = a_color.red != grey || a_color.green != grey || a_color.blue != grey;
            a_color.red = grey;
            a_color.green = grey;
            a_color.blue = grey;
            return changed;
        }
    }

    int DesaturateColorModifiers(RE::NiParticleSystem* a_particles)
    {
        static REL::Relocation<const RE::NiRTTI*> simpleColorRTTI{ RE::NiRTTI_BSPSysSimpleColorModifier };
        int changed = 0;
        for (const auto& modifier : a_particles->GetParticleSystemRuntimeData().modifierList) {
            if (!modifier || modifier->GetRTTI() != simpleColorRTTI.get()) {
                continue;
            }
            auto* layout = reinterpret_cast<SimpleColorModifierLayout*>(modifier.get());
            bool any = false;
            for (auto& color : layout->colors) {
                any = Desaturate(color) || any;
            }
            if (any) {
                ++changed;
            }
        }
        return changed;
    }
}
