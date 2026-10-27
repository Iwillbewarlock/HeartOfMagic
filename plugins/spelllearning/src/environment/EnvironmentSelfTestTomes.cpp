#include "environment/EnvironmentSelfTest.h"
#include "SpellScanner.h"
#include "SpellTomes.h"
#include "tomereach/TomeReach.h"
#include "librarian/LoadOrderSource.h"

#include "ThreadUtils.h"

#include <format>
#include <thread>

// =============================================================================
// EnvironmentSelfTestTomes - the tome pairs check of the dev self-test
//
//   sr-paired-tomes  SpellTomes' pairs for tomes that teach nothing (Spell Research
//                    Synthesizer): every pair from SynthesizedPatch.json and Spell
//                    Research's lists resolved to a BOOK that teaches nothing and a
//                    SPEL; SpellOf answers each pair's book with its spell, the spell has
//                    a tome in TomesBySpell, and the tome reach pass of this generation
//                    got every tome ForEachTome gives. SKIP without
//                    SpellResearch.esp and without the JSON.
// =============================================================================

namespace Environment::SelfTest
{
    namespace
    {
        constexpr const char* kId = "sr-paired-tomes";
        constexpr const char* kSpellResearchPlugin = "SpellResearch.esp";

        struct PairCheck
        {
            std::size_t pairs = 0;
            std::size_t wrongSpellOf = 0;        // SpellOf(book) is not the pair's spell
            std::size_t notInTomesBySpell = 0;   // the pair's spell has no tome in TomesBySpell
            std::size_t reachMissing = 0;        // ForEachTome's tomes the pass of this generation did not get
            std::size_t reachExtra = 0;          // ... and books it got that ForEachTome does not give now
            bool reachSameGeneration = false;    // the last pass is this generation's
        };

        // ForEachTome's books against the books the pass was handed (less those KeyOf left out)
        void CompareWithPass(std::uint64_t generation, PairCheck& check)
        {
            std::unordered_set<RE::FormID> keyed, skipped, tomes;
            check.reachSameGeneration = TomeReach::TomeBooks(generation, keyed, skipped);
            SpellTomes::ForEachTome([&](RE::TESObjectBOOK* book, RE::SpellItem*) {
                if (!skipped.contains(book->GetFormID())) tomes.insert(book->GetFormID());
            });
            for (const RE::FormID book : tomes) check.reachMissing += keyed.contains(book) ? 0 : 1;
            for (const RE::FormID book : keyed) check.reachExtra += tomes.contains(book) ? 0 : 1;
        }

        PairCheck CheckPairs(std::uint64_t generation)
        {
            PairCheck check;
            const auto bySpell = SpellScanner::TomesBySpell();
            SpellTomes::ForEachPair([&](RE::FormID bookId, RE::FormID spellId) {
                ++check.pairs;
                auto* spell = SpellTomes::SpellOf(RE::TESForm::LookupByID<RE::TESObjectBOOK>(bookId));
                if (!spell || spell->GetFormID() != spellId) ++check.wrongSpellOf;
                if (!bySpell.contains(spellId)) ++check.notInTomesBySpell;
            });
            CompareWithPass(generation, check);
            return check;
        }
    }

    void CheckSpellTomes()
    {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        const bool srLoaded = dataHandler && Librarian::PluginLoaded(dataHandler, kSpellResearchPlugin);
        SpellTomes::Status status;
        const bool read = SpellTomes::CurrentStatus(status);
        if (!srLoaded && (!read || !status.patchFound)) {
            Report(kId, Outcome::kSkip, "SpellResearch.esp not loaded, no SynthesizedPatch.json");
            return;
        }
        if (!read) {
            Report(kId, Outcome::kFail, "SpellTomes never read the pairs (Rebuild not called at data load)");
            return;
        }
        const auto& c = status.counts;
        const PairCheck pairs = CheckPairs(status.generation);
        const std::string observed = std::format("generation={} ownSpell={} paired={} json={} liveRecord={} liveScript={} "
            "patchFound={} patchUsable={} liveRead={} unresolved={} notBookOrSpell={} teachesOther={} "
            "liveConflicts={} liveOutsideJson={} jsonTwice={} unequalParts={} pairsChecked={} wrongSpellOf={} "
            "notInTomesBySpell={} passSameGeneration={} passMissing={} passExtra={}",
            status.generation, status.ownSpell, status.paired, c.patch, c.liveRecord, c.liveScript, status.patchFound,
            status.patchUsable, status.liveRead, c.unresolved, c.notBookOrSpell, c.teachesOther,
            c.liveConflicts, c.liveOutsidePatch, c.patchConflicts, c.unequalParts, pairs.pairs, pairs.wrongSpellOf,
            pairs.notInTomesBySpell, pairs.reachSameGeneration, pairs.reachMissing, pairs.reachExtra);
        const bool resolved = c.unresolved == 0 && c.notBookOrSpell == 0 && c.patchConflicts == 0;
        const bool readable = !status.patchFound || status.patchUsable;
        const bool answered = pairs.pairs == status.paired && pairs.wrongSpellOf == 0 && pairs.notInTomesBySpell == 0;
        const bool reached = pairs.reachSameGeneration && pairs.reachMissing == 0 && pairs.reachExtra == 0;
        Report(kId, resolved && readable && answered && reached ? Outcome::kPass : Outcome::kFail, observed);
    }

    // Which threads the engine ran our entries on (ThreadUtils.h SampleThread), once each, with the
    // main thread's id then: data to judge in game. Judged: a new thread of ours is not the game
    // thread, and the self-test's own task (posted by its ticker) is
    void CheckGameThreadFlag()
    {
        constexpr const char* kKinds[] = { "task", "native", "message", "eventSink" };
        constexpr std::uint64_t kLowMask = 0xFFFFFFFFull;
        std::string samples;
        for (std::size_t i = 0; i < std::size(kKinds); ++i) {
            const std::uint64_t value = g_threadSamples[i].load(std::memory_order_relaxed);
            if (!samples.empty()) samples += " ";
            samples += value ? std::format("{}={}/main{}", kKinds[i], value >> kThreadIdShift, value & kLowMask)
                             : std::format("{}=unseen", kKinds[i]);
        }
        const auto* main = RE::Main::GetSingleton();
        Note("game-thread", std::format("{} now={} main={}", samples, REX::W32::GetCurrentThreadId(),
            main ? main->threadID : 0u));
        bool elsewhere = true;
        try {
            std::thread([&elsewhere] { elsewhere = IsOnGameThread(); }).join();
        } catch (const std::system_error& e) {
            Report("game-thread", Outcome::kSkip, std::string("no thread to ask from: ") + e.what());
            return;
        }
        const bool here = IsOnGameThread();
        Report("game-thread", here && !elsewhere ? Outcome::kPass : Outcome::kFail,
            std::format("inSelfTestTask={} newThread={}", here, elsewhere));
    }
}
