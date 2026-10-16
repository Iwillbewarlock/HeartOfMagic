// =============================================================================
// SELF-TEST - bound fire weapons (bound-weapon stage 1): their arrows take the PROJ copy
// =============================================================================
//   T0-bound        per bound fire weapon seed (FireSet.h BoundFireSeeds): its MGEF's projectile and, for a bow or crossbow,
//                   the bound-arrow AMMOs' projectiles each have a copy (PASS), are excluded from the closure or have no source
//                   mesh (INFO, the reason), were dropped by the copy model probe (FAIL, said so), or have no copy (FAIL: run
//                   the builder again). No seed: INFO.
//   E-bound-launch  while stoked: the player's hands and ammo are recorded, the first bow seed with a spell is cast on the
//                   player (CastSpellImmediate) and, once the weapon is on the runtime bound fire list (PlayerArt.h), an arrow
//                   is shot at the self-test's target deer through the AMMO LaunchData constructor (SelfTestWorld::LaunchAmmoAt,
//                   T3's aim): PASS when the Launch detour's record has the copy of that PROJ. E-bound-npc: the same ammo and
//                   weapon from the deer caster at the target is not swapped (contamination check). No target: SKIP, nothing
//                   is launched (only the test's own actors are ever shot: no crime).
//   E-bound-off     after T8 (the buff is off): the same shot from the player is not swapped; then the spell is dispelled and
//                   the player's hands and ammo are put back.
// Every test arrow is deleted after its judgement (Disable + SetDelete by its launch record's handle), so no copy-based
// arrow reference is left for a save (E3 asks for one later). A seed without a spell, or no bow seed: SKIP with the reason.

#include "SelfTestCtx.h"

#include "BuffState.h"
#include "FireClosure.h"
#include "FireSet.h"
#include "FormCopies.h"
#include "PlayerArt.h"
#include "SelfTestLog.h"
#include "SelfTestText.h"

#include <algorithm>
#include <format>

#ifdef GetObject
#    undef GetObject  // windows.h: BGSDefaultObjectManager::GetObject below
#endif

namespace BlackFlame::SelfTest
{
    namespace
    {
        using namespace SelfTestText;

        constexpr auto kListWait = std::chrono::milliseconds(3000);  // the ticker (100 ms while stoked) refreshes the list
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

        // J5: what the player held before the cast, to put back after the dispel
        struct Loadout
        {
            RE::TESForm* left = nullptr;
            RE::TESForm* right = nullptr;
            RE::TESAmmo* ammo = nullptr;
        };

        struct BoundState
        {
            std::optional<BoundPick> pick;
            Loadout loadout;
            std::uint64_t mark = 0;
            bool npcLaunched = false;
            std::vector<RE::ObjectRefHandle> arrows;  // J7: the test arrows still to delete
        };
        BoundState g_bound;

        // A closure form's state as a short text: "copy XXXXXXXX", "excluded: why", "source mesh absent", "dropped ..." (a
        // failure), or empty (no copy)
        std::string CopyState(const RE::TESForm* a_form, bool& a_failed)
        {
            a_failed = false;
            if (const auto* copy = CopyOf(a_form)) {
                return std::format("copy {:08X}", copy->GetFormID());
            }
            for (const auto& excluded : LastFireClosure().excluded) {
                if (excluded.form == a_form) {
                    return "excluded: " + excluded.reason;
                }
            }
            if (IsSourceMeshAbsent(a_form)) {
                return "source mesh absent";
            }
            a_failed = true;
            return IsDroppedByProbe(a_form) ? "dropped by the copy model probe (its NIF did not load)" :  // J2
                                              "NO copy (run the builder again)";
        }

        bool IsBowSeed(const BoundFireSeed& a_seed) { return a_seed.weapon->IsBow() || a_seed.weapon->IsCrossbow(); }

        std::optional<SelfTestProbe::LaunchRecord> LaunchBy(RE::FormID a_shooter, RE::FormID a_base)
        {
            for (const auto& r : SelfTestProbe::LaunchesSince(g_bound.mark)) {
                if (r.shooter == a_shooter && r.base == a_base) {
                    return r;
                }
            }
            return std::nullopt;
        }

        // J7: the judged launches' arrows are deleted (their handles from the records)
        void Remember(const std::optional<SelfTestProbe::LaunchRecord>& a_launch)
        {
            if (a_launch && a_launch->projectile) {
                g_bound.arrows.push_back(a_launch->projectile);
            }
        }

