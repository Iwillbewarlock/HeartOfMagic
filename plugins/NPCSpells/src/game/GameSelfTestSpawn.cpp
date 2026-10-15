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
// returns no ref: the actor is the new one close to the player whose base is an
// entry of the LVLN (FindLeveledSpawn; in-game run 8: no ExtraLeveledCreature),
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

    struct NewActor
    {
        RE::NiPointer<RE::Actor> actor;
        const RE::ExtraLeveledCreature* leveled = nullptr;  // RE/E/ExtraLeveledCreature.h:24-25; null without the extra
        float distance = 0.0f;                              // from the player, game units
    };

    // Actors that were not loaded before the placeatme, are made at runtime (0xFF..., RE/T/TESForm.h:334), are not disabled
    // or marked for deletion (TESObjectREFR.cpp:810, :874; the test's own cleanup does both), were never spawned, matched
    // or counted as left by this test, and stand within kSpawnAcceptRadius of the player (any distance with anyDistance:
    // the failure log). With or without ExtraLeveledCreature (RE/T/TESObjectREFR.h:508): the console's placeatme of an
    // LVLN gave an actor whose base is the drawn entry NPC record, without the extra (in-game run 8, 2026-10-16).
    std::vector<NewActor> NewActors(const SelfTest::State& s, const SelfTest::SampleRun& r, bool anyDistance = false)
    {
        std::vector<NewActor> out;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return out;
        }
        const auto origin = player->GetPosition();  // RE/T/TESObjectREFR.h:421
        for (auto& actor : LoadedActors()) {
            const auto id = actor->GetFormID();
            if (r.actorsBefore.contains(id) || s.spawnedIds.contains(id) || !actor->IsDynamicForm() || actor->IsDisabled() || actor->IsMarkedForDeletion()) {
                continue;
            }
            const auto distance = origin.GetDistance(actor->GetPosition());  // RE/N/NiPoint3.h:45
            if (!anyDistance && distance > kSpawnAcceptRadius) {
                continue;
            }
            const auto* leveled = actor->extraList.GetByType<RE::ExtraLeveledCreature>();
            out.push_back({ std::move(actor), leveled, distance });
        }
        return out;
    }

    // originalBase and templateBase are declared TESActorBase*, and a leveled spawn's originalBase may hold the LVLN record
    // itself: compared by FormID only (TESForm is the first base of both, at offset 0, and GetFormID is not virtual)
    RE::FormID IdOf(const RE::TESActorBase* base)
    {
        return base ? static_cast<const RE::TESForm*>(base)->GetFormID() : 0;
    }

    NpcSpells::LeveledSpawnCandidate ToCandidate(const NewActor& a, const SelfTest::SampleRun& r)
    {
        NpcSpells::LeveledSpawnCandidate c;
        c.refId = a.actor->GetFormID();
        const auto base = IdOf(a.actor->GetActorBase());  // RE/A/Actor.h:556
        c.baseIsEntry = base != 0 && r.entries.contains(base);
        if (a.leveled) {
            const auto original = IdOf(a.leveled->originalBase);
            const auto templ = IdOf(a.leveled->templateBase);
            const auto sampleId = r.placed ? r.placed->GetFormID() : 0;
            c.originalIsSample = original != 0 && original == sampleId;
            c.originalIsEntry = original != 0 && r.entries.contains(original);
            c.templateIsEntry = templ != 0 && r.entries.contains(templ);
        }
        return c;
    }

    // "actorBase X extraLeveled=no", or with the extra its template base (the NPC the engine drew, GetTemplateBase,
    // RE/A/Actor.h:604, Actor.cpp:754) and its original base
    std::string BasesText(const NewActor& a)
    {
        auto out = std::format("actorBase {} extraLeveled={}", SelfTest::FormText(a.actor->GetActorBase()), a.leveled ? "yes" : "no");
        if (a.leveled) {
            out += std::format(" templateBase {} originalBase {}", SelfTest::FormText(a.leveled->templateBase), SelfTest::FormText(a.leveled->originalBase));
        }
        return out;
    }

    // An actor the test leaves in the world: counted for the save warning and the WARN before DONE, and never another
    // leveled spawn's candidate
    void LeaveInWorld(SelfTest::State& s, RE::FormID id)
    {
        s.spawnedIds.insert(id);
        ++s.leftActors;
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
    // Logged and flushed before the command runs: a crash inside it keeps the line naming what ran
    Log(s, std::format("SELFTEST spawn sample {} kind=leveled console \"player.{}\" actorsLoadedBefore={} sampleEntries={}", r.label, command,
        r.actorsBefore.size(), r.entries.size()));
    FlushLog();
    script->SetCommand(command);    // RE/S/Script.h:52, Script.cpp:46 (game heap copy of the text)
    script->CompileAndRun(player);  // RE/S/Script.h:49, Script.cpp:30: compiled and run on the player, as "player.placeatme"
    script->ClearCommand();         // RE/S/Script.h:48, Script.cpp:22: frees the text and nulls it before the form goes
    // At once, in this main-thread call: the next sample's snapshot would count an actor placed by now as already there.
    // Not found: the ticks look again (the fallback). The only new actor is not taken here (the wait is not over).
    if (FindLeveledSpawn(s, r, 0, false) == NpcSpells::LeveledSpawnMatch::kNone) {
        r.spawnPending = true;
        ++s.pendingSpawns;
    }
    return true;
}

