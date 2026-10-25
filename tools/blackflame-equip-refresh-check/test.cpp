#include "FakeEngine.h"
#include <iostream>

struct Fixture {
    RE::SpellItem fire;
    RE::SpellItem parentFire, parentNonfire, unrelated, copyArt, burnShader;
    RE::ActiveEffect effect;
    RE::ModelReferenceEffect model;
    RE::ShaderReferenceEffect shader;
    std::vector<RE::ReferenceEffect*> hits;
    RE::PlayerCharacter player;
    RE::ProcessLists lists;
    Fixture() {
        BlackFlame::ResetVisualEquipContext();
        parentFire.id = 0x7A82B;
        parentNonfire.id = 0x7E8E1;
        unrelated.id = 0x12FD2;
        fire.id = 0x225AE;
        copyArt.copied = true;
        BlackFlame::visualSpells = { parentFire.id };
        BlackFlame::abilityOwners = { { fire.id, { parentFire.id, parentNonfire.id } } };
        BlackFlame::forms.burnShader = &burnShader;
        player.runtimeData.selectedSpells[0] = &parentFire;
        BlackFlame::testConfig = {};
        BlackFlame::stoked = true;
        BlackFlame::onGameThread = true;
        BlackFlame::testGeneration = 0;
        BlackFlame::Patch::hasIds = true;
        BlackFlame::MainQueue::accepts = true;
        BlackFlame::MainQueue::installed = true;
        BlackFlame::MainQueue::pending.clear();
        REL::runtime = REL::Version(1, 6, 1170, 0);
        REL::lastRelocation = 0;
        RE::PlayerCharacter::singleton = &player;
        RE::ProcessLists::singleton = &lists;
        effect.spell = &fire;
        effect.target = &player;
        effect.hitEffectController.effect = &effect;
        model.controller = &effect.hitEffectController;
        shader.controller = &effect.hitEffectController;
        hits = { &model, &shader };
        effect.hitEffects = &hits;
        player.effects = { &effect };
        lists.temps = { &model, &shader };
    }
    void run(bool state = true) {
        BlackFlame::RequestEquipAbilityRefresh(state);
        BlackFlame::MainQueue::Drain();
    }
    bool untouched() const {
        return effect.starts == 0 && effect.hitEffectController.stops == 0 && model.detaches == 0 &&
            shader.detaches == 0 && player.removeCalls == 0 && player.addCalls == 0 && effect.oldVisualPresent;
    }
};

#include "ContextTests.h"

