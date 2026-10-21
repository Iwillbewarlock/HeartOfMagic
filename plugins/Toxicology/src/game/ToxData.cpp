#include "game/ToxData.h"

#include "PluginNames.h"
#include "game/ToxMagicka.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace
{
    // =========================================================================
    // Constants
    // =========================================================================

    // The ESL's records (plugins/Toxicology/ESL.md)
    constexpr RE::FormID kMimicKeywordId = 0x800;
    constexpr RE::FormID kAbilityId = 0x802;
    constexpr RE::FormID kTomeId = 0x803;
    constexpr RE::FormID kPowerEffectId = 0x804;  // 1.1: the power (spec 2026-10-20)
    constexpr RE::FormID kPowerId = 0x805;
    constexpr RE::FormID kMessageOnId = 0x806;
    constexpr RE::FormID kMessageOffId = 0x807;

    constexpr std::uint8_t kNotLoadedCompileIndex = 0xFF;  // RE/T/TESDataHandler.cpp:128, LookupFormID's "not loaded" test
    constexpr std::uint32_t kMaxLightLocalId = 0xFFF;      // a light plugin's local id: 12 bits

    // =========================================================================
    // State (game thread only, ToxData.h)
    // =========================================================================

    Toxicology::Settings g_settings;
    RE::BGSKeyword* g_mimicKeyword = nullptr;
    RE::SpellItem* g_ability = nullptr;
    RE::TESObjectBOOK* g_tome = nullptr;
    RE::EffectSetting* g_powerEffect = nullptr;
    RE::SpellItem* g_power = nullptr;
    RE::BGSMessage* g_messageOn = nullptr;
    RE::BGSMessage* g_messageOff = nullptr;
    std::unordered_set<RE::FormID> g_poisonSpells;  // the hit set (ToxData.h)
    // The cost set, replaced whole by LoadPoisonSpells; read from any thread (ToxData.h PoisonCostSnapshot)
    std::atomic<std::shared_ptr<const std::unordered_set<RE::FormID>>> g_costSnapshot{
        std::make_shared<const std::unordered_set<RE::FormID>>() };

    using Clock = std::chrono::steady_clock;
    // The catalog's last-write time when LoadPoisonSpells read it; nullopt = the file was missing
    std::optional<std::filesystem::file_time_type> g_catalogStamp;
    bool g_catalogStampValid = false;  // false until LoadPoisonSpells ran once
    std::optional<Clock::time_point> g_lastCatalogCheck;

    // =========================================================================
    // Files
    // =========================================================================

    // The whole file, or nullopt when it cannot be opened or read
    std::optional<std::string> ReadText(const char* a_path)
    {
        std::ifstream file(a_path, std::ios::binary | std::ios::ate);
        if (!file) return std::nullopt;
        const auto size = file.tellg();
        if (size < 0) return std::nullopt;
        std::string text(static_cast<std::size_t>(size), '\0');
        file.seekg(0);
        if (!file.read(text.data(), size)) return std::nullopt;
        return text;
    }

    std::optional<std::filesystem::file_time_type> WriteTime(const char* a_path)
    {
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(a_path, ec);
        if (ec) return std::nullopt;
        return time;
    }

    // =========================================================================
    // Catalog key -> form
    // =========================================================================
    // The plugin is found by PluginNames::Lookup (plugins/spelllearning/include/PluginNames.h,
    // the lookup SpellScanner::ResolvePersistentFormId uses: the game's own lookup by raw bytes,
    // then the UTF-8 forms of non-ASCII file names), the form by TESDataHandler::LookupForm
    // with the file's raw name (light plugins and VR handled there, RE/T/TESDataHandler.cpp).

    using PluginLookup = PluginNames::Lookup<const RE::TESFile*>;

    RE::SpellItem* ResolveSpell(RE::TESDataHandler* a_dh, PluginLookup& a_lookup, const std::string& a_key)
    {
        const auto parts = Toxicology::SplitCatalogKey(a_key);  // core/ToxCore.h, checked by tools/toxicology-test
        if (!parts) return nullptr;
        const RE::TESFile* file = a_lookup.Find(
            parts->plugin,
            [a_dh](const std::string& a_name) { return a_dh->LookupModByName(a_name); },
            [a_dh](auto&& a_visit) {
                for (const auto* f : a_dh->files) {
                    if (f) a_visit(f->fileName, f);
                }
            });
        // LookupFormID adds the local id to the file's index bits unmasked: a local id too wide
        // for a light plugin would land in another plugin's range
        if (!file || file->GetCompileIndex() == kNotLoadedCompileIndex ||
            (file->IsLight() && parts->local > kMaxLightLocalId)) {
            return nullptr;
        }
        auto* form = a_dh->LookupForm(parts->local, file->GetFilename());
        return form ? form->As<RE::SpellItem>() : nullptr;
    }

    // =========================================================================
    // Poison set
    // =========================================================================

    bool HasHostileEffect(const RE::SpellItem* a_spell)
    {
        for (const auto* effect : a_spell->effects) {
            if (effect && effect->baseEffect && effect->baseEffect->IsHostile()) return true;
        }
        return false;
    }

    // The spell each of a spell's cloak effects casts on whoever comes near, one level down
    // (EffectSettingData::associatedForm, RE/E/EffectSetting.h:71). The cloak effect on the
    // caster is not hostile (vanilla FireCloakFFSelf), its sub-spell is: core BuildPoisonSets puts
    // a hostile sub-spell in the hit set and its cloak in the cost set.
    std::vector<Toxicology::CloakSubSpell> CloakSubSpells(const RE::SpellItem* a_spell)
    {
        std::vector<Toxicology::CloakSubSpell> subs;
        for (const auto* effect : a_spell->effects) {
            const auto* base = effect ? effect->baseEffect : nullptr;
            if (!base || !base->HasArchetype(RE::EffectSetting::Archetype::kCloak) || !base->data.associatedForm) continue;
            if (const auto* sub = base->data.associatedForm->As<RE::SpellItem>()) {
                subs.push_back({ sub->GetFormID(), HasHostileEffect(sub) });
            }
        }
        return subs;
    }

    void PublishCostSnapshot(std::unordered_set<RE::FormID> a_cost)
    {
        g_costSnapshot.store(std::make_shared<const std::unordered_set<RE::FormID>>(std::move(a_cost)));
    }

    std::optional<nlohmann::json> ReadCatalog()
    {
        const auto text = ReadText(ToxGame::kCatalogPath);
        if (!text) {
            logger::warn("Toxicology: {} not found - no poison spells (run a full Heart of Magic scan once)", ToxGame::kCatalogPath);
            return std::nullopt;
        }
        auto catalog = nlohmann::json::parse(*text, nullptr, false);
        if (catalog.is_discarded()) {
            logger::warn("Toxicology: {} is not valid JSON - no poison spells", ToxGame::kCatalogPath);
            return std::nullopt;
        }
        return catalog;
    }
}

