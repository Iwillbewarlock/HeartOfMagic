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
        // W4: queued equips land within a few frames; the restored hands are read again after this
        constexpr auto kRestoreSettle = std::chrono::milliseconds(1000);
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

        // J5: what the player held before the cast, to put back after the dispel. W2: each hand's instance (its extra data:
        // enchantment, tempering, name) so the same item comes back, not another copy of its base. W4: the shield too (a
        // bound bow takes the left hand)
        struct Loadout
        {
            RE::TESForm* left = nullptr;
            RE::TESForm* right = nullptr;
            RE::TESAmmo* ammo = nullptr;
            RE::ExtraDataList* leftExtra = nullptr;
            RE::ExtraDataList* rightExtra = nullptr;
            RE::TESObjectARMO* shield = nullptr;
        };

        struct BoundState
        {
            std::optional<BoundPick> pick;
            Loadout loadout;
            std::uint64_t mark = 0;
            bool npcLaunched = false;
            std::vector<RE::ObjectRefHandle> arrows;  // J7: the test arrows still to delete
            std::optional<Loadout> restored;           // W4: what RestoreLoadout put back, to read again once settled
            Clock::time_point restoredAt{};
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

        // J7: the test arrows are deleted (W3: each one's handle taken at its launch, so an abort or a failed later step
        // still finds it; the launch records add any the launch did not hand back)
        void Remember(const RE::ObjectRefHandle& a_arrow)
        {
            if (a_arrow && std::ranges::find(g_bound.arrows, a_arrow) == g_bound.arrows.end()) {
                g_bound.arrows.push_back(a_arrow);
            }
        }

        void Remember(const std::optional<SelfTestProbe::LaunchRecord>& a_launch)
        {
            if (a_launch) {
                Remember(a_launch->projectile);
            }
        }

        [[nodiscard]] bool Launch(RE::Actor* a_shooter, const BoundPick& a_pick, RE::TESObjectREFR* a_target, std::string& a_detail)
        {
            RE::ObjectRefHandle arrow;
            const bool launched = SelfTestWorld::LaunchAmmoAt(a_shooter, a_pick.ammo, a_pick.seed->weapon, a_target, a_detail, &arrow);
            Remember(arrow);
            return launched;
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

        // W2: the worn instance of a hand's item: the extra data list of its inventory entry that carries the worn flag
        RE::ExtraDataList* WornExtra(RE::PlayerCharacter* a_player, bool a_left)
        {
            const auto* entry = a_player->GetEquippedEntryData(a_left);
            if (!entry || !entry->extraLists) {
                return nullptr;
            }
            for (auto* list : *entry->extraLists) {
                if (list && (a_left ? list->HasType<RE::ExtraWornLeft>() : list->HasType<RE::ExtraWorn>())) {
                    return list;
                }
            }
            return nullptr;
        }

        // W2: a recorded extra data list is used only while the player's inventory still holds it for that item (the
        // pointer is never used after its item left)
        RE::ExtraDataList* StillHeld(RE::PlayerCharacter* a_player, const RE::TESForm* a_object, RE::ExtraDataList* a_extra)
        {
            auto* changes = a_extra ? a_player->GetInventoryChanges() : nullptr;
            if (!changes || !changes->entryList) {
                return nullptr;
            }
            for (auto* entry : *changes->entryList) {
                if (!entry || entry->GetObject() != a_object || !entry->extraLists) {
                    continue;
                }
                for (auto* list : *entry->extraLists) {
                    if (list == a_extra) {
                        return a_extra;
                    }
                }
            }
            return nullptr;
        }

        Loadout CurrentLoadout(RE::PlayerCharacter* a_player)
        {
            return { a_player->GetEquippedObject(true), a_player->GetEquippedObject(false), a_player->GetCurrentAmmo(),
                WornExtra(a_player, true), WornExtra(a_player, false),
                a_player->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kShield) };
        }

        // W1: a two-handed weapon (or bow / crossbow) shows in both hands and is equipped once; anything else in the right
        // hand leaves the left hand to be put back on its own, even the same spell or one-handed base
        bool TwoHanded(const RE::TESForm* a_right)
        {
            if (const auto* spell = a_right ? a_right->As<RE::SpellItem>() : nullptr) {
                return spell->IsTwoHanded();  // X1: a two-handed (ritual) spell fills both hands from the right slot
            }
            const auto* weapon = a_right ? a_right->As<RE::TESObjectWEAP>() : nullptr;
            return weapon && (weapon->IsTwoHandedSword() || weapon->IsTwoHandedAxe() || weapon->IsBow() || weapon->IsCrossbow());
        }

        std::string Name(const RE::TESForm* a_form)
        {
            return a_form ? std::format("{:08X}", a_form->GetFormID()) : std::string("empty");
        }

        // J5: re-equip what the player held when it differs now (a spell with EquipSpell, a weapon, scroll, ammo or shield
        // with EquipObject, each hand to its own equip slot, the recorded instance when still held); an empty hand before is
        // left as the dispel leaves it. W4: what was put back is read again once settled (CheckBoundRestore)
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
            const auto hand = [&](RE::TESForm* a_was, RE::TESForm* a_is, RE::ExtraDataList* a_extra, RE::ExtraDataList* a_isExtra,
                                  bool a_left) {
                if (!a_was || a_was->As<RE::TESObjectARMO>()) {
                    return;  // X2: a shield shows in the left hand; the shield branch below puts it back, once
                }
                auto* held = a_was->As<RE::TESBoundObject>() ? StillHeld(player, a_was, a_extra) : nullptr;
                if (a_was == a_is && (!held || held == a_isExtra)) {
                    return;  // X3: the same base and, when the instance is known, the same instance
                }
                const auto* slot = defaults->GetObject<RE::BGSEquipSlot>(a_left ? RE::DEFAULT_OBJECTS::kLeftHandEquip :
                                                                                  RE::DEFAULT_OBJECTS::kRightHandEquip);
                if (auto* scroll = a_was->As<RE::ScrollItem>()) {  // W19: a scroll is an item (a SpellItem by type)
                    equip->EquipObject(player, scroll, nullptr, 1, slot);
                } else if (auto* spell = a_was->As<RE::SpellItem>()) {
                    equip->EquipSpell(player, spell, slot);
                } else if (auto* object = a_was->As<RE::TESBoundObject>()) {
                    equip->EquipObject(player, object, held, 1, slot);
                } else {
                    done += std::format(" {} hand: {} not re-equipped (neither spell nor item);", a_left ? "left" : "right", Name(a_was));
                    return;
                }
                // X4: whether the recorded instance was found again (else the base: another copy of it may come back)
                done += std::format(" {} hand {} -> {} ({});", a_left ? "left" : "right", Name(a_is), Name(a_was),
                    a_was->As<RE::SpellItem>() && !a_was->As<RE::ScrollItem>() ? "spell" :
                    held                                                       ? "instance re-equipped" :
                    a_extra                                                    ? "base only: the recorded instance is gone" :
                                                                                 "base only: no instance recorded");
            };
            hand(before.right, now.right, before.rightExtra, now.rightExtra, false);
            if (!TwoHanded(before.right)) {  // W1, X1
                hand(before.left, now.left, before.leftExtra, now.leftExtra, true);
            }
            if (before.ammo && before.ammo != now.ammo) {
                equip->EquipObject(player, before.ammo);
                done += std::format(" ammo {} -> {};", Name(now.ammo), Name(before.ammo));
            }
            if (before.shield && before.shield != now.shield) {
                equip->EquipObject(player, before.shield);
                done += std::format(" shield {} -> {};", Name(now.shield), Name(before.shield));
            }
            g_bound.restored = before;
            g_bound.restoredAt = Clock::now();
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
            const bool launched = Launch(player, pick, target, detail);
            SelfTestLog::Info("E-bound-launch", detail);
            if (auto* deer = ActorOf(a_c.caster)) {
                std::string npcDetail;
                g_bound.npcLaunched = Launch(deer, pick, target, npcDetail);
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
            if (!Launch(player, pick, target, detail)) {
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

    void NoteBoundRestoreUnchecked()
    {
        if (g_bound.restored) {
            g_bound.restored.reset();
            SelfTestLog::Info("T9-loadout", "not read again (late cleanup: the run ended before the 1 s check)");  // X5
        }
    }

    bool CheckBoundRestore()
    {
        if (!g_bound.restored) {
            return true;
        }
        if (Clock::now() - g_bound.restoredAt < kRestoreSettle) {
            return false;
        }
        const auto want = *g_bound.restored;
        g_bound.restored.reset();
        auto* player = Player();
        if (!player) {
            SelfTestLog::Info("T9", "restored loadout not read again: no player");
            return true;
        }
        const auto now = CurrentLoadout(player);
        std::string wrong;
        const auto compare = [&](const char* a_what, const RE::TESForm* a_want, const RE::TESForm* a_now) {
            if (a_want && a_want != a_now) {
                wrong += std::format(" {} {} (wanted {});", a_what, Name(a_now), Name(a_want));
            }
        };
        compare("right", want.right, now.right);
        compare("left", TwoHanded(want.right) ? nullptr : want.left, now.left);
        compare("ammo", want.ammo, now.ammo);
        compare("shield", want.shield, now.shield);
        const bool instances = (!want.rightExtra || want.rightExtra == now.rightExtra || !StillHeld(player, want.right, want.rightExtra)) &&
                               (!want.leftExtra || TwoHanded(want.right) || want.leftExtra == now.leftExtra ||
                                   !StillHeld(player, want.left, want.leftExtra));
        if (!instances) {
            wrong += " a hand holds another instance of its item than before;";
        }
        SelfTestLog::Check("T9-loadout", wrong.empty(), wrong.empty() ?
            std::format("the player's hands, ammo and shield are as before the bound weapon (read {} ms after the restore)",
                kRestoreSettle.count()) :
            "after the restore:" + wrong);
        return true;
    }
}
