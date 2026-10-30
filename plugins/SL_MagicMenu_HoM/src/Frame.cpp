// =============================================================================
// FRAME - the Heart of Magic block's per-frame work and its keys (HoM add-on plan
// Task 6), ported from the core's Widget.cpp at f83dc6f3: WatchTab's school and
// centre part (W:7645-7686), WatchSelection's HoM part (W:7747-7788), DoNext
// (W:7818-7842), DoSetTarget (W:7844-7857), LogAvailability, WatchSnapshotCover,
// RequestState (W:8118-8122) and the frame handler's U / G / polling lines
// (W:8182-8199). The tree block's frame drives it (it shows on every tab the
// three blocks show on and is never hidden by 'SLMV'), keys come through the tree
// block's key callback (keys go to every v3 block that has one: one is enough).
// What the host's frame handler did with g_menu.dirty is a 'SLMD' per block here:
// the cards are drawn again on the next UI frame.
// =============================================================================

#include "Common.h"

#include "AddonState.h"
#include "Cards.h"
#include "GameThread.h"
#include "Hom.h"
#include "HomSettings.h"
#include "Selection.h"
#include "SpellSchool.h"

#include <algorithm>
#include <format>

namespace HomAddon
{
    namespace
    {
        using State = Spells::State;

        bool SameMetrics(const TextFit::Metrics& a_lhs, const TextFit::Metrics& a_rhs)
        {
            return a_lhs.rowSize == a_rhs.rowSize && a_lhs.lineFirst == a_rhs.lineFirst && a_lhs.linePitch == a_rhs.linePitch;
        }

        // The host's text metrics (Context; the fallback 1.25 for a line share it did not give); a block drawn with others is due
        // to be drawn again (the first draw of a menu comes before the first frame, with the fallback)
        void TakeMetrics(const BlockApi::Context& a_context)
        {
            auto& menu = Menu();
            menu.metrics = { a_context.rowSize > 0 ? a_context.rowSize : 0.0, a_context.lineFirst > 0 ? a_context.lineFirst : kLineFactor,
                a_context.linePitch > 0 ? a_context.linePitch : kLineFactor };
            for (const auto kind : { BlockKind::kProgress, BlockKind::kNodeCard }) {
                const auto& drawn = menu.drawnWith[Index(kind)];
                if (drawn && !SameMetrics(*drawn, menu.metrics)) menu.dirty = true;
            }
        }

        // A category tab change (design 3.7): on a school tab its school and default centre - unless a selection is kept across
        // the change (review v12 #S3: the school and the centre come from it; the host puts the list back on it once filled)
        void WatchTab(const BlockApi::Context& a_context, const Spells::Snapshot& a_snapshot)
        {
            auto& menu = Menu();
            const std::string school = a_context.tabSchool ? a_context.tabSchool : "";
            if (menu.tabBits && *menu.tabBits == a_context.tabBits && menu.tabSchool == school) return;
            const bool first = !menu.tabBits.has_value();
            menu.tabBits = a_context.tabBits;
            menu.tabSchool = school;
            const bool keep = !first && a_context.shown != 0;
            if (keep) {
                logger::info("HoM block: category change keeps the selection {:08X} (school {}, centre kept)", a_context.shown,
                    TheSession().school.empty() ? "-" : TheSession().school);
            } else if (!school.empty()) {
                SetSchool(school);
                if (CentreSchool() != school) SetDefaultCentre(school, a_snapshot);
            }
            menu.dirty = true;
        }

