// =============================================================================
// Toxicology - dev self test: step 4a, the tome as the quest reward
// =============================================================================
// See game/ToxSelfTestState.h for the split between the self test's files.
// Spec 4a: the tome is the reward of Few and Far Between at stage 200
// (game/ToxReward.h). Runs first, before step 1 gives the ability, so the
// ability and tomes it finds are the save's own:
//   phase 0: the tome teaches the ability, value 350. The save's ability and
//            tomes are recorded and taken, the reward flag set to false. Quest
//            below stage 200: Quest.SetStage(200) through the Papyrus VM (the
//            real path: the stage event's task gives the tome). At 200: the
//            load path, TryGrantTome(kAlreadyFinished). Past 200: skipped.
//   phase 1: the stage path's wait: the tome given (0 -> 1), the flag set, the
//            stage 200 and the quest stopped (its fragment calls Stop()).
//   phase 2: the given tome taken again (the ability still not known), the load
//            path run once more: nothing may be given.
// Then the save's ability and tomes are put back. The quest stays at stage 200
// with what its fragment did (Ingun's key, the thane count, her relationship)
// and the flag stays set: the save is changed for good (a disposable save, as
// the whole self test asks). The reload check (ToxSelfTestReload.cpp) later
// wants the flag and the stage back after a save and a load.
// executedStages is logged before and after (log only, no PASS/FAIL): an
// outside reviewer claimed bit 0 of a stage's flags marks it done; CommonLib
// names no such bit (RE/T/TESQuest.h:116-121).

#include "game/ToxSelfTestState.h"

#include "Common.h"
#include "game/ToxData.h"
#include "game/ToxReward.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace SelfTest = ToxGame::SelfTest;
using SelfTest::State;

// What the Papyrus VM's callback reported; written on the VM's thread, read in the ticks
struct SelfTest::SetStageCall
{
    std::atomic<bool> returned{ false };
    std::atomic<bool> isBool{ false };
    std::atomic<bool> value{ false };
};

namespace
{
    // =========================================================================
    // Constants
    // =========================================================================

    constexpr std::string_view kStep = "4a reward";
    constexpr std::int32_t kTomeValue = 350;  // spec 4a; plugins/Toxicology/ESL.md
    constexpr double kStageWaitMs = 10000.0;  // phase 1: the stage event's task and the fragment's Stop()
    constexpr int kMaxRemoveSteps = 32;
    // Papyrus Quest.SetStage(int aiStage): a vanilla script function, not in a CommonLib header
    constexpr const char* kQuestClass = "Quest";
    constexpr const char* kSetStage = "SetStage";

    enum Phase
    {
        kPhaseStart = 0,
        kPhaseWaitStage = 1,
        kPhaseOnce = 2,
    };

    // =========================================================================
    // Papyrus: Quest.SetStage
    // =========================================================================

    // Modelled on IVirtualMachine::Awaitable::CallbackFunctor (RE/I/IVirtualMachine.h:44-52)
    class SetStageCallback final : public RE::BSScript::IStackCallbackFunctor
    {
    public:
        explicit SetStageCallback(std::shared_ptr<SelfTest::SetStageCall> a_call) :
            _call(std::move(a_call))
        {}

        // RE/I/IStackCallbackFunctor.h:23,25; RE/V/Variable.h:53,67
        void operator()(RE::BSScript::Variable a_result) override
        {
            const bool isBool = a_result.IsBool();
            _call->isBool = isBool;
            _call->value = isBool && a_result.GetBool();
            _call->returned = true;
        }

        void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

    private:
        std::shared_ptr<SelfTest::SetStageCall> _call;
    };

    // false with a_why when the call could not be queued
    bool DispatchSetStage(RE::TESQuest& a_quest, const std::shared_ptr<SelfTest::SetStageCall>& a_call, std::string& a_why)
    {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();  // RE/V/VirtualMachine.h:175
        auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;          // RE/I/IVirtualMachine.h:137
        if (!policy) {
            a_why = "no Papyrus VM or handle policy";
            return false;
        }
        const auto handle = policy->GetHandleForObject(RE::TESQuest::FORMTYPE, &a_quest);  // RE/I/IObjectHandlePolicy.h:34
        if (handle == policy->EmptyHandle()) {  // RE/I/IObjectHandlePolicy.h:23
            a_why = "the quest has no Papyrus handle";
            return false;
        }
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{ new SetStageCallback(a_call) };
        // RE/I/IVirtualMachine.h:132; RE/F/FunctionArguments.h:80 (the VM owns the arguments)
        auto* args = RE::MakeFunctionArguments(static_cast<std::int32_t>(ToxGame::kRewardStage));
        if (!vm->DispatchMethodCall(handle, kQuestClass, kSetStage, args, callback)) {
            a_why = "DispatchMethodCall(Quest.SetStage) returned false";
            return false;
        }
        return true;
    }

