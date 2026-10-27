#include "Common.h"
#include "AsciiText.h"
#include "FormIdText.h"
#include "EncodingUtils.h"
#include "ProgressionManager.h"
#include "SpellScanner.h"
#include "ThreadUtils.h"
#include "tomereach/TomeReach.h"

#include <chrono>
#include <mutex>
#include <thread>

namespace SpellScanner
{
    // =============================================================================
    // ONE SPELL FROM TWO PLUGINS
    // =============================================================================
    //
    // A mod and its add-on can both carry a spell: Saints and Seducers' Conjure
    // Hunger (EC_SS_ConjureHunger) and ECSS Summonable Creatures' own record with
    // the same editor id, each with its own tome. The scan and the tree go by
    // FormID, so both landed in the tree and both tomes taught a copy.
    //
    // Same spell here: the same editor id (case ignored) in the same school, among
    // the spells the tree could take (a tome teaches it, not in the voice slot).
    // Not the name - translations give different spells one name (Korean Calm and
    // Pacify). Spells without an editor id (SE without po3's Tweaks) never merge.
    //
    // One record per group stays: the one from the earliest plugin in the load
    // order, unless nothing hands out its tomes (tomeUnreachable) and something
    // hands out another's - then the earliest of those. The others are marked
    // duplicateOf in the scan (they stay for the librarian and the perk adapters)
    // and the tree leaves them out (isTaughtByTome, proceduralTreeBuilder.js).
    // Reading a tome of a group member that is not a node of the tree in use reads
    // the one member that is (SpellTomeHook, TreeSpellForTome): usually the kept
    // spell, but a tree built before the copy was marked, or by hand in edit mode,
    // can hold another member. With no member a node, or two, the tome teaches its
    // own spell.
    //
    // The voice slot check reads the record (its equip slot against the game's
    // voice slot default object), not what the player has equipped, so the groups
    // do not change with the player.

    namespace
    {
        constexpr std::size_t kUnknownLoadPosition = (std::numeric_limits<std::size_t>::max)();

        // Each loaded plugin's place in the load order (the TomeReach pattern)
        std::unordered_map<const RE::TESFile*, std::size_t> LoadPositions()
        {
            std::unordered_map<const RE::TESFile*, std::size_t> positions;
            auto* dh = RE::TESDataHandler::GetSingleton();
            if (!dh) return positions;

            std::unordered_set<const RE::TESFile*> loaded;
            for (std::uint32_t i = 0; i < dh->GetLoadedModCount(); ++i) loaded.insert(dh->GetLoadedMods()[i]);
            for (std::uint32_t i = 0; i < dh->GetLoadedLightModCount(); ++i) loaded.insert(dh->GetLoadedLightMods()[i]);
            for (const RE::TESFile* file : dh->files) {
                if (file && loaded.contains(file)) positions.emplace(file, positions.size());
            }
            return positions;
        }

        // The group a spell belongs to, or empty when it takes no part
        std::string DuplicateKey(RE::SpellItem* spell, const std::unordered_set<RE::FormID>& taught)
        {
            if (!spell || !taught.contains(spell->GetFormID())) return {};
            const char* editorId = spell->GetFormEditorID();
            const char* name = spell->GetFullName();
            if (!editorId || !*editorId || !name || !*name) return {};
            const RE::ActorValue school = GetSpellSchool(spell);
            if (!IsValidMagicSchool(school) || IsEquippedInVoiceSlot(spell)) return {};

            std::string key = AsciiText::ToLower(editorId);
            key += '|';
            key += std::to_string(static_cast<int>(school));
            return key;
        }

        std::string Describe(RE::SpellItem* spell)
        {
            return std::format("0x{:08X} ({})", spell->GetFormID(), GetPluginName(spell->GetFormID()));
        }

        // The groups of two or more, each sorted with the kept record first
        std::vector<std::vector<RE::SpellItem*>> FindDuplicateGroups(const std::vector<RE::SpellItem*>& spells,
            const std::unordered_set<RE::FormID>& taught, const std::unordered_set<RE::FormID>& unreachable)
        {
            std::unordered_map<std::string, std::vector<RE::SpellItem*>> groups;
            for (auto* spell : spells) {
                std::string key = DuplicateKey(spell, taught);
                if (!key.empty()) groups[std::move(key)].push_back(spell);
            }

            const auto positions = LoadPositions();
            auto positionOf = [&positions](RE::SpellItem* spell) {
                const auto it = positions.find(spell->GetFile(0));
                return it != positions.end() ? it->second : kUnknownLoadPosition;
            };

            std::vector<std::vector<RE::SpellItem*>> found;
            for (auto& [key, members] : groups) {
                if (members.size() < 2) continue;
                std::ranges::sort(members, [&](RE::SpellItem* a, RE::SpellItem* b) {
                    const auto pa = positionOf(a), pb = positionOf(b);
                    return pa != pb ? pa < pb : a->GetFormID() < b->GetFormID();
                });
                const auto reachable = std::ranges::find_if(members,
                    [&](RE::SpellItem* s) { return !unreachable.contains(s->GetFormID()); });
                if (reachable != members.end()) std::rotate(members.begin(), reachable, reachable + 1);
                found.push_back(std::move(members));
            }
            return found;
        }
    }

