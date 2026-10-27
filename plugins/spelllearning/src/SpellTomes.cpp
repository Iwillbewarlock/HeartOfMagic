#include "SpellTomes.h"
#include "AsciiText.h"
#include "JsonText.h"
#include "PathText.h"
#include "SpellScanner.h"
#include "SpellTomeHook.h"
#include "SpellTomePairs.h"
#include "ThreadUtils.h"
#include "librarian/SpellResearchPatch.h"
#include "tomereach/TomeReach.h"
#include "uimanager/UIManager.h"

#include <atomic>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <unordered_map>

// =============================================================================
// SpellTomes - the snapshot of tome pairs and the one SpellOf (SpellTomes.h)
// =============================================================================

namespace SpellTomes
{
    namespace
    {
        namespace Pairs = SpellTomePairs;
        namespace SRPatch = Librarian::SpellResearchPatch;

        constexpr const char* kPatchFile = "Data/SKSE/Plugins/SpellResearchSynthesizer/SynthesizedPatch.json";

        struct Snapshot
        {
            std::unordered_map<RE::FormID, RE::FormID> paired;   // book that teaches nothing -> its spell
            Status status;
        };

        // Read from any thread, swapped on the game thread
        std::atomic<std::shared_ptr<const Snapshot>> g_snapshot;

        // Game thread only
        bool g_patchRead = false;
        std::optional<Pairs::PatchRead> g_patch;                 // nullopt: no file
        std::vector<Pairs::LivePart> g_lastLive;                 // the last readable state of SR's lists
        bool g_liveRead = false;
        // The last full scan's generation (0 = none) and paired tomes; a Papyrus scan may note it off the game thread
        std::atomic<std::uint64_t> g_scanGeneration = 0;
        std::atomic<std::size_t> g_scanPaired = 0;

        std::optional<Pairs::PatchRead> ReadPatchFile()
        {
            const std::filesystem::path path(kPatchFile);
            std::error_code error;
            if (!std::filesystem::exists(path, error)) return std::nullopt;
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                logger::warn("SpellTomes: '{}' exists but cannot be opened - Spell Research's lists are used, unverified",
                    PathText::Utf8(path));
                return Pairs::PatchRead{};
            }
            const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            auto read = Pairs::ReadPatch(bytes);
            for (const auto& problem : read.problems) logger::warn("SpellTomes: {}: {}", kPatchFile, problem);
            if (!read.usable) {
                logger::warn("SpellTomes: '{}' is not usable - Spell Research's lists are used, unverified. Run Spell "
                             "Research Synthesizer again to write it anew", kPatchFile);
            } else {
                logger::info("SpellTomes: {} - {} new spells, {} with a tome, {} without, {} malformed", kPatchFile,
                    read.newSpells, read.entries.size(), read.noTome, read.malformed);
            }
            return read;
        }

        // One tier list's parts as ids: the plugin's entries, then the ones scripts added
        void AddParts(const RE::BGSListForm* books, const RE::BGSListForm* spells, std::vector<Pairs::LivePart>& out)
        {
            if (!books || !spells) return;
            Pairs::LivePart record;
            for (const auto* form : books->forms) record.books.push_back(form ? form->GetFormID() : 0);
            for (const auto* form : spells->forms) record.spells.push_back(form ? form->GetFormID() : 0);
            out.push_back(std::move(record));
            Pairs::LivePart script;
            script.script = true;
            if (books->scriptAddedTempForms) script.books.assign(books->scriptAddedTempForms->begin(), books->scriptAddedTempForms->end());
            if (spells->scriptAddedTempForms) script.spells.assign(spells->scriptAddedTempForms->begin(), spells->scriptAddedTempForms->end());
            out.push_back(std::move(script));
        }

        // SR's lists when they can be read now; otherwise the last readable state stays
        void ReadLive()
        {
            SRPatch::TomeLists lists{};
            if (SRPatch::ReadTomeLists(lists) != SRPatch::TomeListState::kReady) return;
            std::vector<Pairs::LivePart> parts;
            for (const auto& [books, spells] : lists) AddParts(books, spells, parts);
            g_lastLive = std::move(parts);
            g_liveRead = true;
        }

        // "plugin|local id" -> FormID (0: not there). The load order does not change in a
        // session, and LookupForm walks every file by name: each id is looked up once.
        std::unordered_map<std::string, Pairs::FormId> g_resolved;

