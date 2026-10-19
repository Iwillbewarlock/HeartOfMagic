#include "environment/EnvironmentSelfTest.h"
#include "environment/EnvironmentTwins.h"
#include "librarian/LoadOrderSource.h"
#include "librarian/PerkAdapterPatch.h"
#include "librarian/PerkAdapters.h"
#include "SpellScanner.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <fstream>
#include <optional>

// =============================================================================
// EnvironmentSelfTestChecks - what the 1.6 environment self-test checks
//
//   H2           does a spell's (virtual) HasKeyword answer for its effects'
//                keywords, including one added to an effect at runtime? Decides
//                whether the OIF / candle adapter packs need spell-record targets
//   twins        HeartOfMagic.esp's twin is where it should be, with the original's values
//   frostwalker  Frostwalker's own matcher on the installed lists: original cold, twin unknown
//   adapters     perk_adapters_report.json lists the env-* files as config and plugins say;
//                Desecration's necrotic spells got no MagicDamageFire from here
// =============================================================================

namespace Environment::SelfTest
{
    namespace
    {
        using json = nlohmann::json;

        constexpr const char* kFlamesId = "Skyrim.esm|0x012FCD";
        constexpr const char* kMagicDamageFireId = "Skyrim.esm|0x01CEAD";
        constexpr const char* kShakeEffectId = "MysticismMagic.esp|0xBDE626";      // MAG_ScreenShakeEffect
        constexpr const char* kBlizzardHazardId = "Skyrim.esm|0x0B79FE";          // MAG_FrostDamageFFHazardArea
        constexpr const char* kShakeOriginalId = "Skyrim.esm|0x0B7A05";           // MAG_BlizzardForceExplosion
        constexpr const char* kConfigPath = "Data/SKSE/Plugins/SpellLearning/config.json";
        constexpr const char* kReportPath = "Data/SKSE/Plugins/SpellLearning/perk_adapters_report.json";
        constexpr const char* kAdapterDir = "Data/SKSE/Plugins/SpellLearning/librarian/adapters/";
        constexpr const char* kNecroticPrefix = "DAR_Necrotic";
        // Keywords that mean nothing to a spell or a magic effect, tried in turn
        // as H2's throwaway: added to one effect of Flames, read, taken off
        constexpr const char* kThrowawayKeywords[] = { "ActorTypeDwarven", "ActorTypeGiant", "ActorTypeHorse" };

        const char* YesNo(bool value)
        {
            return value ? "yes" : "no";
        }

        template <class T>
        T* Resolve(const char* persistentId)
        {
            const RE::FormID formId = SpellScanner::ResolvePersistentFormId(persistentId);
            return formId ? RE::TESForm::LookupByID<T>(formId) : nullptr;
        }

        bool PluginLoaded(const char* name)
        {
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            return dataHandler && Librarian::PluginLoaded(dataHandler, name);
        }

        std::optional<json> ReadJson(const std::string& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) return std::nullopt;
            json document = json::parse(file, nullptr, false);
            if (document.is_discarded()) return std::nullopt;
            return document;
        }

        bool EnvironmentSwitchOn()
        {
            const auto config = ReadJson(kConfigPath);
            return !config || Librarian::Adapters::ConfigFlagOn(*config, kSwitchPath);
        }

        RE::BGSKeyword* KeywordByEditorId(std::string_view editorId)
        {
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            for (auto* keyword : dataHandler->GetFormArray<RE::BGSKeyword>()) {
                const char* id = keyword ? keyword->GetFormEditorID() : nullptr;
                if (id && editorId == id) return keyword;
            }
            return nullptr;
        }

        std::string IdOf(const RE::TESForm* form)
        {
            return form ? SpellScanner::GetPersistentFormId(form->GetFormID()) : std::string("none");
        }

        const char* NameOf(const RE::BGSExplosion* explosion)
        {
            const char* name = static_cast<const RE::TESFullName*>(explosion)->GetFullName();
            return name ? name : "";
        }

        const char* ModelOf(const RE::BGSExplosion* explosion)
        {
            const char* model = static_cast<const RE::TESModel*>(explosion)->GetModel();
            return model ? model : "";
        }

        std::optional<TwinInfo> ShakeTwin()
        {
            for (const auto& twin : Twins()) {
                if (twin.originalId == kShakeOriginalId) return twin;
            }
            return std::nullopt;
        }

