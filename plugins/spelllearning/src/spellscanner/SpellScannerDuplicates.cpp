#include "Common.h"
#include "EncodingUtils.h"
#include "SpellScanner.h"
#include "tomereach/TomeReach.h"

#include <mutex>

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
    // Reading one of their tomes reads the kept spell's (SpellTomeHook).

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

            std::string key = editorId;
            std::transform(key.begin(), key.end(), key.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            key += '|';
            key += std::to_string(static_cast<int>(school));
            return key;
        }

        std::string Describe(RE::SpellItem* spell)
        {
            return std::format("0x{:08X} ({})", spell->GetFormID(), GetPluginName(spell->GetFormID()));
        }
    }

    std::unordered_map<RE::FormID, RE::FormID> FindDuplicateSpells(const std::vector<RE::SpellItem*>& spells,
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

        std::unordered_map<RE::FormID, RE::FormID> duplicateOf;
        std::size_t merged = 0;
        for (auto& [key, members] : groups) {
            if (members.size() < 2) continue;
            std::ranges::sort(members, [&](RE::SpellItem* a, RE::SpellItem* b) {
                const auto pa = positionOf(a), pb = positionOf(b);
                return pa != pb ? pa < pb : a->GetFormID() < b->GetFormID();
            });
            const auto reachable = std::ranges::find_if(members,
                [&](RE::SpellItem* s) { return !unreachable.contains(s->GetFormID()); });
            RE::SpellItem* kept = reachable != members.end() ? *reachable : members.front();

            std::string dropped;
            for (auto* spell : members) {
                if (spell == kept) continue;
                duplicateOf[spell->GetFormID()] = kept->GetFormID();
                if (!dropped.empty()) dropped += ", ";
                dropped += Describe(spell);
            }
            ++merged;
            logger::debug("SpellScanner: duplicate '{}' ({}): kept {}, merged {}",
                EncodingUtils::SanitizeToUTF8(kept->GetFullName()), key, Describe(kept), dropped);
        }
        logger::info("SpellScanner: {} duplicate spells merged into {}", duplicateOf.size(), merged);
        return duplicateOf;
    }

    RE::SpellItem* TreeSpellForTome(RE::SpellItem* spell)
    {
        if (!spell) return spell;

        // Built once the tome reach pass is done; until then (or when it failed,
        // as the scan sees it too) without tomeUnreachable, and built again on
        // the next call that finds the pass done
        static std::mutex lock;
        static std::unordered_map<RE::FormID, RE::FormID> duplicateOf;
        static bool built = false;
        static bool settled = false;

        std::lock_guard guard(lock);
        if (!settled) {
            std::unordered_set<RE::FormID> unreachable;
            const bool ready = TomeReach::UnreachableSpells(unreachable);
            if (!built || ready) {
                auto* dh = RE::TESDataHandler::GetSingleton();
                if (!dh) return spell;
                const auto& all = dh->GetFormArray<RE::SpellItem>();
                const std::vector<RE::SpellItem*> spells(all.begin(), all.end());
                duplicateOf = FindDuplicateSpells(spells, SpellsTaughtByTomes(), unreachable);
                built = true;
                settled = ready;
            }
        }

        const auto it = duplicateOf.find(spell->GetFormID());
        if (it == duplicateOf.end()) return spell;
        auto* kept = RE::TESForm::LookupByID<RE::SpellItem>(it->second);
        return kept ? kept : spell;
    }
}