        // spec v12 G2: follows the host's committed selection (Context::selectionSeq), not the list's highlight; a change of it is
        // a new selection even for the same spell. Bug G1: it drops the key pick and a clicked node, unless the change was the
        // add-on's own served show request (Selection::DropPick)
        void WatchSelection(const BlockApi::Context& a_context, const Spells::Snapshot& a_snapshot)
        {
            auto& menu = Menu();
            auto& session = TheSession();
            if (menu.seqSeen && *menu.seqSeen == a_context.selectionSeq) return;
            const auto seen = menu.seqSeen;
            menu.seqSeen = a_context.selectionSeq;
            auto* form = a_context.listForm ? RE::TESForm::LookupByID(a_context.listForm) : nullptr;
            auto* spell = form ? form->As<RE::SpellItem>() : nullptr;
            const RE::FormID id = spell ? spell->GetFormID() : 0;
            const bool ours = Selection::OwnRequest(a_context.source, a_context.servedRequest, menu.lastRequest);
            const int selectedBefore = menu.selected;
            const bool notInTreeBefore = menu.notInTree;
            // review v11 #2: a list choice that is not our click drops the clicked node, so the focus follows the list; the U key's
            // pick goes with it (G1)
            if (!seen || Selection::DropPick(*seen, a_context.selectionSeq, a_context.source, a_context.servedRequest, menu.lastRequest)) {
                menu.selected = -1;
                menu.selectedByKey = false;
            }
            // our own show request centres on the clicked node itself (a "_dup_" node's twin could push it out of the window);
            // any other tree spell on its non-dup node
            const int node = ours && menu.selected >= 0 && Spells::Get() && Data().gameIds[menu.selected] == id ? menu.selected : Spells::PrimaryNode(id);
            // the school: a school tab's own, else a tree spell's node's, else the spell's magic skill
            if (!menu.tabSchool.empty()) {
                SetSchool(menu.tabSchool);
            } else {
                SetSchool(node >= 0 ? Data().tree.nodes[node].school : SpellSchool::Of(spell));
            }
            menu.notInTree = id && node < 0;
            if (Centrable(node)) {
                SetCentre(node);
            } else if (session.centre < 0 || (!menu.tabSchool.empty() && !session.school.empty() && CentreSchool() != session.school)) {
                SetDefaultCentre(session.school, a_snapshot);
            }
            logger::info("HoM block: list selection {:08X} {} (school {}, tree node {}{}{}; seq {})", id, spell ? spell->GetName() : "(none)",
                session.school.empty() ? "-" : session.school, node, node >= 0 && !Centrable(node) ? " without x/y" : "",
                ours ? ", from our show request" : "", a_context.selectionSeq);
            // the host drew the cards for the new form already; again only when what they show from our state changed
            if (menu.selected != selectedBefore || menu.notInTree != notInTreeBefore) menu.dirty = true;
        }

        void DoNext(const Spells::Snapshot& a_snapshot)
        {
            auto& menu = Menu();
            const auto& session = TheSession();
            if (!Spells::Get() || session.school.empty()) {
                logger::info("HoM block: next learnable - no school yet");
                return;
            }
            // the same candidates the cards count (Q2): learnable, with a place in the tree (only those can be centred on)
            const auto& info = SchoolInfoOf(a_snapshot);
            const auto& nodes = info.candidates;
            if (nodes.empty()) {
                logger::info("HoM block: next learnable - none in {} (snapshot {}): {}", session.school, a_snapshot.epoch,
                    HomStrings::Key(NextStepLine(info, false).id));
                return;
            }
            auto next = nodes.front();
            const auto here = std::ranges::find(nodes, session.centre);
            if (here != nodes.end() && std::next(here) != nodes.end()) next = *std::next(here);
            SetCentre(next);
            // bug G1: the pick counts as chosen (Selection::Chosen) - the node card shows it and G acts on it; no 'SLMS' (the 3D
            // and the host's card stay on the list)
            menu.selected = next;
            menu.selectedByKey = true;
            menu.notInTree = false;  // the tree shows the next candidate: no longer dimmed (review v7 round 1)
            menu.dirty = true;
            logger::info("HoM block: next learnable in {} ({} of them): {} {:08X}", session.school, nodes.size(), Spells::NodeName(next),
                Data().gameIds[next]);
        }

        void DoSetTarget(const Spells::Snapshot& a_snapshot)
        {
            const auto& menu = Menu();
            if (!Spells::Get() || menu.selected < 0) {
                logger::info("HoM block: set target - no node selected");
                return;
            }
            const auto& node = Data().tree.nodes[menu.selected];
            const auto look = Spells::LookOf(a_snapshot, menu.selected);
            if (look.state != State::kAvailable) {
                logger::info("HoM block: set target - {} is {}, not learnable", Spells::NodeName(menu.selected), Spells::StateText(look.state));
                return;
            }
            Hom::RequestTarget(Data().gameIds[menu.selected], node.school, Spells::NodeName(menu.selected));
        }

        // review I8: nothing chosen - nothing on the node card to act on (G1: a key pick is chosen)
        void SetTargetIfChosen(const char* a_source)
        {
            const auto& menu = Menu();
            if (!Selection::SetTargetActs(menu.shown, KeyPick())) {
                logger::info("HoM block: set target ({}) - nothing chosen", a_source);
                return;
            }
            DoSetTarget(*Spells::Current());
        }

        // Once per menu open and school: how many nodes are available now (the tree-side availability rule)
        void LogAvailability(const Spells::Snapshot& a_snapshot)
        {
            auto& menu = Menu();
            for (const auto& [name, school] : a_snapshot.schools) {
                if (!menu.availabilityLogged.insert(name).second) continue;
                const auto* tree = Spells::SchoolNamed(name);
                logger::info("HoM block: {} - {} nodes, {} available to learn now, target {:08X} (snapshot {})", name, tree ? tree->nodes.size() : 0,
                    Spells::Learnable(a_snapshot, name).size(), school.target, a_snapshot.epoch);
            }
        }

