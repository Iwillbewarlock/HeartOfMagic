// =============================================================================
// TREE DRAW - the tree window (HoM add-on plan Task 7): the core's Widget.cpp at
// f83dc6f3, W:6428-6597 (MarkScale, DrawChain, DrawEdges, NodeBox), W:6690-6731
// (DrawNode), W:6785-6851 (DrawBadges, CheckSpacing) and RedrawTree W:6888-7117,
// unchanged but for: the panel clip in the host's content clip (TreeDraw.h), the
// add-on's settings, the node handlers bound by TreeInput.cpp, the picked node for
// the form drawn (SelectedFor), one look for the path and the legend (B1), and
// the legend drawn at every tree draw (the content clip is new each time).
// =============================================================================

#include "Common.h"

#include "TreeDraw.h"

#include "Cards.h"
#include "Draw.h"
#include "TreeRules.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace HomAddon
{
    namespace
    {
        using HomStrings::Fill;
        using HomStrings::Str;
        using HomStrings::Text;
        using State = Spells::State;

        // The node states that are clickable on their own (learned or learnable; W:1061)
        bool Shown(State a_state)
        {
            return a_state == State::kMastered || a_state == State::kReady || a_state == State::kTarget || a_state == State::kLearning ||
                   a_state == State::kAvailable;
        }

        // A link as drawn, for the labels to keep off it (LayoutMath::LinkBoxes)
        struct DrawnLine
        {
            double ax{}, ay{}, bx{}, by{}, width{};
        };

        struct EdgeTally
        {
            int dim{}, locked{}, frontier{}, unlocked{}, selected{}, learning{}, chains{}, hidden{};
            std::vector<DrawnLine> lines;
        };

        // A chain from a hard prerequisite to the centre (canvasRendererEdges.js:354-434): links LINK_W x LINK_H every 1.15 x
        // LINK_W, every second one turned a quarter, at least 3. HoM draws a cached sprite in world units (a rounded link with a
        // dark hole); here a plain filled rectangle in its fill and outline, at the mark scale.
        void DrawChain(RE::GFxValue& a_content, double a_ax, double a_ay, double a_bx, double a_by, double a_scale, double a_alpha, EdgeTally& a_tally)
        {
            constexpr int kMinLinks = 3;
            const double dx = a_bx - a_ax, dy = a_by - a_ay, dist = std::hypot(dx, dy);
            if (dist < a_scale) return;  // HoM: dist < 1 world unit
            const double linkW = HomStyle::kChainLinkW * a_scale, linkH = HomStyle::kChainLinkH * a_scale;
            const int links = std::max(kMinLinks, static_cast<int>(std::lround(dist / (linkW * HomStyle::kChainSpacing))));
            const double angle = std::atan2(dy, dx);
            Draw::LineStyle(a_content, HomStyle::kChainStroke * a_scale, HomStyle::kChainEdge.rgb, Percent(HomStyle::kChainEdge, a_alpha));
            for (int i = 0; i < links; ++i) {
                const double t = (i + 0.5) / links, cx = a_ax + dx * t, cy = a_ay + dy * t;
                const double turn = angle + (i % 2 != 0 ? std::numbers::pi / 2 : 0.0);
                const double c = std::cos(turn), s = std::sin(turn), hw = linkW / 2, hh = linkH / 2;
                const std::array<HomStyle::Point, 4> corners{ { { cx - hw * c + hh * s, cy - hw * s - hh * c }, { cx + hw * c + hh * s, cy + hw * s - hh * c },
                    { cx + hw * c - hh * s, cy + hw * s + hh * c }, { cx - hw * c - hh * s, cy - hw * s + hh * c } } };
                Draw::BeginFill(a_content, HomStyle::kChainFill.rgb, Percent(HomStyle::kChainFill, a_alpha));
                Draw::Polygon(a_content, corners);
                Draw::EndFill(a_content);
            }
            ++a_tally.chains;
            a_tally.lines.push_back({ a_ax, a_ay, a_bx, a_by, linkW });
        }

        // The links between drawn nodes (spec v11 T14; canvasRendererEdges.js:157-434), bottom to top as HoM strokes them: its
        // dim batch, locked, frontier (learned -> learnable), learned; the centre's links to its direct neighbours
        // (showSelectionPath: HoM's selected path, here the centre's own links, user 2026-10-26); the learning target's path
        // (spec v10 T8; 3 wide into the target, R5); the chains of the centre's hard prerequisites (HoM's are the selected
        // spell's). With showSelectionPath every other link is HoM's dim batch (its own dimming: no x 0.6 on top); otherwise a
        // link with an end outside the full set gets the dim factor in its alpha (T11). showBaseConnections off: only learned
        // and frontier links and the paths (:207-208). Widths are HoM's world units x fTreeScale (review v11 #5).
        EdgeTally DrawEdges(RE::GFxValue& a_content, const TreeData::Window& a_window, const std::unordered_map<int, const TreeNode*>& a_nodes,
            const std::set<std::pair<int, int>>& a_path, int a_target, const HomStyle::Style& a_style)
        {
            using HomStyle::Display;
            const auto& tree = Data().tree;
            const auto& t = a_style.tree;
            const int centre = TheSession().centre;
            EdgeTally tally;
            const double lineScale = HomSettings::Get().treeScale;  // review v11 #5: HoM's widths are world units
            struct Line
            {
                const TreeData::PlacedEdge* placed;
                double width;
                HomStyle::Color colour;
                double alpha;
            };
            enum Layer : std::size_t
            {
                kDim,
                kLocked,
                kFrontier,
                kUnlocked,
                kSelected,
                kLearning,
                kLayers
            };
            std::array<std::vector<Line>, kLayers> layers;
            const auto node = [&](int a_index) {
                const auto it = a_nodes.find(a_index);
                return it == a_nodes.end() ? nullptr : it->second;
            };
            // HoM dims only while the selected spell has a path to show (hasSelectedPath, :157)
            const bool dimAll = a_style.showSelectionPath && std::ranges::any_of(tree.adjacent[centre], [&](int a_edge) {
                return tree.edges[a_edge].kind == TreeData::EdgeKind::kStructural;
            });
            for (const auto& placed : a_window.edges) {
                const auto& edge = tree.edges[placed.edge];
                const auto* from = node(edge.from);
                const auto* to = node(edge.to);
                if (edge.kind != TreeData::EdgeKind::kStructural || !from || !to) continue;  // a lock link: a chain below or nothing
                const double factor = from->full && to->full ? 1.0 : a_style.DimFactor();
                if (a_path.contains(std::minmax(edge.from, edge.to))) {
                    // review v11 #6: 3 wide only when its end toward the target is the target in the learning look
                    // (canvasRendererEdges.js:343-345 toNode.state === 'learning'); the path ends at the target
                    const auto* targetNode = node(a_target);
                    const bool intoTarget =
                        (edge.from == a_target || edge.to == a_target) && targetNode && targetNode->display == Display::kLearning;
                    layers[kLearning].push_back({ &placed, intoTarget ? HomStyle::kLearningEdgeIntoTarget : HomStyle::kLearningEdgeWidth, a_style.learning,
                        HomStyle::kLearningEdgeAlpha * factor });
                    continue;
                }
                if (a_style.showSelectionPath && (edge.from == centre || edge.to == centre)) {
                    layers[kSelected].push_back({ &placed, t.selectedPathWidth, t.selectedPathColor, t.selectedPathAlpha });
                    continue;
                }
                const bool both = from->display == Display::kUnlocked && to->display == Display::kUnlocked;
                const bool frontier = t.frontierEdgeAlpha > 0 && !both && from->display == Display::kUnlocked &&
                                      (to->display == Display::kAvailable || to->display == Display::kLearning);
                if (!both && !frontier && !a_style.showBaseConnections) {
                    ++tally.hidden;
                    continue;
                }
                const auto school = a_style.SchoolColor(tree.nodes[edge.from].school);
                if (dimAll) {
                    layers[kDim].push_back({ &placed, HomStyle::kDimEdgeWidth, t.dimEdgeColor, HomStyle::kDimEdgeAlpha });
                } else if (both) {
                    layers[kUnlocked].push_back({ &placed, t.unlockedEdgeWidth, t.unlockedEdgeColor.value_or(school), t.unlockedEdgeAlpha * factor });
                } else if (frontier) {
                    layers[kFrontier].push_back({ &placed, HomStyle::kFrontierEdgeWidth, school, t.frontierEdgeAlpha * factor });
                } else {
                    layers[kLocked].push_back({ &placed, HomStyle::kLockedEdgeWidth, t.lockedEdgeColor, t.lockedEdgeAlpha * factor });
                }
            }
            const std::array<int*, kLayers> counts{ &tally.dim, &tally.locked, &tally.frontier, &tally.unlocked, &tally.selected, &tally.learning };
            for (std::size_t layer = 0; layer < kLayers; ++layer) {
                for (const auto& line : layers[layer]) {
                    // one alpha per drawing call: the line's own, with the dim factor in it (R10)
                    const double width = line.width * lineScale;
                    Draw::LineStyle(a_content, width, line.colour.rgb, Percent(line.colour, line.alpha));
                    Draw::MoveTo(a_content, line.placed->ax, line.placed->ay);
                    Draw::LineTo(a_content, line.placed->bx, line.placed->by);
                    ++*counts[layer];
                    tally.lines.push_back({ line.placed->ax, line.placed->ay, line.placed->bx, line.placed->by, width });
                }
            }
            // the chains: from each drawn hard prerequisite of the centre to the centre (HoM, :356-366)
            const auto* hub = node(centre);
            for (const auto prerequisite : tree.nodes[centre].hard) {
                for (const auto& [index, drawn] : a_nodes) {
                    if (index == centre || !hub || tree.nodes[index].formId != prerequisite) continue;
                    const double factor = drawn->full ? 1.0 : a_style.DimFactor();
                    DrawChain(a_content, drawn->placed.sx, drawn->placed.sy, hub->placed.sx, hub->placed.sy, MarkScale(), HomStyle::kChainAlpha * factor, tally);
                }
            }
            return tally;
        }

        LabelBox NodeBox(const TreeNode& a_node)
        {
            const double r = a_node.reach;
            return { a_node.placed.sx - r, a_node.placed.sy - r, a_node.placed.sx + r, a_node.placed.sy + r };
        }

        // A drawn node's clip at its place, in a_layer (spec v10 T4, T6; v11 T12, T13, R6). HoM's mark of its state (the mystery
        // "?" in a text field of its own), a hidden focus outline child (shown on roll-over, so a hover needs no redraw), and a
        // hit area on the mark alone (a child clip set as hitArea, so the label later drawn in the same clip takes no click or
        // hover). A clickable node gets onRelease; every node the roll-over / roll-out handlers (the tooltip).
        RE::GFxValue DrawNode(RE::GFxMovieView* a_movie, RE::GFxValue& a_layer, const TreeNode& a_node, const HomStyle::Style& a_style)
        {
            const auto name = std::format("n{}", a_node.placed.index);
            auto clip = Draw::Clip(a_layer, name.c_str());
            if (!clip.IsDisplayObject()) return clip;
            clip.SetMember("_x", a_node.placed.sx);
            clip.SetMember("_y", a_node.placed.sy);
            clip.SetMember(kIndexMember, static_cast<double>(a_node.placed.index));
            const auto& data = Data().tree.nodes[a_node.placed.index];
            const double s = MarkScale();
            const double lineScale = HomSettings::Get().treeScale;  // review v11 #5
            // review v11 #2: dimmed by `full` alone - the focus keeps its outline and size, not a full alpha
            const auto drawn = DrawHomMark(clip, a_style,
                { data.school, data.themeColor, data.skillLevel, data.tier, data.x, data.y, 0.0, 0.0, s, lineScale },
                { a_node.display, a_style.Mystery(), a_node.focus, a_node.onPath, a_node.full ? 1.0 : a_style.DimFactor() });
            DrawQuestion(a_movie, clip, 0.0, 0.0, s, drawn, Metrics());
            if (auto focus = Draw::Clip(clip, kFocusChild); focus.IsDisplayObject()) {
                const auto& stroke = a_style.tree.focusStroke;
                Draw::LineStyle(focus, HomStyle::kFocusStroke * lineScale, stroke.rgb, Percent(stroke, 1.0));
                Draw::Polygon(focus, drawn.body);
                focus.SetMember("_visible", a_node.placed.index == Menu().hovered);
            }
            if (auto hit = Draw::Clip(clip, "hit"); hit.IsDisplayObject()) {
                Draw::NoLine(hit);
                Draw::BeginFill(hit, kBackdrop, 0.0);  // alpha 0 still takes the mouse
                Draw::Polygon(hit, HomStyle::ShapePoints({}, 0, 0, 0, 0, a_node.reach));
                Draw::EndFill(hit);
                clip.SetMember("hitArea", hit);
            }
            BindNodeHandlers(a_movie, clip, a_node.clickable);
            return clip;
        }

        // An arrow at the window edge in each sector's direction (from the window centre) and its "N개 더" text just inside it
        // (spec v10 T7; the arrow is drawn, the font's arrow characters are not relied on). False, drawing nothing, when the
        // window has no room for an arrow and its text (review round 1 #2: a 32-wide tree rect passes the settings check and
        // turned the text's clamp range around)
        bool DrawBadges(RE::GFxMovieView* a_movie, RE::GFxValue& a_content, const LayoutMath::Exits& a_exits, const HomSettings::Area& a_frame,
            std::vector<LabelBox>& a_boxes)
        {
            constexpr double kTiny = 1e-9, kFar = 1e9;
            const double size = Size(kNoteSize), h = FieldH(size), textW = kExitTextW;
            const double hw = a_frame.halfW - kExitInset, hh = a_frame.halfH - kExitInset;
            if (hw < textW / 2 + kExitArrow || hh < kExitArrow || a_frame.halfH + kPad < h / 2 + kExitArrow) return false;
            for (std::size_t i = 0; i < a_exits.sectors.size(); ++i) {
                if (a_exits.sectors[i] == 0) continue;
                const double angle = static_cast<double>(i) * 2 * std::numbers::pi / static_cast<double>(a_exits.sectors.size());
                const double ux = std::cos(angle), uy = std::sin(angle);
                const double t = std::min(std::abs(ux) > kTiny ? hw / std::abs(ux) : kFar, std::abs(uy) > kTiny ? hh / std::abs(uy) : kFar);
                const double px = ux * t, py = uy * t, a = kExitArrow;
                Draw::NoLine(a_content);
                Draw::BeginFill(a_content, kLabelColor, Draw::kAlphaFull);
                Draw::MoveTo(a_content, px + ux * a, py + uy * a);
                Draw::LineTo(a_content, px - ux * a - uy * a, py - uy * a + ux * a);
                Draw::LineTo(a_content, px - ux * a + uy * a, py - uy * a - ux * a);
                Draw::LineTo(a_content, px + ux * a, py + uy * a);
                Draw::EndFill(a_content);
                const double cx = std::clamp(px - ux * (a + textW / 2), -a_frame.halfW + textW / 2, a_frame.halfW - textW / 2);
                const double cy = std::clamp(py - uy * (a + h / 2), -a_frame.halfH - kPad + h / 2, a_frame.halfH + kPad - h / 2);
                const auto name = std::format("exit{}", i);
                FitLine(a_movie, a_content, name.c_str(), cx - textW / 2, cy - h / 2, textW, size,
                    Fill(Str::kTreeExit, { { "{n}", std::to_string(a_exits.sectors[i]) } }), kLabelColor, "center");
                // what the labels keep off: the arrow and its text
                a_boxes.push_back({ px - 2 * a, py - 2 * a, px + 2 * a, py + 2 * a });
                a_boxes.push_back({ cx - textW / 2, cy - h / 2, cx + textW / 2, cy + h / 2 });
            }
            return true;
        }

        // In-game spacing check (spec v11 R5): the largest reach of the drawn marks against half the distance of the closest two
        // drawn marks (a mark reaching past it could touch its neighbour's). PASS once per menu, every FAIL. O(n^2) for n drawn
        // nodes (iMaxNodes at most 200).
        void CheckSpacing(const std::vector<TreeNode>& a_nodes)
        {
            double closest = std::numeric_limits<double>::infinity(), reach = 0.0;
            int a = -1, b = -1, widest = -1;
            for (std::size_t i = 0; i < a_nodes.size(); ++i) {
                if (a_nodes[i].reach > reach) {
                    reach = a_nodes[i].reach;
                    widest = a_nodes[i].placed.index;
                }
                for (std::size_t j = i + 1; j < a_nodes.size(); ++j) {
                    const auto& p = a_nodes[i].placed;
                    const auto& q = a_nodes[j].placed;
                    const double d = std::hypot(p.sx - q.sx, p.sy - q.sy);
                    if (d < closest) {
                        closest = d;
                        a = p.index;
                        b = q.index;
                    }
                }
            }
            if (a < 0) return;
            const bool pass = reach <= closest / 2;
            if (pass && std::exchange(Menu().spacingLogged, true)) return;
            SelfTest(std::format("T2 spacing {} - closest drawn marks {:.1f} apart ({} / {}), half {:.2f}; the largest drawn reach {:.2f} ({}) "
                                 "(HoM sizes x fTreeScale {} x fTreeMarkScale {})",
                pass ? "PASS" : "FAIL", closest, Spells::NodeName(a), Spells::NodeName(b), closest / 2, reach, Spells::NodeName(widest),
                HomSettings::Get().treeScale, HomSettings::Get().treeMarkScale));
        }
        // The tree window (W:6888-7117), drawn into a fresh content clip a_width x a_height (the host's, below the title strip and
        // inside the padding) for the card's form a_shown. Task 7: the panel clip keeps the core's places; one look decides the
        // path and the legend (TreeRules::LookOf / LegendShown, B1)
        void RedrawTree(RE::GFxMovieView* a_movie, RE::GFxValue& a_lent, double a_width, double a_height, RE::FormID a_shown,
            const Spells::Snapshot& a_snapshot)
        {
            auto& menu = Menu();
            const auto& session = TheSession();
            const auto started = std::chrono::steady_clock::now();  // the redraw's time goes in its log line (GPT design review #7)
            std::optional<std::pair<double, double>> hoveredAt;  // the hovered node's place before this redraw
            for (const auto& placed : menu.drawn) {
                if (placed.index == menu.hovered) hoveredAt = { placed.sx, placed.sy };
            }
            menu.clickable.clear();
            menu.drawn.clear();
            auto treePanel = PanelClip(a_lent);
            auto content = treePanel.IsDisplayObject() ? Draw::Clip(treePanel, kTreeContent) : RE::GFxValue{};
            if (!content.IsDisplayObject()) {
                RecheckHover(hoveredAt);
                return;
            }
            const auto& settings = HomSettings::Get();
            // the tree's box: the content clip and the host's padding and title strip (the column's width is the player's). The tree
            // window keeps its registered height in every state (user 2026-10-30: "트리창도 자동세로 줄이기 기능의 대상에서 빼자"):
            // its drawing centred in its body, as before the blocks fitted their content
            const double boxW = a_width + 2 * kPad, boxH = a_height + kTitleH + 2 * kPad;
            const auto frame = HomSettings::TreeContent({ 0.0, 0.0, boxW, boxH });
            menu.treeOrigin = { frame.cx, frame.cy };
            menu.treeBox = { boxW, boxH };
            content.SetMember("_x", frame.cx);
            content.SetMember("_y", frame.cy);
            // T7.1: a list selection without a tree node keeps the previous centre (design), drawn dimmed so it is not taken for
            // the selection's tree; the note goes in a clip of its own over the content, at full strength
            content.SetMember("_alpha", menu.notInTree ? kDimTreeAlpha : Draw::kAlphaFull);
            const auto style = CurrentStyle();
            const bool hasTree = Spells::Get() != nullptr;
            const bool hasCentre = hasTree && session.centre >= 0;
            const auto treeLook = TreeRules::LookOf(Chosen(a_shown), hasTree, session.centre >= 0,
                hasCentre && style->SchoolShown(Data().tree.nodes[session.centre].school));
            // B1 (verified at f83dc6f3: the legend stayed up over the message states): one flag for the legend, before every path -
            // only a drawn tree has one (the content clip is new at every draw, so a legend not drawn here is not there). In the centre's
            // school shape and colour (T18)
            const bool legendShown = TreeRules::LegendShown(treeLook);
            if (legendShown) DrawLegend(a_movie, treePanel, boxW, boxH, Data().tree.nodes[session.centre].school);
            if (menu.notInTree && treeLook != TreeRules::Look::kEmpty) {
                // in the title strip, right of the title: the reading size as far as the strip's height allows
                auto note = Draw::Clip(treePanel, "note");
                const auto text = Text(Str::kTreeNotInTree);
                const auto field = note.IsDisplayObject() ?
                                       FitLine(a_movie, note, "note", kPad, kPad / 2, boxW - 2 * kPad,
                                           std::min(Size(kNoteSize), (kTitleH - TextFit::kGutter) / Metrics().lineFirst), text, kDimText, "right") :
                                       RE::GFxValue{};
                Q2State(Str::kTreeNotInTree, text, &field);
            }
            switch (treeLook) {
            case TreeRules::Look::kEmpty: {
                // user 2026-10-30 (the board): nothing chosen - the window keeps its size, one dim line in its middle (no legend, no
                // not-in-tree note - review I9)
                const double size = Size(kTreeRowSize);
                FitLines(a_movie, content, "empty", -frame.halfW, -FieldH(size) / 2, std::max(0.0, 2 * frame.halfW), size, 1, Text(Str::kEmptyChoose), kDimText,
                    "center");
                RecheckHover(hoveredAt);
                return;
            }
            case TreeRules::Look::kNoTree:
            case TreeRules::Look::kNoCentre: {
                const Str id = treeLook == TreeRules::Look::kNoCentre ? Str::kTreeNoCentre : Str::kTreeNoTree;
                const double size = Size(kTreeRowSize);
                const auto empty = FitLines(a_movie, content, "empty", -frame.halfW, -FieldH(size), std::max(0.0, 2 * frame.halfW), size, 2,
                    Text(id), kDimText, "center");
                Q2State(id, Text(id), &empty);
                RecheckHover(hoveredAt);
                return;
            }
            case TreeRules::Look::kSchoolHidden: {
                // T17: HoM's schoolVisibility hides this school - nothing of it is drawn
                const auto& school = Data().tree.nodes[session.centre].school;
                const double size = Size(kTreeRowSize);
                const auto empty = FitLines(a_movie, content, "empty", -frame.halfW, -FieldH(size), std::max(0.0, 2 * frame.halfW), size, 2,
                    Text(Str::kTreeNoCentre), kDimText, "center");
                Q2State("tree_school_hidden", std::format("{} - \"{}\"", school, Text(Str::kTreeNoCentre)), &empty);
                if (menu.textLog.cut.insert("hidden|" + school).second) {
                    logger::info("HoM block: tree - school {} is hidden by Heart of Magic's schoolVisibility: nothing of it drawn", school);
                }
                RecheckHover(hoveredAt);
                return;
            }
            case TreeRules::Look::kTree: break;
            }
            const auto& centre = Data().tree.nodes[session.centre];
            // the picked node for this form (Task 6 review 3: a key pick made under another form counts as dropped already)
            int selected = SelectedFor(a_shown);
            const double scale = MarkScale();
            const double maxReach = HomStyle::MaxReach(scale, style->tree.availableRing, settings.treeScale);
            // the learning target of the centre's school (kept under the cap, its path drawn: spec v10 T3, T8)
            const int target = Spells::FindNode(a_snapshot.Target(centre.school), centre.school);
            TreeData::View view;
            view.scale = settings.treeScale;  // HoM's saved x/y, not turned (spec v11 T10 / R12)
            view.halfW = frame.halfW;
            view.halfH = frame.halfH;
            view.margin = maxReach;  // a node's whole mark inside the window
            view.maxSteps = settings.maxSteps;
            view.maxNodes = settings.maxNodes;
            for (const int keep : { selected, target }) {
                if (keep >= 0 && keep != TheSession().centre) view.keep.push_back(keep);
            }
            const auto window = TreeData::Query(Data().tree, TheSession().centre, view);
            const auto fullList = TreeData::StructuralNeighbours(Data().tree, TheSession().centre);
            const std::unordered_set<int> full(fullList.begin(), fullList.end());
            const auto path = target >= 0 && target != TheSession().centre ? TreeData::StructuralPath(Data().tree, TheSession().centre, target) :
                                                                         std::vector<int>{};
            std::set<std::pair<int, int>> pathLinks;
            for (std::size_t i = 1; i < path.size(); ++i) pathLinks.insert(std::minmax(path[i - 1], path[i]));
            const std::unordered_set<int> pathNodes(path.begin(), path.end());

            std::vector<TreeNode> nodes;
            for (const auto& placed : window.nodes) {
                const auto look = Spells::LookOf(a_snapshot, placed.index);
                const bool clickable = placed.steps >= 0 || Shown(look.state);
                TreeNode node{ placed, look, HomStyle::DisplayOf(BehaviourOf(look.state), placed.index == target), clickable, full.contains(placed.index) };
                node.onPath = pathNodes.contains(placed.index);
                nodes.push_back(node);
                menu.drawn.push_back(placed);
                if (clickable) menu.clickable.push_back(placed);
            }
            // keep the clicked node only while it is drawn and clickable (the node card is drawn again for it: 'SLMD' from the frame)
            if (selected >= 0 && std::ranges::none_of(menu.clickable, [&](const TreeData::PlacedNode& a_placed) { return a_placed.index == selected; })) {
                logger::info("HoM block: tree - the picked node {} is not drawn clickable here: dropped", Spells::NodeName(selected));
                menu.selected = -1;
                selected = -1;
                MarkDirty();
            }
            // before the marks are drawn: a hovered node that moved is no longer hovered, so its outline is drawn hidden (R6)
            RecheckHover(hoveredAt);
            int fullCount = 0;
            for (auto& node : nodes) {
                const auto& data = Data().tree.nodes[node.placed.index];
                // the focus look on the clicked spell, else on the centre (then the spell chosen in the list is the selection)
                node.focus = node.placed.index == (selected >= 0 ? selected : TheSession().centre);
                node.reach = HomStyle::Reach(HomStyle::SizeOf(node.display, style->Mystery(), data.skillLevel, data.tier, node.focus, style->tree.availableRing),
                    data.school, scale, settings.treeScale);
                fullCount += node.full ? 1 : 0;
            }

            std::unordered_map<int, const TreeNode*> byIndex;
            for (const auto& node : nodes) byIndex.emplace(node.placed.index, &node);
            const auto edges = DrawEdges(content, window, byIndex, pathLinks, target, *style);
            // the marks over the links: the dimmed ones first, then the full ones, the selected one last (HoM draws the selected
            // and hovered spells after the rest)
            std::vector<std::size_t> drawOrder(nodes.size());
            for (std::size_t i = 0; i < drawOrder.size(); ++i) drawOrder[i] = i;
            std::ranges::stable_sort(drawOrder, {}, [&](std::size_t a_i) { return (nodes[a_i].full ? 1 : 0) + (nodes[a_i].focus ? 2 : 0); });
            for (const auto i : drawOrder) nodes[i].clip = DrawNode(a_movie, content, nodes[i], *style);
            CheckSpacing(nodes);

            std::vector<LabelBox> labels;  // every drawn mark first: no label over a node
            for (const auto& node : nodes) labels.push_back(NodeBox(node));
            // then every drawn link, so a label keeps off a line too (review v10), half its width each side
            for (const auto& line : edges.lines) {
                const auto link = LayoutMath::LinkBoxes(line.ax, line.ay, line.bx, line.by, maxReach, kLinkBoxStep, line.width / 2);
                labels.insert(labels.end(), link.begin(), link.end());
            }
            {
                // and no label on the legend strip (DrawLegend's row at its size), which the window's clamp alone can graze
                // (review round 1 #5)
                const double legendMid = boxH - HomSettings::kTreeLegendH / 2 - kPad / 2 - frame.cy;
                const double legendHalf = FieldH(kLegendSize) / 2;
                labels.push_back({ -frame.halfW - kPad, legendMid - legendHalf, frame.halfW + kPad, legendMid + legendHalf });
            }
            // The badges first (build 6 review round 2 #1): their arrows and texts go into `labels` before any node label, so the
            // labels keep off them. The centre's direct neighbours outside the window (spec v10 T7); the ones inside it over the
            // cap, without x/y or of another school are only logged
            const auto badges = LayoutMath::Badges(window.outside);
            const bool noLinks = Data().tree.adjacent[TheSession().centre].empty();
            bool badgesDrawn = false;
            if (badges.total > 0) {
                badgesDrawn = DrawBadges(a_movie, content, badges, frame, labels);
                if (badgesDrawn) {
                    Q2State(Str::kTreeExit, Fill(Str::kTreeExit, { { "{n}", std::to_string(badges.total) } }));
                } else if (menu.textLog.cut.insert("exits|no room").second) {
                    logger::info("HoM block: {} tree badges not drawn - the tree window ({:.0f} x {:.0f} inside) has no room for them", badges.total,
                        2 * frame.halfW, 2 * frame.halfH);
                }
            } else if (noLinks) {
                const double size = Size(kNoteSize), top = frame.halfH - FieldH(size);
                const auto note = FitLine(a_movie, content, "nolinks", -frame.halfW, top, std::max(0.0, 2 * frame.halfW), size,
                    Text(Str::kTreeNoLinks), kDimText, "center");
                labels.push_back({ -frame.halfW, top, frame.halfW, top + FieldH(size) });
                Q2State(Str::kTreeNoLinks, Text(Str::kTreeNoLinks), &note);
            }

            // Labels in rank order, each only at a clear place (spec v10 T5); none at all when HoM's showNodeNames is off (T15)
            std::vector<std::size_t> order(nodes.size());
            for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::ranges::stable_sort(order, [&](std::size_t a_lhs, std::size_t a_rhs) {
                return LabelRank(nodes[a_lhs], target, selected) < LabelRank(nodes[a_rhs], target, selected);
            });
            int labelsDrawn = 0, labelsDropped = 0, labelsNone = 0;
            KeptPlace centreSide, selectedSide, targetSide;
            for (const auto i : style->showNodeNames ? order : std::vector<std::size_t>{}) {
                auto& node = nodes[i];
                if (!node.clip.IsDisplayObject()) continue;
                const int index = node.placed.index;
                const bool defaultLeft = index != TheSession().centre && node.placed.sx > 0;
                const auto side = PlaceNodeLabel(a_movie, node, defaultLeft, LabelRank(node, target, selected) <= kKeptRank ? kKeptFarSteps : 0, frame,
                    *style, labels);
                ++(side.none ? labelsNone : side.drawn ? labelsDrawn : labelsDropped);
                if (index == TheSession().centre) {
                    centreSide = side;
                } else if (index == selected) {
                    selectedSide = side;
                } else if (index == target) {
                    targetSide = side;
                }
            }
            if (const auto hidden = std::ranges::find_if(nodes, [&](const TreeNode& a_node) {
                    return HomStyle::Reveal(a_node.display, a_node.look.percent, style->revealName, style->cheatMode) == HomStyle::Name::kHidden;
                });
                hidden != nodes.end() && style->showNodeNames) {
                Q2State("tree_name_hidden", std::format("{} - \"{}\" ({:.0f}% under revealName {})", Spells::NodeName(hidden->placed.index),
                    HomStyle::kHiddenName, hidden->look.percent, style->revealName));
            }
            const auto labelNote = [](const char* a_what, const KeptPlace& a_side) {
                return a_side.none  ? std::format(", {} label none (HoM shows no name)", a_what) :
                       a_side.drawn ? std::format(", {} label {} (overlap at its side {:.1f})", a_what, PlaceName(a_side.place), a_side.firstOverlap) :
                                      std::format(", {} label dropped (least overlap {:.1f})", a_what, a_side.overlap);
            };
            const bool selectedApart = selected >= 0 && selected != TheSession().centre;
            const bool targetApart = target >= 0 && target != TheSession().centre && target != selected;
            const auto clickable = std::ranges::count_if(nodes, [](const TreeNode& a_node) { return a_node.clickable; });
            logger::info("HoM block: tree redraw centre {} {:08X} ({}), scale {} marks x{} (HoM sizes x {:.3f}, preset {}): in window {} -> drawn {} "
                         "({} clickable; full {} / dim {} at x{}), capped {}; links dim {} locked {} frontier {} learned {} centre's {} path {} "
                         "chains {} (not drawn: {}); outside {} -> badges {} (centre's neighbours apart: {} capped, {} without x/y, {} other "
                         "school){}; labels {} drawn / {} dropped / {} none{}{}{}{}; target {}, path {} node(s), snapshot {}, {:.1f} ms",
                Spells::NodeName(TheSession().centre), Data().gameIds[TheSession().centre], centre.school, view.scale, settings.treeMarkScale, scale,
                style->presetFile.empty() ? "DEFAULTS" : style->presetId, window.candidatesBeforeCap, nodes.size(), clickable, fullCount,
                nodes.size() - static_cast<std::size_t>(fullCount), style->DimFactor(), window.candidatesBeforeCap - static_cast<int>(nodes.size()),
                edges.dim, edges.locked, edges.frontier, edges.unlocked, edges.selected, edges.learning, edges.chains, edges.hidden,
                window.outside.size(), badgesDrawn ? "drawn" : badges.total > 0 ? "no room" : "none", window.neighboursCapped, window.neighboursNoXY,
                window.neighboursOtherSchool, noLinks ? ", no links" : "", labelsDrawn, labelsDropped, labelsNone,
                style->showNodeNames ? "" : " (showNodeNames off)", labelNote("centre", centreSide),
                selectedApart ? labelNote("selected", selectedSide) : std::string{}, targetApart ? labelNote("target", targetSide) : std::string{},
                target >= 0 ? Spells::NodeName(target) : std::string("none"), path.size(), a_snapshot.epoch,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
            if (menu.notInTree) logger::info("HoM block: tree drawn dimmed to {:.0f}% (the selection is not in the tree)", kDimTreeAlpha);
            RecheckHover(hoveredAt);        }
    }

    double MarkScale()
    {
        const auto& settings = HomSettings::Get();
        return settings.treeScale * settings.treeMarkScale;
    }

    RE::GFxValue PanelClip(RE::GFxValue& a_content)
    {
        auto panel = Draw::Clip(a_content, kPanelName);
        if (!panel.IsDisplayObject()) return panel;
        panel.SetMember("_x", -kPad);
        panel.SetMember("_y", -(kTitleH + kPad));
        return panel;
    }

    // The tree block's drawSelection: the window for the card's form (the host draws it again for a new form, a size or column
    // change and on 'SLMD')
    void DrawTreeBlock(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip, float a_width, float a_height, std::uint32_t a_shown, void*)
    {
        EnsureMenu();
        if (!a_movie) return;
        auto& menu = Menu();
        menu.drawnWith[Index(BlockKind::kTree)] = menu.metrics;
        RedrawTree(a_movie, a_clip, a_width, a_height, a_shown, *Spells::Current());
    }
}
