// magic-menu-block-api-check, its key hint part (2026-10-31, user "단축키 표시"): the v3 'SLMK' message (BlockApi::KeyHint) and
// the pure rules of KeyHints.cpp in BlockRulesV3.h - reading (whole, cut short, versions, scan codes, labels cut at a UTF-8
// boundary), the per-block list (add, relabel in place, remove, the cap), the frame's hint buttons (order, shown per block, the
// layout mode, gamepad) and the panel's growth and tail rules. See magic-menu-block-api-check.cpp for the tool's usage.

#include "magic-menu-block-api-check.h"

#include "BlockRulesV3.h"

#include <string>
#include <vector>

namespace BlockApiCheck
{
    namespace
    {
        const std::string kTree = "MagicMenuRewrite_HoM.tree";
        const std::string kOther = "AddonB.stats";

        BlockRules::KeyHintRead Hint(const std::string& a_id, std::uint32_t a_key, const std::string& a_label)
        {
            return { a_id, a_key, a_label };
        }

        void Reading()
        {
            std::string why;
            const BlockApi::KeyHint whole{ .id = kTree.c_str(), .scanCode = 22, .label = "Next spell" };
            const auto read = BlockRules::ReadKeyHint(&whole, sizeof(whole), why);
            Expect(read && read->id == kTree && read->scanCode == 22 && read->label == "Next spell", "'SLMK' whole: id, key 22 and label read");
            Expect(sizeof(BlockApi::KeyHint) == 32 && BlockRules::kKeyHintNeeded == sizeof(BlockApi::KeyHint), "KeyHint is 32 bytes, all of it read");
            const auto shortBy = static_cast<std::uint32_t>(BlockRules::kKeyHintNeeded - 1);
            Expect(!BlockRules::ReadKeyHint(&whole, shortBy, why) && why.find(std::to_string(shortBy) + " bytes") != std::string::npos,
                Text("'SLMK' one byte short: refused with the byte count (%s)", why.c_str()));
            auto smallStruct = whole;
            smallStruct.structSize = static_cast<std::uint32_t>(BlockRules::kKeyHintNeeded - 1);
            Expect(!BlockRules::ReadKeyHint(&smallStruct, sizeof(smallStruct), why) && why.find("structSize") != std::string::npos,
                "'SLMK' structSize short of the label: refused");
            for (const std::uint32_t version : { BlockApi::kVersionV2, BlockApi::kVersion + 1 }) {
                auto bad = whole;
                bad.apiVersion = version;
                Expect(!BlockRules::ReadKeyHint(&bad, sizeof(bad), why), Text("'SLMK' apiVersion %u: refused", version));
            }
            for (const std::uint32_t key : { 0u, BlockApi::kMaxHintScanCode + 1 }) {
                auto bad = whole;
                bad.scanCode = key;
                Expect(!BlockRules::ReadKeyHint(&bad, sizeof(bad), why) && why.find("scanCode") != std::string::npos, Text("'SLMK' scanCode %u: refused", key));
            }
            auto top = whole;
            top.scanCode = BlockApi::kMaxHintScanCode;
            Expect(BlockRules::ReadKeyHint(&top, sizeof(top), why).has_value(), "'SLMK' scanCode 255: read");
            const BlockApi::KeyHint noId{ .id = "", .scanCode = 22, .label = "x" }, nullId{ .scanCode = 22, .label = "x" };
            Expect(!BlockRules::ReadKeyHint(&noId, sizeof(noId), why) && !BlockRules::ReadKeyHint(&nullId, sizeof(nullId), why) &&
                       !BlockRules::ReadKeyHint(nullptr, sizeof(whole), why),
                "'SLMK' with an empty or null id, or null data: refused");
            const BlockApi::KeyHint nullLabel{ .id = kTree.c_str(), .scanCode = 22 };
            const auto removed = BlockRules::ReadKeyHint(&nullLabel, sizeof(nullLabel), why);
            Expect(removed && removed->label.empty(), "'SLMK' with a null label: read as a removal (empty label)");

            // 31 x "가" (3 bytes) = 93 bytes: cut to 64 -> 21 whole characters (63 bytes), never inside one
            std::string korean;
            for (int i = 0; i < 31; ++i) korean += "\xEA\xB0\x80";
            const BlockApi::KeyHint longKo{ .id = kTree.c_str(), .scanCode = 34, .label = korean.c_str() };
            const auto cut = BlockRules::ReadKeyHint(&longKo, sizeof(longKo), why);
            Expect(cut && cut->label.size() == 63 && cut->label == korean.substr(0, 63), Text("a 93-byte Korean label cut at a character boundary (%zu bytes)",
                                                                                              cut ? cut->label.size() : 0));
            const std::string ascii(100, 'a');
            const BlockApi::KeyHint longEn{ .id = kTree.c_str(), .scanCode = 34, .label = ascii.c_str() };
            const auto cutEn = BlockRules::ReadKeyHint(&longEn, sizeof(longEn), why);
            Expect(cutEn && cutEn->label.size() == BlockApi::kMaxHintLabelBytes, "a 100-byte ASCII label cut to 64");
            Expect(BlockRules::CutUtf8("Edit mode", 64) == "Edit mode" && BlockRules::CutUtf8("\xED\x8E\xB8\xEC\xA7\x91", 4) == "\xED\x8E\xB8" &&
                       BlockRules::CutUtf8("ab", 0).empty(),
                "CutUtf8: short text kept, a cut inside the second character keeps the first, 0 bytes empty");
        }