// =============================================================================
// Settings
// =============================================================================

const Toxicology::Settings& ToxGame::GetSettings()
{
    return g_settings;
}

void ToxGame::LoadSettings()
{
    g_settings = {};
    const auto text = ReadText(kSettingsPath);
    if (!text) {
        logger::info("Toxicology: {} not found, using the defaults", kSettingsPath);
    } else if (const auto parsed = nlohmann::json::parse(*text, nullptr, false); parsed.is_discarded()) {
        logger::warn("Toxicology: {} is not valid JSON, using the defaults", kSettingsPath);
    } else {
        std::vector<std::string> warnings;
        g_settings = Toxicology::ParseSettings(parsed, warnings);
        for (const auto& w : warnings) {
            logger::warn("Toxicology: settings: {}", w);
        }
    }
    logger::info("Toxicology: settings: enabled {}, showIcon {}, magickaPerDose {}, immunityMultiplier {}, minImmunitySeconds {}",
        g_settings.enabled, g_settings.showIcon, g_settings.magickaPerDose, g_settings.immunityMultiplier,
        g_settings.minImmunitySeconds);
}

// =============================================================================
// ESL forms
// =============================================================================

RE::BGSKeyword* ToxGame::MimicKeyword() { return g_mimicKeyword; }
RE::SpellItem* ToxGame::Ability() { return g_ability; }
RE::TESObjectBOOK* ToxGame::Tome() { return g_tome; }
RE::EffectSetting* ToxGame::PowerEffect() { return g_powerEffect; }
RE::SpellItem* ToxGame::Power() { return g_power; }
RE::BGSMessage* ToxGame::MessageOn() { return g_messageOn; }
RE::BGSMessage* ToxGame::MessageOff() { return g_messageOff; }

void ToxGame::LoadForms()
{
    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh) {
        logger::error("Toxicology: no TESDataHandler at kDataLoaded - the ESL's forms are not looked up");
        return;
    }
    // RE/T/TESDataHandler.h:62-65; null when the ESL is not loaded or the record is not of that type
    g_mimicKeyword = dh->LookupForm<RE::BGSKeyword>(kMimicKeywordId, kEslName);
    g_ability = dh->LookupForm<RE::SpellItem>(kAbilityId, kEslName);
    g_tome = dh->LookupForm<RE::TESObjectBOOK>(kTomeId, kEslName);
    g_powerEffect = dh->LookupForm<RE::EffectSetting>(kPowerEffectId, kEslName);
    g_power = dh->LookupForm<RE::SpellItem>(kPowerId, kEslName);
    g_messageOn = dh->LookupForm<RE::BGSMessage>(kMessageOnId, kEslName);
    g_messageOff = dh->LookupForm<RE::BGSMessage>(kMessageOffId, kEslName);
    if (!g_mimicKeyword && !g_ability && !g_tome && !g_powerEffect && !g_power && !g_messageOn && !g_messageOff) {
        logger::warn("Toxicology: {} is not loaded - no ability, no power, no tome, no icon keyword; the addon does nothing", kEslName);
        return;
    }
    const auto id = [](const RE::TESForm* a_form) { return a_form ? a_form->GetFormID() : 0; };
    logger::info("Toxicology: {} loaded: keyword {:08X}, ability {:08X}, tome {:08X}, power effect {:08X}, power {:08X}, "
        "on message {:08X}, off message {:08X}", kEslName, id(g_mimicKeyword), id(g_ability), id(g_tome), id(g_powerEffect),
        id(g_power), id(g_messageOn), id(g_messageOff));
    if (!g_mimicKeyword || !g_ability || !g_tome || !g_powerEffect || !g_power || !g_messageOn || !g_messageOff) {
        logger::error("Toxicology: {} is loaded but a record is missing (00000000 above) - the ESL is not this version's "
            "(a 1.0 ESL has no power: the feature cannot be turned on or off)", kEslName);
    }
}

