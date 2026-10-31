// magic-menu-layout-check, its [Blocks] sHidden part (2026-10-31, the SKSE Menu Framework settings tab): HiddenBlocks.h's parse
// (spaces, empty items, repeats, unknown ids kept), the value written back, a checkbox's toggle, the ids the host hides (known ones,
// a retired id read as its final id), and the layout as the widget arranges it with them left out of the heights (BlockHeights): a
// hidden built-in leaves no gap, a hidden add-on keeps its place in the layout, every block hidden arranges nothing. See
// magic-menu-layout-check.cpp for the tool's usage.

#include "HiddenBlocks.h"
#include "Layout.h"
#include "Settings.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
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

    void Run(ExpectFn a_expect)
    {
        Parsing(a_expect);
        Effective(a_expect);
        Arranged(a_expect);
    }
}
