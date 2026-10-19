#include "game/ToxSave.h"

#include "Common.h"
#include "game/ToxApply.h"
#include "game/ToxHits.h"
#include "game/ToxMagicka.h"
#include "game/ToxReward.h"

#include <atomic>
#include <cmath>
#include <thread>

namespace
{
    // =========================================================================
    // State
    // =========================================================================

    std::thread::id g_mainThread;  // the thread RegisterCoSave ran on (SKSEPluginLoad, the main thread)
    std::atomic<bool> g_saveThreadLogged{ false };
    std::atomic<bool> g_loadThreadLogged{ false };
    std::atomic<bool> g_revertThreadLogged{ false };

    void LogThreadOnce(std::atomic<bool>& a_flag, const char* a_callback)
    {
        if (a_flag.exchange(true)) return;
        const bool onMain = std::this_thread::get_id() == g_mainThread;
        logger::info("Toxicology: co-save {} callback runs on the {} thread", a_callback, onMain ? "main" : "a non-main");
    }

    // =========================================================================
    // Callbacks (SKSE/Interfaces.h:255-263)
    // =========================================================================

    void SaveAccumulated(SKSE::SerializationInterface* a_intfc)
    {
        // The counted total plus the concentration Magicka still waiting for its drain task (a load resets
        // the hook's state, so it would be lost). Nothing is counted here: no bottle is used during a save.
        const float counted = ToxGame::Accumulated();
        const float pending = ToxGame::PendingMagicka();
        const float accumulated = counted + pending;
        // SKSE/Interfaces.h:272 (WriteRecord<T>: type, version, one value)
        if (!a_intfc->WriteRecord(ToxGame::kAccumulatedRecord, ToxGame::kAccumulatedVersion, accumulated)) {
            logger::error("Toxicology: co-save record ACCM ({:.2f} Magicka toward the next bottle: {:.2f} counted + {:.2f} "
                "concentration not yet counted) not written", accumulated, counted, pending);
            return;
        }
        logger::info("Toxicology: co-save: {:.2f} Magicka toward the next bottle saved ({:.2f} counted + {:.2f} concentration "
            "not yet counted)", accumulated, counted, pending);
    }

    void SaveReward(SKSE::SerializationInterface* a_intfc)
    {
        const bool satisfied = ToxGame::RewardSatisfied();
        const std::uint8_t value = satisfied ? 1 : 0;
        if (!a_intfc->WriteRecord(ToxGame::kRewardRecord, ToxGame::kRewardVersion, value)) {  // SKSE/Interfaces.h:272
            logger::error("Toxicology: co-save record TOMG (tome reward satisfied: {}) not written - this save loads with "
                "the reward open", satisfied);
            return;
        }
        logger::info("Toxicology: co-save: tome reward satisfied {} saved", satisfied);
    }

    // Each record on its own: a failed write of one does not skip the other
    void OnSave(SKSE::SerializationInterface* a_intfc)
    {
        LogThreadOnce(g_saveThreadLogged, "save");
        SaveAccumulated(a_intfc);
        SaveReward(a_intfc);
        FlushLog();
    }

    void ReadAccumulated(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length)
    {
        if (a_version != ToxGame::kAccumulatedVersion || a_length != sizeof(float)) {
            logger::warn("Toxicology: co-save record ACCM version {} length {} not read (expected version {}, length {}) - "
                "the count toward the next bottle starts at 0", a_version, a_length, ToxGame::kAccumulatedVersion, sizeof(float));
            return;
        }
        float value = 0.0f;
        if (a_intfc->ReadRecordData(value) != sizeof(float)) {  // SKSE/Interfaces.h:313
            logger::warn("Toxicology: co-save record ACCM ended early - the count toward the next bottle starts at 0");
            return;
        }
        if (!std::isfinite(value) || value < 0.0f) {
            logger::warn("Toxicology: co-save record ACCM holds {} - not a count; the count toward the next bottle starts at 0", value);
            return;
        }
        ToxGame::SetAccumulated(value);
        logger::info("Toxicology: co-save: {:.2f} Magicka toward the next bottle loaded", value);
    }

    // Every way out sets the flag: the record's value, or false (ToxSave.h)
    void ReadReward(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length)
    {
        ToxGame::SetRewardSatisfied(false);
        if (a_version != ToxGame::kRewardVersion || a_length != sizeof(std::uint8_t)) {
            logger::warn("Toxicology: co-save record TOMG version {} length {} not read (expected version {}, length {}) - "
                "the tome reward is open", a_version, a_length, ToxGame::kRewardVersion, sizeof(std::uint8_t));
            return;
        }
        std::uint8_t value = 0;
        if (a_intfc->ReadRecordData(value) != sizeof(std::uint8_t)) {  // SKSE/Interfaces.h:313
            logger::warn("Toxicology: co-save record TOMG ended early - the tome reward is open");
            return;
        }
        const auto satisfied = Toxicology::ParseRewardFlag({ value });  // core/ToxCore.h, checked by tools/toxicology-test
        if (!satisfied) {
            logger::warn("Toxicology: co-save record TOMG holds {} - not 0 or 1; the tome reward is open", value);
            return;
        }
        ToxGame::SetRewardSatisfied(*satisfied);
        logger::info("Toxicology: co-save: tome reward satisfied {} loaded", *satisfied);
    }

    // Revert has already set the total to 0 and the reward flag to false; a save without our records keeps them there
    void OnLoad(SKSE::SerializationInterface* a_intfc)
    {
        LogThreadOnce(g_loadThreadLogged, "load");
        std::uint32_t type = 0;
        std::uint32_t version = 0;
        std::uint32_t length = 0;
        bool rewardRead = false;
        while (a_intfc->GetNextRecordInfo(type, version, length)) {  // SKSE/Interfaces.h:307
            if (type == ToxGame::kAccumulatedRecord) {
                ReadAccumulated(a_intfc, version, length);
            } else if (type == ToxGame::kRewardRecord) {
                if (rewardRead) logger::warn("Toxicology: co-save holds a second TOMG record - the last one wins");
                rewardRead = true;
                ReadReward(a_intfc, version, length);
            } else {
                logger::warn("Toxicology: co-save record type {:08X} version {} is unknown - skipped", type, version);
            }
        }
    }

    void OnRevert(SKSE::SerializationInterface*)
    {
        LogThreadOnce(g_revertThreadLogged, "revert");
        ToxGame::ResetMagicka();
        ToxGame::SetRewardSatisfied(false);
        ToxGame::BumpGeneration();
        ToxGame::ClearApplications();
        logger::debug("Toxicology: co-save revert - count toward the next bottle back to 0, tome reward open");
    }
}

// =============================================================================
// Public
// =============================================================================

void ToxGame::RegisterCoSave()
{
    g_mainThread = std::this_thread::get_id();
    const auto* serialization = SKSE::GetSerializationInterface();  // SKSE/API.h:57
    if (!serialization) {
        logger::error("Toxicology: no SKSE serialization interface - the count toward the next bottle and the tome reward "
            "are not saved");
        return;
    }
    serialization->SetUniqueID(kCoSaveUniqueId);  // SKSE/Interfaces.h:260-263
    serialization->SetSaveCallback(OnSave);
    serialization->SetLoadCallback(OnLoad);
    serialization->SetRevertCallback(OnRevert);
    logger::info("Toxicology: co-save registered (HMTX)");
}
