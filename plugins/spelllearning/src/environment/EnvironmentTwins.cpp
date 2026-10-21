#include "environment/EnvironmentTwins.h"
#include "librarian/PerkAdapters.h"
#include "PathText.h"
#include "SpellScanner.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

// =============================================================================
// EnvironmentTwins - see EnvironmentTwins.h and docs/ENVIRONMENT.md
// =============================================================================

namespace Environment
{
    namespace
    {
        using json = nlohmann::json;

        constexpr const char* kTwinMapPath = "Data/SKSE/Plugins/SpellLearning/librarian/environment/twins.json";
        constexpr const char* kConfigPath = "Data/SKSE/Plugins/SpellLearning/config.json";
        constexpr const char* kFrostwalkerDir = "Data/SKSE/Plugins/Frostwalker";
        constexpr std::size_t kMaxLoggedRefusals = 40;

        struct Twin
        {
            std::string originalId;
            std::string twinId;
            RE::BGSExplosion* original = nullptr;
            RE::BGSExplosion* twin = nullptr;
            bool modelCopied = false;
        };

        // One effect that carried an original at kDataLoaded and may take its twin
        struct Recorded
        {
            RE::EffectSetting* effect = nullptr;
            RE::BGSExplosion* original = nullptr;
            RE::BGSExplosion* twin = nullptr;
            RE::BGSExplosion* lastWritten = nullptr;   // what ApplySwitch last put there (null: nothing yet)
            std::string id;                            // persistentId, for the log
        };

        // Game thread only, like every caller (kDataLoaded, the config apply
        // task, the scan's form reads - CollectScan and Papyrus RunScan both run
        // there). The table is filled once by Initialize, before g_ready is set,
        // and only lastWritten changes after that.
        std::vector<Twin> g_twins;
        std::vector<Recorded> g_recorded;
        std::unordered_map<RE::FormID, std::size_t> g_recordedByEffect;
        bool g_ready = false;
        bool g_initialized = false;
        bool g_switchOn = false;

        std::string IdOf(const RE::TESForm* form)
        {
            return form ? SpellScanner::GetPersistentFormId(form->GetFormID()) : std::string("(none)");
        }

