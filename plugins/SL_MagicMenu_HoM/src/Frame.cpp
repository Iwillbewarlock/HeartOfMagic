// =============================================================================
// FRAME - the Heart of Magic block's per-frame work and its keys (HoM add-on plan
// Task 6), ported from the core's Widget.cpp at f83dc6f3: WatchTab's school and
// centre part (W:7645-7686), WatchSelection's HoM part (W:7747-7788), DoNext
// (W:7818-7842), DoSetTarget (W:7844-7857), LogAvailability, WatchSnapshotCover,
// RequestState (W:8118-8122) and the frame handler's U / G / polling lines
// (W:8182-8199). The tree block's frame drives it (it shows on every tab the
// three blocks show on and is never hidden by 'SLMV'), keys come through the tree
// block's key callback (keys go to every v3 block that has one: one is enough),
// which only records them: U and G are acted on in the frame after the list
// change, as at f83dc6f3 (Task 7, Task 6 review 1-2). The tree window's own work
// (its node click, hover and tooltip) is TreeInput.cpp / Tooltip.cpp.
// What the host's frame handler did with g_menu.dirty is a 'SLMD' per block here:
// the blocks are drawn again on the next UI frame.
// =============================================================================

#include "Common.h"

#include "AddonState.h"
#include "Cards.h"
#include "GameThread.h"
#include "Hom.h"
#include "HomSettings.h"
#include "Selection.h"
#include "TreeDraw.h"