        void List()
        {
            BlockRules::KeyHintList list;
            Expect(BlockRules::SetKeyHint(list, Hint(kTree, 22, "Next spell"), BlockApi::kMaxKeyHints) == BlockRules::HintSet::kAdded &&
                       BlockRules::SetKeyHint(list, Hint(kTree, 34, "Set learning target"), BlockApi::kMaxKeyHints) == BlockRules::HintSet::kAdded &&
                       list.size() == 2 && list[0].scanCode == 22 && list[1].scanCode == 34,
                "two hints added in the order sent");
            Expect(BlockRules::SetKeyHint(list, Hint(kTree, 22, "Next"), BlockApi::kMaxKeyHints) == BlockRules::HintSet::kReplaced && list.size() == 2 &&
                       list[0].scanCode == 22 && list[0].label == "Next",
                "the same block and key again: relabelled, its place kept");
            Expect(BlockRules::SetKeyHint(list, Hint(kOther, 22, "Other"), BlockApi::kMaxKeyHints) == BlockRules::HintSet::kAdded && list.size() == 3,
                "the same key of another block: a hint of its own");
            Expect(BlockRules::SetKeyHint(list, Hint(kTree, 22, ""), BlockApi::kMaxKeyHints) == BlockRules::HintSet::kRemoved && list.size() == 2 &&
                       list[0].id == kTree && list[0].scanCode == 34,
                "an empty label removes that hint, the others keep their order");
            Expect(BlockRules::SetKeyHint(list, Hint(kTree, 99, ""), BlockApi::kMaxKeyHints) == BlockRules::HintSet::kNotThere && list.size() == 2,
                "removing a hint that is not there changes nothing");
            BlockRules::KeyHintList full;
            for (std::uint32_t key = 1; key <= BlockApi::kMaxKeyHints; ++key) BlockRules::SetKeyHint(full, Hint(kTree, key, "k"), BlockApi::kMaxKeyHints);
            Expect(BlockRules::SetKeyHint(full, Hint(kTree, 50, "k"), BlockApi::kMaxKeyHints) == BlockRules::HintSet::kFull &&
                       BlockRules::SetKeyHint(full, Hint(kTree, 1, "relabel"), BlockApi::kMaxKeyHints) == BlockRules::HintSet::kReplaced &&
                       BlockRules::SetKeyHint(full, Hint(kOther, 50, "k"), BlockApi::kMaxKeyHints) == BlockRules::HintSet::kAdded,
                Text("a block's %u keys: a new key refused, a relabel still taken, another block's key taken", BlockApi::kMaxKeyHints));
        }