int main() {
    int failures = 0;
    int checks = 0;
    const auto check = [&](bool condition, const char* name) {
        ++checks;
        std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
        failures += !condition;
    };
    {
        Fixture f;
        BlackFlame::RequestEquipAbilityRefresh(true);
        check(BlackFlame::MainQueue::pending.size() == 1 && f.untouched(), "request queues once without immediate mutation");
        BlackFlame::MainQueue::Drain();
        check(f.effect.starts == 1 && !f.effect.oldVisualPresent && REL::lastRelocation == 34094,
            "HasSpell=false requests one visual restart using AE34094");
        check(f.player.removeCalls == 0 && f.player.addCalls == 0 && f.player.effects[0] == &f.effect,
            "unlisted ability preserves gameplay active effect and spell ownership");
        check(f.model.finished && f.shader.finished && f.model.detaches == 1 && f.shader.detaches == 1 &&
            f.model.detachedAfterStop && f.shader.detachedAfterStop && f.hits.empty() &&
            f.model.controller == nullptr && f.shader.controller == nullptr && !f.lists.mutationUnderLock,
            "all old model/shader refs finish and detach after Stop outside manager iteration lock");
    }
    {
        Fixture f;
        BlackFlame::stoked = false;
        f.run(false);
        check(f.effect.starts == 1 && f.player.addCalls == 0, "buff-off edge refreshes unlisted ability without adding spell");
    }
    {
        Fixture f;
        f.player.knownSpell = true;
        f.player.effects.push_back(&f.effect);
        f.run();
        check(f.player.removeCalls == 0 && f.player.addCalls == 0 && f.effect.starts == 1,
            "listed ability preserves gameplay and refreshes visuals once despite duplicate active-effect entries");
    }
    {
        Fixture f;
        f.player.knownSpell = true;
        f.player.removeSucceeds = false;
        f.run();
        check(f.player.removeCalls == 0 && f.player.addCalls == 0 && f.effect.starts == 1,
            "RemoveSpell capability is irrelevant to visual-only refresh");
    }
    const auto skip = [&](const char* name, auto setup) {
        Fixture f;
        setup(f);
        f.run();
        check(f.untouched(), name);
    };
    const auto restartsOn = [&](REL::Version a_runtime, const char* name) {
        Fixture f;
        REL::runtime = a_runtime;
        f.run();
        check(f.effect.starts == 1 && !f.effect.oldVisualPresent && REL::lastRelocation == 34094, name);
    };
    check(BlackFlame::Patch::IsCodeReadVersion(REL::Version(1, 6, 1170, 0)), "IsCodeReadVersion: AE 1.6.1170 is code read");
    check(BlackFlame::Patch::IsCodeReadVersion(REL::Version(1, 7, 104, 0)), "IsCodeReadVersion: AE 1.7.104 is code read");
    check(!BlackFlame::Patch::IsCodeReadVersion(REL::Version(1, 6, 640, 0)), "IsCodeReadVersion: AE 1.6.640 is not");
    check(!BlackFlame::Patch::IsCodeReadVersion(REL::Version(1, 7, 99, 0)), "IsCodeReadVersion: AE 1.7.99 is not");
    check(!BlackFlame::Patch::IsCodeReadVersion(REL::Version(1, 5, 97, 0)), "IsCodeReadVersion: SE 1.5.97 is not");
    restartsOn(REL::Version(1, 6, 1170, 0), "AE 1.6.1170 (code read) restarts visuals using AE34094");
    restartsOn(REL::Version(1, 7, 104, 0), "AE 1.7.104 (code read) restarts visuals using AE34094");
    skip("AE 1.6.640 (code not read) does not mutate", [](Fixture&) { REL::runtime = REL::Version(1, 6, 640, 0); });
    skip("SE 1.5.97 does not mutate", [](Fixture&) { REL::runtime = REL::Version(1, 5, 97, 0); });
    skip("missing relocation ID does not mutate", [](Fixture&) { BlackFlame::Patch::hasIds = false; });
    skip("foreign caster does not mutate", [](Fixture& f) { f.effect.caster.value = 2; });
    skip("null caster does not mutate", [](Fixture& f) { f.effect.caster.value = 0; });
    skip("foreign target does not mutate", [](Fixture& f) { f.effect.target = nullptr; });
    skip("inactive effect does not mutate", [](Fixture& f) { f.effect.flags.bits = 2; });
    skip("dispelled effect does not mutate", [](Fixture& f) { f.effect.flags.bits = 1; });
    skip("non-visual ability does not mutate", [](Fixture&) { BlackFlame::abilityOwners.clear(); });
    skip("foreign-owned ref blocks complete refresh", [](Fixture& f) { f.shader.ownController = true; });
    skip("no existing visuals blocks refresh", [](Fixture& f) { f.lists.temps.clear(); });
    skip("mismatched controller owner blocks refresh", [](Fixture& f) { f.effect.hitEffectController.effect = nullptr; });
    skip("no hit art or shader blocks refresh", [](Fixture& f) { f.effect.hitEffectController.hasArt = false; });
    skip("no player 3D blocks refresh", [](Fixture& f) { f.player.has3D = false; });
    skip("no process lists blocks refresh", [](Fixture&) { RE::ProcessLists::singleton = nullptr; });
    skip("no player blocks refresh", [](Fixture&) { RE::PlayerCharacter::singleton = nullptr; });
    skip("disabled art blocks refresh", [](Fixture&) { BlackFlame::testConfig.copyHitArt = false; });
    skip("uninstalled main queue blocks engine mutation", [](Fixture&) { BlackFlame::MainQueue::installed = false; });
    skip("wrong thread blocks engine mutation", [](Fixture&) { BlackFlame::onGameThread = false; });
    skip("rejected queue blocks engine mutation", [](Fixture&) { BlackFlame::MainQueue::accepts = false; });
    {
        Fixture f;
        BlackFlame::RequestEquipAbilityRefresh(true);
        ++BlackFlame::testGeneration;
        BlackFlame::MainQueue::Drain();
        check(f.untouched(), "queued refresh from old world generation is discarded");
    }
    {
        Fixture f;
        BlackFlame::RequestEquipAbilityRefresh(true);
        BlackFlame::stoked = false;
        BlackFlame::MainQueue::Drain();
        check(f.untouched(), "queued refresh from old buff state is discarded");
    }
    {
        Fixture f;
        f.model.removeHitListOnDetach = false;
        f.run();
        check(f.effect.hitEffectController.stops == 1 && f.model.detaches == 1 && f.effect.starts == 0,
            "lingering hitEffects entry prevents restart after detach");
    }
    {
        Fixture f;
        f.model.clearControllerOnDetach = false;
        f.run();
        check(f.hits.empty() && f.model.controller != nullptr && f.effect.starts == 0,
            "lingering old controller prevents restart even when hitEffects is empty");
    }
    RunContextTests(check);
    std::cout << checks << " checks, " << failures << " failures. Fake engine verifies calls/guards, not engine ABI, native lifetime, or rendered pixels.\n";
    return failures ? 1 : 0;
}
