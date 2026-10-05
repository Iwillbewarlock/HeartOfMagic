#include "Common.h"
#include "JsonText.h"
#include "SpellScanner.h"

#include <fstream>
#include <mutex>
#include <set>

// =============================================================================
// SPELL GATE - is a tome spell a player's spell? (docs/librarian/ToDo.md, M9)
// =============================================================================
//
// Evidence from the record, each with a reason the panel shows. "out" leaves the
// tree and Spell Research; "review" stays in and goes to the panel's review list,
// where the player keeps it or blacklists it; "in" is everything else.
//
// Measured on TAKEALOOK (2026-10-06, lab/spell-gate): the free rule alone caught 37
// of the 89 spells judged "not a player spell" with no false hit; a summon over an
// hour (mounts and companions that stay - Witcher Horses 99 days, nixads 300 days)
// and banish-only spells (a mount's dismissal) add the rest of what a record field
// shows. Horse summons that fight (skeletal horse, phantom mare) and named unique
// summons are left to the player (review). Families no field ties together - an
// adult framework's detect and inject spells - come from the rules file below, a
// set by plugin masters and effect archetypes, never by spell name.

namespace SpellScanner
{
    namespace
    {
        // Below this a spell costs nothing to cast (IsFreeSpell)
        constexpr float kFreeSpellCost = 0.5f;
        // A summon that lasts longer than this is a companion or a mount that stays,
        // not help in a fight (combat summons last seconds to a few minutes)
        constexpr float kLongSummonSeconds = 3600.0f;
        // A script-only spell this cheap is likely a tool rather than magic (review only)
        constexpr float kCheapScriptCost = 20.0f;

        constexpr const char* kRulesPath = "Data/SKSE/Plugins/SpellLearning/gate/rules.json";

        struct GateRule
        {
            std::string id;
            std::string band;                        // "out" or "review"
            std::vector<std::string> masters;        // the spell's plugin depends on one of these (lowercase)
            std::set<RE::EffectArchetype> archetypes; // every effect of the spell is one of these (empty: any)
        };

        std::string Lower(std::string text)
        {
            std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        std::optional<RE::EffectArchetype> ArchetypeByName(const std::string& name)
        {
            static const std::unordered_map<std::string, RE::EffectArchetype> kByName = {
                { "Script", RE::EffectArchetype::kScript },
                { "DetectLife", RE::EffectArchetype::kDetectLife },
                { "ValueModifier", RE::EffectArchetype::kValueModifier },
                { "PeakValueModifier", RE::EffectArchetype::kPeakValueModifier },
                { "SummonCreature", RE::EffectArchetype::kSummonCreature },
                { "SpawnScriptedRef", RE::EffectArchetype::kSpawnScriptedRef },
                { "Banish", RE::EffectArchetype::kBanish },
                { "Light", RE::EffectArchetype::kLight },
                { "Telekinesis", RE::EffectArchetype::kTelekinesis },
                { "Invisibility", RE::EffectArchetype::kInvisibility },
            };
            const auto it = kByName.find(name);
            return it == kByName.end() ? std::nullopt : std::optional(it->second);
        }

        // A rule's text field; anything but a string (a number, null) reads as the default
        std::string TextField(const json& entry, const char* name, const char* fallback)
        {
            const auto it = entry.find(name);
            return (it != entry.end() && it->is_string()) ? it->get<std::string>() : std::string(fallback);
        }

        // gate/rules.json, read once: {"rules": [{"id", "band", "masters": [...], "archetypes": [...]}]}.
        // Nothing in the file may throw out of here: the scan, a saved tree's load and
        // Spell Research all judge spells through it.
        const std::vector<GateRule>& Rules()
        {
            static std::vector<GateRule> rules;
            static std::once_flag once;
            std::call_once(once, [] {
                std::ifstream file(kRulesPath);
                if (!file.is_open()) {
                    logger::info("SpellGate: no {} - only the built-in evidence", kRulesPath);
                    return;
                }
                json document;
                try {
                    file >> document;
                } catch (const std::exception& e) {
                    logger::error("SpellGate: {} is not valid JSON - {}", kRulesPath, e.what());
                    return;
                }
                if (!document.is_object()) return;
                const auto list = document.find("rules");
                if (list == document.end() || !list->is_array()) return;
                for (const auto& entry : *list) {
                    if (!entry.is_object()) continue;
                    GateRule rule;
                    rule.id = TextField(entry, "id", "");
                    const auto band = entry.find("band");
                    rule.band = band == entry.end() ? "out" : TextField(entry, "band", "");
                    if (rule.id.empty() || (rule.band != "out" && rule.band != "review")) {
                        logger::warn("SpellGate: rule without an id or with band '{}' - skipped", rule.band);
                        continue;
                    }
                    const auto masters = entry.find("masters");
                    const auto names = entry.find("archetypes");
                    if (masters == entry.end() || !masters->is_array() ||
                        (names != entry.end() && !names->is_array())) {
                        logger::warn("SpellGate: rule '{}': masters and archetypes must be lists - skipped", rule.id);
                        continue;
                    }
                    for (const auto& master : *masters) {
                        if (master.is_string()) rule.masters.push_back(Lower(master.get<std::string>()));
                    }
                    bool known = true;
                    for (const auto& name : names != entry.end() ? *names : json::array()) {
                        const auto archetype = name.is_string() ? ArchetypeByName(name.get<std::string>()) : std::nullopt;
                        if (!archetype) {
                            known = false;
                            break;
                        }
                        rule.archetypes.insert(*archetype);
                    }
                    if (!known || rule.masters.empty()) {
                        logger::warn("SpellGate: rule '{}' has no masters or an archetype it does not know - skipped",
                            rule.id);
                        continue;
                    }
                    rules.push_back(std::move(rule));
                }
                logger::info("SpellGate: {} rules from {}", rules.size(), kRulesPath);
            });
            return rules;
        }

        // The plugin is one of the masters, or depends on one directly
        bool PluginDependsOn(RE::TESFile* file, const std::vector<std::string>& masters)
        {
            if (!file) return false;
            if (std::ranges::find(masters, Lower(std::string(file->GetFilename()))) != masters.end()) return true;
            for (const char* master : file->masters) {
                if (master && std::ranges::find(masters, Lower(master)) != masters.end()) return true;
            }
            return false;
        }

        bool IsHorse(RE::TESNPC* npc)
        {
            const auto* race = npc ? npc->GetRace() : nullptr;
            if (!race) return false;
            for (const auto* keyword : race->GetKeywords()) {
                const char* editorId = keyword ? keyword->GetFormEditorID() : nullptr;
                if (editorId && std::string_view(editorId).find("Horse") != std::string_view::npos) return true;
            }
            return false;
        }
    }