        void Buttons()
        {
            const BlockRules::HintButton host{ 37, "Edit mode", false };
            BlockRules::KeyHintList hints;
            BlockRules::SetKeyHint(hints, Hint(kOther, 48, "Stats"), BlockApi::kMaxKeyHints);
            BlockRules::SetKeyHint(hints, Hint(kTree, 22, "Next spell"), BlockApi::kMaxKeyHints);
            BlockRules::SetKeyHint(hints, Hint(kTree, 34, "Set learning target"), BlockApi::kMaxKeyHints);
            BlockRules::SetKeyHint(hints, Hint("Gone.block", 40, "Gone"), BlockApi::kMaxKeyHints);
            const std::vector<BlockRules::HintBlock> blocks{ { kTree, true }, { kOther, false } };  // registration order: tree first
            const auto view = BlockRules::HintButtons(host, blocks, hints, false, false);
            const std::vector<BlockRules::HintButton> want{ { 37, "Edit mode", true }, { 22, "Next spell", true }, { 34, "Set learning target", true },
                { 48, "Stats", false } };
            Expect(view == want, "view mode: the host's K first, then the blocks in registration order (not the order sent), a hidden block's hint not shown, "
                                 "a hint of no listed block has no button");
            const auto layout = BlockRules::HintButtons(host, blocks, hints, true, false);
            Expect(layout.size() == 4 && layout[0].shown && !layout[1].shown && !layout[2].shown && !layout[3].shown,
                "layout mode: K still shown, the blocks' hints hidden");
            const auto pad = BlockRules::HintButtons(host, blocks, hints, false, true);
            Expect(pad.size() == 4 && std::ranges::none_of(pad, [](const auto& a_button) { return a_button.shown; }), "gamepad buttons up: every hint hidden");
            Expect(BlockRules::HintButtons({ 0, "Edit mode", false }, blocks, hints, false, false).size() == 3 &&
                       BlockRules::HintButtons({ 37, "", false }, blocks, hints, false, false).size() == 3,
                "the host's hint left out with no key or no label");
            Expect(BlockRules::SameHintSet(view, layout) && BlockRules::SameHintSet(view, pad) && !BlockRules::SameHintSet(view, BlockRules::HintButtons(host, {}, hints, false, false)),
                "SameHintSet: shown flags aside alike (a tab or mode change only shows or hides); a block gone is another set");
            auto relabelled = hints;
            BlockRules::SetKeyHint(relabelled, Hint(kTree, 22, "Next"), BlockApi::kMaxKeyHints);
            Expect(!BlockRules::SameHintSet(view, BlockRules::HintButtons(host, blocks, relabelled, false, false)), "SameHintSet: a new label is another set");
        }

        void Panel()
        {
            Expect(BlockRules::ButtonsToGrow(6, 3, 3, 8) == 0, "6 buttons, 3 in use, 3 wanted: none to make");
            Expect(BlockRules::ButtonsToGrow(6, 5, 3, 8) == 2, "6 buttons, 5 in use (SkyUI 3 + STB 2), 3 wanted: 2 to make");
            Expect(BlockRules::ButtonsToGrow(6, 6, 3, 1) == 1 && BlockRules::ButtonsToGrow(6, 6, 3, 0) == 0, "growth held to what is left of the cap");
            Expect(BlockRules::ButtonsToGrow(4, 9, 2, 8) == 2, "more in use than the array reads: every wanted one made");
            Expect(BlockRules::OursAtTail(3, 3, 6) && !BlockRules::OursAtTail(3, 3, 8) && BlockRules::OursAtTail(5, 0, 5),
                "ours at the panel's end: only when nothing came after them");
        }

        // game run 2026-10-31: Untarnished UI's panel wraps at button index 4 (x 0, y 35); the L10 FAILs read x alone
        void ReadingOrder()
        {
            using BlockRules::ButtonSpot;
            Expect(BlockRules::LaidOutAfter(std::nullopt, { 0.0, 0.0 }), "the first shown button: after nothing");
            Expect(BlockRules::LaidOutAfter(ButtonSpot{ 169.0, 0.0 }, { 251.2, 0.0 }), "same row, right of the last: after it");
            Expect(BlockRules::LaidOutAfter(ButtonSpot{ 251.2, 0.0 }, { 0.0, 35.0 }) && BlockRules::LaidOutAfter(ButtonSpot{ 0.0, 35.0 }, { 82.3, 35.0 }),
                "the 10-31 FAIL case (button 3 at 251.2, then 4 at 0 and 5 at 82.3 on the next row): after it");
            Expect(!BlockRules::LaidOutAfter(ButtonSpot{ 251.2, 0.0 }, { 0.0, 0.0 }) && !BlockRules::LaidOutAfter(ButtonSpot{ 82.3, 35.0 }, { 82.3, 35.0 }),
                "same row at or left of the last: not after it (an overlap)");
            Expect(!BlockRules::LaidOutAfter(ButtonSpot{ 0.0, 35.0 }, { 300.0, 0.0 }), "a row above the last: not after it");
            Expect(BlockRules::LaidOutAfter(ButtonSpot{ 10.0, 0.0 }, { 20.0, BlockRules::kRowTolerance * 0.5 }),
                "a _y within the tolerance is the same row (right of it: after it)");
        }
    }

    void V3KeyHints()
    {
        std::printf("'SLMK' key hints (BlockRules::ReadKeyHint, SetKeyHint, HintButtons, ButtonsToGrow, LaidOutAfter)\n");
        Reading();
        List();
        Buttons();
        Panel();
        ReadingOrder();
    }
}