        Pairs::FormId Resolve(const Pairs::FormRef& ref)
        {
            const std::string key = std::format("{}|{}", ref.plugin, ref.localId);
            if (const auto it = g_resolved.find(key); it != g_resolved.end()) return it->second;
            auto* dh = RE::TESDataHandler::GetSingleton();
            auto* form = dh ? dh->LookupForm(ref.localId, ref.plugin) : nullptr;
            const Pairs::FormId id = form ? form->GetFormID() : 0;
            g_resolved.emplace(key, id);
            return id;
        }

        Pairs::Facts GameFacts()
        {
            Pairs::Facts facts;
            facts.resolve = Resolve;
            facts.book = [](Pairs::FormId id) {
                Pairs::BookFacts out;
                auto* book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(id);
                if (!book) return out;
                out.isBook = true;
                auto* spell = book->TeachesSpell() ? book->GetSpell() : nullptr;
                out.taughtSpell = spell ? spell->GetFormID() : 0;
                const auto* winner = book->GetFile(-1);
                out.winner = winner ? std::string(winner->GetFilename()) : std::string("(no file)");
                out.fromPatchPlugin = AsciiText::EqualsIgnoreCase(out.winner, Pairs::kPatchPlugin);
                return out;
            };
            facts.isSpell = [](Pairs::FormId id) { return RE::TESForm::LookupByID<RE::SpellItem>(id) != nullptr; };
            return facts;
        }

        std::size_t CountOwnSpellTomes()
        {
            auto* dh = RE::TESDataHandler::GetSingleton();
            if (!dh) return 0;
            std::size_t count = 0;
            for (auto* book : dh->GetFormArray<RE::TESObjectBOOK>()) {
                if (book && book->TeachesSpell() && book->GetSpell()) ++count;
            }
            return count;
        }

        std::string Describe(const Status& s)
        {
            const auto& c = s.counts;
            const std::size_t live = c.liveRecord + c.liveScript;
            return std::format("SpellTomes: {} tomes - {} by the book's own spell, {} by Spell Research pairs "
                "(records {}, script {}{}), {} by SynthesizedPatch.json ({}); {} pairs skipped ({} unresolved, {} not a "
                "book and a spell, {} teach a spell themselves of which {} another, {} stale, {} conflicts with the JSON "
                "or each other, {} outside the JSON, {} twice in the JSON, {} list parts of unequal length) [generation {}]",
                s.ownSpell + c.Paired(), s.ownSpell, live, c.liveRecord, c.liveScript,
                live ? ", unverified" : "", c.patch, s.patchUsable ? "read" : (s.patchFound ? "unusable" : "none"),
                c.unresolved + c.notBookOrSpell + c.teaches + c.stale + c.liveConflicts + c.liveOutsidePatch + c.patchConflicts,
                c.unresolved, c.notBookOrSpell, c.teaches, c.teachesOther, c.stale, c.liveConflicts,
                c.liveOutsidePatch, c.patchConflicts, c.unequalParts, s.generation);
        }

        // Synthesis runs a patcher into its own output plugin (Synthesis.esp by default);
        // SpellResearchSynthesizer.esp is only the patcher's standalone name (its Program.cs,
        // SetTypicalOpen). Books overridden last by one other plugin are worth one loud line.
        void LogStaleWinners(const Pairs::Result& result)
        {
            for (const auto& [winner, count] : result.staleByWinner) {
                logger::warn("SpellTomes: {} SynthesizedPatch.json books are overridden last by '{}', not {} - if Spell "
                             "Research Synthesizer ran inside the Synthesis app, '{}' is its output; those tomes take "
                             "Spell Research's pairs (unverified) once a save with them loads", count, winner,
                    Pairs::kPatchPlugin, winner);
            }
        }

