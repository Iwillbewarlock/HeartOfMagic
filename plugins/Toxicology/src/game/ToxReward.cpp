#include "game/ToxReward.h"

#include "Common.h"
#include "game/ToxData.h"
#include "game/ToxHits.h"

#include <atomic>
#include <exception>
#include <thread>

namespace
{
    // =========================================================================
    // State
    // =========================================================================

    RE::TESQuest* g_quest = nullptr;              // main thread (written at kDataLoaded)
    std::atomic<RE::FormID> g_questId{ 0 };       // any thread (the sink reads it); 0 = no quest
    std::atomic<bool> g_rewardSatisfied{ false }; // any thread (ToxReward.h)
    std::atomic<bool> g_threadLogged{ false };    // any thread
    std::atomic<bool> g_noTaskLogged{ false };    // any thread
    std::thread::id g_mainThread;                 // written once by RegisterQuestStageSink, before the sink exists

    constexpr std::int32_t kTomesGiven = 1;

    // =========================================================================
    // Task
    // =========================================================================

    // a_body runs on the main thread unless a load or a new game came first
    template <class F>
    void QueueRewardTask(const char* a_what, F&& a_body)
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            if (!g_noTaskLogged.exchange(true)) {
                logger::error("Toxicology: no SKSE task interface - the tome reward is not checked");
            }
            return;
        }
        const auto generation = ToxGame::SessionGeneration();
        // SKSE/Interfaces.h:394; runs on the game's main thread
        tasks->AddTask([a_what, generation, body = std::forward<F>(a_body)]() {
            if (generation != ToxGame::SessionGeneration()) {
                logger::info("Toxicology: tome reward: {} task queued before a load or new game - dropped", a_what);
                return;
            }
            try {
                body();
            } catch (const std::exception& e) {
                logger::error("Toxicology: tome reward: {} task failed: {}", a_what, e.what());
            }
        });
    }

    // =========================================================================
    // Quest stage sink
    // =========================================================================

    class QuestStageSink final : public RE::BSTEventSink<RE::TESQuestStageEvent>
    {
    public:
        static QuestStageSink* GetSingleton()
        {
            static QuestStageSink singleton;
            return &singleton;
        }

        QuestStageSink(const QuestStageSink&) = delete;
        QuestStageSink& operator=(const QuestStageSink&) = delete;

        // RE/B/BSTEvent.h:188; RE/T/TESQuestStageEvent.h:10-11 (formID, stage)
        RE::BSEventNotifyControl ProcessEvent(const RE::TESQuestStageEvent* a_event,
            RE::BSTEventSource<RE::TESQuestStageEvent>*) override
        {
            if (!a_event) return RE::BSEventNotifyControl::kContinue;
            const RE::FormID questId = g_questId.load();
            if (questId == 0 || a_event->formID != questId || a_event->stage != ToxGame::kRewardStage) {
                return RE::BSEventNotifyControl::kContinue;
            }
            LogThreadOnce();
            QueueRewardTask("stage 200", [] { ToxGame::TryGrantTome(ToxGame::RewardPath::kQuestStage); });
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        QuestStageSink() = default;
        ~QuestStageSink() override = default;

        static void LogThreadOnce()
        {
            if (g_threadLogged.exchange(true)) return;
            const bool onMain = std::this_thread::get_id() == g_mainThread;
            logger::info("Toxicology: quest stage events arrive on the {} thread", onMain ? "main" : "a non-main");
        }
    };

    // =========================================================================
    // Grant
    // =========================================================================

    void Grant(RE::PlayerCharacter& a_player, RE::TESObjectBOOK& a_tome, std::int32_t a_before)
    {
        a_player.AddObjectToContainer(&a_tome, nullptr, kTomesGiven, nullptr);  // RE/T/TESObjectREFR.h:270
        const std::int32_t after = ToxGame::PlayerTomeCount();
        if (after <= a_before) {
            logger::error("Toxicology: tome reward: AddObjectToContainer left the player's tomes at {} (was {}) - not given; "
                "the reward stays open for the next try", after, a_before);
            return;
        }
        ToxGame::SetRewardSatisfied(true);
        // The game's own "<name> <sAddItemtoInventory>" line and pick-up sound (RE/S/SendHUDMessage.h:10,
        // src/RE/S/SendHUDMessage.cpp:21-38), as an AddItem that is not silent shows
        RE::SendHUDMessage::ShowInventoryChangeMessage(&a_tome, kTomesGiven, true, true);
        const char* name = a_tome.GetName();  // RE/T/TESForm.h:308
        logger::info("Toxicology: tome reward: {} [{:08X}] given (tomes held {} -> {}), reward satisfied",
            name ? name : "", a_tome.GetFormID(), a_before, after);
    }
}

