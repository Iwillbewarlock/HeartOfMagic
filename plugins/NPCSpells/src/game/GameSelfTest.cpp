#include "game/GameSelfTest.h"

#include "Common.h"

#include "core/NpcSpellsEsl.h"
#include "core/NpcSpellsSelfTest.h"
#include "core/NpcSpellsSlots.h"
#include "game/GameFiles.h"
#include "game/GameNpcs.h"
#include "game/GamePool.h"
#include "PluginNames.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;
    using Ms = std::chrono::milliseconds;
    using IdSet = std::unordered_set<std::uint32_t>;

    // A task that queued itself again might run again in the same drain of SKSE's task queue (not checked in SKSE's
    // source), so the wait is not a self-requeueing task: a worker thread queues one Tick per kPollMs. The worker only
    // sleeps and queues; every game object is touched inside the queued tasks, on the game's main thread.
    constexpr int kPollMs = 100;
    constexpr int kPlayerWaitMs = 60000;        // the player's 3D and cell, counted from the load message
    constexpr int kWorkerSlackMs = 5000;        // the worker stops queueing this long after every deadline has passed
    constexpr std::size_t kMaxLevDepth = 4;     // nested leveled spell lists followed in other lists (vanilla nests one level)
    constexpr std::size_t kMaxHitNames = 5;     // hit spells named per list line
    constexpr std::uint16_t kNoEntryLevel = 0xFFFF;

    // Our ESL's records, told apart by their file and local FormID (GetFormEditorID() is empty without
    // powerofthree's Tweaks, GameNpcs.cpp). The local id gives slot and chunk: LocalFormId (core/NpcSpellsSlots.h).
    struct OurLists
    {
        const RE::TESFile* file = nullptr;

        std::optional<std::pair<std::size_t, std::uint8_t>> Find(const RE::TESLevSpell* lev) const
        {
            if (!file || !lev || lev->GetFile(0) != file) {  // RE/T/TESForm.h:275
                return std::nullopt;
            }
            const auto local = lev->GetLocalFormID();  // RE/T/TESForm.h:295
            if (local < NpcSpells::kFirstLocalFormId) {
                return std::nullopt;
            }
            const auto rel = local - NpcSpells::kFirstLocalFormId;
            const std::size_t slot = rel / NpcSpells::kMaxChunks;
            if (slot >= NpcSpells::SlotTable().size()) {
                return std::nullopt;
            }
            return std::pair{ slot, static_cast<std::uint8_t>(rel % NpcSpells::kMaxChunks) };
        }
    };

    std::string FormText(const RE::TESForm* form)
    {
        if (!form) {
            return "none";
        }
        const auto* file = form->GetFile(0);
        if (!file) {
            return std::format("0x{:08X}", form->GetFormID());  // made at runtime (0xFF...)
        }
        return PluginNames::MakePersistentId(file->fileName, form->GetLocalFormID());
    }

    std::string SpellText(const RE::SpellItem* spell)
    {
        const char* name = spell->GetName();  // RE/T/TESForm.h:308; system code page, the log is UTF-8
        return std::format("0x{:08X} {}", spell->GetFormID(),
            EncodingUtils::SanitizeToUTF8(std::string(name ? name : ""), PluginNames::kSystemCodePage));
    }

    // RE/T/TESLeveledList.h:70-73. data(), not operator[]: our lists' arrays have no size head (GameLists.cpp), and
    // operator[] asserts through size()
    void AddLevEntries(const RE::TESLevSpell* lev, IdSet& out, std::size_t depth)
    {
        const auto* entries = lev ? lev->entries.data() : nullptr;
        if (!entries || depth > kMaxLevDepth) {
            return;
        }
        for (std::uint8_t i = 0; i < lev->numEntries; ++i) {
            const auto* form = entries[i].form;
            if (!form) {
                continue;
            }
            if (const auto* nested = form->As<RE::TESLevSpell>()) {
                AddLevEntries(nested, out, depth + 1);
            } else {
                out.insert(form->GetFormID());
            }
        }
    }

    // What a spell list gives besides our lists: its static spells and its other leveled lists' entries (RE/T/TESSpellList.h:44-49)
    void AddOtherSpells(const RE::TESSpellList::SpellData* d, const OurLists& ours, IdSet& out)
    {
        if (!d) {
            return;
        }
        for (std::uint32_t i = 0; d->spells && i < d->numSpells; ++i) {
            if (d->spells[i]) {
                out.insert(d->spells[i]->GetFormID());
            }
        }
        for (std::uint32_t i = 0; d->levSpells && i < d->numlevSpells; ++i) {
            if (d->levSpells[i] && !ours.Find(d->levSpells[i])) {
                AddLevEntries(d->levSpells[i], out, 0);
            }
        }
    }

    std::vector<RE::TESLevSpell*> OurListsOn(const RE::TESSpellList::SpellData* d, const OurLists& ours)
    {
        std::vector<RE::TESLevSpell*> out;
        for (std::uint32_t i = 0; d && d->levSpells && i < d->numlevSpells; ++i) {
            if (ours.Find(d->levSpells[i])) {
                out.push_back(d->levSpells[i]);
            }
        }
        return out;
    }

    struct ListInfo
    {
        std::string editorId;
        NpcSpells::School school{};
        std::vector<std::uint32_t> spells;  // entry order, ids of SpellItem entries (ours hold no nested lists)
        std::uint16_t minLevel = kNoEntryLevel;
    };

    ListInfo ReadOurList(RE::TESLevSpell* lev, const OurLists& ours, std::unordered_map<std::uint32_t, RE::SpellItem*>& spellById)
    {
        ListInfo info;
        const auto at = ours.Find(lev);
        const auto& key = NpcSpells::SlotTable()[at->first].key;
        info.editorId = NpcSpells::EditorIdFor(key, at->second);
        info.school = key.school;
        auto* entries = lev->entries.data();
        for (std::uint8_t i = 0; entries && i < lev->numEntries; ++i) {
            auto* spell = entries[i].form ? entries[i].form->As<RE::SpellItem>() : nullptr;
            if (!spell) {
                continue;
            }
            info.spells.push_back(spell->GetFormID());
            spellById.emplace(spell->GetFormID(), spell);
            info.minLevel = std::min(info.minLevel, entries[i].level);
        }
        return info;
    }

    struct SampleRun
    {
        std::string text;
        NpcSpells::FormRef ref;
        RE::TESNPC* npc = nullptr;
        RE::TESNPC* holder = nullptr;
        bool unresolvable = false;
        RE::ObjectRefHandle handle;  // a handle, not a NiPointer: the state may be freed on the worker thread
        Clock::time_point spawnedAt{};
        bool done = false;
    };

    struct State
    {
        NpcSpells::SelfTestConfig config;
        std::vector<SampleRun> samples;
        OurLists ours;
        bool spawned = false;
        Clock::time_point start = Clock::now();
        std::optional<Clock::time_point> playerReadyAt;
        std::size_t pass = 0, fail = 0;
        std::vector<std::string> lines;  // appended to the report at the end
        std::atomic<bool> finished{ false };
    };

    void Log(State& s, std::string line)
    {
        logger::info("{}", line);
        s.lines.push_back(std::move(line));
    }

    void Fail(State& s, SampleRun& r, std::string_view reason)
    {
        if (r.done) {
            return;
        }
        r.done = true;
        ++s.fail;
        Log(s, std::format("SELFTEST RESULT sample {} FAIL ({})", r.text, reason));
    }

    // RE/T/TESObjectREFR.h:330 Disable, :218 SetDelete
    void Cleanup(SampleRun& r)
    {
        if (const auto ref = r.handle.get()) {  // RE/B/BSPointerHandle.h:162
            ref->Disable();
            ref->SetDelete(true);
        }
        r.handle = {};
    }

    void Spawn(State& s, RE::PlayerCharacter* player)
    {
        NpcSpellsGame::PluginLookup lookup;
        for (auto& r : s.samples) {
            auto* form = NpcSpellsGame::LookupLocalForm(lookup, r.ref.pluginLower, r.ref.local);
            r.npc = form ? form->As<RE::TESNPC>() : nullptr;
            if (!r.npc) {
                Fail(s, r, form ? "the record is not an NPC (TESNPC)" : "base record not found in this load order");
                continue;
            }
            r.holder = NpcSpellsGame::SpellListHolder(r.npc, r.unresolvable);
            if (OurListsOn(r.holder->actorEffects, s.ours).empty()) {
                Fail(s, r, std::format("spell list holder {} has no HoMNS list (is it in the report's NPC lists? is the ESL loaded?)", FormText(r.holder)));
                continue;
            }
            // RE/T/TESObjectREFR.h:475: at the player's position, a hostile NPC stands next to the player until it is checked
            const auto ref = player->PlaceObjectAtMe(r.npc, false);
            if (!ref || !ref->As<RE::Actor>()) {
                if (ref) {
                    ref->Disable();
                    ref->SetDelete(true);
                }
                Fail(s, r, "PlaceObjectAtMe gave no actor");
                continue;
            }
            r.handle = ref->GetHandle();  // RE/T/TESObjectREFR.h:402
            r.spawnedAt = Clock::now();
            Log(s, std::format("SELFTEST spawn sample {} ref 0x{:08X}", r.text, ref->GetFormID()));
        }
        FlushLog();  // info lines are buffered (Common.h): a crash while hostile actors stand here keeps these lines
    }

    // Every HasSpell call here is one task on one actor: Actor::HasSpell (RE/A/Actor.h:620) goes through Actor::VisitSpells,
    // which reads the actor's leveled spell cache (built once per high process), so every query sees the same roll
    void Check(State& s, SampleRun& r, RE::Actor* actor, long long waitedMs)
    {
        const auto* base = actor->GetActorBase();  // RE/A/Actor.h:556
        IdSet excluded;
        AddOtherSpells(r.holder->actorEffects, s.ours, excluded);
        AddOtherSpells(r.npc->actorEffects, s.ours, excluded);
        if (base) {
            AddOtherSpells(base->actorEffects, s.ours, excluded);
        }
        if (const auto* race = actor->GetRace()) {  // RE/T/TESRace.h:144, a TESSpellList
            AddOtherSpells(race->actorEffects, s.ours, excluded);
        }
        for (auto* spell : actor->GetActorRuntimeData().addedSpells) {  // RE/A/Actor.h:744
            if (spell) {
                excluded.insert(spell->GetFormID());
            }
        }

        std::unordered_map<std::uint32_t, RE::SpellItem*> spellById;
        std::vector<ListInfo> lists;
        for (auto* lev : OurListsOn(r.holder->actorEffects, s.ours)) {
            lists.push_back(ReadOurList(lev, s.ours, spellById));
        }
        const auto baseLists = base ? OurListsOn(base->actorEffects, s.ours).size() : 0;
        Log(s, std::format("SELFTEST sample {} base {} holder {} inherited={}{} actorBase {} holderLists={} actorBaseLists={} 3D=loaded high=yes waitedMs={}",
            r.text, FormText(r.npc), FormText(r.holder), r.holder != r.npc ? "yes" : "no", r.unresolvable ? " unresolvable" : "",
            FormText(base), lists.size(), baseLists, waitedMs));

        auto* avOwner = actor->AsActorValueOwner();  // RE/A/Actor.h:778; GetActorValue = current value (RE/A/ActorValueOwner.h:16)
        std::array<IdSet, NpcSpells::kSchoolCount> schoolHits;
        std::array<std::size_t, NpcSpells::kSchoolCount> schoolLists{};
        IdSet allHits;
        for (std::size_t i = 0; i < lists.size(); ++i) {
            const auto& list = lists[i];
            IdSet others;
            for (std::size_t j = 0; j < lists.size(); ++j) {
                if (j != i) {
                    others.insert(lists[j].spells.begin(), lists[j].spells.end());
                }
            }
            const auto witnesses = NpcSpells::WitnessSpells(list.spells, excluded);
            const auto school = static_cast<std::size_t>(list.school);
            // RE/A/ActorValues.h:26-30: kAlteration..kRestoration are 18..22 in School order (static_assert in GamePool.cpp)
            const auto skill = avOwner->GetActorValue(static_cast<RE::ActorValue>(static_cast<int>(RE::ActorValue::kAlteration) + static_cast<int>(school)));
            std::size_t hits = 0;
            std::string hitText;
            for (const auto id : witnesses) {
                auto* spell = spellById.at(id);
                if (!actor->HasSpell(spell)) {
                    continue;
                }
                ++hits;
                schoolHits[school].insert(id);
                allHits.insert(id);
                if (hits <= kMaxHitNames) {
                    hitText += std::format("{}{}", hits == 1 ? " hitSpells: " : ", ", SpellText(spell));
                }
            }
            ++schoolLists[school];
            const bool belowEvery = list.minLevel != kNoEntryLevel && skill < static_cast<float>(list.minLevel);
            Log(s, std::format("SELFTEST list {} {} school={} skill={:.0f} entries={} minLevel={}{} witnesses={} shared={} hits={}{}",
                r.text, list.editorId, NpcSpells::kSchoolNames[school], skill, list.spells.size(), list.minLevel,
                belowEvery ? " (skill below every entry)" : "", witnesses.size(), NpcSpells::CountShared(witnesses, others), hits, hitText));
        }
        for (std::size_t sc = 0; sc < NpcSpells::kSchoolCount; ++sc) {
            if (schoolLists[sc] > 0) {
                Log(s, std::format("SELFTEST school {} {} lists={} distinctHits={}", r.text, NpcSpells::kSchoolNames[sc], schoolLists[sc], schoolHits[sc].size()));
            }
        }
        const bool pass = !allHits.empty();
        r.done = true;  // here, not on entry: a throw above leaves the sample to RunTick's Fail, so DONE counts every sample
        ++(pass ? s.pass : s.fail);
        Log(s, std::format("SELFTEST RESULT sample {} {} (hits on HoMNS-only witnesses: {})", r.text, pass ? "PASS" : "FAIL", allHits.size()));
    }

    void AppendReport(const State& s)
    {
        std::ofstream file(NpcSpellsGame::kReportPath, std::ios::binary | std::ios::app);
        file << "\n== Self test ==\n";
        for (const auto& line : s.lines) {
            file << line << '\n';
        }
        if (!file) {
            logger::warn("SELFTEST could not append to the report {}", NpcSpellsGame::kReportPath);
        }
    }

    void Finish(State& s)
    {
        for (auto& r : s.samples) {
            Cleanup(r);
        }
        Log(s, std::format("SELFTEST DONE pass={} fail={}", s.pass, s.fail));
        AppendReport(s);
        FlushLog();
        s.finished = true;
    }

    void Tick(State& s)
    {
        const auto now = Clock::now();
        if (!s.spawned) {
            auto* player = RE::PlayerCharacter::GetSingleton();  // RE/P/PlayerCharacter.h:725
            if (!player || !player->Is3DLoaded() || !player->GetParentCell()) {  // RE/T/TESObjectREFR.h:448, :420
                if (now - s.start >= Ms(kPlayerWaitMs)) {
                    for (auto& r : s.samples) {
                        Fail(s, r, "the player's 3D or cell never came");
                    }
                    Finish(s);
                }
                return;
            }
            if (!s.playerReadyAt) {
                s.playerReadyAt = now;
            }
            if (now - *s.playerReadyAt < Ms(s.config.startDelayMs)) {
                return;
            }
            s.spawned = true;
            Spawn(s, player);
        }
        bool open = false;
        for (auto& r : s.samples) {
            if (r.done) {
                continue;
            }
            const auto ref = r.handle.get();
            auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
            const auto waited = std::chrono::duration_cast<Ms>(now - r.spawnedAt).count();
            // The leveled spell cache needs the high process (RE/A/Actor.h GetHighProcess; 2026-10-10: before 3D, HasSpell was false)
            if (actor && actor->Is3DLoaded() && actor->GetHighProcess()) {
                Check(s, r, actor, waited);
                Cleanup(r);
                continue;
            }
            if (waited >= s.config.maxWaitMs) {
                Fail(s, r, std::format("no 3D or high process after {} ms (actor={} 3D={} high={})", waited, actor ? "yes" : "no",
                    actor && actor->Is3DLoaded() ? "yes" : "no", actor && actor->GetHighProcess() ? "yes" : "no"));
                Cleanup(r);
                continue;
            }
            open = true;
        }
        if (!open) {
            Finish(s);
        }
    }

    // A thrown error ends the test with every sample cleaned up; it never leaves the task
    void RunTick(State& s)
    {
        if (s.finished) {
            return;
        }
        try {
            Tick(s);
        } catch (const std::exception& e) {
            logger::error("SELFTEST stopped by an error: {}", e.what());
            for (auto& r : s.samples) {
                Fail(s, r, std::format("stopped by an error: {}", e.what()));
            }
            Finish(s);
        } catch (...) {
            logger::error("SELFTEST stopped by an unknown error");
            for (auto& r : s.samples) {
                Fail(s, r, "stopped by an unknown error");
            }
            Finish(s);
        }
    }

    void Start()
    {
        std::ifstream file(NpcSpellsGame::kSelfTestPath, std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        auto state = std::make_shared<State>();
        std::string error;
        if (!file || !NpcSpells::ParseSelfTestConfig(text.str(), state->config, error)) {
            logger::error("SELFTEST DONE pass=0 fail=0 (not started: {} - {})", NpcSpellsGame::kSelfTestPath, error.empty() ? "unreadable" : error);
            return;
        }
        if (!error.empty()) {
            logger::warn("SELFTEST config: {}", error);
        }
        for (std::size_t i = 0; i < state->config.samples.size(); ++i) {
            state->samples.push_back({ state->config.sampleTexts[i], state->config.samples[i] });
        }
        if (auto* dh = RE::TESDataHandler::GetSingleton()) {
            state->ours.file = dh->LookupLoadedLightModByName(NpcSpells::kEslFileName);  // RE/T/TESDataHandler.h:78
        }
        logger::info("SELFTEST start samples={} startDelayMs={} maxWaitMs={} esl={}", state->samples.size(), state->config.startDelayMs,
            state->config.maxWaitMs, state->ours.file ? "loaded" : "not loaded");
        const auto deadline = Clock::now() + Ms(kPlayerWaitMs + state->config.startDelayMs + state->config.maxWaitMs + kWorkerSlackMs);
        std::thread([state, deadline] {
            while (!state->finished && Clock::now() < deadline) {
                std::this_thread::sleep_for(Ms(kPollMs));
                const auto* tasks = SKSE::GetTaskInterface();  // SKSE/API.h:58
                if (!tasks) {
                    break;
                }
                tasks->AddTask([state] { RunTick(*state); });  // SKSE/Interfaces.h:394, runs on the main thread
            }
            if (!state->finished) {
                logger::error("SELFTEST DONE pass=0 fail=0 (no queued task ran before the deadline)");
            }
        }).detach();
    }
}

void NpcSpellsGame::MaybeStartSelfTest()
{
    static bool started = false;  // SKSE sends the load messages on the main thread
    if (started) {
        return;
    }
    std::error_code ec;
    if (!std::filesystem::exists(kSelfTestPath, ec)) {
        return;
    }
    started = true;
    try {
        Start();
    } catch (const std::exception& e) {
        logger::error("SELFTEST DONE pass=0 fail=0 (not started: {})", e.what());
    }
}
