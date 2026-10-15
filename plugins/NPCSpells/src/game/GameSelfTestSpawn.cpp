#include "game/GameSelfTestState.h"

#include "Common.h"

#include <format>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

// =============================================================================
// NPC SPELLS - DEV SELF TEST: THE LEVELED SPAWN
// =============================================================================
// TESObjectREFR::PlaceObjectAtMe (RE/T/TESObjectREFR.h:475) is TESDataHandler::
// CreateReferenceAtLocation (TESObjectREFR.cpp:969), which gave no actor for a
// TESLevCharacter (in-game run 6). CommonLib has no other PlaceAtMe. The
// console's "player.placeatme <LVLN>" spawns a leveled actor (run 5), so the
// same command runs here as a console script on the player (RE::Script). It
// returns no ref: the actor is the new one near the player (see FindLeveledSpawn).
// =============================================================================

namespace SelfTest = NpcSpellsGame::SelfTest;

namespace
{
    constexpr float kSpawnSearchRadius = 2048.0f;  // game units around the player; placeatme puts the actor at the player
    constexpr std::size_t kMaxLoggedCandidates = 5;
    constexpr std::size_t kMaxLevCharDepth = 4;    // nested leveled NPC lists followed for the sample's entries
    constexpr const char* kPlaceAtMeCount = "1";

