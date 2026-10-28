#include "environment/EnvironmentSelfTest.h"
#include "uimanager/StartupState.h"
#include "uimanager/UIManager.h"
#include "uimanager/UIManagerInternal.h"

#include <format>
#include <functional>
#include <string>

// =============================================================================
// EnvironmentSelfTestGraft - the tree graft check of the dev self-test
//
//   tree-graft  The session's graft at the main menu (TreeGraftRun.cpp): PASS when it
//               wrote the tree and every check passed - every pre-graft node's x, y,
//               tier, children and prerequisites as they were (ids compared through the
//               id refresh, Ruling 2), each grafted node under a live parent of its
//               school in the validated tree, no seen spell grafted - and the panel's
//               last updateTreeData text is what the file validates to now (what the
//               panel's tree load would send). SKIP when no graft wrote this session
//               (why), or when spell_tree.json was saved again since. FAIL otherwise.
// =============================================================================

namespace Environment::SelfTest
{
    void CheckTreeGraft()
    {
        constexpr const char* kId = "tree-graft";
        using State = TreeGraftOutcome::State;
        const auto graft = LastTreeGraft();
        switch (graft.state) {
            case State::kNotRun:
                Report(kId, Outcome::kSkip, "no graft ran this session: " + (graft.detail.empty() ? std::string("the scan at game start did not reach it") : graft.detail));
                return;
            case State::kNothingWritten:
            case State::kDropped:
                Report(kId, graft.passed || graft.state == State::kDropped ? Outcome::kSkip : Outcome::kFail,
                    "the graft wrote no new spell: " + graft.detail);
                return;
            case State::kFailed:
                Report(kId, Outcome::kFail, graft.detail);
                return;
            case State::kWritten:
                break;
        }
        if (!graft.passed) {
            Report(kId, Outcome::kFail, graft.detail);
            return;
        }
        if (graft.writtenHash == 0) {
            Report(kId, Outcome::kPass, "only the record was written (nothing grafted): " + graft.detail);
            return;
        }
        auto file = ReadTreeFileText(UIManager::GetTreeFilePath());
        if (file.outcome != TreeFileRead::Ok) {
            Report(kId, Outcome::kFail, "spell_tree.json could not be read back");
            return;
        }
        if (std::hash<std::string>{}(file.text) != graft.writtenHash) {
            Report(kId, Outcome::kSkip, "spell_tree.json was saved again since the graft - the panel's copy is not compared");
            return;
        }
        const auto* ui = UIManager::GetSingleton();
        if (!ui || ui->LastTreeDataHash() == 0) {
            Report(kId, Outcome::kSkip, "no tree was sent to a panel this session: " + graft.detail);
            return;
        }
        const PanelTree panel = ValidateTreeAsPanelPath(std::move(file.text));
        const bool same = std::hash<std::string>{}(panel.panelText) == ui->LastTreeDataHash();
        Report(kId, same ? Outcome::kPass : Outcome::kFail,
            std::format("{}; the panel's last tree {} the file's validated tree ({} bytes, {})", graft.detail,
                same ? "is" : "is NOT", panel.panelText.size(), panel.fromValidated ? "validated dump" : "the file's text"));
    }
}
