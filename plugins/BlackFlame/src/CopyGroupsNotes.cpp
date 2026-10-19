// =============================================================================
// CopyGroupsNotes - a slot's group state in words, for the self-test's slot lines (CopyGroups.h)
// =============================================================================
// Split out of CopyGroups.cpp for the 600-line limit.

#include "CopyGroups.h"
#include "CopyGroupsState.h"

#include "SlotAssign.h"

#include <format>
#include <map>

namespace BlackFlame::CopyGroups
{
    namespace
    {
        using namespace Detail;
        namespace CC = CopyContract;

        [[nodiscard]] const char* GroupStateText(GroupState a_state) noexcept
        {
            switch (a_state) {
            case GroupState::kWaiting: return "waiting (its objects still building or coming into Data)";
            case GroupState::kVerifying: return "verifying";
            case GroupState::kOpen: return "open";
            case GroupState::kUpgrading: return "open (newer objects verified for the next start)";
            case GroupState::kFailed: return "failed";
            case GroupState::kGone: return "left";
            }
            return "unknown";
        }
    }

    SlotState SlotStateOf(std::uint16_t a_slot)
    {
        SlotState out;
        for (const auto& group : TheState().groups) {
            for (const auto& member : group.members) {
                if (member.id != a_slot) {
                    continue;
                }
                out.inGroup = true;
                out.groupOpen = group.state == GroupState::kOpen || group.state == GroupState::kUpgrading;
                // (publication fix, 2026-10-19) a member its group opened at W1 without (fresh) is left for this session
                out.left = member.state == Member::kLeft || (out.groupOpen && LeftForNow(group, a_slot));
                out.state = out.left && group.state != GroupState::kFailed ? "left" : GroupStateText(group.state);
                out.why = !member.model.why.empty() ? member.model.why : member.muzzle.why;
                return out;
            }
        }
        const auto& slot = SlotAssign::At(a_slot);
        out.state = slot.usable ? "in no group" : "not usable";
        out.why = slot.usable ? std::string() : slot.why;
        return out;
    }

    bool ClosedUnexplained(std::uint16_t a_slot)
    {
        const auto state = SlotStateOf(a_slot);
        return !SlotAssign::GateOpen(a_slot) && state.groupOpen && !state.left && state.why.empty();
    }

    std::string SlotNote(std::uint16_t a_slot)
    {
        const auto state = SlotStateOf(a_slot);
        return state.why.empty() ? state.state : state.state + ": " + state.why;
    }

    std::string SlotSummary()
    {
        std::size_t open = 0;
        std::size_t assigned = 0;
        std::map<std::string, std::size_t> closed;
        for (const auto id : SlotAssign::Assigned()) {
            const auto& slot = SlotAssign::At(id);
            if (!slot.active || slot.type == CC::SlotType::kADDN) {
                continue;  // tombstones and other sessions' bindings; ADDN slots open through their MPS (counted apart)
            }
            ++assigned;
            if (SlotAssign::GateOpen(id)) {
                ++open;
                continue;
            }
            ++closed[ClosedUnexplained(id) ? std::string("CLOSED IN AN OPEN GROUP WITH NO REASON") : SlotStateOf(id).state];
        }
        std::string text;
        for (const auto& [state, count] : closed) {
            text += std::format("{}{} {}", text.empty() ? "" : ", ", state, count);
        }
        const auto progress = GetProgress();
        return std::format("{} of {} assigned slots open (ADDN MPS open {}); closed: {}", open, assigned, progress.addnOpen,
            text.empty() ? "none" : text);
    }
}
