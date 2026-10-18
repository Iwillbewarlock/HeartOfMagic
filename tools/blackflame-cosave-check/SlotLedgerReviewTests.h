#pragma once

// =============================================================================
// SLOT LEDGER REVIEW TESTS - the regression tests of the later P2 review rounds (out of SlotLedgerTests.h, 600-line limit)
// =============================================================================
// Each test a finding of a review of the ledger model, its JSON and its files; files in a temporary folder.

#include "../../plugins/BlackFlame/src/SlotLedgerFiles.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace SlotLedgerReviewTests
{
    namespace LM = BlackFlame::SlotLedgerModel;
    namespace LF = BlackFlame::SlotLedgerFiles;
    namespace CC = BlackFlame::CopyContract;
    namespace fs = std::filesystem;

    inline std::optional<LM::State> StateOf(const LM::Ledger& a_ledger, std::uint16_t a_slot)
    {
        const auto* entry = LM::FindSlot(a_ledger, a_slot);
        return entry ? std::optional<LM::State>(entry->state) : std::nullopt;
    }

    // Review fixes, second round (Gemini and agy Opus, 2026-10-18) and after
    template <class Check>
    void Run(Check&& a_check)
    {
        std::error_code ec;
        const auto temp = fs::temp_directory_path(ec);
        if (ec || !temp.is_absolute()) {
            a_check(false, "ledger review tests: no temporary folder (" + ec.message() + ")");  // review agy redo 6_01 #6
            return;
        }
        const auto folder = temp / ("blackflame-cosave-check-r2-" + std::to_string(::GetCurrentProcessId()));
        fs::remove_all(folder, ec);
        fs::create_directories(folder / "mirror", ec);
        const auto main = folder / "HoMBF_slots.json";
        const auto mirror = folder / "mirror" / "HoMBF_slots.0000abcd.json";
        const auto write = [&](const fs::path& a_file, const LM::Ledger& a_ledger) {
            std::string why;
            return LF::WriteReplacing(a_file, LM::ToJson(a_ledger).dump(1) + "\n", why);
        };

        // Gemini 17: main (v11) lacks slot 0x801; the mirror (v10) binds it to Mirror.esp, backup 5 (v6) to Old.esp. The
        // newer file's binding must win (files merged newest first), with no conflict
        LM::Ledger base;
        base.epoch = 90;
        (void)LM::Assign(base, { { CC::SlotType::kPROJ, "Base.esp", 1 } }, true, 1);
        LM::Ledger newer = base;
        LM::Ledger older = base;
        LM::Entry mirrored;
        mirrored.slot = 0x801;
        mirrored.type = CC::SlotType::kPROJ;
        mirrored.plugin = "Mirror.esp";
        mirrored.localId = 2;
        mirrored.since = 1;
        LM::Entry old = mirrored;
        old.plugin = "Old.esp";
        old.localId = 3;
        LM::Insert(newer, mirrored);
        LM::Insert(older, old);
        base.version = 11;
        newer.version = 10;
        older.version = 6;
        const bool written = write(main, base) && write(mirror, newer) && write(LF::BackupPath(main, 5), older);
        const auto loaded = LF::LoadAll(main, mirror);
        const auto* slot = loaded.ledger ? LM::FindSlot(*loaded.ledger, 0x801) : nullptr;
        a_check(written && slot && slot->plugin == "Mirror.esp" && loaded.conflicts.empty(),
            "review Gemini 17: a slot main lacks takes the newest other file's binding (mirror v10 over backup 5 v6), no conflict");

        // Gemini 18: a conflict text with bytes outside printable ASCII comes back as it was
        LM::Ledger conflicted = base;
        conflicted.entries[0].state = LM::State::kConflict;
        conflicted.entries[0].conflict = std::string("\xB8\xB6.esp|801");
        std::string why;
        const auto back = LM::FromJson(LM::ToJson(conflicted), why);
        a_check(back && back->entries.size() == 1 && back->entries[0].conflict == conflicted.entries[0].conflict,
            "review Gemini 18: a non-ASCII conflict text survives the JSON (conflictHex) " + why);

        // Gemini 18: a field that is there but damaged refuses the file (the caller then reads a backup)
        auto badSince = LM::ToJson(base);
        badSince["slots"][0]["since"] = "three";
        auto badTerminal = LM::ToJson(base);
        badTerminal["slots"][0]["terminal"] = LM::Json::array({ "not a key" });
        auto badFailure = LM::ToJson(base);
        badFailure["slots"][0]["parseFailures"] = LM::Json::array({ { { "key", CC::Hex16(5) }, { "count", std::uint64_t{ 1 } } } });
        std::string w1;
        std::string w2;
        std::string w3;
        const bool sinceRefused = !LM::FromJson(badSince, w1);
        const bool terminalRefused = !LM::FromJson(badTerminal, w2);
        const bool failureRefused = !LM::FromJson(badFailure, w3);
        a_check(sinceRefused && terminalRefused && failureRefused,
            "review Gemini 18: a damaged since, terminal key or parse failure (no session) refuses the ledger: " + w1 + "; " + w2 + "; " + w3);

        // N2: the same epoch's newer copy (a co-save over a restored backup) replaces the base's binding and what it published
        LM::Ledger restored;
        restored.epoch = 91;
        restored.version = 3;
        LM::Entry bound;
        bound.slot = 0x900;
        bound.type = CC::SlotType::kPROJ;
        bound.plugin = "Old.esp";
        bound.localId = 7;
        bound.since = 1;
        LM::SetPublished(bound, { LM::Role::kModel, 0x77, "", "meshes\\heartofmagic\\blackflame\\copies\\0000000000000077\\copy.nif", 9, 0x78, {} });
        LM::Insert(restored, bound);
        LM::Ledger coSave;
        coSave.epoch = 91;
        coSave.version = 7;
        LM::Entry rebound = bound;
        rebound.plugin = "New.esp";
        rebound.published.clear();
        LM::Insert(coSave, rebound);
        std::vector<std::string> mergeConflicts;
        std::vector<std::string> mergeStale;
        (void)LM::Merge(restored, coSave, mergeConflicts, "co-save v7", &mergeStale);
        const auto* replaced = LM::FindSlot(restored, 0x900);
        a_check(replaced && replaced->plugin == "New.esp" && replaced->published.empty() && mergeConflicts.empty() && mergeStale.size() == 1,
            "review N2: a newer copy of the same epoch replaces the binding (no conflict, the older source's published object gone)");
        a_check(restored.version == 7, "review Codex 3 P2: absorbing the newer copy (v7) moves the ledger to v7, so its next write "
                                       "(v8) is newer than that copy; now v" + std::to_string(restored.version));

        // N2: co-saves of one epoch merged newest first - v9 binds 0x901 to Nine.esp, v8 to Eight.esp; the base (v5) lacks it;
        // listed v9 first, so merging them one by one in that order would end on v8
        LM::Ledger lost;
        lost.epoch = 92;
        lost.version = 5;
        const auto save = [](std::uint32_t a_version, const char* a_plugin) {
            LM::Ledger ledger;
            ledger.epoch = 92;
            ledger.version = a_version;
            LM::Entry entry;
            entry.slot = 0x901;
            entry.type = CC::SlotType::kPROJ;
            entry.plugin = a_plugin;
            entry.localId = 1;
            entry.state = LM::State::kTombstone;
            LM::Insert(ledger, entry);
            return ledger;
        };
        std::vector<std::pair<std::string, LM::Ledger>> saves{ { "v9", save(9, "Nine.esp") }, { "v8", save(8, "Eight.esp") },
            { "v2", save(2, "Two.esp") } };
        // review agy redo 6_01 #1: every order of the three
        std::vector<std::size_t> order{ 0, 1, 2 };
        bool everyOrder = true;
        do {
            LM::Ledger lostCopy = lost;
            std::vector<std::pair<std::string, LM::Ledger>> shuffled;
            for (const auto i : order) {
                shuffled.push_back(saves[i]);
            }
            std::vector<std::string> orderConflicts;
            LM::MergeCoSaves(lostCopy, shuffled, orderConflicts);
            const auto* found = LM::FindSlot(lostCopy, 0x901);
            everyOrder = everyOrder && found && found->plugin == "Nine.esp" && orderConflicts.empty() && lostCopy.version == 9;
        } while (std::next_permutation(order.begin(), order.end()));
        std::vector<std::string> unionConflicts;
        LM::MergeCoSaves(lost, saves, unionConflicts);
        const auto* newest = LM::FindSlot(lost, 0x901);
        a_check(everyOrder && newest && newest->plugin == "Nine.esp" && unionConflicts.empty() && lost.version == 9,
            "review N2 / Codex 3 P2: co-saves of one epoch take the newest binding (v9 over v8 and v2, in any order), no "
            "conflict, and the ledger goes on from v9 (now v" + std::to_string(lost.version) + ")");

        // agy redo 3.1 / 3.2: a conflict survives a same-source copy that holds it and a newer copy of the epoch
        LM::Ledger calm;
        calm.epoch = 93;
        calm.version = 4;
        LM::Entry plain;
        plain.slot = 0x902;
        plain.type = CC::SlotType::kPROJ;
        plain.plugin = "Same.esp";
        plain.localId = 1;
        LM::Insert(calm, plain);
        LM::Ledger claimed = calm;
        claimed.entries[0].state = LM::State::kConflict;
        claimed.entries[0].conflict = "Other.esp|1";
        std::vector<std::string> keptConflicts;
        (void)LM::Merge(calm, claimed, keptConflicts, "a copy of one version");
        LM::Ledger newerCopy = calm;
        newerCopy.version = 8;
        newerCopy.entries[0] = plain;
        newerCopy.entries[0].plugin = "Third.esp";
        (void)LM::Merge(calm, newerCopy, keptConflicts, "a newer copy");
        a_check(StateOf(calm, 0x902) == LM::State::kConflict && calm.entries[0].plugin == "Same.esp" && !keptConflicts.empty(),
            "review agy redo 3.1 / 3.2: a conflict another copy holds is kept, and a newer copy of the epoch does not settle it");

        // agy redo 1.3: with main readable, the base is of main's epoch (an older epoch's backup has a higher version)
        {
            const auto epochFolder = folder / "epochs";
            fs::create_directories(epochFolder, ec);
            const auto epochMain = epochFolder / "HoMBF_slots.json";
            LM::Ledger fresh;
            fresh.epoch = 200;
            fresh.version = 2;
            LM::Ledger stale;
            stale.epoch = 100;
            stale.version = 40;
            const bool wrote = write(epochMain, fresh) && write(LF::BackupPath(epochMain, 3), stale);
            const auto epochLoad = LF::LoadAll(epochMain, {});
            a_check(wrote && epochLoad.ledger && epochLoad.ledger->epoch == 200,
                "review agy redo 1.3: the base keeps main's epoch over an older epoch's backup with a higher version");
        }

        // Codex 3 P1: the files an object stands on, its own and its copy deps' (not the ADDN slots)
        LM::Object standing{ LM::Role::kModel, 1, "", "meshes\\heartofmagic\\blackflame\\copies\\0000000000000001\\copy.nif", 4, 0x11,
            { { true, 40000, "", 0 }, { false, 0, "textures\\heartofmagic\\blackflame\\copies\\0000000000000002\\copy.dds", 0x22 } } };
        const auto files = LM::FilesOf(standing);
        a_check(files.size() == 2 && files[0].second == 0x11 && files[1].second == 0x22 && files[1].first.find("copy.dds") != std::string::npos &&
                    LM::FilesOf(LM::Object{}).empty(),
            "review Codex 3 P1: FilesOf lists the object's own file and its copy deps, never an ADDN slot");
        // review round 1 D1: the Data file BuildProgress keeps for another build of its key is one a published object stands on
        {
            LM::Ledger holding;
            LM::Entry entry;
            entry.slot = 0x810;
            entry.published.push_back(standing);
            LM::Insert(holding, entry);
            const auto& dds = standing.deps[1].path;
            a_check(LM::PublishedHolds(holding, standing.path, 0x11) && LM::PublishedHolds(holding, dds, 0x22),
                "review round 1 D1: PublishedHolds finds an object's own file and a dep's file at the fnv64 the ledger recorded");
            a_check(!LM::PublishedHolds(holding, dds, 0x23) && !LM::PublishedHolds(holding, standing.path, 0x22),
                "review round 1 D1: the same path with another fnv64 (another build of the key) is not held");
            a_check(!LM::PublishedHolds(holding, "textures\\heartofmagic\\blackflame\\copies\\0000000000000003\\copy.dds", 0x22) &&
                        !LM::PublishedHolds(holding, "", 0) && !LM::PublishedHolds(LM::Ledger{}, dds, 0x22),
                "review round 1 D1: another path, an empty one (an ADDN dep's), or an empty ledger hold nothing");
            // (reviewer round 2) the path-first lookup KeepPublished and ApplyDeferred use before they hash a Data file
            auto earlier = standing;
            earlier.fnv64 = 0x10;
            LM::Entry second;
            second.slot = 0x811;
            second.published.push_back(earlier);
            LM::Insert(holding, second);
            const auto map = LM::PublishedFiles(holding);
            const auto own = map.find(standing.path);
            const auto dep = map.find(dds);
            a_check(map.size() == 2 && own != map.end() && own->second == std::set<std::uint64_t>{ 0x10, 0x11 } && dep != map.end() &&
                        dep->second == std::set<std::uint64_t>{ 0x22 },
                "reviewer round 2: PublishedFiles maps each path published objects stand on to every fnv64 recorded for it");
            a_check(map.count("") == 0 && map.count("textures\\heartofmagic\\blackflame\\copies\\0000000000000003\\copy.dds") == 0 &&
                        LM::PublishedFiles(LM::Ledger{}).empty(),
                "reviewer round 2: an ADDN dep, an unrecorded path or an empty ledger gives no entry (no Data file is hashed for it)");
            bool agrees = true;
            for (const auto& [path, fnv] : { std::pair<std::string, std::uint64_t>{ standing.path, 0x10 }, { standing.path, 0x11 },
                     { standing.path, 0x12 }, { dds, 0x22 }, { dds, 0x23 } }) {
                const auto it = map.find(path);
                agrees = agrees && LM::PublishedHolds(holding, path, fnv) == (it != map.end() && it->second.count(fnv) == 1);
            }
            a_check(agrees, "reviewer round 2: a PublishedFiles lookup agrees with PublishedHolds path by path");
        }
        // Codex: main that cannot be written (here a name Windows refuses) leaves the mirror written, and says so
        {
            const auto badMain = folder / "bad<name>.json";
            const auto goodMirror = folder / "mirror" / "HoMBF_slots.0000beef.json";
            LM::Ledger kept;
            kept.epoch = 94;
            kept.version = 6;
            std::string mainWhy;
            std::string mirrorWhy;
            const bool saved = LF::SaveAll(kept, badMain, goodMirror, mainWhy, mirrorWhy);
            std::string readWhy;
            const auto mirrorRead = LF::ReadOne(goodMirror, readWhy);
            a_check(!saved && mirrorRead && mirrorRead->version == 6 && mirrorWhy == "the mirror was written",
                "review Codex: main not written, the mirror still is and the log names it (" + mainWhy + " / " + mirrorWhy + ")");
        }
        fs::remove_all(folder, ec);
    }
}