NpcSpells::LeveledSpawnMatch SelfTest::FindLeveledSpawn(State& s, SampleRun& r, std::size_t waitedTicks, bool waitOver)
{
    const auto found = NewActors(s, r);
    std::vector<NpcSpells::LeveledSpawnCandidate> candidates;
    std::size_t withExtra = 0;
    for (const auto& a : found) {
        candidates.push_back(ToCandidate(a, r));
        withExtra += a.leveled ? 1 : 0;
    }
    const auto choice = NpcSpells::ChooseLeveledSpawn(candidates, waitOver);
    if (choice.match == NpcSpells::LeveledSpawnMatch::kNone) {
        return choice.match;
    }
    const auto& chosen = found[choice.index];
    auto* actor = chosen.actor.get();
    const auto id = actor->GetFormID();
    s.spawnedIds.insert(id);
    ClearSpawnPending(s, r);
    Log(s, std::format("SELFTEST spawn sample {} kind=leveled ref 0x{:08X} match={} matching={} candidates={} withExtraLeveled={} sampleEntries={} {} foundAfterMs={}",
        r.label, id, NpcSpells::LeveledSpawnMatchName(choice.match), choice.matching, candidates.size(), withExtra, r.entries.size(), BasesText(chosen),
        waitedTicks * NpcSpells::kSelfTestPollMs));
    if (choice.match == NpcSpells::LeveledSpawnMatch::kOnlyNew) {
        // Nothing ties it to the sample: it may be a walk-in or another mod's spawn, so it is not checked and not deleted
        r.onlyNew = true;
        ++s.leftActors;
        Warn(s, std::format("SELFTEST WARN sample {} ref 0x{:08X} matched only as the one new actor: not checked, left in the world", r.label, id));
        return choice.match;
    }
    if (choice.matching > 1) {
        // One placeatme places one actor: the other matching ones are not this run's, and are left alone
        std::string others;
        for (std::size_t i = choice.index + 1; i < found.size(); ++i) {
            if (NpcSpells::LeveledSpawnMatches(candidates[i], choice.match)) {
                LeaveInWorld(s, candidates[i].refId);
                others += std::format(" 0x{:08X}", candidates[i].refId);
            }
        }
        Warn(s, std::format("SELFTEST WARN sample {} {} actors matched {}: the first, 0x{:08X}, is checked; the others are left in the world:{}", r.label,
            choice.matching, NpcSpells::LeveledSpawnMatchName(choice.match), id, others));
    }
    r.handle = actor->GetHandle();  // RE/T/TESObjectREFR.h:402
    r.spawnedId = id;
    ++s.liveActors;
    return choice.match;
}

std::string SelfTest::LeveledSpawnMissing(State& s, SampleRun& r, std::size_t waitedTicks)
{
    // At any distance, so the log tells "nothing spawned" from "spawned further than kSpawnAcceptRadius"
    const auto found = NewActors(s, r, true);
    std::size_t inReach = 0, withExtra = 0;
    for (std::size_t i = 0; i < found.size(); ++i) {
        const bool inRadius = found[i].distance <= kSpawnAcceptRadius;
        inReach += inRadius ? 1 : 0;
        withExtra += found[i].leveled ? 1 : 0;
        if (i < kMaxLoggedCandidates) {
            Log(s, std::format("SELFTEST leveled candidate {} ref 0x{:08X} distance={:.0f} {}", r.label, found[i].actor->GetFormID(), found[i].distance, BasesText(found[i])));
        }
        if (inRadius) {
            s.spawnedIds.insert(found[i].actor->GetFormID());  // counted as left below: no later run or sample takes it
        }
    }
    ClearSpawnPending(s, r);
    const auto left = NpcSpells::LeveledSpawnLeftCount(inReach);
    s.leftActors += static_cast<int>(left);
    return std::format("leveled spawn wait: no actor of console placeatme {:08X} found within {} ms (new actors loaded {}, with ExtraLeveledCreature {}, "
                       "within {} units of the player {}: none whose base is an entry of the sample, or several and none matching; {} counted as left in the world)",
        r.placed ? r.placed->GetFormID() : 0, waitedTicks * NpcSpells::kSelfTestPollMs, found.size(), withExtra, static_cast<int>(kSpawnAcceptRadius), inReach, left);
}
