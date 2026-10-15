#include "ParticleColors.h"

#include "Tint.h"

#include <atomic>
#include <cmath>
#include <format>
#include <string>

namespace BlackFlame
{
    namespace
    {
        constexpr std::size_t kColorKeys = 3;
        // A colour key component outside this range (or NaN) means the layout below does not
        // match this runtime: the bytes are not colours, so nothing is written. 0..2 leaves room
        // for HDR-ish keys above 1 while catching floats that are really pointers or counts.
        constexpr float kMinPlausibleColor = 0.0f;
        constexpr float kMaxPlausibleColor = 2.0f;
        constexpr int kMaxImplausibleLogs = 5;

        std::atomic<int> g_implausibleLogs{ 0 };

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

        bool Plausible(float a_value)
        {
            return !std::isnan(a_value) && a_value >= kMinPlausibleColor && a_value <= kMaxPlausibleColor;
        }

        bool LooksLikeColors(const SimpleColorModifierLayout& a_layout)
        {
            for (const auto& c : a_layout.colors) {
                if (!Plausible(c.red) || !Plausible(c.green) || !Plausible(c.blue) || !Plausible(c.alpha)) {
                    return false;
                }
            }
            return true;
        }

        std::string Dump(const SimpleColorModifierLayout& a_layout)
        {
            std::string out;
            for (const auto& c : a_layout.colors) {
                out += std::format("{}({:.3f} {:.3f} {:.3f} {:.3f})", out.empty() ? "" : " ", c.red, c.green, c.blue,
                    c.alpha);
            }
            return out;
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
            // Read before any write: skip a modifier whose keys do not read as colours
            if (!LooksLikeColors(*layout)) {
                if (g_implausibleLogs.fetch_add(1) < kMaxImplausibleLogs) {
                    logger::warn("BlackFlame: particle colour modifier 0x{:X} keys do not look like colours, left "
                                 "untouched (layout mismatch?): {}",
                        reinterpret_cast<std::uintptr_t>(layout), Dump(*layout));
                }
                continue;
            }
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