        // A new generation: everything that kept an answer about tomes drops it
        void OnPairsChanged(const Snapshot& now, std::string_view reason)
        {
            SpellTomeHook::GetSingleton()->InvalidateTomeInventoryCache();
            TomeReach::Restart(now.status.generation);   // before the groups: they read the pass's answer
            SpellScanner::InvalidateDuplicateGroups();
            SpellScanner::PrepareTreeSpellForTomeAfterPass();
            const std::uint64_t generation = now.status.generation;
            const std::uint64_t scanned = g_scanGeneration.load();
            const bool stale = scanned != 0 && scanned != generation && now.paired.size() > g_scanPaired.load();
            logger::info("SpellTomes: {} - the pairs changed (generation {}, {} paired tomes{}) - tome reach runs again",
                reason, generation, now.paired.size(), stale ? ", the last full scan had fewer: stale" : "");
            // A save load or an Import happen in a game: the panel hears of the save only after
            // this (NotifySaveGameLoaded ends kPostLoadGame), so it is told here that a retry must hold
            const bool inGame = reason == kReasonSaveLoaded || reason == kReasonImport;
            const nlohmann::json payload = { { "generation", generation }, { "pairedTomes", now.paired.size() },
                                             { "stale", stale }, { "inGame", inGame }, { "reason", std::string(reason) } };
            UIManager::GetSingleton()->NotifySpellTomesChanged(JsonText::Dump(payload));
        }

        void RebuildOnGameThread(std::string_view reason)
        {
            if (!g_patchRead) {
                g_patch = ReadPatchFile();
                g_patchRead = true;
            }
            ReadLive();
            const Pairs::Result result = Pairs::Build({ g_patch ? &*g_patch : nullptr, g_lastLive }, GameFacts());

            auto next = std::make_shared<Snapshot>();
            for (const auto& [book, entry] : result.pairs) next->paired.emplace(book, entry.spell);
            const auto previous = g_snapshot.load();
            if (previous && previous->paired == next->paired) return;

            Status& status = next->status;
            status.generation = previous ? previous->status.generation + 1 : 1;
            status.ownSpell = CountOwnSpellTomes();
            status.paired = next->paired.size();
            status.counts = result.counts;
            status.patchFound = g_patch.has_value();
            status.patchUsable = g_patch && g_patch->usable;
            status.liveRead = g_liveRead;
            g_snapshot.store(next);

            logger::info("{} ({})", Describe(status), reason);
            for (const auto& note : result.notes) logger::info("SpellTomes: {}", note);
            LogStaleWinners(result);
            if (previous) OnPairsChanged(*next, reason);
        }
    }

    RE::SpellItem* SpellOf(RE::TESObjectBOOK* book)
    {
        if (!book) return nullptr;
        if (book->TeachesSpell()) {
            if (auto* spell = book->GetSpell()) return spell;
        }
        const auto snapshot = g_snapshot.load();
        if (!snapshot) return nullptr;
        const auto it = snapshot->paired.find(book->GetFormID());
        return it == snapshot->paired.end() ? nullptr : RE::TESForm::LookupByID<RE::SpellItem>(it->second);
    }

    bool IsPairedTome(RE::FormID book)
    {
        const auto snapshot = g_snapshot.load();
        return snapshot && snapshot->paired.contains(book);
    }

    void ForEachTome(const std::function<void(RE::TESObjectBOOK*, RE::SpellItem*)>& visit)
    {
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) return;
        for (auto* book : dh->GetFormArray<RE::TESObjectBOOK>()) {
            if (auto* spell = SpellOf(book)) visit(book, spell);
        }
    }

    void Rebuild(std::string_view reason)
    {
        if (!IsOnGameThread()) {
            AddTaskToGameThread("SpellTomesRebuild", [why = std::string(reason)] { Rebuild(why); });
            return;
        }
        try {
            RebuildOnGameThread(reason);
        } catch (const std::exception& e) {
            logger::error("SpellTomes: {} - the pairs could not be read again ({}), the last ones stay", reason, e.what());
        }
    }

    std::uint64_t Generation()
    {
        const auto snapshot = g_snapshot.load();
        return snapshot ? snapshot->status.generation : 0;
    }

    bool CurrentStatus(Status& out)
    {
        const auto snapshot = g_snapshot.load();
        if (!snapshot) return false;
        out = snapshot->status;
        return true;
    }

    std::string StatusLine()
    {
        const auto snapshot = g_snapshot.load();
        return snapshot ? Describe(snapshot->status) : std::string("SpellTomes: not read yet");
    }

    void NoteFullScan()
    {
        const auto snapshot = g_snapshot.load();
        g_scanGeneration = snapshot ? snapshot->status.generation : 0;
        g_scanPaired = snapshot ? snapshot->paired.size() : 0;
    }
}
