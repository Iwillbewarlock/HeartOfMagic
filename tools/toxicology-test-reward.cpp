// toxicology-test - the tome reward's and the power toggle's checks (plugins/Toxicology/src/core/ToxCore.h):
// DecideTomeReward's table, every row and the order the rows are tried in, and
// ParseRewardFlag on the co-save record TOMG's data; since 1.1 DecideToggle,
// DecideMigration and IsRepeatToggle (the power's debounce). toxicology-test.cpp
// runs them after its own checks, with the same PASS / FAIL lines and exit code.

#include "core/ToxCore.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using Toxicology::TomeRewardAction;
    using Toxicology::TomeRewardInputs;

    // A player who finished the quest, the reward not yet given: the one row that grants
    TomeRewardInputs Fresh()
    {
        TomeRewardInputs in;
        in.formsPresent = true;
        in.enabled = true;
        return in;
    }

    std::string ExpectAction(const TomeRewardInputs& a_in, TomeRewardAction a_want)
    {
        const auto got = Toxicology::DecideTomeReward(a_in);
        if (got == a_want) return {};
        return std::string("got ") + Toxicology::TomeRewardActionName(got) + ", expected " +
               Toxicology::TomeRewardActionName(a_want) + " (forms " + std::to_string(a_in.formsPresent) + ", enabled " +
               std::to_string(a_in.enabled) + ", satisfied " + std::to_string(a_in.rewardSatisfied) + ")";
    }

    std::string RewardGrant()
    {
        return ExpectAction(Fresh(), TomeRewardAction::kGrant);
    }

    // A form or the player missing comes first, whatever else holds
    std::string RewardFormsMissing()
    {
        auto in = Fresh();
        in.formsPresent = false;
        if (auto detail = ExpectAction(in, TomeRewardAction::kRetryLater); !detail.empty()) return detail;
        in.enabled = false;
        in.rewardSatisfied = true;
        return ExpectAction(in, TomeRewardAction::kRetryLater);
    }

    // Off: nothing given and the flag left alone
    std::string RewardDisabled()
    {
        auto in = Fresh();
        in.enabled = false;
        return ExpectAction(in, TomeRewardAction::kSkip);
    }

    // Satisfied: never again, also when the tome was sold unread
    std::string RewardAlreadySatisfied()
    {
        auto in = Fresh();
        in.rewardSatisfied = true;
        return ExpectAction(in, TomeRewardAction::kSkip);
    }

    std::string ExpectFlag(const std::vector<std::uint8_t>& a_data, std::optional<bool> a_want)
    {
        const auto got = Toxicology::ParseRewardFlag(a_data);
        if (got == a_want) return {};
        const auto text = [](std::optional<bool> a_v) { return a_v ? std::string(*a_v ? "true" : "false") : std::string("nullopt"); };
        std::string bytes;
        for (const auto b : a_data) bytes += (bytes.empty() ? "" : " ") + std::to_string(b);
        return "[" + bytes + "]: got " + text(got) + ", expected " + text(a_want);
    }

    std::string RewardFlagZeroOne()
    {
        if (auto detail = ExpectFlag({ 0 }, false); !detail.empty()) return detail;
        return ExpectFlag({ 1 }, true);
    }

    std::string RewardFlagBadByte()
    {
        if (auto detail = ExpectFlag({ 2 }, std::nullopt); !detail.empty()) return detail;
        return ExpectFlag({ 0xFF }, std::nullopt);
    }

    std::string RewardFlagWrongLength()
    {
        if (auto detail = ExpectFlag({}, std::nullopt); !detail.empty()) return detail;
        return ExpectFlag({ 1, 0 }, std::nullopt);
    }

    // ------------------------------------------------------------ power toggle (1.1)

    using Toxicology::MigrationAction;
    using Toxicology::ToggleAction;

    constexpr double kWindow = 0.25;  // exact in binary, so the edge case below is exact
    constexpr int kNotAnEnumValue = 99;

    std::string ExpectToggle(bool a_hasAbility, ToggleAction a_want)
    {
        const auto got = Toxicology::DecideToggle(a_hasAbility);
        if (got == a_want) return {};
        return std::string("ability ") + (a_hasAbility ? "held" : "not held") + ": got " + Toxicology::ToggleActionName(got) +
               ", expected " + Toxicology::ToggleActionName(a_want);
    }

    std::string ExpectMigration(bool a_hasAbility, bool a_hasPower, MigrationAction a_want)
    {
        const auto got = Toxicology::DecideMigration(a_hasAbility, a_hasPower);
        if (got == a_want) return {};
        return "ability " + std::to_string(a_hasAbility) + ", power " + std::to_string(a_hasPower) + ": got " +
               Toxicology::MigrationActionName(got) + ", expected " + Toxicology::MigrationActionName(a_want);
    }

    std::string ExpectRepeat(std::optional<double> a_last, double a_now, double a_window, bool a_want)
    {
        const bool got = Toxicology::IsRepeatToggle(a_last, a_now, a_window);
        if (got == a_want) return {};
        return "(" + (a_last ? std::to_string(*a_last) : std::string("none")) + ", " + std::to_string(a_now) + ", " +
               std::to_string(a_window) + ") gave " + (got ? "repeat" : "not a repeat");
    }

    // Off turns on, on turns off
    std::string ToggleBothWays()
    {
        if (auto detail = ExpectToggle(false, ToggleAction::kAdd); !detail.empty()) return detail;
        return ExpectToggle(true, ToggleAction::kRemove);
    }

    // The log names: distinct, and "?" for a value outside the enum
    std::string ToggleNames()
    {
        const std::string add = Toxicology::ToggleActionName(ToggleAction::kAdd);
        const std::string remove = Toxicology::ToggleActionName(ToggleAction::kRemove);
        const std::string bad = Toxicology::ToggleActionName(static_cast<ToggleAction>(kNotAnEnumValue));
        if (add != "Add" || remove != "Remove" || bad != "?") return "got " + add + ", " + remove + ", " + bad;
        return {};
    }

    // A 1.0 save (the ability, no power) gets the power
    std::string MigrationOldSave()
    {
        return ExpectMigration(true, false, MigrationAction::kGrantPower);
    }

    // Every other state: nothing (a new reader starts off, the power only stays off, both stay as they are)
    std::string MigrationNothing()
    {
        if (auto detail = ExpectMigration(false, false, MigrationAction::kNothing); !detail.empty()) return detail;
        if (auto detail = ExpectMigration(false, true, MigrationAction::kNothing); !detail.empty()) return detail;
        return ExpectMigration(true, true, MigrationAction::kNothing);
    }

    std::string MigrationNames()
    {
        const std::string grant = Toxicology::MigrationActionName(MigrationAction::kGrantPower);
        const std::string nothing = Toxicology::MigrationActionName(MigrationAction::kNothing);
        const std::string bad = Toxicology::MigrationActionName(static_cast<MigrationAction>(kNotAnEnumValue));
        if (grant != "GrantPower" || nothing != "Nothing" || bad != "?") return "got " + grant + ", " + nothing + ", " + bad;
        return {};
    }

    // No accepted toggle yet: never a repeat
    std::string RepeatFirstToggle()
    {
        return ExpectRepeat(std::nullopt, 10.0, kWindow, false);
    }

    // Inside the window (the same moment included) is a repeat, also on negative clock values
    std::string RepeatInsideWindow()
    {
        if (auto detail = ExpectRepeat(10.0, 10.1, kWindow, true); !detail.empty()) return detail;
        if (auto detail = ExpectRepeat(10.0, 10.0, kWindow, true); !detail.empty()) return detail;
        return ExpectRepeat(-5.0, -4.9, kWindow, true);
    }

    // Exactly the window after: a new toggle; later: a new toggle
    std::string RepeatAtAndAfterEdge()
    {
        if (auto detail = ExpectRepeat(10.0, 10.0 + kWindow, kWindow, false); !detail.empty()) return detail;
        return ExpectRepeat(10.0, 11.0, kWindow, false);
    }

    // Bad input never locks the power: non-finite values, a window not above 0, a clock that went back
    std::string RepeatBadInput()
    {
        constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
        constexpr double kInf = std::numeric_limits<double>::infinity();
        struct Row
        {
            std::optional<double> last;
            double now;
            double window;
        };
        const Row rows[] = {
            { kNaN, 10.0, kWindow }, { 10.0, kNaN, kWindow }, { 10.0, 10.1, kNaN }, { kInf, 10.0, kWindow },
            { 10.0, kInf, kWindow }, { 10.0, 10.1, kInf }, { 10.0, 10.1, 0.0 }, { 10.0, 10.1, -1.0 }, { 10.0, 9.9, kWindow },
        };
        for (const auto& row : rows) {
            if (auto detail = ExpectRepeat(row.last, row.now, row.window, false); !detail.empty()) return detail;
        }
        return {};
    }
}

std::vector<std::pair<const char*, std::string (*)()>> RewardChecks()
{
    return {
        { "RewardGrant", RewardGrant },
        { "RewardFormsMissing", RewardFormsMissing },
        { "RewardDisabled", RewardDisabled },
        { "RewardAlreadySatisfied", RewardAlreadySatisfied },
        { "RewardFlagZeroOne", RewardFlagZeroOne },
        { "RewardFlagBadByte", RewardFlagBadByte },
        { "RewardFlagWrongLength", RewardFlagWrongLength },
        { "ToggleBothWays", ToggleBothWays },
        { "ToggleNames", ToggleNames },
        { "MigrationOldSave", MigrationOldSave },
        { "MigrationNothing", MigrationNothing },
        { "MigrationNames", MigrationNames },
        { "RepeatFirstToggle", RepeatFirstToggle },
        { "RepeatInsideWindow", RepeatInsideWindow },
        { "RepeatAtAndAfterEdge", RepeatAtAndAfterEdge },
        { "RepeatBadInput", RepeatBadInput },
    };
}
