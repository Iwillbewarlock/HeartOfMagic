#include "ParticleColors.h"

#include "Tint.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <format>
#include <string>

namespace BlackFlame
{
    namespace
    {
        constexpr std::size_t kColorKeys = 3;
        constexpr float kMinPlausibleColor = 0.0f;
        constexpr float kMaxPlausibleColor = 2.0f;
        constexpr int kMaxKeyLogs = 5;

        std::atomic<int> g_keyLogs{ 0 };

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
        static_assert(offsetof(SimpleColorModifierLayout, colors) == kColorKeysOffset);
        static_assert(sizeof(RE::NiColorA) * kColorKeys == sizeof(float) * kColorKeyFloats);
        static_assert(sizeof(SimpleColorModifierLayout) == 0x80);

    }

    std::string DescribeColorKeys(const ColorKeys& a_keys)
    {
        std::string out;
        for (const float f : a_keys) {
            out += std::format("{}{:.3f}", out.empty() ? "" : " ", f);
        }
        return out;
    }

    void ForEachSimpleColorModifier(RE::NiParticleSystem* a_particles,
        const std::function<void(RE::NiPSysModifier*, const ColorKeys&)>& a_visit)
    {
        if (!a_particles) {
            return;
        }
        for (const auto& modifier : a_particles->GetParticleSystemRuntimeData().modifierList) {
            if (IsSimpleColorModifier(modifier.get())) {
                a_visit(modifier.get(), ReadColorKeys(modifier.get()));
            }
        }
    }

    bool IsSimpleColorModifier(const RE::NiPSysModifier* a_modifier)
    {
        static REL::Relocation<const RE::NiRTTI*> simpleColorRTTI{ RE::NiRTTI_BSPSysSimpleColorModifier };
        return a_modifier && a_modifier->GetRTTI() == simpleColorRTTI.get();
    }

    ColorKeys ReadColorKeys(const RE::NiPSysModifier* a_modifier)
    {
        ColorKeys keys{};
        std::memcpy(keys.data(), reinterpret_cast<const std::uint8_t*>(a_modifier) + kColorKeysOffset, sizeof(keys));
        return keys;
    }

    bool KeysPlausible(std::span<const float> a_keys)
    {
        const bool inRange = !a_keys.empty() && std::ranges::all_of(a_keys, [](float a_v) {
            return !std::isnan(a_v) && a_v >= kMinPlausibleColor && a_v <= kMaxPlausibleColor;
        });
        return inRange && std::ranges::any_of(a_keys, [](float a_v) { return a_v > 0.0f; });
    }

    int DesaturateColorModifiers(RE::NiParticleSystem* a_particles)
    {
        int changed = 0;
        ForEachSimpleColorModifier(a_particles, [&](RE::NiPSysModifier* a_modifier, const ColorKeys& a_keys) {
            // Read before any write; the first few are dumped either way
            const auto& keys = a_keys;
            const bool plausible = KeysPlausible(keys);
            if (g_keyLogs.fetch_add(1) < kMaxKeyLogs) {
                logger::info("BlackFlame: particle colour modifier 0x{:X} keys: {} ({})",
                    reinterpret_cast<std::uintptr_t>(a_modifier), DescribeColorKeys(keys),
                    plausible ? "desaturated" : "do not look like colours - left untouched (layout mismatch?)");
            }
            if (!plausible) {
                return;
            }
            auto* layout = reinterpret_cast<SimpleColorModifierLayout*>(a_modifier);
            bool any = false;
            for (auto& color : layout->colors) {
                any = Desaturate(color) || any;
            }
            if (any) {
                ++changed;
            }
        });
        return changed;
    }
}