        void DeleteArrows(const char* a_id)
        {
            if (g_bound.arrows.empty()) {
                return;
            }
            int removed = 0;
            for (const auto& handle : g_bound.arrows) {
                removed += handle.get() ? 1 : 0;
                (void)SelfTestWorld::Remove(handle);
            }
            SelfTestLog::Info(a_id, std::format("{} test arrow(s) of {} deleted (no copy-based arrow is left for a save)", removed,
                g_bound.arrows.size()));
            g_bound.arrows.clear();
        }

        Loadout CurrentLoadout(RE::PlayerCharacter* a_player)
        {
            return { a_player->GetEquippedObject(true), a_player->GetEquippedObject(false), a_player->GetCurrentAmmo() };
        }

        std::string Name(const RE::TESForm* a_form)
        {
            return a_form ? std::format("{:08X}", a_form->GetFormID()) : std::string("empty");
        }

        // J5: re-equip what the player held when it differs now (a spell with EquipSpell, a weapon or ammo with EquipObject,
        // each hand to its own equip slot); an empty hand before is left as the dispel leaves it
        void RestoreLoadout(const char* a_id)
        {
            auto* player = Player();
            auto* equip = RE::ActorEquipManager::GetSingleton();
            const auto* defaults = RE::BGSDefaultObjectManager::GetSingleton();
            if (!player || !equip || !defaults) {
                SelfTestLog::Info(a_id, "loadout not restored: no player, equip manager or default objects");
                return;
            }
            const auto before = g_bound.loadout;
            const auto now = CurrentLoadout(player);
            std::string done;
            const auto hand = [&](RE::TESForm* a_was, RE::TESForm* a_is, bool a_left) {
                if (!a_was || a_was == a_is) {
                    return;
                }
                const auto* slot = defaults->GetObject<RE::BGSEquipSlot>(a_left ? RE::DEFAULT_OBJECTS::kLeftHandEquip :
                                                                                  RE::DEFAULT_OBJECTS::kRightHandEquip);
                if (auto* spell = a_was->As<RE::SpellItem>()) {
                    equip->EquipSpell(player, spell, slot);
                } else if (auto* object = a_was->As<RE::TESBoundObject>()) {
                    equip->EquipObject(player, object, nullptr, 1, slot);
                } else {
                    done += std::format(" {} hand: {} not re-equipped (neither spell nor item);", a_left ? "left" : "right", Name(a_was));
                    return;
                }
                done += std::format(" {} hand {} -> {};", a_left ? "left" : "right", Name(a_is), Name(a_was));
            };
            hand(before.right, now.right, false);
            if (before.left != before.right) {  // a two-handed weapon shows in both hands: equipped once
                hand(before.left, now.left, true);
            }
            if (before.ammo && before.ammo != now.ammo) {
                equip->EquipObject(player, before.ammo);
                done += std::format(" ammo {} -> {};", Name(now.ammo), Name(before.ammo));
            }
            SelfTestLog::Info(a_id, std::format("loadout before the bound weapon: left {}, right {}, ammo {}; {}", Name(before.left),
                Name(before.right), Name(before.ammo), done.empty() ? "nothing to put back" : "re-equipped:" + done));
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
            int failed = 0;
            std::string text;
            for (std::size_t i = 0; i < arrows.size(); ++i) {
                bool bad = false;
                const auto state = CopyState(arrows[i], bad);
                failed += bad ? 1 : 0;
                if (i < kMaxListed) {
                    text += std::format("{}PROJ {:08X}: {}", text.empty() ? "" : "; ", arrows[i]->GetFormID(), state);
                }
            }
            const auto line = std::format("weapon {:08X} '{}' from effect {:08X} (fire {}{}), spell {:08X}: {}",
                seed.weapon->GetFormID(), seed.weapon->GetName(), seed.effect->GetFormID(), seed.byEffect ? "effect" : "enchantment",
                seed.byEffect && seed.byEnchantment ? " and enchantment" : "", seed.spell ? seed.spell->GetFormID() : 0,
                arrows.empty() ? "no arrow PROJ (a melee weapon without a projectile)" : text);
            if (arrows.empty()) {
                SelfTestLog::Info("T0-bound", line);
            } else {
                SelfTestLog::Check("T0-bound", failed == 0, line);
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
            if (!ActorOf(a_c.dummy)) {
                SelfTestLog::Skip("E-bound-launch", "not run: no target deer to shoot at (nothing is launched without one)");  // J6
                g_bound.pick.reset();
                return true;
            }
            if (!IsStoked()) {
                SelfTestLog::Fail("E-bound-launch", "not stoked at the start");
                g_bound.pick.reset();
                return true;
            }
            const auto& pick = *g_bound.pick;
            g_bound.loadout = CurrentLoadout(player);  // J5: before the bound weapon changes the hands
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
            auto* target = ActorOf(a_c.dummy);
            if (!target) {
                SelfTestLog::Skip("E-bound-launch", "not run: the target deer is gone (nothing launched)");
                return true;
            }
            g_bound.mark = SelfTestProbe::Mark();
            std::string detail;
            const bool launched = SelfTestWorld::LaunchAmmoAt(player, pick.ammo, pick.seed->weapon, target, detail);
            SelfTestLog::Info("E-bound-launch", detail);
            if (auto* deer = ActorOf(a_c.caster)) {
                std::string npcDetail;
                g_bound.npcLaunched = SelfTestWorld::LaunchAmmoAt(deer, pick.ammo, pick.seed->weapon, target, npcDetail);
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
        Remember(playerLaunch);
        SelfTestLog::Check("E-bound-launch", copy && playerLaunch && playerLaunch->copyBase == copy->GetFormID(), std::format(
            "player's arrow PROJ {:08X} (expected copy {:08X}): launch {}", pick.arrow->GetFormID(), copy ? copy->GetFormID() : 0,
            playerLaunch ? std::format("copy {:08X}, rule matched {}", playerLaunch->copyBase, playerLaunch->playerFire) :
                           std::string("NOT RECORDED")));
        if (const auto* deer = ActorOf(a_c.caster); deer && g_bound.npcLaunched) {
            const auto npcLaunch = LaunchBy(deer->GetFormID(), pick.arrow->GetFormID());
            Remember(npcLaunch);
            SelfTestLog::Check("E-bound-npc", npcLaunch && npcLaunch->copyBase == 0 && !npcLaunch->playerFire, std::format(
                "the deer's arrow with the same ammo and weapon: launch {}", npcLaunch ?
                    std::format("copy {:08X}, rule matched {}", npcLaunch->copyBase, npcLaunch->playerFire) : std::string("NOT RECORDED")));
        } else {
            SelfTestLog::Info("E-bound-npc", "not run: no caster deer");
        }
        DeleteArrows("E-bound-launch");
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
            auto* target = ActorOf(a_c.dummy);
            if (!target) {
                SelfTestLog::Skip("E-bound-off", "not run: no target deer (nothing launched)");
                CleanupBoundSpell();
                return true;
            }
            if (IsStoked()) {
                SelfTestLog::Fail("E-bound-off", "still stoked after T8: not judged");
                CleanupBoundSpell();
                return true;
            }
            g_bound.mark = SelfTestProbe::Mark();
            std::string detail;
            if (!SelfTestWorld::LaunchAmmoAt(player, pick.ammo, pick.seed->weapon, target, detail)) {
                SelfTestLog::Fail("E-bound-off", "the arrow could not be launched: " + detail);
                CleanupBoundSpell();
                return true;
            }
            SelfTestLog::Info("E-bound-off", detail);
            a_c.waitingFor = "the unbuffed launch record";
            return false;
        }
        const auto launch = LaunchBy(player->GetFormID(), pick.arrow->GetFormID());
        Remember(launch);
        SelfTestLog::Check("E-bound-off", launch && launch->copyBase == 0, std::format("player's arrow PROJ {:08X} with the buff "
            "off: launch {}", pick.arrow->GetFormID(), launch ? std::format("copy {:08X}, rule matched {}", launch->copyBase,
            launch->playerFire) : std::string("NOT RECORDED")));
        CleanupBoundSpell();
        return true;
    }

    void CleanupBoundSpell()
    {
        DeleteArrows("T9");
        if (!g_bound.pick || !g_bound.pick->seed->spell) {
            return;
        }
        const int dispelled = SelfTestWorld::DispelSpell(Player(), g_bound.pick->seed->spell);
        SelfTestLog::Info("T9", std::format("bound fire weapon spell {:08X}: {} effect(s) dispelled on the player",
            g_bound.pick->seed->spell->GetFormID(), dispelled));
        RestoreLoadout("T9");
        g_bound.pick.reset();
    }
}
