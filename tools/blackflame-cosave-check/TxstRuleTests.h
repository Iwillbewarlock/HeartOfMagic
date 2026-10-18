#pragma once

// =============================================================================
// TXST RULE TESTS - plugins/BlackFlame/src/SlotTxstRule.h: the textureFileIDs rule voted slot by slot
// =============================================================================
// Run 3's shape (2026-10-18): 78 slots match "textures\" + name; 14 do not: 10 'black.dds' TX03 no record has and 4
// PSI.esp TX05 'cubemap\shinydull_e.dds' (a missing file) - the 6 / 4 split of the 'black.dds' IDs and the TX05 zeros are
// the counts' best reading, not each seen in the log. Included by main.cpp.

#include "../../plugins/BlackFlame/src/DataPathRule.h"
#include "../../plugins/BlackFlame/src/SlotTxstRule.h"

#include <cstddef>
#include <string>

namespace TxstRuleTests
{
    namespace TR = BlackFlame::SlotTxstRule;
    namespace DP = BlackFlame::DataPathRule;

    inline TR::SlotEvidence Prefixed()
    {
        TR::SlotEvidence e;
        e.matchesNormalized = true;
        return e;
    }

    inline TR::SlotEvidence Bare()
    {
        TR::SlotEvidence e;
        e.matchesRaw = true;
        return e;
    }

    inline TR::SlotEvidence Zero()
    {
        TR::SlotEvidence e;
        e.storedZero = true;
        return e;
    }

    inline TR::SlotEvidence OtherFile()
    {
        return TR::SlotEvidence{};
    }

    inline void Add(TR::Tally& a_tally, const std::string& a_name, const TR::SlotEvidence& a_slot, std::size_t a_times = 1)
    {
        for (std::size_t i = 0; i < a_times; ++i) {
            a_tally.Add(a_name, TR::KindOf(a_slot), "Skyrim.esm|2b95a TX03 stored x");
        }
    }