        // Q2 (review round 1 #3): "loading" only while HoM's state for the school can still come. When no snapshot has had the
        // school for kNoDataFrames frames (a refresh is asked for every kPollFrames frames), the progress card says the data did
        // not arrive; logged once per school and menu.
        void WatchSnapshotCover(const Spells::Snapshot& a_snapshot)
        {
            auto& menu = Menu();
            const auto& school = TheSession().school;
            if (school.empty() || a_snapshot.Covers(school) || !Spells::SchoolNamed(school)) {
                menu.uncoveredSchool.clear();
                menu.noData = false;
                return;
            }
            if (menu.uncoveredSchool != school) {
                menu.uncoveredSchool = school;
                menu.uncoveredFrom = menu.frame;
                menu.noData = false;
                return;
            }
            if (menu.noData || menu.frame - menu.uncoveredFrom < kNoDataFrames) return;
            menu.noData = true;
            menu.dirty = true;
            logger::warn("HoM block: no HoM state for {} after {} frames (HoM API {}, a refresh asked every {} frames) - the progress card says the "
                         "data did not arrive",
                school, kNoDataFrames, Hom::Ready() ? "ready" : "NOT ready", kPollFrames);
        }

        void RequestState()
        {
            Spells::RequestRefresh({ TheSession().school, CentreSchool() });
            Menu().refresh = false;
        }

        // The progress card waits for a school (the core's Visibility at f83dc6f3, W:3952): hidden by 'SLMV', its place kept.
        // Every menu opens with it shown
        void WatchProgressShown()
        {
            auto& menu = Menu();
            const bool want = !TheSession().school.empty();
            if (menu.progressShown.value_or(true) != want) {
                RequestVisible(BlockKind::kProgress, want);
                logger::info("HoM block: the progress card {} ({})", want ? "shown again" : "hidden", want ? "a school" : "no school yet");
            }
            menu.progressShown = want;
        }

        void SendRedraws()
        {
            auto& menu = Menu();
            if (!menu.dirty) return;
            menu.dirty = false;
            for (const auto kind : { BlockKind::kTree, BlockKind::kProgress, BlockKind::kNodeCard }) RequestRedraw(kind);
        }
    }

    // The tree block's frame: the per-frame work of all three blocks (once per UI frame - only this block has a frame that does it)
    void TreeFrame(RE::GFxMovieView*, RE::GFxValue&, RE::GFxValue*, const BlockApi::Context& a_context, void*)
    {
        EnsureMenu();
        auto& menu = Menu();
        if (menu.firstFrame) {
            logger::info("HoM block: first frame on thread {} (main thread {}); HoM is read by main-thread tasks", REX::W32::GetCurrentThreadId(),
                GameThread::Id());
        }
        menu.layoutMode = a_context.layoutMode;
        menu.shown = a_context.shown;
        menu.selectionSeq = a_context.selectionSeq;
        TakeMetrics(a_context);
        // our own SetLearningTarget ran: ask for HoM's state now, not at the next poll
        if (Hom::TakeStateChanged()) menu.refresh = true;
        const auto snapshot = Spells::Current();
        WatchTab(a_context, *snapshot);
        WatchSelection(a_context, *snapshot);
        menu.firstFrame = false;
        if (TakeButton()) SetTargetIfChosen("the node card's button");
        if (++menu.frame % kPollFrames == 0) menu.refresh = true;
        if (menu.refresh) RequestState();
        if (snapshot->epoch != menu.epoch) {
            menu.epoch = snapshot->epoch;
            menu.dirty = true;
            SettleProvisionalCentre(*snapshot);
            LogAvailability(*snapshot);
        }
        WatchSnapshotCover(*snapshot);
        WatchProgressShown();
        SendRedraws();
    }

    // U and G (the core's Input.cpp at f83dc6f3 queued them for the frame handler; the host hands us the key on the frame, before
    // the frame calls, never in the layout mode or while a text field takes keys)
    bool TreeKey(std::uint32_t a_scanCode, void*)
    {
        EnsureMenu();
        const auto& settings = HomSettings::Get();
        if (a_scanCode == settings.keyNext) {
            DoNext(*Spells::Current());
            SendRedraws();
            return true;
        }
        if (a_scanCode == settings.keySetTarget) {
            SetTargetIfChosen("the key");
            return true;
        }
        return false;
    }

    // Task 7 draws the tree window here; until then the block keeps its place and its title strip
    void DrawTreeBlock(RE::GFxMovieView*, RE::GFxValue&, float, float, std::uint32_t, void*)
    {
        EnsureMenu();
    }
}
