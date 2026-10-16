#include "game/ToxSave.h"

#include "Common.h"
#include "game/ToxApply.h"
#include "game/ToxHits.h"
#include "game/ToxMagicka.h"

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

    void OnSave(SKSE::SerializationInterface* a_intfc)
    {
        LogThreadOnce(g_saveThreadLogged, "save");
        const float accumulated = ToxGame::Accumulated();
        // SKSE/Interfaces.h:272 (WriteRecord<T>: type, version, one value)
        if (!a_intfc->WriteRecord(ToxGame::kAccumulatedRecord, ToxGame::kAccumulatedVersion, accumulated)) {
            logger::error("Toxicology: co-save record ACCM ({:.2f} Magicka toward the next bottle) not written", accumulated);
            return;
        }
        logger::info("Toxicology: co-save: {:.2f} Magicka toward the next bottle saved", accumulated);
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

    // Revert has already set the total to 0; a save without our record keeps it there
    void OnLoad(SKSE::SerializationInterface* a_intfc)
    {
        LogThreadOnce(g_loadThreadLogged, "load");
        std::uint32_t type = 0;
        std::uint32_t version = 0;
        std::uint32_t length = 0;
        while (a_intfc->GetNextRecordInfo(type, version, length)) {  // SKSE/Interfaces.h:307
            if (type == ToxGame::kAccumulatedRecord) {
                ReadAccumulated(a_intfc, version, length);
            } else {
                logger::warn("Toxicology: co-save record type {:08X} version {} is unknown - skipped", type, version);
            }
        }
    }

    void OnRevert(SKSE::SerializationInterface*)
    {
        LogThreadOnce(g_revertThreadLogged, "revert");
        ToxGame::ResetMagicka();
        ToxGame::BumpGeneration();
        ToxGame::ClearApplications();
        logger::debug("Toxicology: co-save revert - count toward the next bottle back to 0");
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
        logger::error("Toxicology: no SKSE serialization interface - the count toward the next bottle is not saved");
        return;
    }
    serialization->SetUniqueID(kCoSaveUniqueId);  // SKSE/Interfaces.h:260-263
    serialization->SetSaveCallback(OnSave);
    serialization->SetLoadCallback(OnLoad);
    serialization->SetRevertCallback(OnRevert);
    logger::info("Toxicology: co-save registered (HMTX)");
}
