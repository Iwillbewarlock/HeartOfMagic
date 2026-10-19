// toxicology-test - the tome reward's checks (plugins/Toxicology/src/core/ToxCore.h):
// DecideTomeReward's table, every row and the order the rows are tried in, and
// ParseRewardFlag on the co-save record TOMG's data. toxicology-test.cpp runs
// them after its own checks, with the same PASS / FAIL lines and exit code.

#include "core/ToxCore.h"

#include <cstdint>
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
    };
}
