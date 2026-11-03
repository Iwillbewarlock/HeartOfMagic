// magic-menu-layout-check, its [Blocks] sHidden part (2026-10-31, the SKSE Menu Framework settings tab): HiddenBlocks.h's parse
// (spaces, empty items, repeats, unknown ids kept), the value written back, a checkbox's toggle, the ids the host hides (known ones,
// a retired id read as its final id), and the layout as the widget arranges it with them left out of the heights (BlockHeights): a
// hidden built-in leaves no gap, a hidden add-on keeps its place in the layout, every block hidden arranges nothing; and a block
// away and back (2026-11-04, the Heart of Magic block moving to HoM's release): an old layout file with the retired ids and
// sHidden read with the block, without it, saved without it, read with it again - the same places and hidden blocks. See
// magic-menu-layout-check.cpp for the tool's usage.

#include "HiddenBlocks.h"
#include "Layout.h"
#include "Settings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace HiddenCheck
{
    namespace
    {
        using Layout::BlockId;
        using Ids = std::vector<std::string>;
        using ExpectFn = void (*)(bool, const std::string&);

        constexpr double kTolerance = 1e-9;
        const std::string kTree = "MagicMenuRewrite_HoM.tree", kProgress = "MagicMenuRewrite_HoM.progress", kNodeCard = "MagicMenuRewrite_HoM.nodecard";

        std::string Show(const Ids& a_ids)
        {
            return "[" + HiddenBlocks::Join(a_ids) + "]";
        }

        bool Top(const std::map<BlockId, Settings::Rect>& a_rects, const BlockId& a_id, double a_top)
        {
            const auto it = a_rects.find(a_id);
            return it != a_rects.end() && std::abs(it->second.top - a_top) <= kTolerance;
        }

        void Parsing(ExpectFn a_expect)
        {
            std::printf("[Blocks] sHidden: parse, write back, toggle\n");
            a_expect(HiddenBlocks::Parse("").empty() && HiddenBlocks::Parse("  ,, ,\t").empty(), "empty value and only separators/spaces: nothing hidden");
            const auto spaced = HiddenBlocks::Parse(" preview , MagicMenuRewrite_HoM.tree,,\tspellcard ,");
            a_expect(spaced == Ids{ "preview", kTree, "spellcard" }, "spaces and tabs trimmed, empty items dropped, order kept (got " + Show(spaced) + ")");
            const auto repeated = HiddenBlocks::Parse("preview,Other.block,preview");
            a_expect(repeated == Ids{ "preview", "Other.block" }, "a repeat dropped (the first kept); an unknown id kept as written (got " + Show(repeated) + ")");
            a_expect(HiddenBlocks::Join(spaced) == "preview," + kTree + ",spellcard" && HiddenBlocks::Join({}).empty(),
                "written back joined by ',' with no spaces; none -> \"\"");
            a_expect(HiddenBlocks::Parse(HiddenBlocks::Join(spaced)) == spaced, "parse(join(x)) == x");
            const Ids withUnknown{ "Gone.addon", "preview" };
            a_expect(HiddenBlocks::With(withUnknown, kTree, true) == Ids{ "Gone.addon", "preview", kTree }, "ticking a block off appends it, the unknown id kept");
            a_expect(HiddenBlocks::With(withUnknown, "preview", false) == Ids{ "Gone.addon" }, "ticking a block on removes it, the unknown id kept");
            a_expect(HiddenBlocks::With(withUnknown, "preview", true) == withUnknown && HiddenBlocks::With(withUnknown, kTree, false) == withUnknown &&
                         HiddenBlocks::With(withUnknown, "", true) == withUnknown,
                "hiding a hidden id, showing a shown one or an empty id: unchanged");
        }

        void Effective(ExpectFn a_expect)
        {
            std::printf("[Blocks] sHidden: the ids the host hides\n");
            const std::set<std::string> known{ Layout::kPreview, Layout::kSpellCard, kTree, kProgress, kNodeCard };
            const std::map<std::string, std::string> aliases{ { Layout::kTree, kTree }, { Layout::kProgress, kProgress } };
            const Ids ids{ "Other.block", "tree", "spellcard", "nosuch" };
            const auto hidden = HiddenBlocks::Effective(ids, known, aliases);
            a_expect(hidden == std::set<std::string>{ Layout::kSpellCard, kTree },
                "known ids hidden, the retired id 'tree' read as " + kTree + ", unknown ids not (got " + std::to_string(hidden.size()) + " ids)");
            a_expect(HiddenBlocks::Unknown(ids, known, aliases) == Ids{ "Other.block", "nosuch" }, "the unknown ids named for the log");
            a_expect(HiddenBlocks::Effective(ids, known, {}) == std::set<std::string>{ Layout::kSpellCard }, "with no alias (no add-on took 'tree'): 'tree' hides nothing");
        }

        void Arranged(ExpectFn a_expect)
        {
            std::printf("[Blocks] sHidden: the layout with hidden blocks left out of the heights\n");
            const Settings::Values values{};
            const auto frames = Layout::DefaultFrames(values);
            const Layout::State state{ { std::vector<BlockId>{ Layout::kPreview, kProgress, Layout::kSpellCard }, std::vector<BlockId>{ kTree, kNodeCard } },
                values.preview.right };
            auto all = Layout::DefaultHeights(values);
            all[kTree] = 230;
            all[kProgress] = 102;
            all[kNodeCard] = 317;
            const double gap = values.panelGap, top = values.preview.top;

            // a hidden built-in: the preview gone, the progress card moves up to the column's top, no gap left
            auto heights = all;
            HiddenBlocks::Drop(heights, { Layout::kPreview });
            auto rects = Layout::Arrange(state, heights, {}, { Layout::kSpellCard }, frames);
            a_expect(!rects.contains(Layout::kPreview) && Top(rects, kProgress, top) && Top(rects, Layout::kSpellCard, top + 102 + gap),
                "hidden preview: not arranged, the blocks below it close up from the column's top (no gap)");
            a_expect(rects.at(Layout::kSpellCard).bottom == frames[0].bottom, "hidden preview: our card (fill) still reaches the column's end");

            // a hidden add-on keeps its place in the layout (nothing removes it from the State), absent from the rects
            heights = all;
            HiddenBlocks::Drop(heights, { kTree });
            rects = Layout::Arrange(state, heights, {}, {}, frames);
            a_expect(!rects.contains(kTree) && Top(rects, kNodeCard, top) && state.columns[1].front() == kTree,
                "hidden add-on: not arranged, the node card at the right column's top, the tree's place in the layout kept");

            // the spell card hidden: the preview and the progress card stay where they were
            heights = all;
            HiddenBlocks::Drop(heights, { Layout::kSpellCard });
            rects = Layout::Arrange(state, heights, {}, { Layout::kSpellCard }, frames);
            a_expect(!rects.contains(Layout::kSpellCard) && Top(rects, Layout::kPreview, top) && Top(rects, kProgress, top + values.preview.Height() + gap),
                "hidden spell card (in fill): not arranged, nothing above it moves, no fill applied");

            // every block hidden: nothing arranged, both columns empty
            heights = all;
            HiddenBlocks::Drop(heights, { Layout::kPreview, Layout::kSpellCard, kTree, kProgress, kNodeCard });
            rects = Layout::Arrange(state, heights, {}, { Layout::kSpellCard }, frames);
            const auto content = Layout::ContentHeight(state, rects, frames);
            a_expect(heights.empty() && rects.empty() && content[0] == 0 && content[1] == 0, "every block hidden: no rect, both columns' content 0");

            // an unknown id in the hidden set changes nothing
            heights = all;
            HiddenBlocks::Drop(heights, { "Gone.addon" });
            a_expect(heights == all, "an id that is no block: the heights unchanged");
        }
    }

    // 2026-11-04 (docs/magicmenu/BLOCKS_MOVE.md): the Heart of Magic block moves from Magic Menu Rewrite's release to Heart of
    // Magic Librarian's, so for a while a player may run the magic menu without it (MMR 1.2.0 before HoM 1.9.0, or the HoM
    // option unchecked). A layout file and [Blocks] sHidden from before must come through that: read with the block, read
    // without it, saved without it (the layout mode), read with it again - the same places, the same hidden blocks. The
    // session below is the host's path: Settings::LoadLayout (FromJson, ToFinal with no alias yet, Repair against the built-ins
    // and the default), Settings::AddBlocks (WithAddons with the aliases and add-ons the registry knows), Widget's hidden set
    // (HiddenBlocks::Effective) and BlockHeights (the hidden ones dropped), Settings::SaveLayout (ToLegacy, ToJson), and the
    // settings tab's checkbox (HiddenBlocks::With on the ids as read, a built-in's legacy id empty).
    namespace AwayAndBack
    {
        const std::map<BlockId, BlockId> kHomAliases{ { Layout::kTree, kTree }, { Layout::kProgress, kProgress }, { Layout::kNodeCard, kNodeCard } };
        const std::vector<BlockId> kHomBlocks{ kTree, kProgress, kNodeCard };  // the order MagicMenuRewrite_HoM registers them
        const std::set<BlockId> kBuiltins{ Layout::kPreview, Layout::kSpellCard };

        struct Session
        {
            bool read{};
            Layout::State layout;
            std::set<std::string> hidden;
            Ids unknown;
            std::map<BlockId, Settings::Rect> rects;
        };

        Session Open(std::string_view a_file, std::string_view a_hiddenValue, bool a_withBlock)
        {
            Session session;
            const Settings::Values values{};
            const auto fallback = Layout::Default(values);
            const auto file = Layout::FromJson(a_file, nullptr);
            if (!file) return session;
            session.read = true;
            const auto aliases = a_withBlock ? kHomAliases : std::map<BlockId, BlockId>{};
            const auto addons = a_withBlock ? kHomBlocks : std::vector<BlockId>{};
            const auto loaded = Layout::Repair(Layout::ToFinal(*file, {}), kBuiltins, fallback);
            session.layout = Layout::WithAddons(loaded, kBuiltins, aliases, addons, fallback).layout;
            auto known = kBuiltins;
            known.insert(addons.begin(), addons.end());
            const auto setting = HiddenBlocks::Parse(a_hiddenValue);
            session.hidden = HiddenBlocks::Effective(setting, known, aliases);
            session.unknown = HiddenBlocks::Unknown(setting, known, aliases);
            auto heights = Layout::DefaultHeights(values);
            if (a_withBlock) {
                heights[kTree] = 230;
                heights[kProgress] = 102;
                heights[kNodeCard] = 317;
            }
            HiddenBlocks::Drop(heights, session.hidden);
            session.rects = Layout::Arrange(session.layout, heights, {}, { Layout::kSpellCard }, Layout::DefaultFrames(values));
            return session;
        }

        std::string Save(const Session& a_session, bool a_withBlock)
        {
            return Layout::ToJson(Layout::ToLegacy(a_session.layout, a_withBlock ? kHomAliases : std::map<BlockId, BlockId>{}));
        }

        bool SameRects(const std::map<BlockId, Settings::Rect>& a_one, const std::map<BlockId, Settings::Rect>& a_two)
        {
            if (a_one.size() != a_two.size()) return false;
            for (const auto& [id, rect] : a_one) {
                const auto it = a_two.find(id);
                if (it == a_two.end()) return false;
                const auto& other = it->second;
                if (std::abs(rect.left - other.left) > kTolerance || std::abs(rect.top - other.top) > kTolerance ||
                    std::abs(rect.right - other.right) > kTolerance || std::abs(rect.bottom - other.bottom) > kTolerance) {
                    return false;
                }
            }
            return true;
        }

        std::string Columns(const Layout::State& a_state)
        {
            return Show(a_state.columns[0]) + " / " + Show(a_state.columns[1]);
        }

        /// The settings tab while the block is away: it lists the built-ins only; the spell card ticked off and on again
        Ids TabToggle(const Ids& a_ids)
        {
            const auto off = HiddenBlocks::With(HiddenBlocks::With(a_ids, "", false), Layout::kSpellCard, true);
            return HiddenBlocks::With(HiddenBlocks::With(off, "", false), Layout::kSpellCard, false);
        }

        /// a_hiddenValue: [Blocks] sHidden; a_hiddenWith: what the host hides with the block installed (the progress card, maybe
        /// a built-in too); a_hiddenAway: without it (the built-ins of a_hiddenWith)
        void Case(ExpectFn a_expect, const std::string& a_hiddenValue, const std::set<std::string>& a_hiddenWith,
            const std::set<std::string>& a_hiddenAway, const char* a_label)
        {
            // the player's layout from MMR 1.0.0 / 1.1.0: the node card moved to the left column, the progress card under the tree,
            // the border moved; written with the retired ids, as every save does while the block is installed
            const std::string oldFile = R"({"version":1,"columns":[["preview","nodecard","spellcard"],["tree","progress"]],"border":940})";
            const std::string tag = std::string("away and back (") + a_label + "): ";

            const auto before = Open(oldFile, a_hiddenValue, true);
            a_expect(before.read && before.layout.columns[0] == Ids{ Layout::kPreview, kNodeCard, Layout::kSpellCard } &&
                         before.layout.columns[1] == Ids{ kTree, kProgress } && std::abs(before.layout.border - 940) <= kTolerance,
                tag + "with the block the old file is read in the player's places (got " + Columns(before.layout) + ")");
            a_expect(before.hidden == a_hiddenWith && !before.rects.contains(kProgress) && before.rects.contains(kTree) && before.rects.contains(kNodeCard),
                tag + "with the block the progress card is hidden, the tree and node card shown");

            // MMR 1.2.0 without HoM 1.9.0's block: the retired ids keep their places (unknown ids), the block's id hides nothing
            // and is named as unknown, and stays in the setting
            const auto away = Open(oldFile, a_hiddenValue, false);
            a_expect(away.read && away.layout.columns[0] == Ids{ Layout::kPreview, Layout::kNodeCard, Layout::kSpellCard } &&
                         away.layout.columns[1] == Ids{ Layout::kTree, Layout::kProgress } && std::abs(away.layout.border - 940) <= kTolerance,
                tag + "without the block the retired ids keep their places (got " + Columns(away.layout) + ")");
            const bool onlyBuiltins = std::ranges::all_of(away.rects, [](const auto& a_rect) { return kBuiltins.contains(a_rect.first); });
            a_expect(away.hidden == a_hiddenAway && away.unknown.size() == 1 && onlyBuiltins && !away.rects.empty(),
                tag + "without the block its id hides nothing and is named unknown; only built-ins arranged");

            // the layout mode saves while the block is away; the settings tab ticks a built-in off and on
            const auto savedAway = Save(away, false);
            const auto reread = Layout::FromJson(savedAway, nullptr);
            const auto original = Layout::FromJson(oldFile, nullptr);
            a_expect(reread && original && *reread == *original, tag + "saved without the block: the file holds the same ids in the same places");
            const auto hiddenAfterTab = HiddenBlocks::Join(TabToggle(HiddenBlocks::Parse(a_hiddenValue)));
            a_expect(hiddenAfterTab == HiddenBlocks::Join(HiddenBlocks::Parse(a_hiddenValue)),
                tag + "the settings tab without the block keeps its id in sHidden (got '" + hiddenAfterTab + "')");

            // HoM 1.9.0 installs the block again
            const auto back = Open(savedAway, hiddenAfterTab, true);
            a_expect(back.read && back.layout == before.layout && SameRects(back.rects, before.rects),
                tag + "the block back: the same places as before it went (got " + Columns(back.layout) + ")");
            a_expect(back.hidden == before.hidden, tag + "the block back: the same blocks hidden");
            a_expect(Save(back, true) == Save(before, true), tag + "the block back: the next save writes what the first one did");

            // moved while away: the spell card to the left column's top; the block's three keep their columns and order
            auto moved = away;
            moved.layout = Layout::Move(away.layout, Layout::kSpellCard, Layout::Slot{ 0, Layout::kPreview });
            const auto movedBack = Open(Save(moved, false), hiddenAfterTab, true);
            a_expect(movedBack.layout.columns[0] == Ids{ Layout::kSpellCard, Layout::kPreview, kNodeCard } &&
                         movedBack.layout.columns[1] == Ids{ kTree, kProgress } && movedBack.hidden == before.hidden,
                tag + "a built-in moved while away: the block's three keep their places among the others (got " + Columns(movedBack.layout) + ")");
        }

        void Run(ExpectFn a_expect)
        {
            std::printf("[Blocks] sHidden and the layout file: a block away and back (the Heart of Magic block moved to HoM 1.9.0)\n");
            Case(a_expect, kProgress, { kProgress }, {}, "final id in sHidden");
            Case(a_expect, Layout::kProgress, { kProgress }, {}, "retired id in sHidden");
            Case(a_expect, std::string(Layout::kPreview) + "," + kProgress, { Layout::kPreview, kProgress }, { Layout::kPreview },
                "a built-in and the block's id in sHidden");
        }
    }

    void Run(ExpectFn a_expect)
    {
        Parsing(a_expect);
        Effective(a_expect);
        Arranged(a_expect);
        AwayAndBack::Run(a_expect);
    }
}
