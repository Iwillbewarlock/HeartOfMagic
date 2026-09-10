#include "librarian/Librarian.h"
#include "librarian/LibrarianInternal.h"
#include "librarian/TagVocabulary.h"
#include "SpellScanner.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

// =============================================================================
// LibrarianKeywordPatch - filling in the vanilla keywords a mod forgot
//
// Every perk that boosts fire damage asks the same question:
// HasMagicEffectKeyword MagicDamageFire. A modded fire spell without that
// keyword answers no, and the perk does nothing. The librarian already worked
// out that the effect is fire, so it adds the keyword the perks are looking
// for. Additive only - see the contract in Librarian.h.
//
// Note this classifies a base effect on its own rather than reading the
// catalog: a perk asks about the effect, and the catalog is keyed by spell. A
// consequence is that spell level rules cannot contribute here - see the
// comment on the wrap below.
// =============================================================================

namespace Librarian
{
    using namespace Librarian::Detail;

    namespace
    {
        constexpr const char* ADAPTER_FILE = "adapter_vanilla_keywords.json";
        constexpr const char* CONFIG_FILE = "config.json";
        constexpr const char* CONFIG_SECTION = "vanillaKeywordPatch";

        // What the last run did, so a scan taken afterwards can say so.
        KeywordPatchStats g_lastRun;

        // One "an effect tagged X, shaped like this, should carry keyword K".
        struct Adapter
        {
            std::string tag;         // element or technique the effect must have
            RuleMatch match;         // extra conditions on the effect itself
            std::string keyword;     // vanilla keyword editor id to add
        };

        struct Settings
        {
            bool enabled = true;
            std::unordered_set<std::string> excludedPlugins;  // lowercased
        };

        // The switch and the exclusion list live in the same config.json the
        // panel writes. Absent means on, because a user who has never opened
        // the settings should still get working perks.
        Settings LoadSettings()
        {
            Settings settings;

            json config;
            if (!ReadJsonFile(DataPath(CONFIG_FILE), config) || !config.is_object()) {
                return settings;
            }

            const auto section = config.find(CONFIG_SECTION);
            if (section == config.end() || !section->is_object()) {
                return settings;
            }

            const auto enabled = section->find("enabled");
            if (enabled != section->end() && enabled->is_boolean()) {
                settings.enabled = enabled->get<bool>();
            }

            const auto excluded = section->find("excludePlugins");
            if (excluded != section->end() && excluded->is_array()) {
                for (const auto& entry : *excluded) {
                    if (entry.is_string()) {
                        settings.excludedPlugins.insert(Lowered(entry.get<std::string>()));
                    }
                }
            }

            return settings;
        }

        std::vector<Adapter> LoadAdapters()
        {
            std::vector<Adapter> adapters;

            const auto path = RulesPath() / ADAPTER_FILE;
            json document;
            if (!ReadJsonFile(path, document)) {
                logger::info("Librarian: no keyword adapter at '{}'", path.string());
                return adapters;
            }

            const json* entries = nullptr;
            if (document.is_array()) {
                entries = &document;
            } else if (document.is_object()) {
                const auto found = document.find("adapters");
                if (found != document.end() && found->is_array()) {
                    entries = &(*found);
                }
            }
            if (!entries) {
                logger::warn("Librarian: '{}' has no adapters array", ADAPTER_FILE);
                return adapters;
            }

            std::size_t rejected = 0;
            for (const auto& entry : *entries) {
                if (!entry.is_object()) {
                    continue;
                }

                Adapter adapter;
                const auto tag = entry.find("tag");
                const auto keyword = entry.find("keyword");
                if (tag == entry.end() || keyword == entry.end()
                    || !tag->is_string() || !keyword->is_string()) {
                    continue;
                }
                adapter.tag = tag->get<std::string>();
                adapter.keyword = keyword->get<std::string>();

                // Same gate the rule files get. Without it a typo here would
                // simply never match and never say anything.
                if (!IsElement(adapter.tag) && !IsTechnique(adapter.tag)) {
                    ++rejected;
                    logger::warn("Librarian: '{}' names '{}', which is not a known tag - dropped",
                        ADAPTER_FILE, adapter.tag);
                    continue;
                }

                // The condition reuses the rule matcher, so an adapter can say
                // "only when the effect actually damages" without inventing a
                // second matching language.
                const auto match = entry.find("match");
                if (match != entry.end()) {
                    adapter.match = ParseRuleMatch(*match);
                }

                adapters.push_back(std::move(adapter));
            }

            logger::info("Librarian: loaded {} keyword adapters ({} dropped)",
                adapters.size(), rejected);
            return adapters;
        }

        // Every keyword the adapters name, resolved once. A keyword the load
        // order does not have stays null and its adapters are skipped - the
        // patch never invents a keyword no perk is looking for.
        std::unordered_map<std::string, RE::BGSKeyword*> ResolveKeywords(
            const std::vector<Adapter>& adapters)
        {
            std::unordered_map<std::string, RE::BGSKeyword*> resolved;
            for (const auto& adapter : adapters) {
                resolved.emplace(adapter.keyword, nullptr);
            }

            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) {
                return resolved;
            }