    // =========================================================================
    // The player's ability and tomes
    // =========================================================================

    // The first extra list of the tome's inventory entry (null when it has none)
    RE::ExtraDataList* FirstTomeList(RE::PlayerCharacter& a_player, const RE::TESObjectBOOK* a_tome)
    {
        auto* changes = a_player.GetInventoryChanges();  // RE/T/TESObjectREFR.h:410
        if (!changes || !changes->entryList) return nullptr;  // RE/I/InventoryChanges.h:101
        for (auto* entry : *changes->entryList) {
            if (!entry || entry->object != a_tome || !entry->extraLists) continue;  // RE/I/InventoryEntryData.h:65
            for (auto* list : *entry->extraLists) {
                if (list) return list;
            }
        }
        return nullptr;
    }

    // The player's tomes brought to a_count; false when that did not work (logged)
    bool SetTomeCount(std::int32_t a_count)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* tome = ToxGame::Tome();
        if (!player || !tome) return false;
        if (const auto held = ToxGame::PlayerTomeCount(); held < a_count) {
            player->AddObjectToContainer(tome, nullptr, a_count - held, nullptr);  // RE/T/TESObjectREFR.h:270
        }
        for (int step = 0; step < kMaxRemoveSteps; ++step) {
            const auto before = ToxGame::PlayerTomeCount();
            if (before <= a_count) break;
            // A stack with its own list first, then the tomes outside any list (as RemoveAllBottles)
            auto* list = FirstTomeList(*player, tome);
            std::int32_t count = before - a_count;
            if (list) count = std::min(count, static_cast<std::int32_t>(list->GetCount()));  // RE/E/ExtraDataList.h:201
            player->RemoveItem(tome, count, RE::ITEM_REMOVE_REASON::kRemove, list, nullptr);  // RE/T/TESObjectREFR.h:266
            if (ToxGame::PlayerTomeCount() >= before) break;
        }
        const auto now = ToxGame::PlayerTomeCount();
        if (now != a_count) logger::warn("SELFTEST WARN the player holds {} tomes, {} wanted", now, a_count);
        return now == a_count;
    }

    std::string QuestText(const RE::TESQuest& a_quest)
    {
        // RE/T/TESQuest.h:244,250,252; CommonLib's IsRunning is !IsStopping() && !promoteTask
        // (src/RE/T/TESQuest.cpp:76-79), so it can read true for a stopped quest: IsStopped is the test
        return std::format("quest {} stage {}, IsRunning {}, IsStopped {}", SelfTest::FormText(&a_quest),
            a_quest.GetCurrentStageID(), a_quest.IsRunning(), a_quest.IsStopped());
    }

    std::string PlayerText()
    {
        return std::format("ability {}, tomes {}, reward flag {}", ToxGame::PlayerHasAbility() ? "known" : "not known",
            ToxGame::PlayerTomeCount(), ToxGame::RewardSatisfied());
    }

    // The same checks the shop step made of the record
    void CheckTomeRecord(State& s, RE::TESObjectBOOK& a_tome)
    {
        auto* teaches = a_tome.GetSpell();  // RE/T/TESObjectBOOK.h:106,112: null unless the teaches-spell flag is set
        SelfTest::Check(s, kStep, a_tome.TeachesSpell() && teaches && teaches == ToxGame::Ability(), std::format("{} teaches "
            "spell {}: {} (the ability is {})", SelfTest::FormText(&a_tome), a_tome.TeachesSpell() ? "yes" : "no",
            SelfTest::FormText(teaches), SelfTest::FormText(ToxGame::Ability())));
        // RE/T/TESValueForm.h:25 (the record's value, no perks or barter)
        SelfTest::Check(s, kStep, a_tome.value == kTomeValue, std::format("{} value {} ({} expected)", SelfTest::FormText(&a_tome),
            a_tome.value, kTomeValue));
    }

    // Step 4a is over: the save's ability and tomes back, then step 1
    void EndReward(State& s)
    {
        SelfTest::UndoReward(s);
        SelfTest::NextStep(s, SelfTest::Step::kSetup);
    }

    // Phase 0 at stage 200, and phase 2: the load path once
    Toxicology::TomeRewardAction RunLoadPath()
    {
        return ToxGame::TryGrantTome(ToxGame::RewardPath::kAlreadyFinished);
    }

    void CheckGiven(State& s, std::string_view a_what, std::size_t a_ticks)
    {
        const auto tomes = ToxGame::PlayerTomeCount();
        const bool flag = ToxGame::RewardSatisfied();
        s.rewardGiven = flag;
        SelfTest::Check(s, kStep, tomes == 1 && flag, std::format("{}: tomes 0 -> {}, reward flag {} (1 and true expected) "
            "after {} ticks", a_what, tomes, flag, a_ticks));
    }

    void StartPhase(State& s, RE::TESQuest& a_quest, RE::TESObjectBOOK& a_tome)
    {
        CheckTomeRecord(s, a_tome);
        s.rewardHadAbility = ToxGame::PlayerHasAbility();
        s.rewardTomesBefore = ToxGame::PlayerTomeCount();
        const auto stage = a_quest.GetCurrentStageID();
        SelfTest::Info(kStep, std::format("before: {}; {}", QuestText(a_quest), PlayerText()));
        if (stage > ToxGame::kRewardStage) {
            SelfTest::Info(kStep, std::format("skipped: Few and Far Between is past stage {} (stage {}: failed, or set by a mod "
                "or the console) - the reward cannot be tested in this save; nothing changed", ToxGame::kRewardStage, stage));
            EndReward(s);
            return;
        }
        s.rewardTaken = true;
        if (auto* player = RE::PlayerCharacter::GetSingleton(); player && s.rewardHadAbility) {
            player->RemoveSpell(ToxGame::Ability());  // RE/A/Actor.h:678
        }
        const bool tomesTaken = SetTomeCount(0);
        ToxGame::SetRewardSatisfied(false);
        if (ToxGame::PlayerHasAbility() || !tomesTaken) {
            SelfTest::Fail(s, kStep, std::format("could not take the save's ability and tomes: {}", PlayerText()));
            EndReward(s);
            return;
        }
        SelfTest::ProbeQuestStages("before the reward");
        if (stage == ToxGame::kRewardStage) {
            SelfTest::Info(kStep, "the quest is already at stage 200: the load path (TryGrantTome kAlreadyFinished) is tested, "
                "not the stage event");
            const auto action = RunLoadPath();
            CheckGiven(s, std::format("load path ({})", Toxicology::TomeRewardActionName(action)), 0);
            SelfTest::NextPhase(s, kPhaseOnce);
            return;
        }
        auto call = std::make_shared<SelfTest::SetStageCall>();
        std::string why;
        if (!DispatchSetStage(a_quest, call, why)) {
            SelfTest::Fail(s, kStep, std::format("Quest.SetStage({}) not dispatched: {}", ToxGame::kRewardStage, why));
            EndReward(s);
            return;
        }
        s.setStageCall = std::move(call);
        SelfTest::Info(kStep, std::format("Quest.SetStage({}) dispatched through the Papyrus VM; waiting up to {:.0f} ms for the "
            "stage event's task", ToxGame::kRewardStage, kStageWaitMs));
        SelfTest::NextPhase(s, kPhaseWaitStage);
    }

    void WaitStagePhase(State& s, RE::TESQuest& a_quest)
    {
        const bool given = ToxGame::RewardSatisfied() && ToxGame::PlayerTomeCount() >= 1;
        const bool done = a_quest.GetCurrentStageID() == ToxGame::kRewardStage && a_quest.IsStopped();
        if (!(given && done) && !SelfTest::WaitOver(s.phaseTicks, kStageWaitMs)) return;
        const auto& call = *s.setStageCall;
        SelfTest::Info(kStep, std::format("SetStage callback: {} (returned a Bool {}, value {}); {}",
            call.returned ? "came" : "not yet", call.isBool.load(), call.value.load(), QuestText(a_quest)));
        CheckGiven(s, "stage event", s.phaseTicks);
        SelfTest::Check(s, kStep, done, std::format("after SetStage({}): {} (stage {} and stopped expected)",
            ToxGame::kRewardStage, QuestText(a_quest), ToxGame::kRewardStage));
        SelfTest::ProbeQuestStages("after stage 200");
        SelfTest::NextPhase(s, kPhaseOnce);
    }

    void OncePhase(State& s)
    {
        if (!ToxGame::RewardSatisfied()) {
            SelfTest::Info(kStep, "the reward was not given above: the no-second-tome check is skipped");
            EndReward(s);
            return;
        }
        const bool tomesTaken = SetTomeCount(0);
        const auto action = RunLoadPath();
        const auto tomes = ToxGame::PlayerTomeCount();
        SelfTest::Check(s, kStep, tomesTaken && tomes == 0 && ToxGame::RewardSatisfied() &&
            action == Toxicology::TomeRewardAction::kSkip, std::format("given tome taken again, load path run once more: {}, "
            "tomes {}, reward flag {} (Skip, 0 and true expected: a satisfied reward is never given twice)",
            Toxicology::TomeRewardActionName(action), tomes, ToxGame::RewardSatisfied()));
        EndReward(s);
    }
}