    template <class Check>
    void Run(Check&& a_check)
    {
        const auto has = [](const std::string& a_text, const std::string& a_part) { return a_text.find(a_part) != std::string::npos; };
        {
            TR::Tally run3;
            Add(run3, "Effects\\RuneFire01.dds", Prefixed(), 78);
            Add(run3, "black.dds", OtherFile(), 6);
            Add(run3, "black.dds", Zero(), 4);
            Add(run3, "cubemap\\shinydull_e.dds", Zero(), 4);
            const auto text = run3.Text();
            a_check(run3.Decide() == TR::Rule::kPrefixed && run3.Checked() == 92 && run3.Prefixed() == 78 && run3.Unexplained() == 14,
                "txst rule: run 3's shape (78 prefixed, 14 unexplained) is prefixed, not NONE (" + text + ")");
            a_check(has(text, "'black.dds' x10 (6 another file, 4 zero;") && has(text, "'cubemap\\shinydull_e.dds' x4 (4 zero;"),
                "txst rule: the line groups the unexplained slots by name, TX05 included (" + text + ")");
        }
        {
            TR::Tally rooted;  // 'textures\fx\a.dds': its normalised form and the raw string hash alike
            Add(rooted, "Effects\\a.dds", Prefixed(), 5);
            TR::SlotEvidence both = Prefixed();
            both.matchesRaw = true;
            Add(rooted, "textures\\fx\\a.dds", both);
            a_check(TR::KindOf(both) == TR::Kind::kBoth && rooted.Decide() == TR::Rule::kPrefixed && rooted.Prefixed() == 5 &&
                        rooted.Unexplained() == 0,
                "txst rule: a rooted record string matching both forms does not vote, the rule stays prefixed");
            TR::Tally bareRooted;  // an engine hashing the bare name: the rooted slot must not split it either
            Add(bareRooted, "fx\\b.dds", Bare(), 4);
            Add(bareRooted, "textures\\fx\\a.dds", both);
            a_check(bareRooted.Decide() == TR::Rule::kBare, "txst rule: a slot matching both forms does not split bare votes");
        }
        {
            TR::Tally split;
            Add(split, "a.dds", Prefixed(), 50);
            Add(split, "textures\\b.dds", Bare());
            a_check(split.Decide() == TR::Rule::kNone, "txst rule: prefixed and bare votes together are NONE");
        }
        {
            TR::Tally zeros;
            Add(zeros, "a.dds", Zero(), 8);
            a_check(zeros.Decide() == TR::Rule::kNone, "txst rule: every stored ID zero is NONE (no zero rule)");
            Add(zeros, "black.dds", OtherFile(), 3);
            a_check(zeros.Decide() == TR::Rule::kNone, "txst rule: zero and another file with no vote is NONE");
        }
        {
            TR::Tally one;
            Add(one, "a.dds", Prefixed());
            Add(one, "black.dds", Zero(), 3);
            a_check(one.Decide() == TR::Rule::kPrefixed, "txst rule: one prefixed vote is enough (no floor)");
            TR::Tally bare;
            Add(bare, "a.dds", Bare(), 2);
            a_check(bare.Decide() == TR::Rule::kBare, "txst rule: bare votes alone are bare");
            a_check(TR::Tally{}.Decide() == TR::Rule::kNone && TR::Tally{}.Checked() == 0, "txst rule: no slots is NONE");
        }
        {
            TR::SlotEvidence zeroHash = Zero();
            zeroHash.matchesNormalized = true;  // a form that hashed to zero is no evidence
            TR::SlotEvidence skipped = Zero();
            skipped.shapeSkipped = true;  // a name Normalize refuses is reported as such, zero or not
            TR::SlotEvidence fault = Prefixed();
            fault.fault = true;
            a_check(TR::KindOf(zeroHash) == TR::Kind::kZero && TR::KindOf(skipped) == TR::Kind::kShapeSkipped &&
                        TR::KindOf(fault) == TR::Kind::kFault && TR::KindOf(OtherFile()) == TR::Kind::kOtherFile,
                "txst rule: a stored zero, a name Normalize refuses and a fault never vote");
        }
        a_check(TR::KeepsStored(true, true, false, true) && TR::KeepsStored(false, true, true, true) &&
                    TR::KeepsStored(false, false, false, true) && TR::KeepsStored(false, true, false, false) &&
                    !TR::KeepsStored(false, true, false, true),
            "txst rule: KeepsStored keeps a zero, the rule's own, an unmade or a reshaped one, and not another file's");
        {
            const std::string root(DP::kTexturesRoot);
            const auto shapeOf = [&](const std::string& a_name) {
                const auto path = DP::Normalize(a_name, DP::kTexturesRoot);
                return path && TR::NormalizeKeepsShape(a_name, *path, root);
            };
            a_check(DP::Normalize("textures\\fx\\a.dds", DP::kTexturesRoot).value_or("") == "textures\\fx\\a.dds",
                "txst rule: Normalize leaves a rooted 'textures\\fx\\a.dds' as it is (the both-forms case)");
            a_check(shapeOf("Effects\\RuneFire01.dds") && shapeOf("black.dds") && !shapeOf("textures\\fx\\a.dds") &&
                        !shapeOf("data\\textures\\fx\\a.dds") && !shapeOf("effects\\\\fx.dds") && !shapeOf("effects/fx.dds") &&
                        !shapeOf("\\effects\\fx.dds"),
                "txst rule: the shape is kept for a plain name only (no root, data\\, doubled, leading or '/' separator)");
        }
        {
            TR::Tally odd;
            Add(odd, "a.dds", Prefixed(), 2);
            TR::SlotEvidence skipped;
            skipped.shapeSkipped = true;
            TR::SlotEvidence fault = Prefixed();
            fault.fault = true;
            Add(odd, "c:\\x.dds", skipped);
            Add(odd, "b.dds", fault);
            const auto text = odd.Text();
            a_check(odd.Unexplained() == 2 && odd.Decide() == TR::Rule::kPrefixed && has(text, "(1 shape skipped, 1 faults)") &&
                        has(text, "'c:\\x.dds' x1 (1 skipped or faulted;"),
                "txst rule: shape-skipped and faulted slots are counted, named and unexplained (" + text + ")");
        }
        {
            TR::Tally many;
            for (int i = 0; i < 7; ++i) {
                Add(many, "n" + std::to_string(i) + ".dds", Zero());
            }
            a_check(has(many.Text(), "; 2 more names") && !has(many.Text(), "'n5.dds'"), "txst rule: the line names five, then counts the rest");
        }
    }
}
