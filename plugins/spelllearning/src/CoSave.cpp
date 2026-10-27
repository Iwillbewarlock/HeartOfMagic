// =============================================================================
// CoSave.cpp — SKSE serialization callbacks (moved out of Main.cpp)
// =============================================================================

#include "CoSave.h"
#include "ProgressionManager.h"
#include "SpellEffectivenessHook.h"
#include "SpellTomeHook.h"

namespace CoSave
{
    namespace
    {
        // One owner's step. An exception must not leave the callback (it would take
        // the game down), and one owner failing must not keep the others from
        // writing or reading their records: logged, and the next one goes on.
        template <class F>
        void Guarded(const char* step, F&& call) noexcept
        {
            try {
                call();
            } catch (const std::exception& e) {
                logger::error("SKSE Serialization: {} threw: {} - the other owners go on", step, e.what());
            } catch (...) {
                logger::error("SKSE Serialization: {} threw an unknown exception - the other owners go on", step);
            }
        }
    }

    void OnGameSaved(SKSE::SerializationInterface* a_intfc)
    {
        logger::info("SKSE Serialization: Game saved");
        Guarded("ProgressionManager save", [&] { ProgressionManager::GetSingleton()->OnGameSaved(a_intfc); });
        Guarded("SpellEffectivenessHook save", [&] { SpellEffectivenessHook::GetSingleton()->OnGameSaved(a_intfc); });
        Guarded("SpellTomeHook save", [&] { SpellTomeHook::GetSingleton()->OnGameSaved(a_intfc); });
        // NOTE: DEST registrations are NOT serialized to co-save.
        // They are re-established on each load via AutoRegisterISLAliases()
        // in OnPostLoadGame, since OnInit() only fires once per save creation.
        FlushLog();
    }

    void OnGameLoaded(SKSE::SerializationInterface* a_intfc)
    {
        logger::info("SKSE Serialization: Game loaded");

        // One record stream, read once. Each owner used to run its own
        // GetNextRecordInfo loop, and the first one drained the stream: the
        // progression reader consumed the early-learned record, logged it as an
        // unknown type and threw it away, so a spell learned early lost its
        // weakened state on every load.
        auto* progression = ProgressionManager::GetSingleton();
        auto* effectiveness = SpellEffectivenessHook::GetSingleton();
        auto* tomeHook = SpellTomeHook::GetSingleton();

        Guarded("ProgressionManager begin load", [&] { progression->BeginLoad(); });
        Guarded("SpellEffectivenessHook begin load", [&] { effectiveness->BeginLoad(); });

        // A record whose reader threw is skipped: GetNextRecordInfo goes on to the
        // next record whatever was read of this one
        uint32_t type = 0, version = 0, length = 0;
        while (a_intfc->GetNextRecordInfo(type, version, length)) {
            Guarded("record read", [&] {
                if (progression->ReadRecord(a_intfc, type, version, length)) return;
                if (effectiveness->ReadRecord(a_intfc, type, version, length)) return;
                if (tomeHook->ReadRecord(a_intfc, type, version, length)) return;
                logger::warn("SKSE Serialization: no owner for record type {:08X}, skipped", type);
            });
        }

        // Progression first: the effectiveness hook's EndLoad reads the progress
        Guarded("ProgressionManager end load", [&] { progression->EndLoad(); });
        Guarded("SpellEffectivenessHook end load", [&] { effectiveness->EndLoad(); });
        // NOTE: DEST registrations are handled via AutoRegisterISLAliases()
        // in OnPostLoadGame, not through serialization.
    }

    void OnRevert(SKSE::SerializationInterface* a_intfc)
    {
        logger::info("SKSE Serialization: Reverting (new game or loading different save)");
        Guarded("ProgressionManager revert", [&] { ProgressionManager::GetSingleton()->OnRevert(a_intfc); });
        Guarded("SpellEffectivenessHook revert", [&] { SpellEffectivenessHook::GetSingleton()->OnRevert(a_intfc); });
        // A different save means a different inventory, and its own tomes read
        // (the load that follows brings that save's list, a new game none)
        Guarded("SpellTomeHook revert", [&] {
            SpellTomeHook::GetSingleton()->InvalidateTomeInventoryCache();
            SpellTomeHook::GetSingleton()->ClearTomeXPTracking();
        });
        // NOTE: Do NOT revert DEST registrations here.
        // AutoRegisterISLAliases() in OnPostLoadGame will re-establish them.
        // Reverting would clear them, and since OnInit() only fires once per
        // save creation, they'd never come back until the next load.
    }
}
