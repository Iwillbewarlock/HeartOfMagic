// =============================================================================
// SELF-TEST - bound fire weapons (bound-weapon stage 1): their arrows take the PROJ copy
// =============================================================================
//   T0-bound        per bound fire weapon seed (FireSet.h BoundFireSeeds): its MGEF's projectile and, for a bow or crossbow,
//                   the bound-arrow AMMOs' projectiles each have a copy (PASS), are excluded from the closure or have no source
//                   mesh (INFO, the reason), or have no copy (FAIL: run the builder again). No seed: INFO.
//   E-bound-launch  while stoked: the first bow seed with a spell is cast on the player (CastSpellImmediate) and, once the
//                   weapon is on the runtime bound fire list (PlayerArt.h), an arrow is launched through the AMMO LaunchData
//                   constructor (Projectile.cpp:224: projectileBase = ammo.data.projectile, weaponSource, ammoSource):
//                   PASS when the Launch detour's record has the copy of that PROJ. E-bound-npc: the same ammo and weapon from
//                   the deer caster is not swapped (contamination check).
//   E-bound-off     after T8 (the buff is off): the same launch from the player is not swapped; then the spell is dispelled.
// A seed without a spell, or no bow seed: SKIP with the reason. The launched arrows fly off along the player's heading;
// only the launch records are judged.

#include "SelfTestCtx.h"