            for (auto* keyword : dataHandler->GetFormArray<RE::BGSKeyword>()) {
                if (!keyword) {
                    continue;
                }
                const char* editorId = keyword->GetFormEditorID();
                if (!editorId) {
                    continue;
                }
                const auto found = resolved.find(editorId);
                if (found != resolved.end()) {
                    found->second = keyword;
                }
            }

            for (const auto& [editorId, keyword] : resolved) {
                if (!keyword) {
                    logger::info("Librarian: '{}' is not in this load order - its adapters are inert",
                        editorId);
                }
            }
            return resolved;
        }

        bool HasTag(const TagSet& tags, const std::string& tag)
        {
            return tags.elements.count(tag) > 0 || tags.techniques.count(tag) > 0;
        }

        // Nothing in the rule set can tag an effect that carries no keywords
        // and no archetype, so skip it before paying for a JSON build and a
        // pass over every rule. On a large load order most records are these.
        bool CouldMatchAnything(const RE::EffectSetting* baseEffect)
        {
            return baseEffect->GetNumKeywords() > 0
                || baseEffect->data.archetype != RE::EffectArchetype::kValueModifier
                || baseEffect->data.primaryAV != RE::ActorValue::kNone
                || baseEffect->data.resistVariable != RE::ActorValue::kNone;
        }
    }

    // =========================================================================
    // PATCH
    // =========================================================================

    KeywordPatchStats LastKeywordPatchStats()
    {
        return g_lastRun;
    }

    KeywordPatchStats ApplyVanillaKeywordPatch()
    {
        KeywordPatchStats stats;
        g_lastRun = stats;

        const Settings settings = LoadSettings();
        if (!settings.enabled) {
            logger::info("Librarian: vanilla keyword patch is switched off");
            return stats;
        }

        const std::vector<Adapter> adapters = LoadAdapters();
        if (adapters.empty()) {
            return stats;
        }

        const RuleSet rules = LoadRules(RulesPath().string());
        if (rules.rules.empty()) {
            logger::warn("Librarian: no classification rules - keyword patch has nothing to go on");
            return stats;
        }

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            logger::error("Librarian: no data handler - keyword patch skipped");
            return stats;
        }

        const auto keywords = ResolveKeywords(adapters);
        const bool haveExclusions = !settings.excludedPlugins.empty();

        // The rules read the same fields the scanner emits, so build each
        // effect the way a scan would.
        SpellScanner::FieldConfig fields;
        fields.effectDetails = true;

        std::vector<RE::BGSKeyword*> toAdd;

        for (auto* baseEffect : dataHandler->GetFormArray<RE::EffectSetting>()) {
            if (!baseEffect || !CouldMatchAnything(baseEffect)) {
                continue;
            }
            ++stats.effectsSeen;

            if (haveExclusions) {
                const std::string plugin =
                    Lowered(SpellScanner::GetPluginName(baseEffect->GetFormID()));
                if (settings.excludedPlugins.count(plugin) > 0) {
                    ++stats.excluded;
                    continue;
                }
            }

            // Classify this one effect on its own. A perk asks about the
            // effect, not the spell that happens to carry it.
            //
            // The wrap has no spell level keywords, so rules matching on
            // spellKeyword - all of 10_nsv.json - cannot fire here. That is
            // deliberate: an NSV tag describes the spell, and the keyword being
            // added belongs to one effect of it.
            json effectJson = SpellScanner::BuildBaseEffectJson(baseEffect, fields);
            json asSpell;
            asSpell["effects"] = json::array({ effectJson });

            const TagSet tags = Classify(asSpell, rules);
            if (tags.Empty()) {
                continue;
            }

            toAdd.clear();
            for (const auto& adapter : adapters) {
                if (!HasTag(tags, adapter.tag)) {
                    continue;
                }
                if (!EffectMatches(effectJson, adapter.match)) {
                    continue;
                }

                const auto keyword = keywords.find(adapter.keyword);
                if (keyword == keywords.end() || !keyword->second) {
                    continue;
                }
                if (baseEffect->HasKeyword(keyword->second)) {
                    ++stats.keywordsAlreadyPresent;
                    continue;
                }
                // An adapter list can name the same keyword from two tags.
                if (std::find(toAdd.begin(), toAdd.end(), keyword->second) == toAdd.end()) {
                    toAdd.push_back(keyword->second);
                }
            }

            if (toAdd.empty()) {
                continue;
            }

            // AddKeywords always reports success, so count what actually
            // landed rather than trusting its return value.
            const std::uint32_t before = baseEffect->GetNumKeywords();
            baseEffect->AddKeywords(toAdd);
            const std::uint32_t after = baseEffect->GetNumKeywords();

            if (after > before) {
                ++stats.effectsPatched;
                stats.keywordsAdded += (after - before);
            } else {
                logger::warn("Librarian: {} keyword(s) did not take on effect {:08X}",
                    toAdd.size(), baseEffect->GetFormID());
            }
        }

        logger::info("Librarian: keyword patch added {} keywords to {} of {} candidate effects "
                     "({} keywords already present, {} effects skipped by plugin)",
            stats.keywordsAdded, stats.effectsPatched, stats.effectsSeen,
            stats.keywordsAlreadyPresent, stats.excluded);

        g_lastRun = stats;
        return stats;
    }
}
