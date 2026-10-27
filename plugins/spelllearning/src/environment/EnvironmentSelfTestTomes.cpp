#include "environment/EnvironmentSelfTest.h"
#include "SpellTomes.h"
#include "librarian/LoadOrderSource.h"

#include <format>

// =============================================================================
// EnvironmentSelfTestTomes - the tome pairs check of the dev self-test
//
//   sr-paired-tomes  SpellTomes' pairs for tomes that teach nothing (Spell Research
//                    Synthesizer): every pair from SynthesizedPatch.json and Spell
//                    Research's lists resolved to a BOOK that teaches nothing and a
//                    SPEL, and SpellOf answers each paired book. SKIP without
//                    SpellResearch.esp and without the JSON.
// =============================================================================

namespace Environment::SelfTest
{
    namespace
    {
        constexpr const char* kId = "sr-paired-tomes";
        constexpr const char* kSpellResearchPlugin = "SpellResearch.esp";

        // Paired books ForEachTome visits (SpellOf answered them), against the snapshot's count
        std::size_t PairedTomesVisited()
        {
            std::size_t visited = 0;
            SpellTomes::ForEachTome([&](RE::TESObjectBOOK* book, RE::SpellItem*) {
                if (SpellTomes::IsPairedTome(book->GetFormID())) ++visited;
            });
            return visited;
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
        const std::size_t visited = PairedTomesVisited();
        const std::string observed = std::format("generation={} ownSpell={} paired={} json={} liveRecord={} liveScript={} "
            "patchFound={} patchUsable={} liveRead={} unresolved={} notBookOrSpell={} teachesOther={} "
            "liveConflicts={} liveOutsideJson={} jsonTwice={} unequalParts={} visited={}",
            status.generation, status.ownSpell, status.paired, c.patch, c.liveRecord, c.liveScript, status.patchFound,
            status.patchUsable, status.liveRead, c.unresolved, c.notBookOrSpell, c.teachesOther,
            c.liveConflicts, c.liveOutsidePatch, c.patchConflicts, c.unequalParts, visited);
        const bool resolved = c.unresolved == 0 && c.notBookOrSpell == 0 && c.patchConflicts == 0;
        const bool readable = !status.patchFound || status.patchUsable;
        Report(kId, resolved && readable && visited == status.paired ? Outcome::kPass : Outcome::kFail, observed);
    }
}
