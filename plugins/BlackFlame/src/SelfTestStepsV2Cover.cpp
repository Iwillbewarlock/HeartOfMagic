// =============================================================================
// SELF-TEST v2 STEPS - T0-fresh, T0-cover, E1m (v2 expansion C5)
// =============================================================================
//   T0-fresh  the copies freshness (CopyFreshness.h): fresh PASS; stale, none and pluginMissing FAIL with why.
//   T0-cover  every copy-type form of this session's fire closure has a filled copy or was excluded with a reason
//             (FAIL lists the first 20 missing: type, FormID, the spell that reached it and its plugin). Per fire spell the
//             costliest effect's projectile (and its dual cast projectile), that projectile's explosion and the casting
//             art are counted apart. INFO: copies without a grey model (the builder kept the source's), and copies no
//             swap point uses (a casting art reached only from an effect that is not a costliest one, a hazard reached
//             only as a non-fire spawn-hazard effect's form).
//   E1m       a modded fire spell picked by data - its costliest effect's projectile has a copy, its defining file comes
//             last in the load order (tie: the smaller FormID); INFO when only the five official masters qualify. The
//             player launches it at the target (as E1a): the launch takes the projectile copy, and when its effect or
//             projectile has an explosion with a copy, an explosion copy is seen within 1.5 s.

#include "SelfTestCtx.h"

#include "CopyFreshness.h"
#include "CopySwap.h"
#include "FireClosure.h"
#include "FireSet.h"
#include "FormCopies.h"
#include "SelfTestLog.h"
#include "SelfTestText.h"

