#include "Common.h"
#include "tomereach/TomeReach.h"
#include "tomereach/TomeReachConfig.h"
#include "tomereach/TomeReachPass.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace TomeReach
{
    namespace
    {
        // What the worker needs, taken on the game thread: no RE object crosses over
        struct Job
        {
            std::vector<PluginFile> loadOrder;
            NameTable names;
            std::unordered_set<Key> tomes;
            std::unordered_map<RE::FormID, std::vector<Key>> tomesOf;  // spell -> its tomes
        };

        struct Result
        {
            std::mutex lock;
            std::condition_variable changed;
            bool started = false;
            bool done = false;    // finished, well or not
            bool ready = false;   // finished well: `unreachable` holds the answer
            std::unordered_set<RE::FormID> unreachable;
        };

        Result& Shared()
        {
            static Result result;
            return result;
        }

        // The pass is over, with an answer or without one
        void Finish(std::unordered_set<RE::FormID>* unreachable)
        {
            auto& shared = Shared();
            {
                std::lock_guard guard(shared.lock);
                if (unreachable) {
                    shared.unreachable = std::move(*unreachable);
                    shared.ready = true;
                }
                shared.done = true;
            }
            shared.changed.notify_all();
        }

        // A form's key: its plugin's name and its id inside that plugin
        bool KeyOf(RE::TESDataHandler* dh, RE::FormID formId, NameTable& names, Key& out)
        {
            const RE::TESFile* file = nullptr;
            std::uint32_t local = 0;
            if ((formId >> kFileIndexShift) == kLightIndex) {
                file = dh->LookupLoadedLightModByIndex(
                    static_cast<std::uint16_t>((formId >> kLightFileShift) & kLightFileMask));
                local = formId & kLightLocalMask;
            } else {
                file = dh->LookupLoadedModByIndex(static_cast<std::uint8_t>(formId >> kFileIndexShift));
                local = formId & kLocalMask;
            }
            if (!file) return false;
            out = MakeKey(names.Id(file->GetFilename()), local);
            return true;
        }

        void Run(std::shared_ptr<Job> job)
        {
            const auto started = std::chrono::steady_clock::now();
            try {
                const auto configs = FindDistributionConfigs(std::filesystem::path(kDataDir));
                const auto pass = FindReachedTomes(job->loadOrder, job->names, job->tomes, configs);
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
                if (!pass.unreadable.empty()) {
                    logger::warn("TomeReach: {} plugins could not be read (first: {}) - no spell is marked, "
                                 "every tome counts", pass.unreadable.size(), pass.unreadable.front());
                    Finish(nullptr);
                    return;
                }
                std::unordered_set<RE::FormID> unreachable;
                for (const auto& [spell, tomes] : job->tomesOf) {
                    bool reached = false;
                    for (const Key t : tomes) reached = reached || pass.reached.contains(t);
                    if (!reached) unreachable.insert(spell);
                }
                for (const auto& d : pass.damaged) {
                    logger::warn("TomeReach: '{}' is damaged (read in part) - the tomes of it and its masters count "
                                 "as handed out", d);
                }
                logger::info("TomeReach: {} plugins, {} tomes, {} handed out by a record, {} only by a script "
                             "property or alias, {} only by a distribution config ({} configs), {} kept for a damaged "
                             "plugin; {} spells have no tome anything hands out ({} ms)",
                    job->loadOrder.size(), job->tomes.size(), pass.byRecord, pass.byLooseOnly, pass.byConfig,
                    pass.configFiles, pass.byDamaged, unreachable.size(), ms);
                Finish(&unreachable);
            } catch (const std::exception& e) {
                logger::warn("TomeReach: the pass failed ({}) - no spell is marked", e.what());
                Finish(nullptr);
            } catch (...) {
                // Whatever went wrong, a scan waiting for the pass must not wait on
                logger::warn("TomeReach: the pass failed - no spell is marked");
                Finish(nullptr);
            }
        }
    }

    void StartAfterDataLoaded()
    {
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) return;

        auto job = std::make_shared<Job>();
        std::unordered_set<const RE::TESFile*> loaded;
        for (std::uint32_t i = 0; i < dh->GetLoadedModCount(); ++i) loaded.insert(dh->GetLoadedMods()[i]);
        for (std::uint32_t i = 0; i < dh->GetLoadedLightModCount(); ++i) loaded.insert(dh->GetLoadedLightMods()[i]);
        for (const RE::TESFile* file : dh->files) {
            if (!file || !loaded.contains(file)) continue;
            // The engine's own folder for the file, and Data/ when that one cannot be opened
            const std::string name(file->GetFilename());
            const std::string_view dir(file->path);
            const std::filesystem::path data(kDataDir);
            const std::filesystem::path path = (dir.empty() ? data : std::filesystem::path(dir)) / name;
            job->loadOrder.push_back({ name, path, path == data / name ? std::filesystem::path() : data / name });
            job->names.SetLight(job->names.Id(file->GetFilename()), file->IsLight());
        }

        for (auto* book : dh->GetFormArray<RE::TESObjectBOOK>()) {
            if (!book || !book->TeachesSpell()) continue;
            auto* spell = book->GetSpell();
            Key key = 0;
            if (!spell || !KeyOf(dh, book->GetFormID(), job->names, key)) continue;
            job->tomes.insert(key);
            job->tomesOf[spell->GetFormID()].push_back(key);
        }
        logger::info("TomeReach: reading {} plugins for {} tomes on a worker thread (first: '{}')",
            job->loadOrder.size(), job->tomes.size(),
            job->loadOrder.empty() ? std::string() : job->loadOrder.front().path.string());
        try {
            std::thread(Run, std::move(job)).detach();
        } catch (const std::system_error& e) {
            logger::warn("TomeReach: no worker thread ({}) - no spell is marked", e.what());
            return;
        }
        // Marked after the thread exists: a scan only waits for a pass that runs
        // (one already done lets the wait return at once)
        std::lock_guard guard(Shared().lock);
        Shared().started = true;
    }

    bool WaitUntilDone(std::chrono::milliseconds limit)
    {
        auto& shared = Shared();
        std::unique_lock guard(shared.lock);
        if (!shared.started) return false;
        return shared.changed.wait_for(guard, limit, [&] { return shared.done; });
    }

    bool UnreachableSpells(std::unordered_set<RE::FormID>& out)
    {
        auto& shared = Shared();
        std::lock_guard guard(shared.lock);
        if (!shared.ready) return false;
        out = shared.unreachable;
        return true;
    }
}