    std::unordered_map<RE::FormID, RE::FormID> FindDuplicateSpells(const std::vector<RE::SpellItem*>& spells,
        const std::unordered_set<RE::FormID>& taught, const std::unordered_set<RE::FormID>& unreachable)
    {
        std::unordered_map<RE::FormID, RE::FormID> duplicateOf;
        std::size_t merged = 0;
        for (const auto& members : FindDuplicateGroups(spells, taught, unreachable)) {
            RE::SpellItem* kept = members.front();

            std::string dropped;
            for (auto* spell : members) {
                if (spell == kept) continue;
                duplicateOf[spell->GetFormID()] = kept->GetFormID();
                if (!dropped.empty()) dropped += ", ";
                dropped += Describe(spell);
            }
            ++merged;
            logger::debug("SpellScanner: duplicate '{}' ({}): kept {}, merged {}",
                EncodingUtils::SanitizeToUTF8(kept->GetFullName()),
                EncodingUtils::SanitizeToUTF8(kept->GetFormEditorID()), Describe(kept), dropped);
        }
        logger::info("SpellScanner: {} duplicate spells merged into {}", duplicateOf.size(), merged);
        return duplicateOf;
    }

    void MarkDuplicateSpells(json& spellArray, const std::vector<RE::SpellItem*>& spells)
    {
        std::unordered_set<RE::FormID> unreachable;
        TomeReach::UnreachableSpells(unreachable);
        const auto duplicates = FindDuplicateSpells(spells, SpellsTaughtByTomes(), unreachable);
        for (std::size_t i = 0; i < spells.size() && i < spellArray.size(); ++i) {
            if (auto it = duplicates.find(spells[i]->GetFormID()); it != duplicates.end()) {
                spellArray[i]["duplicateOf"] = FormIdText::Format(it->second);
            }
        }
    }

    namespace
    {
        // The groups as TreeSpellForTome reads them. Built once the tome reach pass
        // is done; until then (or when it failed, as the scan sees it too) without
        // tomeUnreachable, and built again on the next call that finds the pass done
        std::mutex g_duplicateLock;
        std::vector<std::vector<RE::FormID>> g_groups;               // each group's members
        std::unordered_map<RE::FormID, std::size_t> g_groupOf;      // member -> index in g_groups
        bool g_duplicatesBuilt = false;
        bool g_duplicatesSettled = false;

        // Under g_duplicateLock
        void BuildDuplicateGroups()
        {
            if (g_duplicatesSettled) return;
            std::unordered_set<RE::FormID> unreachable;
            const bool ready = TomeReach::UnreachableSpells(unreachable);
            if (g_duplicatesBuilt && !ready) return;
            auto* dh = RE::TESDataHandler::GetSingleton();
            if (!dh) return;
            const auto& all = dh->GetFormArray<RE::SpellItem>();
            const std::vector<RE::SpellItem*> spells(all.begin(), all.end());
            g_groups.clear();
            g_groupOf.clear();
            for (const auto& members : FindDuplicateGroups(spells, SpellsTaughtByTomes(), unreachable)) {
                std::vector<RE::FormID> ids;
                for (auto* member : members) {
                    g_groupOf[member->GetFormID()] = g_groups.size();
                    ids.push_back(member->GetFormID());
                }
                g_groups.push_back(std::move(ids));
            }
            g_duplicatesBuilt = true;
            g_duplicatesSettled = ready;
        }
    }

    void PrepareTreeSpellForTome()
    {
        std::lock_guard guard(g_duplicateLock);
        BuildDuplicateGroups();
    }

    void InvalidateDuplicateGroups()
    {
        std::lock_guard guard(g_duplicateLock);
        g_groups.clear();
        g_groupOf.clear();
        g_duplicatesBuilt = false;
        g_duplicatesSettled = false;
    }

    void PrepareTreeSpellForTomeAfterPass()
    {
        // Copy tomes are grouped on first use when no thread can wait
        TomeReach::RunAfterPass("PrepareTreeSpellForTome", [](bool) { PrepareTreeSpellForTome(); });
    }

    std::vector<RE::FormID> DuplicateGroupOf(RE::FormID spell)
    {
        std::lock_guard guard(g_duplicateLock);
        BuildDuplicateGroups();
        const auto it = g_groupOf.find(spell);
        return it == g_groupOf.end() ? std::vector<RE::FormID>() : g_groups[it->second];
    }

    RE::SpellItem* TreeSpellForTome(RE::SpellItem* spell)
    {
        // The tree in use decides, not the load order alone: the groups come from
        // the spells loaded now, and a plugin added after the tree was built can
        // name a record the tree does not hold. A tree node's tome teaches that
        // node; another member's tome reads as the node's when exactly one member
        // is a node (the kept one, or the copy an older tree holds).
        auto* pm = ProgressionManager::GetSingleton();
        if (!spell || !pm || pm->IsTreeSpell(spell->GetFormID())) return spell;

        std::vector<RE::FormID> members;  // copied out: IsTreeSpell is not asked under the lock
        {
            std::lock_guard guard(g_duplicateLock);
            BuildDuplicateGroups();
            const auto it = g_groupOf.find(spell->GetFormID());
            if (it == g_groupOf.end()) return spell;
            members = g_groups[it->second];
        }
        RE::FormID nodeId = 0;
        for (const RE::FormID member : members) {
            if (!pm->IsTreeSpell(member)) continue;
            if (nodeId != 0) return spell;  // two members are nodes: each tome teaches its own
            nodeId = member;
        }
        if (nodeId == 0) return spell;
        auto* node = RE::TESForm::LookupByID<RE::SpellItem>(nodeId);
        return node ? node : spell;
    }
}
