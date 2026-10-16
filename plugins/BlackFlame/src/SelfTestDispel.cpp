#include "SelfTestWorld.h"

#include "Forms.h"

#include <format>

// =============================================================================
// The self-test's Stoke dispel (v2 fix C4)
// =============================================================================
// Run 11:11 (1.6.1170): ActiveEffect::Dispel(true) from our task did not hold three times (E2, T8, T9 each found
// the effect live again; no remove event, so the buff flag stayed on). Read on 1.7.104 SkyrimSE.exe:
//   - ActiveEffect::Dispel (id 34061, RVA 0x5bb3b0) sets flags bit 18 with a plain load/bts/store of +0x7C, then
//     tail-calls the effect's update (id 34092) - no lock. ActiveEffect updates write +0x7C the same way (e.g.
//     `or [rbx+0x7C], 0x20` at 0x5bcf8d), so a Dispel on another thread while the target is updating can lose
//     its bit. Our tasks ran on several job threads (T-threads), not only the main thread.
//   - MagicTarget::DispelEffect (id 34505, RVA 0x5d0ed0) walks the list matching spell, caster handle and
//     effect, and when the target's flag byte at +0x10 bit 0 (inside its update) is set, it queues the dispel
//     (id 34551, the postUpdateDispelList at +0x08) instead of calling Dispel at once; otherwise it calls
//     Dispel(true). That guard is for re-entrancy (a dispel from inside the target's own effect update); its
//     unlocked check gives no cross-thread safety (D3 review).
// v2 fix D1 gives the cross-thread part: this now runs from the main-thread queue, after the frame's jobs. The
// dispel goes through MagicTarget::DispelEffect only (no direct Dispel fallback: D3), with each effect's flags
// before and after logged.

namespace BlackFlame::SelfTestWorld
{
    namespace
    {
        // MagicTarget +0x10 bit 0: the target is inside its own effect update (1.7.104 DispelEffect at 0x5d0f7d:
        // `test byte ptr [r14+0x10], 1`; CommonLib names the field pad10). Read for the log only.
        constexpr std::ptrdiff_t kTargetUpdatingOffset = 0x10;
        constexpr std::uint8_t kTargetUpdatingBit = 0x1;

        RE::MagicTarget* PlayerTarget()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            return player ? player->AsMagicTarget() : nullptr;
        }

        RE::ActiveEffect* FindById(RE::MagicTarget* a_target, std::uint16_t a_id)
        {
            auto* list = a_target ? a_target->GetActiveEffectList() : nullptr;
            if (!list) {
                return nullptr;
            }
            for (auto* effect : *list) {
                if (effect && effect->usUniqueID == a_id) {
                    return effect;
                }
            }
            return nullptr;
        }

        bool TargetUpdating(const RE::MagicTarget* a_target)
        {
            const auto* raw = reinterpret_cast<const std::uint8_t*>(a_target);
            return (raw[kTargetUpdatingOffset] & kTargetUpdatingBit) != 0;
        }

        std::string FlagsNow(RE::MagicTarget* a_target, std::uint16_t a_id)
        {
            const auto* effect = FindById(a_target, a_id);
            return effect ? std::format("0x{:08X}", effect->flags.underlying()) : std::string("gone from the list");
        }

        bool IsLiveStoke(const RE::ActiveEffect* a_effect, const RE::BGSKeyword* a_keyword)
        {
            const auto* base = a_effect ? a_effect->GetBaseObject() : nullptr;
            return base && base->HasKeyword(a_keyword) &&
                   !a_effect->flags.any(RE::ActiveEffect::Flag::kDispelled, RE::ActiveEffect::Flag::kInactive);
        }
    }

    int DispelStoke()
    {
        auto* target = PlayerTarget();
        const auto* keyword = GetForms().stokedKeyword;
        auto* effects = target ? target->GetActiveEffectList() : nullptr;
        if (!effects || !keyword) {
            return 0;
        }
        // Ids first, no ActiveEffect pointer kept across a dispel (it may change the list)
        std::vector<std::uint16_t> ids;
        for (auto* effect : *effects) {
            if (IsLiveStoke(effect, keyword)) {
                ids.push_back(effect->usUniqueID);
            }
        }
        int dispelled = 0;
        for (const auto id : ids) {
            auto* effect = FindById(target, id);
            if (!effect) {
                continue;
            }
            const auto before = effect->flags.underlying();
            const bool updating = TargetUpdating(target);
            auto caster = effect->caster;  // DispelEffect takes the handle by reference
            const bool matched = target->DispelEffect(effect->spell, caster, effect);  // RE/M/MagicTarget.h:158
            logger::info("BlackFlame: self-test dispel: Stoke effect {} flags 0x{:08X} -> {} via MagicTarget::DispelEffect "
                         "(matched: {}, target inside its update: {}), thread {}", id, before, FlagsNow(target, id),
                matched ? "yes" : "NO (nothing dispelled)", updating ? "yes (queued by the engine)" : "no",
                REX::W32::GetCurrentThreadId());
            if (matched) {
                ++dispelled;
            }
        }
        return dispelled;
    }

    std::string DescribeStokeEffects()
    {
        auto* target = PlayerTarget();
        const auto* keyword = GetForms().stokedKeyword;
        auto* effects = target ? target->GetActiveEffectList() : nullptr;
        if (!effects || !keyword) {
            return "no player effect list";
        }
        std::string out;
        for (const auto* effect : *effects) {
            const auto* base = effect ? effect->GetBaseObject() : nullptr;
            if (!base || !base->HasKeyword(keyword)) {
                continue;
            }
            out += std::format("{}effect {} flags 0x{:08X} ({}), {:.1f}/{:.1f} s", out.empty() ? "" : "; ",
                effect->usUniqueID, effect->flags.underlying(), IsLiveStoke(effect, keyword) ? "live" : "dispelled/inactive",
                effect->elapsedSeconds, effect->duration);
        }
        return out.empty() ? std::string("no Stoke effect listed") :
                             out + std::format(", target inside its update: {}", TargetUpdating(target) ? "yes" : "no");
    }
}
