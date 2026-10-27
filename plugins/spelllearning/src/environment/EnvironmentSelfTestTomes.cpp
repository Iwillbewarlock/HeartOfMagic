#include "environment/EnvironmentSelfTest.h"
#include "SpellScanner.h"
#include "SpellTomes.h"
#include "tomereach/TomeReach.h"
#include "librarian/LoadOrderSource.h"

#include <format>

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

        constexpr RE::FormID kRuntimeIndex = 0xFF;   // forms made at run time: no plugin, not in the pass
        constexpr unsigned kFileIndexShift = 24;

        struct PairCheck
        {
            std::size_t pairs = 0;
            std::size_t wrongSpellOf = 0;        // SpellOf(book) is not the pair's spell
            std::size_t notInTomesBySpell = 0;   // the pair's spell has no tome in TomesBySpell
            std::size_t tomes = 0;               // ForEachTome's tomes from a plugin (what TomeReach should get)
        };

        PairCheck CheckPairs()
        {
            PairCheck check;
            const auto bySpell = SpellScanner::TomesBySpell();
            SpellTomes::ForEachPair([&](RE::FormID bookId, RE::FormID spellId) {
                ++check.pairs;
                auto* spell = SpellTomes::SpellOf(RE::TESForm::LookupByID<RE::TESObjectBOOK>(bookId));
                if (!spell || spell->GetFormID() != spellId) ++check.wrongSpellOf;
                if (!bySpell.contains(spellId)) ++check.notInTomesBySpell;
            });
            SpellTomes::ForEachTome([&](RE::TESObjectBOOK* book, RE::SpellItem*) {
                if ((book->GetFormID() >> kFileIndexShift) != kRuntimeIndex) ++check.tomes;
            });
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
        const PairCheck pairs = CheckPairs();
        const std::size_t reachTomes = TomeReach::TomeCount(status.generation);
        const std::string observed = std::format("generation={} ownSpell={} paired={} json={} liveRecord={} liveScript={} "
            "patchFound={} patchUsable={} liveRead={} unresolved={} notBookOrSpell={} teachesOther={} "
            "liveConflicts={} liveOutsideJson={} jsonTwice={} unequalParts={} pairsChecked={} wrongSpellOf={} "
            "notInTomesBySpell={} tomes={} tomeReachTomes={}",
            status.generation, status.ownSpell, status.paired, c.patch, c.liveRecord, c.liveScript, status.patchFound,
            status.patchUsable, status.liveRead, c.unresolved, c.notBookOrSpell, c.teachesOther,
            c.liveConflicts, c.liveOutsidePatch, c.patchConflicts, c.unequalParts, pairs.pairs, pairs.wrongSpellOf,
            pairs.notInTomesBySpell, pairs.tomes, reachTomes);
        const bool resolved = c.unresolved == 0 && c.notBookOrSpell == 0 && c.patchConflicts == 0;
        const bool readable = !status.patchFound || status.patchUsable;
        const bool answered = pairs.pairs == status.paired && pairs.wrongSpellOf == 0 && pairs.notInTomesBySpell == 0;
        Report(kId, resolved && readable && answered && reachTomes == pairs.tomes ? Outcome::kPass : Outcome::kFail, observed);
    }
}
