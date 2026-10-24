#include "Common.h"
#include "FormIdText.h"
#include "uimanager/StartupState.h"
#include "ProgressionManager.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <vector>

// =============================================================================
// TREE PARITY - game start's tree state against the panel's, once a session
// =============================================================================
//
// Game start fills ProgressionManager's tree state from spell_tree.json
// (UIManagerTreeState.cpp); a few seconds later the panel clears it and sends the
// same tree's prerequisites. Right after that first send the state is read again
// and compared with what game start left: one line in SpellLearning.log says
// whether the plugin made exactly what the panel makes. SKIP instead when game
// start applied nothing, when there is no panel, when a game starts or loads
// first, or when the panel's next data apply comes after kPanelLoadWindow. Game
// thread only (the message handler, then game thread tasks), so no lock.

namespace
{
    constexpr std::size_t kMaxListedDifferences = 5;
    // The panel answers a tree load with its prerequisites within a second or two; a data
    // apply later than this is some other send (edit mode, a build), not the tree load's
    constexpr auto kPanelLoadWindow = std::chrono::seconds(30);

    // One tree spell as ProgressionManager holds it, its lists sorted
    struct SpellState
    {
        std::vector<RE::FormID> hard;
        std::vector<RE::FormID> soft;
        int softNeeded = 0;

        bool operator==(const SpellState&) const = default;
        [[nodiscard]] bool HasPrereqs() const { return !hard.empty() || !soft.empty(); }
    };
    using TreeState = std::map<RE::FormID, SpellState>;

    enum class Phase
    {
        Waiting,   // game start has not decided yet
        Recorded,  // baseline taken, the panel's tree load not seen yet
        Armed,     // the panel was sent the saved tree: its next data apply is compared
        Done,      // the session's line is written
    };

    struct ParityState
    {
        Phase phase = Phase::Waiting;
        TreeState baseline;
        std::chrono::steady_clock::time_point armedAt;  // Armed: when the saved tree went to the panel
    };

    ParityState& Parity()
    {
        static ParityState state;
        return state;
    }

    TreeState CaptureTreeState()
    {
        auto* pm = ProgressionManager::GetSingleton();
        TreeState state;
        for (const RE::FormID spellId : pm->GetTreeSpells()) {
            auto reqs = pm->GetPrereqRequirements(spellId);
            std::sort(reqs.hardPrereqs.begin(), reqs.hardPrereqs.end());
            std::sort(reqs.softPrereqs.begin(), reqs.softPrereqs.end());
            state.emplace(spellId, SpellState{ std::move(reqs.hardPrereqs), std::move(reqs.softPrereqs), reqs.softNeeded });
        }
        return state;
    }

    std::size_t CountWithPrereqs(const TreeState& state)
    {
        return static_cast<std::size_t>(std::count_if(state.begin(), state.end(),
            [](const auto& spell) { return spell.second.HasPrereqs(); }));
    }

    std::string ListText(const std::vector<RE::FormID>& ids)
    {
        std::string text;
        for (const auto id : ids) {
            if (!text.empty()) text += ' ';
            text += FormIdText::Format(id);
        }
        return text;
    }

    std::string SpellText(const TreeState& state, RE::FormID spellId)
    {
        const auto it = state.find(spellId);
        if (it == state.end()) return "not a tree spell";
        return std::format("hard [{}] soft [{}] need {}", ListText(it->second.hard), ListText(it->second.soft),
            it->second.softNeeded);
    }

    // Up to kMaxListedDifferences spells whose state differs, each with both sides
    std::string DifferenceText(const TreeState& startup, const TreeState& panel)
    {
        std::vector<RE::FormID> differing;
        auto note = [&](RE::FormID spellId) {
            if (std::find(differing.begin(), differing.end(), spellId) == differing.end()) differing.push_back(spellId);
        };
        for (const auto& [spellId, spell] : startup) {
            const auto it = panel.find(spellId);
            if (it == panel.end() || !(it->second == spell)) note(spellId);
        }
        for (const auto& [spellId, spell] : panel) {
            if (!startup.contains(spellId)) note(spellId);
        }
        std::sort(differing.begin(), differing.end());

        std::string text = std::format("{} spells differ", differing.size());
        for (std::size_t i = 0; i < differing.size() && i < kMaxListedDifferences; ++i) {
            text += std::format("; {}: startup {} / panel {}", FormIdText::Format(differing[i]),
                SpellText(startup, differing[i]), SpellText(panel, differing[i]));
        }
        return text;
    }

    // The session's line when there is nothing to compare
    void MarkSkipped(ParityState& parity, std::string_view reason)
    {
        parity.phase = Phase::Done;
        parity.baseline.clear();
        logger::info("TreeParity SKIP ({})", reason);
    }
}

namespace TreeParity
{
    void RecordBaseline()
    {
        auto& parity = Parity();
        if (parity.phase != Phase::Waiting) return;
        parity.baseline = CaptureTreeState();
        parity.phase = Phase::Recorded;
        // Not "TreeParity ...": that prefix is the session's one verdict line
        logger::info("Startup tree: state recorded ({} spells, {} with prerequisites) - compared after the panel's "
                     "tree load", parity.baseline.size(), CountWithPrereqs(parity.baseline));
    }

    void Skip(std::string_view reason)
    {
        auto& parity = Parity();
        if (parity.phase == Phase::Waiting) MarkSkipped(parity, reason);
    }

    void Expire(std::string_view reason)
    {
        auto& parity = Parity();
        if (parity.phase == Phase::Recorded || parity.phase == Phase::Armed) MarkSkipped(parity, reason);
    }

    void ArmForPanelLoad()
    {
        auto& parity = Parity();
        // A second tree load before the panel's answer (the page reloaded) waits from the latest send
        if (parity.phase != Phase::Recorded && parity.phase != Phase::Armed) return;
        parity.phase = Phase::Armed;
        parity.armedAt = std::chrono::steady_clock::now();
    }

    void AfterPanelApply(const char* error)
    {
        auto& parity = Parity();
        if (parity.phase != Phase::Armed) return;
        if (std::chrono::steady_clock::now() - parity.armedAt > kPanelLoadWindow) {
            MarkSkipped(parity, "the panel sent no prerequisites for its tree load");
            return;
        }
        parity.phase = Phase::Done;
        if (error) {
            parity.baseline.clear();
            logger::warn("TreeParity FAIL (the panel's SetTreePrerequisites threw: {})", error);
            return;
        }
        // The verdict line is written here or not at all: a throw in the comparison is its own FAIL,
        // not the caller's "SetTreePrerequisites exception"
        try {
            const auto panel = CaptureTreeState();
            if (panel == parity.baseline) {
                logger::info("TreeParity PASS ({} spells, {} with prerequisites)", panel.size(), CountWithPrereqs(panel));
            } else {
                logger::warn("TreeParity FAIL (startup {} spells / {} with prerequisites, panel {} / {}; {})",
                    parity.baseline.size(), CountWithPrereqs(parity.baseline), panel.size(), CountWithPrereqs(panel),
                    DifferenceText(parity.baseline, panel));
            }
        } catch (const std::exception& e) {
            logger::warn("TreeParity FAIL (the comparison threw: {})", e.what());
        }
        parity.baseline.clear();
    }
}
