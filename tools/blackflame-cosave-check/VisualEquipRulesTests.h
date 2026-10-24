#pragma once

#include "../../plugins/BlackFlame/src/VisualEquipRules.h"

namespace VisualEquipRulesTests
{
    template <class Check>
    void Run(Check&& a_check)
    {
        namespace R = BlackFlame::VisualEquipRules;
        constexpr R::FormID fire = 0x0007A82B;
        constexpr R::FormID illusion = 0x0007E8E1;
        constexpr R::FormID unrelated = 0x00012FD2;
        const std::vector<R::FormID> shared{ fire, illusion };

        a_check(R::Allows(shared, { { fire, true } }), "shared ability: fire owner selected alone allows grey");
        a_check(!R::Allows(shared, { { illusion, false } }), "shared ability: nonfire owner selected alone stays original");
        a_check(!R::Allows(shared, { { fire, true }, { illusion, false } }), "shared ability: mixed owners stay original");
        a_check(!R::Allows(shared, { { illusion, false }, { fire, true } }), "shared ability: mixed owner order does not matter");
        a_check(R::Allows(shared, { { fire, true }, { unrelated, false } }), "unrelated nonfire selection does not veto fire owner");
        a_check(R::Allows(shared, { { unrelated, false }, { fire, true } }), "fire owner in second slot also allows grey");
        a_check(R::Allows(shared, { { fire, true }, { fire, true } }), "duplicate visual selection allows grey");
        a_check(R::Allows(shared, { { fire, true }, { illusion, true } }), "two visual owners allow grey");
        a_check(!R::Allows(shared, {}), "no selection stays original");
        a_check(!R::Allows({}, { { fire, true } }), "no owners stays original");
        a_check(!R::Allows(shared, { { unrelated, true } }), "unrelated visual spell does not claim shared ability");
        a_check(!R::Allows(shared, { { unrelated, false } }), "unrelated nonfire spell does not claim shared ability");
        a_check(!R::Allows({ 0 }, { { 0, true } }), "zero form IDs never establish ownership");
        a_check(R::Allows({ 0, fire, fire }, { { 0, false }, { fire, true } }), "zero and duplicate owner entries are harmless");
        a_check(!R::Allows(shared, { { fire, true }, { fire, false } }), "contradictory selected facts conservatively stay original");

        std::vector<R::SelectedRoot> selected{ { fire, true } };
        a_check(R::Allows(shared, selected), "selection transition begins eligible");
        selected = { { illusion, false } };
        a_check(!R::Allows(shared, selected), "selection transition to nonfire does not retain old attribution");
        selected = { { fire, true }, { illusion, false } };
        a_check(!R::Allows(shared, selected), "selection transition to mixed does not retain old attribution");
        selected.clear();
        a_check(!R::Allows(shared, selected), "unequipping all roots removes attribution");
        selected = { { fire, true } };
        a_check(R::Allows(shared, selected), "reselecting fire restores attribution");

    }
}
