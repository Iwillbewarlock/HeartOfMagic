#pragma once

template<class Check>
void RunContextTests(Check&& check) {
    using namespace BlackFlame;
    {
        Fixture f;
        check(IsPlayerVisualEquipEffect(&f.effect), "actual context accepts selected fire owner");
        f.player.runtimeData.selectedSpells[0] = &f.parentNonfire;
        check(!IsPlayerVisualEquipEffect(&f.effect), "actual context rejects nonfire owner immediately without waiting for Tick");
        f.run();
        check(f.untouched(), "original shared ability owned by nonfire remains untouched");
    }
    {
        Fixture f;
        f.player.runtimeData.selectedSpells[1] = &f.parentNonfire;
        check(!IsPlayerVisualEquipEffect(&f.effect), "actual context rejects mixed ownership");
        f.run();
        check(f.untouched(), "mixed shared ownership preserves original visuals");
    }
    {
        Fixture f;
        f.player.runtimeData.selectedSpells[1] = &f.unrelated;
        check(IsPlayerVisualEquipEffect(&f.effect), "unrelated nonfire root does not veto selected fire owner");
    }
    {
        Fixture f;
        f.player.runtimeData.selectedSpells[0] = nullptr;
        f.player.runtimeData.selectedSpells[1] = &f.parentFire;
        check(IsPlayerVisualEquipEffect(&f.effect), "actual context accepts fire owner in right hand");
    }
    const auto rejected = [&](const char* name, auto setup) {
        Fixture f;
        setup(f);
        check(!IsPlayerVisualEquipEffect(&f.effect), name);
    };
    rejected("context rejects NPC caster", [](Fixture& f) { f.effect.caster.value = 2; });
    rejected("context rejects null caster", [](Fixture& f) { f.effect.caster.value = 0; });
    rejected("context rejects foreign target", [](Fixture& f) { f.effect.target = nullptr; });
    rejected("context rejects inactive effect", [](Fixture& f) { f.effect.flags.bits = 2; });
    rejected("context rejects dispelled effect", [](Fixture& f) { f.effect.flags.bits = 1; });
    rejected("context rejects missing player", [](Fixture&) { RE::PlayerCharacter::singleton = nullptr; });
    rejected("context rejects unrelated ability", [](Fixture&) { abilityOwners.clear(); });
    rejected("context rejects unequipped owners", [](Fixture& f) { f.player.runtimeData.selectedSpells = {}; });
    {
        Fixture f;
        check(!IsPlayerVisualEquipEffect(nullptr), "context rejects null effect");
        stoked = false;
        check(IsPlayerVisualEquipEffect(&f.effect), "context retains attribution with buff off so original-art refresh can occur");
    }
    {
        Fixture f;
        f.player.runtimeData.selectedSpells[0] = &f.parentNonfire;
        f.model.artObject = &f.copyArt;
        f.run();
        check(!IsPlayerVisualEquipEffect(&f.effect) && f.effect.starts == 1 && f.model.detaches == 1 &&
            f.player.removeCalls == 0 && f.player.addCalls == 0,
            "nonfire transition restores old copy via visual-only restart");
    }
    {
        Fixture f;
        f.player.runtimeData.selectedSpells[1] = &f.parentNonfire;
        f.model.artObject = &f.copyArt;
        f.run();
        check(!IsPlayerVisualEquipEffect(&f.effect) && f.effect.starts == 1,
            "mixed transition restores old copy despite loss of grey eligibility");
    }
    {
        Fixture f;
        f.player.runtimeData.selectedSpells[0] = &f.parentNonfire;
        f.shader.effectData = &f.burnShader;
        f.effect.hitEffectController.hasArt = false;
        f.effect.hitEffectController.hasShader = true;
        f.run();
        check(f.effect.starts == 1 && f.shader.detaches == 1, "old burn shader restores under nonfire owner without hit art");
    }
    {
        Fixture f;
        f.effect.hitEffectController.hasArt = false;
        f.effect.hitEffectController.hasShader = true;
        f.lists.temps = { &f.shader };
        f.hits = { &f.shader };
        testConfig.copyHitArt = false;
        testConfig.burnShader = true;
        f.run();
        check(f.effect.starts == 1 && f.shader.detaches == 1, "shader-only eligible ability refreshes with shader setting enabled");
    }
    {
        Fixture f;
        f.model.artObject = &f.copyArt;
        stoked = false;
        f.run(false);
        check(f.effect.starts == 1 && f.player.removeCalls == 0 && f.player.addCalls == 0,
            "buff-off copy restoration preserves gameplay spell state");
    }
    {
        Fixture f;
        TickVisualEquipContext();
        check(MainQueue::pending.size() == 1 && f.untouched(), "selection change queues refresh without immediate engine mutation");
        TickVisualEquipContext();
        check(MainQueue::pending.size() == 1, "unchanged selection does not queue duplicate refresh");
        f.player.runtimeData.selectedSpells[0] = &f.parentNonfire;
        TickVisualEquipContext();
        check(MainQueue::pending.size() == 2, "nonfire selection transition queues restoration");
        f.model.artObject = &f.copyArt;
        MainQueue::Drain();
        check(f.effect.starts == 1 && !IsPlayerVisualEquipEffect(&f.effect),
            "queued earlier fire selection uses current nonfire attribution and restores old copy once");
    }
    {
        Fixture f;
        TickVisualEquipContext();
        MainQueue::pending.clear();
        ++testGeneration;
        TickVisualEquipContext();
        check(MainQueue::pending.size() == 1, "world generation change queues refresh with unchanged selection");
        MainQueue::pending.clear();
        ResetVisualEquipContext();
        TickVisualEquipContext();
        check(MainQueue::pending.size() == 1, "reset forgets previous selected identity");
    }
    const auto noQueue = [&](const char* name, auto setup) {
        Fixture f;
        setup(f);
        TickVisualEquipContext();
        check(MainQueue::pending.empty() && f.untouched(), name);
    };
    noQueue("context tick skips uninstalled queue", [](Fixture&) { MainQueue::installed = false; });
    noQueue("context tick skips wrong thread", [](Fixture&) { onGameThread = false; });
    noQueue("context tick skips missing player", [](Fixture&) { RE::PlayerCharacter::singleton = nullptr; });
}
