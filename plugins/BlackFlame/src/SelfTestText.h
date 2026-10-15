#pragma once

#include "Common.h"
#include "SelfTestProbe.h"
#include "SelfTestWorld.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// =============================================================================
// SELF-TEST TEXT - lookups and the detail text of the self-test's lines
// =============================================================================

namespace BlackFlame::SelfTestText
{
    /// How many values of a_lhs are also in a_rhs
    [[nodiscard]] int CommonCount(const std::vector<std::uintptr_t>& a_lhs, const std::vector<std::uintptr_t>& a_rhs);

    /// Every effect's hit shader (EffectSetting::data.effectShader) of a spell
    [[nodiscard]] std::vector<RE::FormID> EngineShadersOf(const RE::SpellItem* a_spell);

    [[nodiscard]] RE::Actor* ActorOf(RE::ObjectRefHandle a_handle);
    [[nodiscard]] RE::TESObjectREFR* RefOf(RE::ObjectRefHandle a_handle);

    /// The first projectile record since a_mark of that shooter side and spell
    [[nodiscard]] std::optional<SelfTestProbe::ProjectileRecord> FindProjectile(std::uint64_t a_mark, bool a_byPlayer,
        const RE::SpellItem* a_spell);

    /// The first projectile record since a_mark shot by that actor with that spell
    [[nodiscard]] std::optional<SelfTestProbe::ProjectileRecord> FindProjectileFrom(std::uint64_t a_mark,
        RE::FormID a_shooter, const RE::SpellItem* a_spell);

    [[nodiscard]] std::string DescribeProjectile(const SelfTestProbe::ProjectileRecord& a_r);
    [[nodiscard]] std::string DescribeExplosion(const SelfTestProbe::ExplosionRecord& a_r);
    [[nodiscard]] std::string DescribeShaders(const std::vector<SelfTestWorld::ShaderOn>& a_shaders);
    [[nodiscard]] std::string DescribeBurns(std::uint64_t a_mark);
}