// =============================================================================
// Public
// =============================================================================

const char* ToxGame::RewardPathName(RewardPath a_path)
{
    switch (a_path) {
    case RewardPath::kQuestStage:
        return "quest stage 200";
    case RewardPath::kAlreadyFinished:
        return "quest already finished at load";
    }
    return "?";
}

void ToxGame::LoadRewardQuest()
{
    auto* dh = RE::TESDataHandler::GetSingleton();
    // RE/T/TESDataHandler.h:62-65; null when the record is missing or not a quest
    g_quest = dh ? dh->LookupForm<RE::TESQuest>(kRewardQuestId, kSkyrimEsm) : nullptr;
    if (!g_quest) {
        logger::error("Toxicology: quest {:06X} ({}, Few and Far Between) not found - the tome is not given as its reward",
            kRewardQuestId, kSkyrimEsm);
        return;
    }
    g_questId.store(g_quest->GetFormID());
    logger::info("Toxicology: reward quest {:08X} found (Few and Far Between; the tome is given at stage {})",
        g_quest->GetFormID(), kRewardStage);
}

RE::TESQuest* ToxGame::RewardQuest()
{
    return g_quest;
}

void ToxGame::RegisterQuestStageSink()
{
    g_mainThread = std::this_thread::get_id();
    if (!g_quest) return;  // LoadRewardQuest logged it
    auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
    if (!holder) {
        logger::error("Toxicology: no ScriptEventSourceHolder at kDataLoaded - the tome is not given at stage {}", kRewardStage);
        return;
    }
    holder->AddEventSink<RE::TESQuestStageEvent>(QuestStageSink::GetSingleton());  // RE/S/ScriptEventSourceHolder.h:96,147
    logger::info("Toxicology: quest stage sink registered (TESQuestStageEvent)");
}

void ToxGame::QueueFinishedQuestCheck()
{
    if (!g_quest) return;
    QueueRewardTask("load check", [] {
        const auto stage = g_quest->GetCurrentStageID();  // RE/T/TESQuest.h:244 (currentStage)
        logger::info("Toxicology: tome reward: Few and Far Between reads stage {} after the load{}", stage,
            stage == kRewardStage ? " (finished)" : " (not at the reward stage, nothing to do)");
        if (stage == kRewardStage) TryGrantTome(RewardPath::kAlreadyFinished);
        FlushLog();
    });
}

Toxicology::TomeRewardAction ToxGame::TryGrantTome(RewardPath a_path)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* tome = Tome();
    auto* ability = Ability();
    Toxicology::TomeRewardInputs in;
    in.formsPresent = player && tome && ability && g_quest;
    in.enabled = GetSettings().enabled;
    in.rewardSatisfied = RewardSatisfied();
    in.knowsAbility = player && ability && player->HasSpell(ability);  // RE/A/Actor.h:620
    in.tomesHeld = PlayerTomeCount();
    const auto action = Toxicology::DecideTomeReward(in);  // core/ToxCore.h, checked by tools/toxicology-test
    logger::info("Toxicology: tome reward ({}): {} - player {}, tome {}, ability {}, quest {}, enabled {}, satisfied {}, "
        "knows the ability {}, tomes held {}", RewardPathName(a_path), Toxicology::TomeRewardActionName(action),
        player ? "yes" : "no", tome ? "yes" : "no", ability ? "yes" : "no", g_quest ? "yes" : "no", in.enabled,
        in.rewardSatisfied, in.knowsAbility, in.tomesHeld);
    switch (action) {
    case Toxicology::TomeRewardAction::kGrant:
        Grant(*player, *tome, in.tomesHeld);
        break;
    case Toxicology::TomeRewardAction::kMarkSatisfied:
        SetRewardSatisfied(true);
        break;
    case Toxicology::TomeRewardAction::kSkip:
    case Toxicology::TomeRewardAction::kRetryLater:
        break;
    }
    FlushLog();
    return action;
}

std::int32_t ToxGame::PlayerTomeCount()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* tome = Tome();
    if (!player || !tome) return 0;
    // RE/T/TESObjectREFR.h:409 (base container included)
    const auto counts = player->GetInventoryCounts([tome](RE::TESBoundObject& a_object) { return &a_object == tome; });
    const auto it = counts.find(tome);
    return it != counts.end() ? it->second : 0;
}

bool ToxGame::RewardSatisfied()
{
    return g_rewardSatisfied.load();
}

void ToxGame::SetRewardSatisfied(bool a_satisfied)
{
    g_rewardSatisfied.store(a_satisfied);
}