#include "BuffState.h"
#include "FireClosure.h"
#include "FireSet.h"
#include "FormCopies.h"
#include "PlayerArt.h"
#include "SelfTestLog.h"
#include "SelfTestText.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace BlackFlame::SelfTest
{
    namespace
    {
        using namespace SelfTestText;

        constexpr auto kListWait = std::chrono::milliseconds(3000);  // the ticker (100 ms while stoked) refreshes the list
        constexpr float kArrowHeight = 100.0f;                      // launch origin above the shooter's feet
        constexpr float kArrowForward = 60.0f;                      // and ahead of it, clear of its own body
        constexpr std::size_t kMaxListed = 8;

        RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

        enum Phase : int
        {
            kWaitList = 1,
            kJudge = 2
        };

        struct BoundPick
        {
            const BoundFireSeed* seed = nullptr;
            RE::TESAmmo* ammo = nullptr;
            RE::BGSProjectile* arrow = nullptr;
        };

        struct BoundState
        {
            std::optional<BoundPick> pick;
            std::uint64_t mark = 0;
            bool npcLaunched = false;
        };
        BoundState g_bound;

        // A closure form's state as a short text: "copy XXXXXXXX", "excluded: why", "source mesh absent", or empty (no copy)
        std::string CopyState(const RE::TESForm* a_form)
        {
            if (const auto* copy = CopyOf(a_form)) {
                return std::format("copy {:08X}", copy->GetFormID());
            }
            for (const auto& excluded : LastFireClosure().excluded) {
                if (excluded.form == a_form) {
                    return "excluded: " + excluded.reason;
                }
            }
            return IsSourceMeshAbsent(a_form) ? "source mesh absent" : std::string();
        }

        bool IsBowSeed(const BoundFireSeed& a_seed) { return a_seed.weapon->IsBow() || a_seed.weapon->IsCrossbow(); }

        // Launches a_ammo from a_shooter with a_weapon as its weapon source, along the shooter's heading
        bool LaunchArrow(RE::Actor* a_shooter, RE::TESAmmo* a_ammo, RE::TESObjectWEAP* a_weapon, std::string& a_detail)
        {
            if (!a_shooter || !a_shooter->GetParentCell()) {
                a_detail = "no shooter in a cell";
                return false;
            }
            const float heading = a_shooter->GetAngleZ();
            RE::NiPoint3 origin = a_shooter->GetPosition();
            origin.x += std::sin(heading) * kArrowForward;
            origin.y += std::cos(heading) * kArrowForward;
            origin.z += kArrowHeight;
            RE::Projectile::ProjectileRot angles{};
            angles.z = heading;
            RE::Projectile::LaunchData data(a_shooter, origin, angles, a_ammo, a_weapon);
            RE::ProjectileHandle handle;
            RE::Projectile::Launch(&handle, data);
            a_detail = std::format("{:08X} launched ammo {:08X} (PROJ {:08X}) with weapon {:08X}: {}", a_shooter->GetFormID(),
                a_ammo->GetFormID(), a_ammo->GetRuntimeData().data.projectile->GetFormID(), a_weapon->GetFormID(),
                handle ? "projectile handle returned" : "NO projectile handle");
            return static_cast<bool>(handle);
        }

        std::optional<SelfTestProbe::LaunchRecord> LaunchBy(RE::FormID a_shooter, RE::FormID a_base)
        {
            for (const auto& r : SelfTestProbe::LaunchesSince(g_bound.mark)) {
                if (r.shooter == a_shooter && r.base == a_base) {
                    return r;
                }
            }
            return std::nullopt;
        }

        std::optional<BoundPick> PickBow(std::string& a_why)
        {
            const auto ammo = BoundArrowAmmo();
            for (const auto& seed : BoundFireSeeds()) {
                if (!IsBowSeed(seed) || !seed.spell) {
                    continue;
                }
                // The AMMO whose PROJ is the MGEF's own (the arrow the spell's script hands out, by convention), else the first
                const auto* own = seed.effect->data.projectileBase;
                auto it = std::ranges::find_if(ammo, [&](const RE::TESAmmo* a) { return a->GetRuntimeData().data.projectile == own; });
                if (it == ammo.end() && !ammo.empty()) {
                    it = ammo.begin();
                }
                if (it == ammo.end()) {
                    a_why = "no AMMO with the WeapTypeBoundArrow keyword";
                    return std::nullopt;
                }
                return BoundPick{ &seed, *it, (*it)->GetRuntimeData().data.projectile };
            }
            a_why = BoundFireSeeds().empty() ? "no bound fire weapon in this load order" : "no bound fire bow or crossbow with a spell";
            return std::nullopt;
        }
    }

    bool StepBoundCover(Ctx&, bool)
    {
        const auto& seeds = BoundFireSeeds();
        const auto ammo = BoundArrowAmmo();
        const auto& closure = LastFireClosure();
        SelfTestLog::Info("T0-bound", std::format("{} bound fire weapon seed(s); {} bound-arrow AMMO; closure: {} seeds walked, {} "
            "AMMO", seeds.size(), ammo.size(), closure.boundSeeds, closure.boundArrowAmmo));
        for (const auto& seed : seeds) {
            std::vector<const RE::TESForm*> arrows;
            if (seed.effect->data.projectileBase) {
                arrows.push_back(seed.effect->data.projectileBase);
            }
            if (IsBowSeed(seed)) {
                for (const auto* a : ammo) {
                    const auto* projectile = a->GetRuntimeData().data.projectile;
                    if (std::ranges::find(arrows, projectile) == arrows.end()) {
                        arrows.push_back(projectile);
                    }
                }
            }
            int missing = 0;
            std::string text;
            for (std::size_t i = 0; i < arrows.size(); ++i) {
                const auto state = CopyState(arrows[i]);
                missing += state.empty() ? 1 : 0;
                if (i < kMaxListed) {
                    text += std::format("{}PROJ {:08X}: {}", text.empty() ? "" : "; ", arrows[i]->GetFormID(),
                        state.empty() ? "NO copy (run the builder again)" : state);
                }
            }
            const auto line = std::format("weapon {:08X} '{}' from effect {:08X} (fire {}{}), spell {:08X}: {}",
                seed.weapon->GetFormID(), seed.weapon->GetName(), seed.effect->GetFormID(), seed.byEffect ? "effect" : "enchantment",
                seed.byEffect && seed.byEnchantment ? " and enchantment" : "", seed.spell ? seed.spell->GetFormID() : 0,
                arrows.empty() ? "no arrow PROJ (a melee weapon without a projectile)" : text);
            if (arrows.empty()) {
                SelfTestLog::Info("T0-bound", line);
            } else {
                SelfTestLog::Check("T0-bound", missing == 0, line);
            }
        }
        return true;
    }

    bool StepBoundLaunch(Ctx& a_c, bool a_first)
    {
        auto* player = Player();
        if (a_first) {
            g_bound = {};
            std::string why;
            g_bound.pick = PickBow(why);
            if (!g_bound.pick) {
                SelfTestLog::Skip("E-bound-launch", "not judged: " + why);
                return true;
            }
            if (!IsStoked()) {
                SelfTestLog::Fail("E-bound-launch", "not stoked at the start");
                g_bound.pick.reset();
                return true;
            }
            const auto& pick = *g_bound.pick;
            if (!SelfTestWorld::Cast(player, pick.seed->spell, player)) {
                SelfTestLog::Fail("E-bound-launch", std::format("CastSpellImmediate refused spell {:08X}", pick.seed->spell->GetFormID()));
                g_bound.pick.reset();
                return true;
            }
            a_c.lastCast = Clock::now();
            a_c.phase = kWaitList;
            a_c.waitingFor = "the bound weapon on the bound fire list";
            return false;
        }
        const auto& pick = *g_bound.pick;
        if (a_c.phase == kWaitList) {
            if (!IsBoundFireWeapon(pick.seed->weapon)) {
                if (Clock::now() - a_c.lastCast.value_or(a_c.stepStart) < kListWait) {
                    return false;
                }
                SelfTestLog::Fail("E-bound-launch", std::format("weapon {:08X} never reached the bound fire list within {} ms of "
                    "casting {:08X}", pick.seed->weapon->GetFormID(), kListWait.count(), pick.seed->spell->GetFormID()));
                return true;
            }
            g_bound.mark = SelfTestProbe::Mark();
            std::string detail;
            const bool launched = LaunchArrow(player, pick.ammo, pick.seed->weapon, detail);
            SelfTestLog::Info("E-bound-launch", detail);
            if (auto* deer = ActorOf(a_c.caster)) {
                std::string npcDetail;
                g_bound.npcLaunched = LaunchArrow(deer, pick.ammo, pick.seed->weapon, npcDetail);
                SelfTestLog::Info("E-bound-npc", npcDetail);
            }
            if (!launched) {
                SelfTestLog::Fail("E-bound-launch", "the arrow could not be launched: " + detail);
                return true;
            }
            a_c.phase = kJudge;
            a_c.waitingFor = "the launch records";
            return false;
        }
        const auto* copy = CopyOf(pick.arrow);
        const auto playerLaunch = LaunchBy(player->GetFormID(), pick.arrow->GetFormID());
        SelfTestLog::Check("E-bound-launch", copy && playerLaunch && playerLaunch->copyBase == copy->GetFormID(), std::format(
            "player's arrow PROJ {:08X} (expected copy {:08X}): launch {}", pick.arrow->GetFormID(), copy ? copy->GetFormID() : 0,
            playerLaunch ? std::format("copy {:08X}, rule matched {}", playerLaunch->copyBase, playerLaunch->playerFire) :
                           std::string("NOT RECORDED")));
        if (const auto* deer = ActorOf(a_c.caster); deer && g_bound.npcLaunched) {
            const auto npcLaunch = LaunchBy(deer->GetFormID(), pick.arrow->GetFormID());
            SelfTestLog::Check("E-bound-npc", npcLaunch && npcLaunch->copyBase == 0 && !npcLaunch->playerFire, std::format(
                "the deer's arrow with the same ammo and weapon: launch {}", npcLaunch ?
                    std::format("copy {:08X}, rule matched {}", npcLaunch->copyBase, npcLaunch->playerFire) : std::string("NOT RECORDED")));
        } else {
            SelfTestLog::Info("E-bound-npc", "not run: no caster deer");
        }
        return true;
    }

    bool StepBoundOff(Ctx& a_c, bool a_first)
    {
        auto* player = Player();
        if (!g_bound.pick) {
            if (a_first) {
                SelfTestLog::Skip("E-bound-off", "not run: E-bound-launch picked nothing");
            }
            return true;
        }
        const auto& pick = *g_bound.pick;
        if (a_first) {
            if (IsStoked()) {
                SelfTestLog::Fail("E-bound-off", "still stoked after T8: not judged");
                CleanupBoundSpell();
                return true;
            }
            g_bound.mark = SelfTestProbe::Mark();
            std::string detail;
            if (!LaunchArrow(player, pick.ammo, pick.seed->weapon, detail)) {
                SelfTestLog::Fail("E-bound-off", "the arrow could not be launched: " + detail);
                CleanupBoundSpell();
                return true;
            }
            SelfTestLog::Info("E-bound-off", detail);
            a_c.waitingFor = "the unbuffed launch record";
            return false;
        }
        const auto launch = LaunchBy(player->GetFormID(), pick.arrow->GetFormID());
        SelfTestLog::Check("E-bound-off", launch && launch->copyBase == 0, std::format("player's arrow PROJ {:08X} with the buff "
            "off: launch {}", pick.arrow->GetFormID(), launch ? std::format("copy {:08X}, rule matched {}", launch->copyBase,
            launch->playerFire) : std::string("NOT RECORDED")));
        CleanupBoundSpell();
        return true;
    }

    void CleanupBoundSpell()
    {
        if (!g_bound.pick || !g_bound.pick->seed->spell) {
            return;
        }
        const int dispelled = SelfTestWorld::DispelSpell(Player(), g_bound.pick->seed->spell);
        SelfTestLog::Info("T9", std::format("bound fire weapon spell {:08X}: {} effect(s) dispelled on the player",
            g_bound.pick->seed->spell->GetFormID(), dispelled));
        g_bound.pick.reset();
    }
}