#include <array>
#include <format>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace BlackFlame::SelfTest
{
    namespace
    {
        using namespace SelfTestText;

        constexpr std::size_t kMaxMissingLines = 20;
        constexpr auto kExplosionWait = std::chrono::milliseconds(1500);
        constexpr auto kSettle = std::chrono::milliseconds(2500);  // an earlier step's explosion has ended (as E1c)
        constexpr std::array<std::string_view, 5> kOfficialMasters{ "skyrim.esm", "update.esm", "dawnguard.esm", "hearthfires.esm",
            "dragonborn.esm" };

        RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

        std::string FileOf(const RE::TESForm* a_form)
        {
            const auto* file = a_form ? a_form->GetFile(0) : nullptr;
            return file ? std::string(file->GetFilename()) : std::string("?");
        }

        bool IsOfficial(const RE::TESForm* a_form)
        {
            std::string name = FileOf(a_form);
            for (auto& c : name) {
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            }
            for (const auto master : kOfficialMasters) {
                if (name == master) {
                    return true;
                }
            }
            return false;
        }

        // A copy no swap point uses: ARTO only from non-costliest casting art edges; HAZD only from non-fire spawn-hazard edges
        bool Unused(const ClosureForm& a_form)
        {
            if (a_form.kind == CopyKind::kArt) {
                return (a_form.edges & ~kEdgeCastingArt) == 0;
            }
            if (a_form.kind == CopyKind::kHazard) {
                return (a_form.edges & ~kEdgeSpawnHazard) == 0;
            }
            return false;
        }

        struct Part
        {
            int present = 0;
            int copied = 0;

            void Add(const RE::TESForm* a_form)
            {
                if (a_form) {
                    ++present;
                    copied += CopyOf(a_form) ? 1 : 0;
                }
            }

            [[nodiscard]] std::string Text() const { return std::format("{} of {} copied", copied, present); }
        };

        struct E1mState
        {
            RE::SpellItem* spell = nullptr;
            const RE::BGSExplosion* explosion = nullptr;  // the explosion the engine gives the launch, when it has a copy
            std::uint64_t mark = 0;
            Aim aim;  // F11: judged only after a confirmed hit, recast up to three times (as E1a)
        };
        E1mState g_e1m;

        enum E1mPhase : int
        {
            kSettling,
            kJudging
        };

        // F11/G2: the explosion the engine uses: the costliest effect (GetAVEffect) - with an area, that MGEF's explosion only
        // (none: no explosion expected); without one, the projectile's own
        const RE::BGSExplosion* ExpectedExplosion(const RE::SpellItem* a_spell)
        {
            const auto* costliest = a_spell->GetAVEffect();
            if (!costliest) {
                return nullptr;
            }
            std::uint32_t area = 0;
            for (const auto* item : a_spell->effects) {
                if (item && item->baseEffect == costliest) {
                    area = item->effectItem.area;
                    break;
                }
            }
            if (area > 0) {
                return costliest->data.explosion;
            }
            return costliest->data.projectileBase ? costliest->data.projectileBase->data.explosionType : nullptr;
        }

        // F11: a candidate launches a projectile at its target: aimed delivery, its costliest effect has a projectile with a
        // copy. Target-location spells (runes) are left out: their projectile lands, so no hit on the target confirms it.
        bool LaunchesAtTarget(const RE::SpellItem* a_spell)
        {
            const auto* costliest = a_spell->GetAVEffect();
            const auto* projectile = costliest ? costliest->data.projectileBase : nullptr;
            // G2: fire and forget only (a concentration spell's one launch is not what E1a's aim judges)
            return a_spell->GetDelivery() == RE::MagicSystem::Delivery::kAimed &&
                   a_spell->GetCastingType() == RE::MagicSystem::CastingType::kFireAndForget && projectile && CopyOf(projectile);
        }
    }

    // G2: Aim's only clock is hitAt (SelfTestCtx.h); the settle and recast waits read Ctx::lastCast, which SelfTest.cpp shifts
    void ShiftCoverClocks(Clock::duration a_paused)
    {
        if (g_e1m.aim.hitAt) {
            *g_e1m.aim.hitAt += a_paused;
        }
    }

    bool StepV2Cover(Ctx&, bool)
    {
        const auto& fresh = GetFreshness();
        SelfTestLog::Check("T0-fresh", fresh.state == CopyFreshness::kFresh, std::format("copies {}: {}", FreshnessName(fresh.state),
            fresh.detail));

        const auto& closure = LastFireClosure();
        std::size_t missing = 0;
        std::size_t unused = 0;
        std::string lines;
        for (const auto& form : closure.forms) {
            if (!CopyOf(form.form)) {
                if (++missing <= kMaxMissingLines) {
                    const auto* spell = RE::TESForm::LookupByID(form.viaSpell);
                    lines += std::format("{}{} {:08X} ({}) via spell {:08X} ({})", lines.empty() ? "" : "; ", CopyKindTag(form.kind),
                        form.form->GetFormID(), FileOf(form.form), form.viaSpell, FileOf(spell));
                }
                continue;
            }
            unused += Unused(form) ? 1 : 0;
        }
        SelfTestLog::Check("T0-cover", missing == 0 && !closure.forms.empty(), std::format("{} closure forms, {} excluded, {} without a "
            "copy{}{}", closure.forms.size(), closure.excluded.size(), missing, missing ? ": " : "", lines));
        Part projectiles, dualProjectiles, explosions, castingArts;
        for (const auto* spell : FireSpellList()) {
            const auto* costliest = spell->GetAVEffect();
            if (!costliest) {
                continue;
            }
            const auto* projectile = costliest->data.projectileBase;
            projectiles.Add(projectile);
            dualProjectiles.Add(costliest->data.dualCastData ? costliest->data.dualCastData->data.pProjectile : nullptr);
            explosions.Add(ExpectedExplosion(spell));  // G2: the engine's rule, as E1m
            castingArts.Add(costliest->data.castingArt);
        }
        SelfTestLog::Info("T0-cover", std::format("per fire spell, its costliest effect: projectiles {}, dual cast projectiles {}, "
            "explosions {}, casting arts {}", projectiles.Text(), dualProjectiles.Text(), explosions.Text(), castingArts.Text()));
        const auto counts = GetCopyCounts();
        SelfTestLog::Info("T0-cover", std::format("copied, not grey: {} (the builder kept the source's model); copies no swap point "
            "uses: {}", counts.greyless, unused));
        return true;
    }

    bool StepE1Modded(Ctx& a_c, bool a_first)
    {
        if (a_first) {
            g_e1m = {};
            auto* dh = RE::TESDataHandler::GetSingleton();
            std::unordered_map<const RE::TESFile*, std::size_t> order;
            std::size_t index = 0;
            if (dh) {
                for (const auto* file : dh->files) {
                    order.emplace(file, index++);
                }
            }
            std::size_t bestAt = 0;
            for (auto* spell : FireSpellList()) {
                if (!LaunchesAtTarget(spell)) {
                    continue;
                }
                const auto it = order.find(spell->GetFile(0));
                if (it == order.end()) {
                    continue;  // F11: a file not in the load order map has no place (no tie with Skyrim.esm at 0)
                }
                if (!g_e1m.spell || it->second > bestAt || (it->second == bestAt && spell->GetFormID() < g_e1m.spell->GetFormID())) {
                    g_e1m.spell = spell;
                    bestAt = it->second;
                }
            }
            if (!g_e1m.spell) {
                SelfTestLog::Skip("E1m", "no aimed fire spell whose costliest effect's projectile has a copy");
                return true;
            }
            if (IsOfficial(g_e1m.spell)) {
                SelfTestLog::Info("E1m", std::format("only the official masters qualify (last pick {:08X} in {}): not judged",
                    g_e1m.spell->GetFormID(), FileOf(g_e1m.spell)));
                return true;
            }
            if (!NeedActors(a_c, "E1m")) {
                return true;
            }
            const auto* explosion = ExpectedExplosion(g_e1m.spell);
            g_e1m.explosion = explosion && CopyOf(explosion) ? explosion : nullptr;
            SelfTestLog::Info("E1m", std::format("picked spell {:08X} '{}' ({}), projectile {:08X}, the engine's explosion {}",
                g_e1m.spell->GetFormID(), g_e1m.spell->GetName(), FileOf(g_e1m.spell),
                g_e1m.spell->GetAVEffect()->data.projectileBase->GetFormID(),
                explosion ? std::format("{:08X}{}", explosion->GetFormID(), g_e1m.explosion ? " (has a copy)" : " (no copy: not judged)") :
                            std::string("none")));
            a_c.phase = kSettling;
            return false;
        }
        if (a_c.phase == kSettling) {
            // An earlier step's explosion (E1c ends with a Fireball) must not land after the mark
            if (a_c.lastCast && Clock::now() - *a_c.lastCast < kSettle) {
                a_c.waitingFor = "the earlier explosion to end";
                return false;
            }
            g_e1m.mark = SelfTestProbe::Mark();
            if (!CastAtTarget(a_c, Player(), g_e1m.spell, "E1m")) {
                return true;
            }
            StartAim(g_e1m.aim, g_e1m.mark);
            a_c.phase = kJudging;
            return false;
        }
        // F11: judged only after the hit is confirmed (recast up to three times, as E1a). G2: the launches and explosions are
        // read from the FIRST cast's mark, so a recast never hides the first cast's launch
        const auto aim = ConfirmHit(a_c, g_e1m.aim, g_e1m.spell, "E1m");
        g_e1m.aim.newCast = false;
        if (aim == AimResult::kGaveUp) {
            return true;
        }
        if (aim == AimResult::kWaiting) {
            return false;
        }
        const auto playerId = Player()->GetFormID();
        bool launched = false;
        bool copied = false;
        for (const auto& r : SelfTestProbe::LaunchesSince(g_e1m.mark)) {
            if (r.shooter == playerId && r.spell == g_e1m.spell->GetFormID()) {
                launched = true;
                copied |= r.copyBase != 0;
            }
        }
        const auto* explosionCopy = g_e1m.explosion ? CopyOf(g_e1m.explosion) : nullptr;
        bool explosionSeen = false;
        for (const auto& r : SelfTestProbe::ExplosionsSince(g_e1m.mark)) {
            explosionSeen |= explosionCopy && r.byPlayer && r.copyBase == explosionCopy->GetFormID();
        }
        if (explosionCopy && !explosionSeen && Clock::now() - *g_e1m.aim.hitAt < kExplosionWait) {
            a_c.waitingFor = "the modded spell's explosion copy";
            return false;
        }
        SelfTestLog::Check("E1m", launched && copied && (!explosionCopy || explosionSeen), std::format("spell {:08X}: hit confirmed, "
            "launch {}, projectile copy {}, explosion copy {}", g_e1m.spell->GetFormID(), launched ? "recorded" : "NOT recorded",
            copied ? "yes" : "NO", explosionCopy ? (explosionSeen ? std::format("{:08X} seen", explosionCopy->GetFormID()) :
                std::format("{:08X} NOT seen within 1.5 s of the hit", explosionCopy->GetFormID())) : std::string("not expected")));
        return true;
    }
}