// =============================================================================
// ToxSelfTestState.h
// =============================================================================

void SelfTest::TickReward(State& s)
{
    auto* quest = ToxGame::RewardQuest();
    auto* tome = ToxGame::Tome();
    if (!quest || !tome || !ToxGame::Ability() || !RE::PlayerCharacter::GetSingleton()) {
        Fail(s, kStep, std::format("no player, quest {:06X} ({}) or the ESL's ability / tome - step skipped",
            ToxGame::kRewardQuestId, ToxGame::kSkyrimEsm));
        EndReward(s);
        return;
    }
    if (!ToxGame::GetSettings().enabled) {
        Info(kStep, "skipped: the setting \"enabled\" is false (the reward gives nothing then); nothing changed");
        EndReward(s);
        return;
    }
    switch (s.phase) {
    case kPhaseStart:
        StartPhase(s, *quest, *tome);
        return;
    case kPhaseWaitStage:
        WaitStagePhase(s, *quest);
        return;
    case kPhaseOnce:
        OncePhase(s);
        return;
    default:
        Fail(s, kStep, std::format("unknown phase {}", s.phase));
        EndReward(s);
        return;
    }
}

void SelfTest::UndoReward(State& s)
{
    if (!s.rewardTaken) return;
    s.rewardTaken = false;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (s.rewardHadAbility && player && ToxGame::Ability() && !ToxGame::PlayerHasAbility()) {
        player->AddSpell(ToxGame::Ability());  // RE/A/Actor.h:518
    }
    SetTomeCount(s.rewardTomesBefore);
    const auto* quest = ToxGame::RewardQuest();
    Info(kStep, std::format("put back: {} (the save had the ability {}, {} tomes). The save is changed for good: {}{}, "
        "with what the quest's own stage fragment did (Ingun's key, the thane count, her relationship), and the reward "
        "flag {} - use a disposable save", PlayerText(), s.rewardHadAbility ? "known" : "not known", s.rewardTomesBefore,
        quest ? QuestText(*quest) : "no quest", s.setStageCall ? " (set by this test)" : "", ToxGame::RewardSatisfied()));
}

void SelfTest::ProbeQuestStages(std::string_view a_when)
{
    const auto* quest = ToxGame::RewardQuest();
    if (!quest) return;
    std::string entries;
    std::size_t count = 0;
    if (quest->executedStages) {  // RE/T/TESQuest.h:272
        for (const auto& stage : *quest->executedStages) {  // RE/T/TESQuest.h:125-127 (index, flags, pad3)
            entries += std::format("{}index {} flags {:02X} pad3 {:02X}", entries.empty() ? "" : "; ", stage.data.index,
                stage.data.flags.underlying(), stage.data.pad3);
            ++count;
        }
    }
    Info("4a probe", std::format("{}: currentStage {}, executedStages {} ({} entries){}{}", a_when, quest->GetCurrentStageID(),
        quest->executedStages ? "present" : "null", count, entries.empty() ? "" : ": ", entries));
}
