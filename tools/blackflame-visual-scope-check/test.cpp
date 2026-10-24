#include "VisualSet.h"

#include <cstdio>
#include <cstdlib>

using namespace BlackFlame;

int main()
{
    int checks = 0;
    auto check = [&](bool a_ok, const char* a_label) {
        ++checks;
        if (!a_ok) {
            std::fprintf(stderr, "FAIL %s\n", a_label);
            std::exit(1);
        }
    };
    RE::TESDataHandler handler;
    RE::TESDataHandler::singleton = &handler;
    RE::SpellItem fire(1), other(2), shared(3), privateChild(4), equip(5), fireShared(6), wrongMember(7);
    shared.type = privateChild.type = equip.type = RE::MagicSystem::SpellType::kAbility;
    RE::EffectSetting damage(11), fear(12), otherMgef(13), sharedMgef(14), privateMgef(15),
        toShared(16), toPrivate(17), fromOther(18), cycle(19), toFireShared(20), fireSharedMgef(21);
    RE::BGSArtObject art(30);
    fear.data.castingArt = &art;
    fear.data.equipAbility = &equip;
    otherMgef.data.equipAbility = &equip;
    privateMgef.data.equipAbility = &equip;
    for (auto* effect : { &toShared, &toPrivate, &fromOther, &cycle, &toFireShared }) {
        effect->type = RE::EffectSetting::Archetype::kCloak;
    }
    toShared.data.associatedForm = &shared;
    toPrivate.data.associatedForm = &privateChild;
    fromOther.data.associatedForm = &shared;
    cycle.data.associatedForm = &privateChild;
    toFireShared.data.associatedForm = &fireShared;
    RE::Effect a{ &damage }, b{ &fear }, c{ &otherMgef }, d{ &sharedMgef }, e{ &privateMgef },
        f{ &toShared }, g{ &toPrivate }, h{ &fromOther }, i{ &cycle }, j{ &toFireShared }, k{ &fireSharedMgef };
    fire.effects = { &b, &a, &f, &g, nullptr };
    other.effects = { &c, &h, &j };
    shared.effects = { &d };
    privateChild.effects = { &e, &i };
    fireShared.effects = { &k };
    wrongMember.effects = { &a };
    handler.spells = { &fire, &other, &shared, &privateChild, &equip, &fireShared, &wrongMember, nullptr };

    BuildVisualSets({ 1, 6 });
    check(IsVisualSpell(&fire), "direct catalog root");
    check(IsVisualEffect(&fear, &fire), "nonfire MGEF included");
    check(!IsVisualEffect(&fear, &other), "unrelated spell denied");
    check(!IsVisualEffect(&otherMgef, &fire), "foreign MGEF denied even in catalog spell");
    check(!IsVisualEffect(&damage, &wrongMember), "noncatalog spell sharing selected MGEF denied");
    check(IsVisualSpell(&privateChild), "exclusive cloak child admitted");
    check(IsVisualEffect(&privateMgef, &privateChild), "exclusive child membership");
    check(!IsVisualSpell(&shared), "noncatalog shared child refused");
    check(!IsVisualSpell(&fireShared), "catalog root reused by noncatalog root refused");
    check(VisualSpellList().size() == 4, "candidates keep both ambiguous spells");
    check(IsVisualEffectForm(&sharedMgef), "shared MGEF remains copy candidate");
    check(!IsVisualEffectForm(&otherMgef), "noncatalog MGEF no copy candidate");
    check(IsVisualEquipAbility(&equip), "equip candidate");
    check(!IsVisualSpell(&equip), "equip relation never admits spell");
    check(VisualEquipOwners(&equip) == std::vector<RE::FormID>({ 1, 2 }), "all distinct owners including noncatalog");
    check(CostliestVisualCastingArtOf(&fire) == &art, "cached casting art");
    check(!IsVisualSpell(nullptr) && !IsVisualEffect(nullptr, &fire) && !IsVisualEffect(&fear, nullptr), "null checks");
    check(VisualEquipOwners(nullptr).empty(), "null owner query");

    BuildVisualSets({});
    check(VisualSpellList().empty() && !IsVisualSpell(&fire) && !IsVisualEffectForm(&fear) &&
        !IsVisualEquipAbility(&equip), "empty catalog failclosed and rebuild reset");
    check(VisualEquipOwners(&equip) == std::vector<RE::FormID>({ 1, 2 }), "owner metadata independent of catalog");
    check(CostliestVisualCastingArtOf(&fire) == nullptr, "casting cache reset");
    RE::TESDataHandler::singleton = nullptr;
    BuildVisualSets({ 1 });
    check(VisualEquipOwners(&equip).empty() && VisualSpellList().empty(), "no handler resets all state");
    std::printf("PASS %d checks; production VisualSet.cpp compiled directly\n", checks);
}
