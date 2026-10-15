#include "SelfTestText.h"

#include "GameThread.h"

#include <algorithm>
#include <format>

namespace BlackFlame::SelfTestText
{
    int CommonCount(const std::vector<std::uintptr_t>& a_lhs, const std::vector<std::uintptr_t>& a_rhs)
    {
        int count = 0;
        for (const auto value : a_lhs) {
            count += std::ranges::find(a_rhs, value) != a_rhs.end() ? 1 : 0;
        }
        return count;
    }

    std::vector<RE::FormID> EngineShadersOf(const RE::SpellItem* a_spell)
    {
        std::vector<RE::FormID> out;
        if (!a_spell) {
            return out;
        }
        for (const auto* effect : a_spell->effects) {
            if (effect && effect->baseEffect && effect->baseEffect->data.effectShader) {
                out.push_back(effect->baseEffect->data.effectShader->GetFormID());
            }
        }
        return out;
    }

    RE::Actor* ActorOf(RE::ObjectRefHandle a_handle)
    {
        const auto ref = a_handle.get();
        return ref ? ref->As<RE::Actor>() : nullptr;
    }

    RE::TESObjectREFR* RefOf(RE::ObjectRefHandle a_handle)
    {
        return a_handle.get().get();  // the handle's actor stays alive while spawned
    }

    std::optional<SelfTestProbe::ProjectileRecord> FindProjectile(std::uint64_t a_mark, bool a_byPlayer, const RE::SpellItem* a_spell)
    {
        for (auto& r : SelfTestProbe::ProjectilesSince(a_mark)) {
            if (r.byPlayer == a_byPlayer && a_spell && r.spell == a_spell->GetFormID()) {
                return r;
            }
        }
        return std::nullopt;
    }

    std::optional<SelfTestProbe::ProjectileRecord> FindProjectileFrom(std::uint64_t a_mark, RE::FormID a_shooter,
        const RE::SpellItem* a_spell)
    {
        for (auto& r : SelfTestProbe::ProjectilesSince(a_mark)) {
            if (a_shooter && r.shooter == a_shooter && a_spell && r.spell == a_spell->GetFormID()) {
                return r;
            }
        }
        return std::nullopt;
    }

    std::string DescribeProjectile(const SelfTestProbe::ProjectileRecord& a_r)
    {
        return std::format("spell {:08X} ({}) recoloured={} [{}] in {} us; 3D after: {} effect properties, {} with "
                           "palette path, {} with palette pointer, {} both, {} grey; hook thread {} ({}), game thread {}",
            a_r.spell, a_r.kind, a_r.recolored, a_r.stats.Describe(), a_r.recolorMicros, a_r.after.effectProperties,
            a_r.after.withPalettePath, a_r.after.withPalettePointer, a_r.after.withBoth, a_r.after.withGrey, a_r.hookThread,
            a_r.hookOnGameThread ? "game thread" : "other thread", g_gameThreadNativeId.load());
    }

    std::string DescribeExplosion(const SelfTestProbe::ExplosionRecord& a_r)
    {
        return std::format("explosion {:08X} byPlayer={} recoloured={} [{}]; owner {:08X}, cause actor {:08X}, "
                           "magicCaster {} currentSpell {:08X}, light at Set3D {}; 3D after: {} with palette path; "
                           "hook thread {} (game {})",
            a_r.base, a_r.byPlayer, a_r.recolored, a_r.stats.Describe(), a_r.owner, a_r.causeActor,
            a_r.hasMagicCaster ? "set" : "null", a_r.casterSpell, a_r.lightAtSet3D ? "yes" : "no",
            a_r.after.withPalettePath, a_r.hookThread, g_gameThreadNativeId.load());
    }

    std::string DescribeShaders(const std::vector<SelfTestWorld::ShaderOn>& a_shaders)
    {
        std::string out;
        for (const auto& s : a_shaders) {
            out += std::format("{}{:08X}{}", out.empty() ? "" : ", ", s.effectData, s.finished ? " (finished)" : "");
        }
        return out.empty() ? "none" : out;
    }

    std::string DescribeBurns(std::uint64_t a_mark)
    {
        std::string out;
        for (const auto& b : SelfTestProbe::BurnsSince(a_mark)) {
            out += std::format("{}[spell {:08X} on {:08X}: engine {:08X} swapped, thread {}]", out.empty() ? "" : " ",
                b.spell, b.target, b.engineShader, b.thread);
        }
        return out.empty() ? "no burn swap recorded" : out;
    }
}