        bool ReadJson(const std::filesystem::path& path, json& out)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) return false;
            out = json::parse(file, nullptr, false);
            return !out.is_discarded();
        }

        bool SwitchFromConfigFile()
        {
            json config;
            if (!ReadJson(kConfigPath, config)) return true;
            return Librarian::Adapters::ConfigFlagOn(config, kSwitchPath);
        }

        RE::BGSExplosion* ResolveExplosion(const std::string& persistentId)
        {
            const RE::FormID formId = SpellScanner::ResolvePersistentFormId(persistentId);
            return formId ? RE::TESForm::LookupByID<RE::BGSExplosion>(formId) : nullptr;
        }

        std::vector<Twin> ReadTwins()
        {
            std::vector<Twin> twins;
            json map;
            if (!ReadJson(kTwinMapPath, map)) {
                logger::info("EnvironmentTwins: no readable {} - no explosion twins", kTwinMapPath);
                return twins;
            }
            const auto explosions = map.find("explosions");
            if (explosions == map.end() || !explosions->is_array()) {
                logger::warn("EnvironmentTwins: {} has no \"explosions\" array - no explosion twins", kTwinMapPath);
                return twins;
            }
            for (const auto& entry : *explosions) {
                if (!entry.is_object() || !entry.contains("original") || !entry["original"].is_string() ||
                    !entry.contains("twin") || !entry["twin"].is_string()) {
                    logger::warn("EnvironmentTwins: {} entry without string \"original\" and \"twin\" skipped: {}",
                        kTwinMapPath, entry.dump());
                    continue;
                }
                Twin twin;
                twin.originalId = entry["original"].get<std::string>();
                twin.twinId = entry["twin"].get<std::string>();
                twin.original = ResolveExplosion(twin.originalId);
                twin.twin = ResolveExplosion(twin.twinId);
                if (!twin.original || !twin.twin) {
                    // HeartOfMagic.esp not enabled, the original's plugin absent, or not an explosion
                    logger::info("EnvironmentTwins: {} -> {} skipped: {} is not a loaded explosion", twin.originalId,
                        twin.twinId, twin.original ? twin.twinId : twin.originalId);
                } else if (twin.original == twin.twin) {
                    logger::warn("EnvironmentTwins: {} is its own twin - skipped", twin.originalId);
                    twin.original = twin.twin = nullptr;
                }
                twins.push_back(std::move(twin));
            }
            return twins;
        }

        // The live original's values into the twin, so the twin follows a mod
        // that changes the original after HeartOfMagic.esp was made. Plain data
        // and pointers to other forms only: the TESModel texture and addon
        // arrays are the form's own heap buffers and are left alone, and the name
        // and EditorID are never copied (Frostwalker reads them). The model path
        // is copied only while Frostwalker would read it as no element.
        void CopyOriginalInto(Twin& entry, const FrostwalkerPatterns& patterns)
        {
            RE::BGSExplosion* original = entry.original;
            RE::BGSExplosion* twin = entry.twin;
            twin->data = original->data;
            twin->boundData = original->boundData;
            twin->formEnchanting = original->formEnchanting;
            twin->castingType = original->castingType;
            twin->amountofEnchantment = original->amountofEnchantment;
            twin->imageSpaceModifying = original->imageSpaceModifying;

            auto* originalModel = static_cast<RE::TESModel*>(original);
            auto* twinModel = static_cast<RE::TESModel*>(twin);
            const char* path = originalModel->GetModel();
            const std::string_view model = path ? path : "";
            const FrostwalkerClass modelClass = ClassifyText(model, patterns);
            if (modelClass != FrostwalkerClass::kUnknown) {
                logger::warn("EnvironmentTwins: {} keeps its own model: the original's '{}' reads as {} to Frostwalker",
                    entry.twinId, model, FrostwalkerClassName(modelClass));
            } else {
                twinModel->SetModel(path ? path : "");
                entry.modelCopied = true;
            }
        }

        // Every magnitude each carried effect is given, over every kind of item
        // that carries effects. Only the effects in `wanted` are collected.
        std::unordered_map<const RE::EffectSetting*, std::vector<float>> CarrierMagnitudes(
            RE::TESDataHandler* dataHandler, const std::unordered_set<const RE::EffectSetting*>& wanted)
        {
            std::unordered_map<const RE::EffectSetting*, std::vector<float>> magnitudes;
            const auto collect = [&](auto& items) {
                for (const auto* item : items) {
                    if (!item) continue;
                    for (const auto* effect : item->effects) {
                        if (effect && effect->baseEffect && wanted.contains(effect->baseEffect)) {
                            magnitudes[effect->baseEffect].push_back(effect->effectItem.magnitude);
                        }
                    }
                }
            };
            collect(dataHandler->GetFormArray<RE::SpellItem>());
            collect(dataHandler->GetFormArray<RE::ScrollItem>());
            collect(dataHandler->GetFormArray<RE::EnchantmentItem>());
            collect(dataHandler->GetFormArray<RE::AlchemyItem>());
            collect(dataHandler->GetFormArray<RE::IngredientItem>());
            return magnitudes;
        }

        EffectFacts FactsOf(const RE::EffectSetting* effect, const RE::BGSExplosion* original,
            const std::unordered_map<const RE::EffectSetting*, std::vector<float>>& magnitudes)
        {
            using Flag = RE::EffectSetting::EffectSettingData::Flag;
            EffectFacts facts;
            facts.explosionIsOriginal = effect->data.explosion == original;
            facts.archetypeScript = effect->data.archetype == RE::EffectArchetype::kScript;
            facts.primaryAvNone = effect->data.primaryAV == RE::ActorValue::kNone;
            facts.hideInUI = effect->data.flags.any(Flag::kHideInUI);
            facts.hostile = effect->IsHostile();
            facts.detrimental = effect->IsDetrimental();
            const auto found = magnitudes.find(effect);
            if (found != magnitudes.end()) facts.carrierMagnitudes = found->second;
            return facts;
        }

        void RecordEffects(RE::TESDataHandler* dataHandler)
        {
            std::unordered_map<const RE::BGSExplosion*, const Twin*> byOriginal;
            for (const auto& twin : g_twins) {
                if (twin.original && twin.twin) byOriginal.emplace(twin.original, &twin);
            }
            if (byOriginal.empty()) return;

            std::vector<RE::EffectSetting*> candidates;
            std::unordered_set<const RE::EffectSetting*> wanted;
            for (auto* effect : dataHandler->GetFormArray<RE::EffectSetting>()) {
                if (effect && effect->data.explosion && byOriginal.contains(effect->data.explosion)) {
                    candidates.push_back(effect);
                    wanted.insert(effect);
                }
            }
            const auto magnitudes = CarrierMagnitudes(dataHandler, wanted);

            std::size_t refused = 0;
            for (auto* effect : candidates) {
                const Twin* twin = byOriginal.at(effect->data.explosion);
                const EffectFacts facts = FactsOf(effect, twin->original, magnitudes);
                const std::string refusal = TwinRefusal(facts);
                const std::string id = IdOf(effect);
                if (!refusal.empty()) {
                    if (++refused <= kMaxLoggedRefusals) {
                        logger::info("EnvironmentTwins: {} keeps {}: {}", id, twin->originalId, refusal);
                    }
                    continue;
                }
                logger::info("EnvironmentTwins: {} takes {} ({} carriers, all magnitude 0)", id, twin->twinId,
                    facts.carrierMagnitudes.size());
                g_recordedByEffect.emplace(effect->GetFormID(), g_recorded.size());
                g_recorded.push_back({ effect, twin->original, twin->twin, nullptr, id });
            }
            logger::info("EnvironmentTwins: {} effects carry an original explosion: {} take its twin, {} keep it",
                candidates.size(), g_recorded.size(), refused);
        }

        // Points every recorded effect at its twin (on) or its original (off).
        // An effect whose explosion another mod changed since this plugin last
        // wrote it is left alone (DecideSwitch, EnvironmentMatch.h).
        void ApplySwitch(bool on, std::string_view reason)
        {
            std::size_t changed = 0;
            std::size_t already = 0;
            std::vector<std::string> elsewhere;
            std::ostringstream ids;
            for (auto& rec : g_recorded) {
                RE::BGSExplosion* current = rec.effect->data.explosion;
                const SwitchStep step = DecideSwitch<RE::BGSExplosion>(current, rec.original, rec.twin, rec.lastWritten, on);
                if (step == SwitchStep::kForeign) {
                    elsewhere.push_back(rec.id + " (now " + IdOf(current) + ", HoM last wrote " +
                                        IdOf(rec.lastWritten ? rec.lastWritten : rec.original) + ")");
                    continue;
                }
                if (step == SwitchStep::kWrite) {
                    rec.effect->data.explosion = on ? rec.twin : rec.original;
                    rec.lastWritten = rec.effect->data.explosion;
                    ++changed;
                } else {
                    ++already;
                }
                ids << (ids.tellp() > 0 ? ", " : "") << rec.id;
            }
            g_switchOn = on;
            logger::info("EnvironmentTwins: {} - environment.enabled {}: {} effects on their {} ({} changed now, {} were): {}",
                reason, on ? "on" : "off", changed + already, on ? "twin" : "original", changed, already,
                ids.str().empty() ? std::string("none") : ids.str());
            for (const auto& text : elsewhere) {
                logger::warn("EnvironmentTwins: {} left alone - another mod changed its explosion", text);
            }
        }

        void InitializeImpl()
        {
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) return;

            g_twins = ReadTwins();
            bool frostwalkerInstalled = false;
            const FrostwalkerPatterns patterns = LoadFrostwalkerPatterns(frostwalkerInstalled);
            std::size_t live = 0;
            for (auto& twin : g_twins) {
                if (!twin.original || !twin.twin) continue;
                CopyOriginalInto(twin, patterns);
                ++live;
                logger::info("EnvironmentTwins: {} ({}) has {}'s values{} (Frostwalker lists: {})", twin.twinId,
                    SpellScanner::GetEditorId(twin.twin), twin.originalId, twin.modelCopied ? "" : " but its own model",
                    frostwalkerInstalled ? "installed" : "none, Frostwalker not installed");
            }
            if (live == 0) {
                logger::info("EnvironmentTwins: no twin is loaded (HeartOfMagic.esp enabled?) - nothing repointed");
                return;
            }
            RecordEffects(dataHandler);
        }
    }

    FrostwalkerPatterns LoadFrostwalkerPatterns(bool& installed)
    {
        PatternDirRead read;
        FrostwalkerPatterns patterns = ReadFrostwalkerPatternDir(kFrostwalkerDir, read);
        installed = read.rootFound;
        static bool warned = false;
        if (!read.missingFolders.empty() && !warned) {
            warned = true;
            std::string folders;
            for (const auto& folder : read.missingFolders) folders += (folders.empty() ? "" : ", ") + folder;
            logger::warn("EnvironmentTwins: {} has no {} - Frostwalker reads those lists as empty", kFrostwalkerDir, folders);
        }
        return patterns;
    }

    void Initialize()
    {
        if (g_initialized) return;
        g_initialized = true;
        // The switch as config.json says, also with nothing recorded (the
        // self-test reports it)
        g_switchOn = SwitchFromConfigFile();
        try {
            InitializeImpl();
        } catch (const std::exception& e) {
            // Nothing recorded is touched later; a twin that was half filled is
            // not reported as live either
            logger::error("EnvironmentTwins: kDataLoaded failed - {}; nothing repointed", e.what());
            g_recorded.clear();
            g_recordedByEffect.clear();
            g_twins.clear();
        }
        if (!g_recorded.empty()) ApplySwitch(g_switchOn, "data loaded");
        g_ready = true;
    }

    void OnConfigApplied(const nlohmann::json& config)
    {
        if (!g_ready || g_recorded.empty()) return;
        const bool on = Librarian::Adapters::ConfigFlagOn(config, kSwitchPath);
        if (on != g_switchOn) ApplySwitch(on, "settings saved");
    }

    RE::BGSExplosion* ScannedExplosion(const RE::EffectSetting* effect)
    {
        if (!effect) return nullptr;
        if (!g_ready || g_recorded.empty()) return effect->data.explosion;
        const auto found = g_recordedByEffect.find(effect->GetFormID());
        if (found == g_recordedByEffect.end()) return effect->data.explosion;
        const Recorded& rec = g_recorded[found->second];
        return const_cast<RE::BGSExplosion*>(ScannedOf<RE::BGSExplosion>(effect->data.explosion, rec.original, rec.twin));
    }

    std::vector<TwinInfo> Twins()
    {
        std::vector<TwinInfo> out;
        for (const auto& twin : g_twins) {
            out.push_back({ twin.originalId, twin.twinId, twin.original, twin.twin, twin.modelCopied });
        }
        return out;
    }

    bool SwitchOn()
    {
        return g_switchOn;
    }

    std::size_t RecordedEffectCount()
    {
        return g_recorded.size();
    }
}