bool ToxGame::PlayerHasAbility()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    return g_ability && player && player->HasSpell(g_ability);  // RE/A/Actor.h:620
}

bool ToxGame::PlayerHasPower()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    return g_power && player && player->HasSpell(g_power);  // RE/A/Actor.h:620
}

// =============================================================================
// Poison spell set
// =============================================================================

namespace
{
    // Both sets built from the catalog, not yet published; nullopt on an error (logged)
    std::optional<Toxicology::PoisonSets> BuildSetsFromCatalog()
    {
        const auto catalog = ReadCatalog();
        if (!catalog) return std::nullopt;
        std::string error;
        const auto keys = Toxicology::PoisonSpellKeys(*catalog, error);
        if (!error.empty()) {
            logger::warn("Toxicology: {}: {} - no poison spells", ToxGame::kCatalogPath, error);
            return std::nullopt;
        }
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            logger::error("Toxicology: no TESDataHandler - no poison spells");
            return std::nullopt;
        }

        PluginLookup lookup;  // one per pass: the file list is converted once, not once per key
        std::vector<Toxicology::PoisonSpellNode> nodes;
        std::size_t unresolved = 0;
        for (const auto& key : keys) {
            const auto* spell = ResolveSpell(dh, lookup, key);
            if (!spell) {
                ++unresolved;
                logger::debug("Toxicology: catalog key {} is not a loaded spell", key);
                continue;
            }
            nodes.push_back({ spell->GetFormID(), HasHostileEffect(spell), CloakSubSpells(spell) });
        }

        // core/ToxCore.h, checked by tools/toxicology-test
        auto sets = Toxicology::BuildPoisonSets(nodes);
        logger::info("Toxicology: poison spells: {} catalog keys, {} resolved, {} unresolved, {} cloak sub-spells added, "
            "{} without a hostile effect left out, {} kept for hits, {} charged when cast (cloaks by their own spell)",
            keys.size(), sets.catalogSpells, unresolved, sets.cloakSubSpells, sets.notHostile, sets.hit.size(), sets.cost.size());
        return sets;
    }

    bool CatalogChanged()
    {
        return !g_catalogStampValid || WriteTime(ToxGame::kCatalogPath) != g_catalogStamp;
    }
}

void ToxGame::LoadPoisonSpells()
{
    // Stamped before the read: a catalog rewritten during it differs next check and is read again
    g_catalogStamp = WriteTime(kCatalogPath);
    g_catalogStampValid = true;

    // The old sets stay in place while the new ones are built, then both are replaced at once
    if (auto sets = BuildSetsFromCatalog()) {
        g_poisonSpells = std::move(sets->hit);
        PublishCostSnapshot(std::move(sets->cost));
    } else {
        g_poisonSpells.clear();
        PublishCostSnapshot({});
    }
    // The caster hook's flag follows the new cost set at once (ToxMagicka.h), not at the next menu or cast
    PublishMagickaTracking(GetSettings().enabled && PlayerHasAbility());
}

bool ToxGame::ReloadPoisonSpellsIfChanged()
{
    const auto now = Clock::now();
    if (g_lastCatalogCheck && std::chrono::duration<double>(now - *g_lastCatalogCheck).count() < kCatalogCheckSeconds) {
        return false;
    }
    g_lastCatalogCheck = now;
    if (!CatalogChanged()) return false;
    logger::info("Toxicology: {} changed, reading it again", kCatalogPath);
    LoadPoisonSpells();
    return true;
}

bool ToxGame::ReloadPoisonSpellsAtLoad()
{
    g_lastCatalogCheck = Clock::now();  // the 5 s window of ReloadPoisonSpellsIfChanged starts again here
    if (!CatalogChanged()) {
        logger::info("Toxicology: {} unchanged since it was last read - poison spells kept", kCatalogPath);
        return false;
    }
    logger::info("Toxicology: {} changed since it was last read, reading it again", kCatalogPath);
    LoadPoisonSpells();
    return true;
}

bool ToxGame::IsPoisonSpell(RE::FormID a_spell)
{
    return g_poisonSpells.contains(a_spell);
}

bool ToxGame::IsPoisonCostSpell(RE::FormID a_spell)
{
    const auto cost = PoisonCostSnapshot();
    return cost && cost->contains(a_spell);
}

std::shared_ptr<const std::unordered_set<RE::FormID>> ToxGame::PoisonCostSnapshot()
{
    return g_costSnapshot.load();
}