#include <algorithm>
#include <format>
#include <utility>

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
            for (const auto kind : { BlockKind::kTree, BlockKind::kProgress, BlockKind::kNodeCard }) {
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
        // add-on's own served show request (Selection::DropPick). Final review I1: the rest is SettleShown, which the first draw for
        // the new form already ran (the host draws before this frame) - run again here it changes nothing, so no second redraw. What
        // only this frame can settle: a clicked node dropped by a player's list change (the draw cannot tell it from our own
        // request) - that one change still redraws the blocks once more
        void WatchSelection(const BlockApi::Context& a_context, const Spells::Snapshot& a_snapshot)
        {
            auto& menu = Menu();
            const auto& session = TheSession();
            if (menu.seqSeen && *menu.seqSeen == a_context.selectionSeq) return;
            const auto seen = menu.seqSeen;
            menu.seqSeen = a_context.selectionSeq;
            const bool ours = Selection::OwnRequest(a_context.source, a_context.servedRequest, menu.lastRequest);
            const bool drawSettled = !Selection::SettleDue(menu.settledShown, a_context.shown);
            const int selectedBefore = menu.selected;
            const bool notInTreeBefore = menu.notInTree;
            // review v11 #2: a list choice that is not our click drops the clicked node, so the focus follows the list; the U key's
            // pick goes with it (G1)
            const bool drop =
                !seen || Selection::DropPick(*seen, a_context.selectionSeq, a_context.source, a_context.servedRequest, menu.lastRequest);
            const auto pick = Selection::PickAfterFrame({ menu.selected, menu.selectedByKey }, drop, -1);
            menu.selected = pick.node;
            menu.selectedByKey = pick.byKey;
            const int node = SettleShown(a_context.shown, a_snapshot);
            auto* form = a_context.listForm ? RE::TESForm::LookupByID(a_context.listForm) : nullptr;
            auto* spell = form ? form->As<RE::SpellItem>() : nullptr;
            const RE::FormID id = spell ? spell->GetFormID() : 0;
            logger::info("HoM block: list selection {:08X} {} (school {}, tree node {}{}{}{}{}; seq {})", id, spell ? spell->GetName() : "(none)",
                session.school.empty() ? "-" : session.school, node, node >= 0 && !Centrable(node) ? " without x/y" : "",
                ours ? ", from our show request" : "", drawSettled ? ", settled in the draw" : "",
                a_context.listForm != a_context.shown ? std::format(", the card shows {:08X}", a_context.shown) : std::string{},
                a_context.selectionSeq);
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
            // and the host's card stay on the list). Made after this frame's list change (WatchSelection), under its form
            const auto pick = Selection::PickAfterFrame({ menu.selected, menu.selectedByKey }, false, next);
            menu.selected = pick.node;
            menu.selectedByKey = pick.byKey;
            menu.pickShown = menu.shown;
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

        // review I8: nothing chosen - nothing on the node card to act on (G1: a key pick is chosen). Task 6 review 1: the G key and
        // the node card's button in one frame are one request (f83dc6f3 W:8186-8189)
        void SetTargetIfChosen(bool a_key, bool a_button, const Spells::Snapshot& a_snapshot)
        {
            if (!a_key && !a_button) return;
            const auto& menu = Menu();
            const char* source = a_key && a_button ? "the key and the node card's button" : a_key ? "the key" : "the node card's button";
            if (Selection::SetTargetRequests(a_key, a_button, Selection::SetTargetActs(menu.shown, KeyPick())) == 0) {
                logger::info("HoM block: set target ({}) - nothing chosen", source);
                return;
            }
            DoSetTarget(a_snapshot);
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

        void SendRedraws()
        {
            auto& menu = Menu();
            if (!menu.dirty) return;
            menu.dirty = false;
            for (const auto kind : { BlockKind::kTree, BlockKind::kProgress, BlockKind::kNodeCard }) RequestRedraw(kind);
        }
    }

    // The tree block's frame: the per-frame work of all three blocks (once per UI frame - only this block has a frame that does it).
    // The order: the tab, the list selection, the tree click, U, G and the button (one request), the polling, the hover and the
    // tooltip. f83dc6f3 took the click before the list selection, so its own list change was seen in the same frame; that change
    // now comes a frame later through the host's 'SLMS' (OwnRequest keeps the pick), and a click after a player's list change of
    // the same frame is the newer act (Task 7)
    void TreeFrame(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, RE::GFxValue* a_overlay, const BlockApi::Context& a_context, void*)
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
        // fix round 1 M3: a key pick made under another form goes now (the draws already treat it as dropped: G and the card agree;
        // since I1 the first draw for the form has dropped it, so this is left for a form change no draw saw)
        const Selection::Pick held{ menu.selected, menu.selectedByKey };
        if (const auto kept = Selection::PickForShown(held, menu.pickShown, a_context.shown); kept != held) {
            menu.selected = kept.node;
            menu.selectedByKey = kept.byKey;
            menu.dirty = true;
        }
        menu.firstFrame = false;
        TakeNodeClick(a_movie, a_content, a_context, *snapshot);
        // the keys the host handed over right before this call (only while the tree shows): acted on in this frame
        const auto keys = Selection::TakeKeys(menu.keys);
        if (keys.next) DoNext(*snapshot);
        const bool key = keys.setTarget;
        const bool button = TakeButton();
        SetTargetIfChosen(key, button, *snapshot);
        if (++menu.frame % kPollFrames == 0) menu.refresh = true;
        if (menu.refresh) RequestState();
        if (snapshot->epoch != menu.epoch) {
            menu.epoch = snapshot->epoch;
            menu.dirty = true;
            SettleProvisionalCentre(*snapshot);
            LogAvailability(*snapshot);
        }
        WatchSnapshotCover(*snapshot);
        SyncProgressShown();
        DropStaleHover(a_movie, a_content);  // review v11 #10
        RedrawTooltip(a_movie, a_overlay, a_context, *snapshot);  // the hover changes no tree: outside the dirty gate (spec v10 T6)
        SendRedraws();
    }

    // U and G (the core's Input.cpp at f83dc6f3 queued them for the frame handler; the host hands us the key on the frame, before
    // the tree's frame call in the same frame and only while the tree shows, never in the layout mode or while a text field takes
    // keys): recorded only, the frame acts on them after the list change (Task 6 review 2)
    bool TreeKey(std::uint32_t a_scanCode, void*)
    {
        EnsureMenu();
        const auto& settings = HomSettings::Get();
        if (a_scanCode == settings.keyNext) {
            Menu().keys.next = true;
            return true;
        }
        if (a_scanCode == settings.keySetTarget) {
            Menu().keys.setTarget = true;
            return true;
        }
        return false;
    }
}