        // H2, part two: a keyword put on the effect now, read through the spell
        void CheckRuntimeKeyword(RE::SpellItem* spell, RE::EffectSetting* target)
        {
            const RE::BGSKeywordForm* spellKeywords = spell;
            RE::BGSKeyword* keyword = nullptr;
            for (const char* candidate : kThrowawayKeywords) {
                auto* found = KeywordByEditorId(candidate);
                if (!found || spellKeywords->HasKeywordID(found->GetFormID())) continue;
                const bool onAnEffect = std::any_of(spell->effects.begin(), spell->effects.end(), [&](const auto* effect) {
                    return effect && effect->baseEffect && effect->baseEffect->HasKeywordID(found->GetFormID());
                });
                if (!onAnEffect) {
                    keyword = found;
                    break;
                }
            }
            if (!keyword) {
                Report("H2-runtime", Outcome::kSkip, "no throwaway keyword free on Flames and its effects");
                return;
            }

            const bool virtualBefore = spellKeywords->HasKeyword(keyword);
            target->AddKeywords({ keyword });
            const bool added = target->HasKeywordID(keyword->GetFormID());
            bool virtualAfter = false;
            bool nonVirtualAfter = false;
            std::uint32_t spellCount = 0;
            try {
                virtualAfter = spellKeywords->HasKeyword(keyword);
                nonVirtualAfter = spellKeywords->HasKeywordID(keyword->GetFormID());
                spellCount = spellKeywords->GetNumKeywords();
            } catch (...) {
                target->RemoveKeywords({ keyword });
                throw;
            }
            target->RemoveKeywords({ keyword });
            const bool removed = !target->HasKeywordID(keyword->GetFormID());
            const bool virtualAfterRemove = spellKeywords->HasKeyword(keyword);

            const std::string observed = std::format(
                "keyword {} added to effect {}: added={} spell.HasKeyword(virtual) before={} after={} afterRemove={}; "
                "spell.HasKeywordID(non-virtual)={} spell.GetNumKeywords={} removed={}",
                keyword->GetFormEditorID(), SpellScanner::GetPersistentFormId(target->GetFormID()), YesNo(added),
                YesNo(virtualBefore), YesNo(virtualAfter), YesNo(virtualAfterRemove), YesNo(nonVirtualAfter), spellCount,
                YesNo(removed));
            // The edit itself not taking, or not coming off, is a defect; what the
            // virtual call sees is the experiment's answer
            if (!added || !removed) {
                Report("H2-runtime", Outcome::kFail, observed + " - the keyword edit did not take or did not come off");
                return;
            }
            if (virtualBefore || virtualAfterRemove) {
                Report("H2-runtime", Outcome::kSkip, observed + " - inconclusive: the spell saw the keyword without the edit");
                return;
            }
            Answer("H2-runtime", virtualAfter, observed);
            Note("H2-answer",
                std::format("observed: the spell's virtual HasKeyword {} a keyword added at runtime to one of its effects; "
                            "the non-virtual HasKeywordID on the spell {} it (it reads the spell record's own list). "
                            "Not observed here: whether OIF, Dynamic Candles and Put Out The Fires call the virtual "
                            "HasKeyword on the spell - unverified",
                    virtualAfter ? "sees" : "does not see", nonVirtualAfter ? "lists" : "does not list"));
        }
    }

    // =========================================================================
    // H2
    // =========================================================================

    void CheckH2()
    {
        auto* spell = Resolve<RE::SpellItem>(kFlamesId);
        auto* fire = Resolve<RE::BGSKeyword>(kMagicDamageFireId);
        if (!spell || !fire) {
            Report("H2-record", Outcome::kFail, std::format("Flames {} or MagicDamageFire {} not found", kFlamesId, kMagicDamageFireId));
            return;
        }
        const RE::BGSKeywordForm* spellKeywords = spell;
        RE::EffectSetting* fireEffect = nullptr;
        for (auto* effect : spell->effects) {
            if (effect && effect->baseEffect && effect->baseEffect->HasKeywordID(fire->GetFormID())) {
                fireEffect = effect->baseEffect;
                break;
            }
        }
        const bool recordHas = spellKeywords->HasKeywordID(fire->GetFormID());
        const bool virtualHas = spellKeywords->HasKeyword(fire);
        const std::string observed = std::format(
            "Flames: an effect carries MagicDamageFire={} ({}); the SPEL record's own list has it={}; "
            "spell.HasKeyword(virtual)={}; spell.GetNumKeywords={}",
            YesNo(fireEffect != nullptr), fireEffect ? SpellScanner::GetPersistentFormId(fireEffect->GetFormID()) : "none",
            YesNo(recordHas), YesNo(virtualHas), spellKeywords->GetNumKeywords());
        if (!fireEffect) {
            Report("H2-record", Outcome::kSkip, observed + " - no effect to read through");
        } else if (recordHas) {
            Report("H2-record", Outcome::kSkip, observed + " - inconclusive: the spell record carries it itself (H2-runtime decides)");
        } else {
            Answer("H2-record", virtualHas, observed);
        }

        RE::EffectSetting* target = fireEffect;
        if (!target && !spell->effects.empty() && spell->effects[0]) target = spell->effects[0]->baseEffect;
        if (!target) {
            Report("H2-runtime", Outcome::kSkip, "Flames has no effect");
            return;
        }
        CheckRuntimeKeyword(spell, target);
    }

    // =========================================================================
    // Twins
    // =========================================================================

    void CheckTwins()
    {
        if (!PluginLoaded("HeartOfMagic.esp")) {
            // Not a defect, but not a pass either: nothing was checked
            Report("twins", Outcome::kSkip, "HeartOfMagic.esp not loaded - the explosion twin fix is inactive, nothing checked");
            return;
        }
        const auto shake = ShakeTwin();
        if (!shake || !shake->original || !shake->twin) {
            Report("twins-loaded", Outcome::kFail, std::format("HeartOfMagic.esp is loaded but {} has no live twin in twins.json",
                kShakeOriginalId));
            return;
        }
        RE::BGSExplosion* original = shake->original;
        RE::BGSExplosion* twin = shake->twin;
        // Judged by the state the DLL applied (kDataLoaded or the last settings save), not by
        // config.json now: a hand edit of the file does not apply until then
        const bool on = SwitchOn();
        const std::size_t recorded = RecordedEffectCount();
        Note("twins", std::format("environment.enabled applied {} (config.json now says {}); {} effects recorded for a twin",
            on ? "on" : "off", EnvironmentSwitchOn() ? "on" : "off", recorded));

        if (!PluginLoaded("MysticismMagic.esp")) {
            Report("twins-shake", Outcome::kSkip, "MysticismMagic.esp not loaded");
        } else if (auto* effect = Resolve<RE::EffectSetting>(kShakeEffectId)) {
            RE::BGSExplosion* expected = on ? twin : original;
            const bool pointed = effect->data.explosion == expected;
            const bool scanned = ScannedExplosion(effect) == original;
            Report("twins-shake", recorded > 0 && pointed && scanned ? Outcome::kPass : Outcome::kFail,
                std::format("MAG_ScreenShakeEffect explosion={} (expected {}), the scan records {} (expected the original), "
                            "effects recorded={}",
                    IdOf(effect->data.explosion), on ? "the twin" : "the original", IdOf(ScannedExplosion(effect)), recorded));
        } else {
            Report("twins-shake", Outcome::kFail, std::format("{} not found though MysticismMagic.esp is loaded", kShakeEffectId));
        }

        if (auto* hazard = Resolve<RE::EffectSetting>(kBlizzardHazardId)) {
            Report("twins-hazard", hazard->data.explosion == original ? Outcome::kPass : Outcome::kFail,
                std::format("MAG_FrostDamageFFHazardArea explosion={} (expected the original {})",
                    IdOf(hazard->data.explosion),
                    kShakeOriginalId));
        } else {
            Report("twins-hazard", Outcome::kFail, std::format("{} not found", kBlizzardHazardId));
        }

        const bool dataSame = std::memcmp(&twin->data, &original->data, offsetof(RE::BGSExplosionData, pad4C)) == 0;
        const bool boundsSame = std::memcmp(&twin->boundData, &original->boundData, sizeof(twin->boundData)) == 0;
        const bool linksSame = twin->formEnchanting == original->formEnchanting &&
                               twin->castingType == original->castingType &&
                               twin->amountofEnchantment == original->amountofEnchantment &&
                               twin->imageSpaceModifying == original->imageSpaceModifying;
        const bool modelSame = std::strcmp(ModelOf(twin), ModelOf(original)) == 0;
        const bool nameless = NameOf(twin)[0] == '\0';
        Report("twins-values", dataSame && boundsSame && linksSame && (modelSame || !shake->modelCopied) && nameless ?
                                   Outcome::kPass : Outcome::kFail,
            std::format("DATA equal={} bounds equal={} enchantment/image space equal={} model equal={} ('{}', copied={}) "
                        "twin name empty={} (radius {} force {} damage {})",
                YesNo(dataSame), YesNo(boundsSame), YesNo(linksSame), YesNo(modelSame), ModelOf(twin),
                YesNo(shake->modelCopied), YesNo(nameless), twin->data.radius, twin->data.force, twin->data.damage));
    }

    // =========================================================================
    // Frostwalker's matcher
    // =========================================================================

    void CheckFrostwalker()
    {
        bool installed = false;
        const FrostwalkerPatterns patterns = LoadFrostwalkerPatterns(installed);
        if (!installed) {
            Report("frostwalker", Outcome::kSkip, "no Data/SKSE/Plugins/Frostwalker folder");
            return;
        }
        const auto shake = ShakeTwin();
        if (!shake || !shake->original || !shake->twin) {
            Report("frostwalker", Outcome::kSkip, "no live twin (HeartOfMagic.esp not loaded)");
            return;
        }
        const std::string originalEditorId = SpellScanner::GetEditorId(shake->original);
        const std::string twinEditorId = SpellScanner::GetEditorId(shake->twin);
        const FrostwalkerClass originalClass =
            ClassifyExplosion(originalEditorId, NameOf(shake->original), ModelOf(shake->original), patterns);
        const FrostwalkerClass twinClass = ClassifyExplosion(twinEditorId, NameOf(shake->twin), ModelOf(shake->twin), patterns);
        const std::string observed = std::format(
            "lists neutral {} cold {} fire {} water {}; original '{}' name '{}' model '{}' -> {}; twin '{}' name '{}' model '{}' -> {}",
            patterns.neutral.size(), patterns.cold.size(), patterns.fire.size(), patterns.water.size(), originalEditorId,
            NameOf(shake->original), ModelOf(shake->original), FrostwalkerClassName(originalClass), twinEditorId,
            NameOf(shake->twin), ModelOf(shake->twin), FrostwalkerClassName(twinClass));
        // The user's own lists decide; the original's class is reported, not required (JudgeTwin)
        switch (JudgeTwin(originalClass, twinClass)) {
            case TwinVerdict::kPass:
                Report("frostwalker", Outcome::kPass, observed);
                break;
            case TwinVerdict::kFail:
                Report("frostwalker", Outcome::kFail, observed + " - the twin reads as an element");
                break;
            default:
                // Frostwalker reads EditorIDs the same way (po3 Tweaks' GetFormEditorID)
                Report("frostwalker", Outcome::kSkip,
                    observed + (originalEditorId.empty() ?
                        " - no EditorIDs (po3 Tweaks' Load EditorIDs off): the original is not cold to Frostwalker either, the twin is not needed" :
                        " - the original is not cold with your patterns: the twin is not needed"));
                break;
        }
    }

    // =========================================================================
    // Adapter report
    // =========================================================================

    void CheckAdapters()
    {
        const auto report = ReadJson(kReportPath);
        if (!report || !report->is_object()) {
            Report("adapters", Outcome::kSkip, "no perk_adapters_report.json");
            return;
        }
        if (report->contains("stopped") && (*report)["stopped"].is_string()) {
            Report("adapters", Outcome::kSkip, "the adapters stopped: " + (*report)["stopped"].get<std::string>());
            return;
        }
        const json config = ReadJson(kConfigPath).value_or(json::object());
        const auto pluginLoaded = [](const std::string& name) { return PluginLoaded(name.c_str()); };
        const auto listedIn = [&](const char* key, const std::string& name) {
            const auto list = report->find(key);
            if (list == report->end() || !list->is_array()) return false;
            return std::any_of(list->begin(), list->end(), [&](const json& entry) {
                return (entry.is_string() && entry.get<std::string>() == name) ||
                       (entry.is_object() && entry.value("file", std::string()) == name);
            });
        };
        for (const std::string name : { "env-elements.json", "env-oif.json", "env-candles.json", "frostwalker.json" }) {
            const auto document = ReadJson(kAdapterDir + name);
            Librarian::Adapters::File file;
            std::vector<std::string> problems;
            if (!document || !Librarian::Adapters::ParseFile(*document, name, file, problems)) {
                Report("adapters-" + name, Outcome::kFail, "not installed or not an adapter file");
                continue;
            }
            // The config gate as the report's run judged it (configJudged), so a
            // config.json edited since does not read as a fault; the plugins and
            // DLLs are as loaded now, which they were then too
            const bool configNow = file.requiresConfig.empty() ||
                                   Librarian::Adapters::ConfigFlagOn(config, file.requiresConfig);
            std::optional<bool> configThen;
            const auto judged = report->find("configJudged");
            if (!file.requiresConfig.empty() && judged != report->end() && judged->is_object()) {
                const auto entry = judged->find(file.requiresConfig);
                if (entry != judged->end() && entry->is_boolean()) configThen = entry->get<bool>();
            }
            Librarian::Adapters::File ungated = file;
            ungated.requiresConfig.clear();
            const bool loadOrderSays = Librarian::Adapters::FileApplies(ungated, json::object(), pluginLoaded);
            const bool applies = configThen.value_or(configNow) && loadOrderSays;
            const bool listed = listedIn("files", name);
            const bool notApplying = listedIn("filesNotApplying", name);
            const bool ok = applies ? (listed && !notApplying) : (!listed && notApplying);
            const std::string configText = file.requiresConfig.empty() ? std::string("no config gate") :
                std::format("{} as the report judged it={} (config.json now={}{})", file.requiresConfig,
                    configThen ? YesNo(*configThen) : "not in the report", YesNo(configNow),
                    configThen && *configThen != configNow ? ", changed since the report" : "");
            Report("adapters-" + name, ok ? Outcome::kPass : Outcome::kFail,
                std::format("{}; plugins/DLLs loaded={}; so it applies={}; report (last game start or full scan): files={} "
                            "filesNotApplying={}", configText, YesNo(loadOrderSays), YesNo(applies), YesNo(listed),
                    YesNo(notApplying)));
        }

        if (!PluginLoaded("Desecration.esp")) {
            Report("adapters-desecration", Outcome::kSkip, "Desecration.esp not loaded");
            return;
        }
        auto* fire = Resolve<RE::BGSKeyword>(kMagicDamageFireId);
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        std::size_t spells = 0;
        std::size_t effects = 0;
        std::size_t withEditorId = 0;
        std::vector<std::string> offenders;
        for (auto* spell : dataHandler->GetFormArray<RE::SpellItem>()) {
            if (!spell) continue;
            const std::string editorId = SpellScanner::GetEditorId(spell);
            if (!editorId.empty()) ++withEditorId;
            if (!editorId.starts_with(kNecroticPrefix)) continue;
            ++spells;
            for (const auto* effect : spell->effects) {
                if (!effect || !effect->baseEffect) continue;
                ++effects;
                if (fire && Librarian::PerkAdapters::IsInjected(effect->baseEffect->GetFormID(), fire->GetFormID())) {
                    offenders.push_back(editorId + " " + SpellScanner::GetPersistentFormId(effect->baseEffect->GetFormID()));
                }
            }
        }
        if (withEditorId == 0) {
            Report("adapters-desecration", Outcome::kSkip, "no spell EditorIDs (po3 Tweaks' Load EditorIDs off)");
            return;
        }
        if (spells == 0) {
            Report("adapters-desecration", Outcome::kSkip,
                std::format("Desecration.esp is loaded but no spell's EditorID starts with {}", kNecroticPrefix));
            return;
        }
        std::string list;
        for (const auto& text : offenders) list += (list.empty() ? "" : ", ") + text;
        Report("adapters-desecration", offenders.empty() ? Outcome::kPass : Outcome::kFail,
            std::format("{} {}* spells, {} effect entries, MagicDamageFire added here to {}{}", spells, kNecroticPrefix,
                effects, offenders.size(), list.empty() ? "" : ": " + list));
    }
}