    bool IsFreeSpell(RE::SpellItem* spell)
    {
        return spell && spell->CalculateMagickaCost(nullptr) < kFreeSpellCost;
    }

    GateVerdict JudgeSpell(RE::SpellItem* spell)
    {
        GateVerdict verdict;
        if (!spell) return verdict;
        const auto out = [&](std::string reason) {
            verdict.band = "out";
            verdict.reasons.push_back(std::move(reason));
        };
        const auto review = [&](std::string reason) {
            if (verdict.band == "in") verdict.band = "review";
            verdict.reasons.push_back(std::move(reason));
        };

        const float cost = spell->CalculateMagickaCost(nullptr);
        if (cost < kFreeSpellCost) out("free");

        std::set<RE::EffectArchetype> archetypes;
        bool longSummon = false, horse = false, unique = false;
        for (const auto* effect : spell->effects) {
            const auto* base = effect ? effect->baseEffect : nullptr;
            if (!base) continue;
            archetypes.insert(base->data.archetype);
            const bool summon = base->data.archetype == RE::EffectArchetype::kSummonCreature ||
                base->data.archetype == RE::EffectArchetype::kReanimate;
            if (!summon) continue;
            if (static_cast<float>(effect->effectItem.duration) > kLongSummonSeconds) longSummon = true;
            std::vector<RE::TESNPC*> npcs;
            CollectSummonedNPCs(base->data.associatedForm, npcs);
            for (auto* npc : npcs) {
                if (IsHorse(npc)) horse = true;
                if (npc->IsUnique()) unique = true;
            }
        }
        if (longSummon) out("longSummon");
        if (!archetypes.empty() && archetypes.size() == 1 && archetypes.contains(RE::EffectArchetype::kBanish)) {
            out("banishOnly");
        }

        auto* file = spell->GetFile(0);
        for (const auto& rule : Rules()) {
            if (!PluginDependsOn(file, rule.masters)) continue;
            // A spell with no effects is not "every effect one of these"
            const bool fits = !archetypes.empty() && (rule.archetypes.empty() ||
                std::ranges::all_of(archetypes, [&](RE::EffectArchetype a) { return rule.archetypes.contains(a); }));
            if (!fits) continue;
            if (rule.band == "out") {
                out("rule:" + rule.id);
            } else {
                review("rule:" + rule.id);
            }
        }

        if (verdict.band != "out") {
            if (horse) review("horseSummon");
            if (unique) review("uniqueSummon");
            if (!archetypes.empty() && archetypes.size() == 1 && archetypes.contains(RE::EffectArchetype::kScript) &&
                cost < kCheapScriptCost) {
                review("cheapScript");
            }
        }
        return verdict;
    }

    void MarkGate(json& spellJson, RE::SpellItem* spell)
    {
        if (IsFreeSpell(spell)) spellJson["freeSpell"] = true;
        const auto verdict = JudgeSpell(spell);
        if (verdict.band != "in") {
            spellJson["gate"] = { { "band", verdict.band }, { "reasons", verdict.reasons } };
        }
    }
}
