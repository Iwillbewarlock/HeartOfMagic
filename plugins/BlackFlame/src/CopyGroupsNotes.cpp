// =============================================================================
// CopyGroupsNotes - a slot's group state in words, for the self-test's slot lines (CopyGroups.h)
// =============================================================================
// Split out of CopyGroups.cpp for the 600-line limit.

#include "CopyGroups.h"
#include "CopyGroupsState.h"

#include "SehGuard.h"
#include "SlotAssign.h"

#include <algorithm>
#include <format>
#include <map>
#include <string>

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
            case GroupState::kUpgrading: return "open (newer objects being verified: shown at the main menu, else at the next start)";
            case GroupState::kFailed: return "failed";
            case GroupState::kGone: return "left";
            }
            return "unknown";
        }

        // The enum names, for the W1 trace lines
        [[nodiscard]] const char* GroupStateName(GroupState a_state) noexcept
        {
            switch (a_state) {
            case GroupState::kWaiting: return "kWaiting";
            case GroupState::kVerifying: return "kVerifying";
            case GroupState::kOpen: return "kOpen";
            case GroupState::kUpgrading: return "kUpgrading";
            case GroupState::kFailed: return "kFailed";
            case GroupState::kGone: return "kGone";
            }
            return "unknown";
        }

        [[nodiscard]] const char* MemberName(Member a_member) noexcept
        {
            switch (a_member) {
            case Member::kReady: return "kReady";
            case Member::kCandidate: return "kCandidate";
            case Member::kPending: return "kPending";
            case Member::kLeft: return "kLeft";
            }
            return "unknown";
        }

        [[nodiscard]] const char* AtW1Name(Decide::AtW1 a_atW1) noexcept
        {
            switch (a_atW1) {
            case Decide::AtW1::kShown: return "kShown";
            case Decide::AtW1::kFresh: return "kFresh";
            case Decide::AtW1::kSource: return "kSource";
            case Decide::AtW1::kLeft: return "kLeft";
            }
            return "unknown";
        }

        // A TXST slot's group and plan in one piece of the W1 trace line
        [[nodiscard]] std::string TxstPlanText(const State& a_state, std::uint16_t a_id)
        {
            for (std::size_t g = 0; g < a_state.groups.size(); ++g) {
                const auto& group = a_state.groups[g];
                if (std::find(group.ids.begin(), group.ids.end(), a_id) == group.ids.end()) {
                    continue;
                }
                const auto member = std::find_if(group.members.begin(), group.members.end(), [&](const Slot& a_m) { return a_m.id == a_id; });
                const auto where = std::format("group {} ({} member(s), first slot {:X}, {})", g, group.ids.size(), group.ids.front(),
                    GroupStateName(group.state));
                if (member == group.members.end()) {
                    return where + ", not planned";
                }
                return std::format("{}, plan {} (set {}{}{}{}), at W1 {}{}", where, MemberName(member->state), MemberName(member->model.state),
                    member->model.older ? ", older generation shown" : "", member->model.candidate ? ", candidate to verify" : "",
                    member->model.why.empty() ? "" : ": " + member->model.why, AtW1Name(member->atW1),
                    LeftForNow(group, a_id) ? ", left for this session" : "");
            }
            const auto& slot = SlotAssign::At(a_id);
            return !slot.usable ? "in no group (not usable: " + slot.why + ")" : std::string("in no group");
        }
    }

    namespace
    {
        void TraceTxstSlots(const State& a_state)
        {
            std::size_t count = 0;
            for (const auto id : SlotAssign::Assigned()) {
                const auto& slot = SlotAssign::At(id);
                if (slot.type != CC::SlotType::kTXST) {
                    continue;
                }
                ++count;
                logger::info("BlackFlame: slots: W1 TXST slot {:X} (form {:08X}, source {:08X} {}{}): {}; textures written {}, TX00 '{}'; gate {}",
                    id, slot.form ? slot.form->GetFormID() : 0, slot.source ? slot.source->GetFormID() : 0, slot.sourceText,
                    slot.active ? "" : ", not active", TxstPlanText(a_state, id),
                    slot.publishedTxst.empty() ? std::string("no (the stub's own strings)") : "yes (key " + slot.publishedTxst + ")",
                    slot.publishedTextures[0], SlotAssign::GateOpen(id) ? "open" : "closed");
            }
            logger::info("BlackFlame: slots: W1 TXST slots traced: {}", count);
        }
    }

    // A trace inside Start before the stepper is queued: a fault or a throw here skips the trace only, never publication
    void Detail::LogTxstSlotsAtW1(const State& a_state)
    {
        try {
            if (!GuardedKeep("slots W1 TXST trace", [&]() { TraceTxstSlots(a_state); })) {
                logger::warn("BlackFlame: slots: the W1 TXST trace faulted - skipped (publication goes on)");
            }
        } catch (const std::exception& e) {
            logger::warn("BlackFlame: slots: the W1 TXST trace threw ({}) - skipped (publication goes on)", e.what());
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
