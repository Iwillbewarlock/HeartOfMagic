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
// returns no ref: the actor is the new one close to the player (FindLeveledSpawn),
// looked for right after the command, before another sample's spawn.
// =============================================================================

namespace SelfTest = NpcSpellsGame::SelfTest;

namespace
{
    // placeatme puts the actor at the player: a new actor further away walked in (or was spawned by something else)
    constexpr float kSpawnAcceptRadius = 512.0f;  // game units
    constexpr std::size_t kMaxLoggedCandidates = 5;
    // Nested leveled NPC lists followed for the sample's entries: the sample is depth 0, so it and up to 4 lists inside
    // one another. A list met again is not followed twice (a list that contains itself, directly or not).
    constexpr std::size_t kMaxLevCharDepth = 4;
    constexpr const char* kPlaceAtMeCount = "1";

    // Every loaded actor: TES::ForEachReference (RE/T/TES.cpp:83; what ForEachReferenceInRange does with radius 0) walks
    // the interior cell, or the attached exterior grid cells, and the sky cell, each under its cell lock
    // (TESObjectCELL.cpp:13-22). Only the actor is taken there (a reference keeps it alive); the caller reads the rest
    // after the locks are released.
    std::vector<RE::NiPointer<RE::Actor>> LoadedActors()
    {
        std::vector<RE::NiPointer<RE::Actor>> out;
        if (auto* tes = RE::TES::GetSingleton()) {  // RE/T/TES.h:88
            tes->ForEachReference([&](RE::TESObjectREFR* ref) {
                if (auto* actor = ref ? ref->As<RE::Actor>() : nullptr) {
                    out.emplace_back(actor);
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }
        return out;
    }

    // The NPC records a leveled NPC list can give (RE/T/TESLeveledList.h:70-73; TESLevCharacter is a TESLeveledList,
    // RE/T/TESLevCharacter.h:10-13). Not TESLeveledList::GetContainedForms (TESLeveledList.cpp:14): it has no guard
    // against a list that contains itself, and it walks `entries` by SimpleArray's size(), which reads a count word
    // stored before the data (RE/M/MemoryManager.h, SimpleArray::size) that the engine's own record arrays need not
    // have; numEntries is the record's count (LLCT).
    void AddEntries(const RE::TESLevCharacter* lev, std::unordered_set<RE::FormID>& out, std::unordered_set<const RE::TESLevCharacter*>& seen,
        std::size_t depth)
    {
        const auto* entries = lev ? lev->entries.data() : nullptr;
        if (!entries || depth > kMaxLevCharDepth || !seen.insert(lev).second) {
            return;
        }
        for (std::uint8_t i = 0; i < lev->numEntries; ++i) {
            const auto* form = entries[i].form;
            if (!form) {
                continue;
            }
            if (const auto* nested = form->As<RE::TESLevCharacter>()) {
                AddEntries(nested, out, seen, depth + 1);
            } else {
                out.insert(form->GetFormID());
            }
        }
    }

    struct NewLeveled
    {
        RE::NiPointer<RE::Actor> actor;
        const RE::ExtraLeveledCreature* leveled = nullptr;  // RE/E/ExtraLeveledCreature.h:24-25
    };

    // Actors that were not loaded before the placeatme, are made at runtime (0xFF..., RE/T/TESForm.h:334), are not disabled
    // or marked for deletion (TESObjectREFR.cpp:810, :874; the test's own cleanup does both), were never spawned or matched
    // by this test, and stand within kSpawnAcceptRadius of the player; of them, those with ExtraLeveledCreature
    // (RE/T/TESObjectREFR.h:508). newActors: all of them, with or without the extra.
    std::vector<NewLeveled> NewLeveledActors(const SelfTest::State& s, const SelfTest::SampleRun& r, std::size_t& newActors)
    {
        std::vector<NewLeveled> out;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return out;
        }
        const auto origin = player->GetPosition();  // RE/T/TESObjectREFR.h:421
        for (auto& actor : LoadedActors()) {
            const auto id = actor->GetFormID();
            if (r.actorsBefore.contains(id) || s.spawnedIds.contains(id) || !actor->IsDynamicForm() || actor->IsDisabled() ||
                actor->IsMarkedForDeletion() || origin.GetSquaredDistance(actor->GetPosition()) > kSpawnAcceptRadius * kSpawnAcceptRadius) {
                continue;
            }
            ++newActors;
            if (const auto* leveled = actor->extraList.GetByType<RE::ExtraLeveledCreature>()) {
                out.push_back({ std::move(actor), leveled });
            }
        }
        return out;
    }

    // originalBase and templateBase are declared TESActorBase*, and a leveled spawn's originalBase may hold the LVLN record
    // itself: compared by FormID only (TESForm is the first base of both, at offset 0, and GetFormID is not virtual)
    RE::FormID IdOf(const RE::TESActorBase* base)
    {
        return base ? static_cast<const RE::TESForm*>(base)->GetFormID() : 0;
    }
}

bool SelfTest::PlaceLeveled(State& s, SampleRun& r, RE::PlayerCharacter* player, std::string& error)
{
    r.entries.clear();
    std::unordered_set<const RE::TESLevCharacter*> seen;
    AddEntries(r.placed ? r.placed->As<RE::TESLevCharacter>() : nullptr, r.entries, seen, 0);
    r.actorsBefore.clear();
    for (const auto& actor : LoadedActors()) {
        r.actorsBefore.insert(actor->GetFormID());
    }
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
    Log(s, std::format("SELFTEST spawn sample {} kind=leveled console \"player.{}\" actorsLoadedBefore={} sampleEntries={}", r.label, command,
        r.actorsBefore.size(), r.entries.size()));
    // At once: the command placed the actor on this main-thread call (run 7: foundAfterMs=0), and the next sample's
    // snapshot would count it as already there. Not found: the ticks look again (the fallback).
    if (FindLeveledSpawn(s, r, 0) == NpcSpells::LeveledSpawnMatch::kNone) {
        r.spawnPending = true;
        ++s.pendingSpawns;
    }
    return true;
}

NpcSpells::LeveledSpawnMatch SelfTest::FindLeveledSpawn(State& s, SampleRun& r, std::size_t waitedTicks)
{
    std::size_t newActors = 0;
    auto found = NewLeveledActors(s, r, newActors);
    std::vector<NpcSpells::LeveledSpawnCandidate> candidates;
    const auto sampleId = r.placed ? r.placed->GetFormID() : 0;
    for (const auto& c : found) {
        const auto original = IdOf(c.leveled->originalBase);
        const auto templ = IdOf(c.leveled->templateBase);
        candidates.push_back({ c.actor->GetFormID(), original != 0 && original == sampleId, original != 0 && r.entries.contains(original),
            templ != 0 && r.entries.contains(templ) });
    }
    const auto choice = NpcSpells::ChooseLeveledSpawn(candidates);
    if (choice.match == NpcSpells::LeveledSpawnMatch::kNone) {
        return choice.match;
    }
    auto* actor = found[choice.index].actor.get();
    const auto* leveled = found[choice.index].leveled;
    const auto id = actor->GetFormID();
    s.spawnedIds.insert(id);
    ClearSpawnPending(s, r);
    // templateBase: the NPC the engine drew from the list (GetTemplateBase, RE/A/Actor.h:604, Actor.cpp:754)
    Log(s, std::format("SELFTEST spawn sample {} kind=leveled ref 0x{:08X} match={} matching={} leveledCandidates={} newActors={} sampleEntries={} "
                       "actorBase {} templateBase {} originalBase {} foundAfterMs={}",
        r.label, id, NpcSpells::LeveledSpawnMatchName(choice.match), choice.matching, candidates.size(), newActors, r.entries.size(),
        FormText(actor->GetActorBase()), FormText(leveled->templateBase), FormText(leveled->originalBase), waitedTicks * NpcSpells::kSelfTestPollMs));
    if (choice.match == NpcSpells::LeveledSpawnMatch::kOnlyNew) {
        // Neither of its bases is the sample or one of its entries: it may be another spawn's or a walk-in's actor, so it
        // is not checked and not deleted
        r.onlyNew = true;
        ++s.leftActors;
        Warn(s, std::format("SELFTEST WARN sample {} ref 0x{:08X} matched only as the one new leveled actor: not checked, left in the world", r.label, id));
        return choice.match;
    }
    r.handle = actor->GetHandle();  // RE/T/TESObjectREFR.h:402
    r.spawnedId = id;
    ++s.liveActors;
    return choice.match;
}

std::string SelfTest::LeveledSpawnMissing(State& s, SampleRun& r, std::size_t waitedTicks)
{
    std::size_t newActors = 0;
    const auto found = NewLeveledActors(s, r, newActors);
    for (std::size_t i = 0; i < found.size() && i < kMaxLoggedCandidates; ++i) {
        Log(s, std::format("SELFTEST leveled candidate {} ref 0x{:08X} actorBase {} templateBase {} originalBase {}", r.label, found[i].actor->GetFormID(),
            FormText(found[i].actor->GetActorBase()), FormText(found[i].leveled->templateBase), FormText(found[i].leveled->originalBase)));
    }
    ClearSpawnPending(s, r);
    s.leftActors += static_cast<int>(found.size());
    return std::format("leveled spawn wait: no actor of console placeatme {:08X} found within {} ms (new actors within {} units of the player {}, with "
                       "ExtraLeveledCreature {}: none, or several and none matching; they are left in the world)",
        r.placed ? r.placed->GetFormID() : 0, waitedTicks * NpcSpells::kSelfTestPollMs, static_cast<int>(kSpawnAcceptRadius), newActors, found.size());
}