    // Every actor in range: TES::ForEachReferenceInRange (RE/T/TES.h:93, TES.cpp:115) walks the interior cell, or the
    // attached exterior grid cells and the sky cell, each under its cell lock (TESObjectCELL.cpp:13-33)
    template <class F>
    void ForEachActorNear(RE::PlayerCharacter* player, F&& visit)
    {
        auto* tes = RE::TES::GetSingleton();  // RE/T/TES.h:88
        if (!tes || !player) {
            return;
        }
        tes->ForEachReferenceInRange(player, kSpawnSearchRadius, [&](RE::TESObjectREFR* ref) {
            if (auto* actor = ref ? ref->As<RE::Actor>() : nullptr) {
                visit(actor);
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
    }

    // The NPC records a leveled NPC list can give (RE/T/TESLeveledList.h:70-73; TESLevCharacter is a TESLeveledList,
    // RE/T/TESLevCharacter.h:10-13), nested lists followed
    void AddEntries(const RE::TESLevCharacter* lev, std::unordered_set<RE::FormID>& out, std::size_t depth)
    {
        const auto* entries = lev ? lev->entries.data() : nullptr;
        if (!entries || depth > kMaxLevCharDepth) {
            return;
        }
        for (std::uint8_t i = 0; i < lev->numEntries; ++i) {
            const auto* form = entries[i].form;
            if (!form) {
                continue;
            }
            if (const auto* nested = form->As<RE::TESLevCharacter>()) {
                AddEntries(nested, out, depth + 1);
            } else {
                out.insert(form->GetFormID());
            }
        }
    }

    struct NewLeveled
    {
        RE::Actor* actor = nullptr;
        const RE::ExtraLeveledCreature* leveled = nullptr;  // RE/E/ExtraLeveledCreature.h:24-25
    };

    // Actors near the player that were not there before the placeatme, are made at runtime (0xFF..., RE/T/TESForm.h:334)
    // and are no other sample's; of them, those with ExtraLeveledCreature (RE/T/TESObjectREFR.h:508)
    std::vector<NewLeveled> NewLeveledActors(const SelfTest::State& s, const SelfTest::SampleRun& r, std::size_t& newActors)
    {
        std::unordered_set<RE::FormID> claimed;
        for (const auto& other : s.samples) {
            if (&other != &r && other.spawnedId != 0) {
                claimed.insert(other.spawnedId);
            }
        }
        std::vector<NewLeveled> out;
        ForEachActorNear(RE::PlayerCharacter::GetSingleton(), [&](RE::Actor* actor) {
            const auto id = actor->GetFormID();
            if (r.actorsBefore.contains(id) || claimed.contains(id) || !actor->IsDynamicForm()) {
                return;
            }
            ++newActors;
            if (const auto* leveled = actor->extraList.GetByType<RE::ExtraLeveledCreature>()) {
                out.push_back({ actor, leveled });
            }
        });
        return out;
    }

    // originalBase is declared TESActorBase*, but on a leveled spawn it may hold the LVLN record: compared as an address
    // only (TESForm is the first base of both, so the same object has the same address)
    bool IsSample(const RE::ExtraLeveledCreature* leveled, const RE::TESBoundObject* placed)
    {
        return static_cast<const void*>(leveled->originalBase) == static_cast<const void*>(static_cast<const RE::TESForm*>(placed));
    }
}

bool SelfTest::PlaceLeveled(State& s, SampleRun& r, RE::PlayerCharacter* player, std::string& error)
{
    r.actorsBefore.clear();
    ForEachActorNear(player, [&](RE::Actor* actor) { r.actorsBefore.insert(actor->GetFormID()); });
    // RE/C/ConcreteFormFactory.h:22 Create, :34 GetConcreteFormFactoryByType (Script::FORMTYPE, RE/S/Script.h:28). A game
    // form: unique_ptr's delete is its virtual deleting destructor, TESForm's heap operators from BaseFormComponent
    // (RE/B/BaseFormComponent.h:20, TES_HEAP_REDEFINE_NEW)
    auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
    std::unique_ptr<RE::Script> script(factory ? factory->Create() : nullptr);
    if (!script) {
        error = "leveled spawn: no console script form (no Script form factory, or no memory)";
        return false;
    }
    const auto command = std::format("placeatme {:08X} {}", r.placed->GetFormID(), kPlaceAtMeCount);
    script->SetCommand(command);    // RE/S/Script.h:52, Script.cpp:46 (game heap copy of the text)
    script->CompileAndRun(player);  // RE/S/Script.h:49, Script.cpp:30: compiled and run on the player, as "player.placeatme"
    script->ClearCommand();         // RE/S/Script.h:48, Script.cpp:22: frees the text and nulls it before the form goes
    r.spawnPending = true;
    Log(s, std::format("SELFTEST spawn sample {} kind=leveled console \"player.{}\" actorsNearBefore={}", r.label, command, r.actorsBefore.size()));
    return true;
}

bool SelfTest::FindLeveledSpawn(State& s, SampleRun& r, std::size_t waitedTicks)
{
    std::size_t newActors = 0;
    const auto found = NewLeveledActors(s, r, newActors);
    std::unordered_set<RE::FormID> entries;
    AddEntries(r.placed ? r.placed->As<RE::TESLevCharacter>() : nullptr, entries, 0);
    std::vector<NpcSpells::LeveledSpawnCandidate> candidates;
    for (const auto& c : found) {
        candidates.push_back({ c.actor->GetFormID(), IsSample(c.leveled, r.placed),
            c.leveled->templateBase && entries.contains(c.leveled->templateBase->GetFormID()) });
    }
    const auto choice = NpcSpells::ChooseLeveledSpawn(candidates);
    if (choice.match == NpcSpells::LeveledSpawnMatch::kNone) {
        return false;
    }
    auto* actor = found[choice.index].actor;
    const auto* leveled = found[choice.index].leveled;
    r.handle = actor->GetHandle();  // RE/T/TESObjectREFR.h:402
    r.spawnedId = actor->GetFormID();
    r.spawnPending = false;
    r.actorsBefore.clear();
    ++s.liveActors;
    // templateBase: the NPC the engine drew from the list (GetTemplateBase, RE/A/Actor.h:604, Actor.cpp:754)
    Log(s, std::format("SELFTEST spawn sample {} kind=leveled ref 0x{:08X} match={} matching={} leveledCandidates={} newActors={} sampleEntries={} "
                       "actorBase {} templateBase {} originalBase {} foundAfterMs={}",
        r.label, r.spawnedId, NpcSpells::LeveledSpawnMatchName(choice.match), choice.matching, candidates.size(), newActors, entries.size(),
        FormText(actor->GetActorBase()), FormText(leveled->templateBase), FormText(leveled->originalBase), waitedTicks * NpcSpells::kSelfTestPollMs));
    return true;
}

std::string SelfTest::LeveledSpawnMissing(State& s, SampleRun& r, std::size_t waitedTicks)
{
    std::size_t newActors = 0;
    const auto found = NewLeveledActors(s, r, newActors);
    for (std::size_t i = 0; i < found.size() && i < kMaxLoggedCandidates; ++i) {
        Log(s, std::format("SELFTEST leveled candidate {} ref 0x{:08X} actorBase {} templateBase {} originalBase {}", r.label, found[i].actor->GetFormID(),
            FormText(found[i].actor->GetActorBase()), FormText(found[i].leveled->templateBase), FormText(found[i].leveled->originalBase)));
    }
    r.spawnPending = false;
    r.actorsBefore.clear();
    return std::format("leveled spawn: no actor of console placeatme {:08X} found within {} ms (new actors near the player {}, with ExtraLeveledCreature {}: "
                       "none, or several and none matching; they are left in the world)",
        r.placed ? r.placed->GetFormID() : 0, waitedTicks * NpcSpells::kSelfTestPollMs, newActors, found.size());
}
