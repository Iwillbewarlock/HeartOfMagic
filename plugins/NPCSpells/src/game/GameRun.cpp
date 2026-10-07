#include "game/GameRun.h"

#include "game/GameFiles.h"
#include "game/GameLists.h"

#include <chrono>
#include <exception>

namespace
{
    using Clock = std::chrono::steady_clock;

    long long MsSince(Clock::time_point start)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    }
}

void NpcSpellsGame::Run()
{
    // A malformed file or an unreadable folder must never take the game down
    try {
        const auto readStart = Clock::now();
        auto inputs = ReadInputs();
        logger::info("NPCSpells: input files read in {} ms", MsSince(readStart));
        if (!inputs.ok) {
            logger::error("NPCSpells: stopped: {}", inputs.stopReason);
            return;
        }
        if (inputs.rulesFromDefaults) {
            logger::info("NPCSpells: using the built-in rules");
        }

        const auto esl = LookupEslRecords();
        logger::info("NPCSpells: ESL records loaded={} found={} missing={} wrongType={}",
            esl.loaded, esl.byRecord.size() - esl.missing - esl.wrongType, esl.missing, esl.wrongType);
        if (!esl.loaded) {
            return;
        }
        // Tasks 9-10: pool, list contents, NPCs, report
    } catch (const std::exception& e) {
        logger::error("NPCSpells: stopped by an error: {}", e.what());
    } catch (...) {
        logger::error("NPCSpells: stopped by an unknown error");
    }
}
