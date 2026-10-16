#include "game/ToxData.h"

#include "PluginNames.h"

#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
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

    // MerchantRiftenElgrimsElixirsChest: the base container of the one chest Elgrim's Elixirs sells from
    constexpr RE::FormID kElgrimChestId = 0x0A31AE;
    constexpr std::string_view kSkyrimEsm = "Skyrim.esm";
    constexpr std::int32_t kTomesInShop = 1;

    constexpr std::uint8_t kNotLoadedCompileIndex = 0xFF;  // RE/T/TESDataHandler.cpp:128, LookupFormID's "not loaded" test
    constexpr std::uint32_t kMaxLocalId = 0xFFFFFF;        // a full plugin's local id: 24 bits
    constexpr std::uint32_t kMaxLightLocalId = 0xFFF;      // a light plugin's: 12 bits
    constexpr std::string_view kHexPrefixLower = "0x";
    constexpr std::string_view kHexPrefixUpper = "0X";
    constexpr int kHexBase = 16;

    // =========================================================================
    // State (game thread only, ToxData.h)
    // =========================================================================

    Toxicology::Settings g_settings;
    RE::BGSKeyword* g_mimicKeyword = nullptr;
    RE::SpellItem* g_ability = nullptr;
    RE::TESObjectBOOK* g_tome = nullptr;
    std::unordered_set<RE::FormID> g_poisonSpells;

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

    struct KeyParts
    {
        std::string plugin;
        std::uint32_t local = 0;
    };

    // "Plugin.esp|0x00XXXX" -> (plugin, local); nullopt for anything else
    std::optional<KeyParts> SplitKey(std::string_view a_key)
    {
        const auto bar = a_key.find('|');
        if (bar == std::string_view::npos || bar == 0) return std::nullopt;
        auto hex = a_key.substr(bar + 1);
        if (hex.starts_with(kHexPrefixLower) || hex.starts_with(kHexPrefixUpper)) {
            hex.remove_prefix(kHexPrefixLower.size());
        }
        KeyParts out{ std::string(a_key.substr(0, bar)), 0 };
        const auto [end, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), out.local, kHexBase);
        if (hex.empty() || ec != std::errc{} || end != hex.data() + hex.size() || out.local > kMaxLocalId) {
            return std::nullopt;
        }
        return out;
    }

    RE::SpellItem* ResolveSpell(RE::TESDataHandler* a_dh, PluginLookup& a_lookup, const std::string& a_key)
    {
        const auto parts = SplitKey(a_key);
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
            parts->local > (file->IsLight() ? kMaxLightLocalId : kMaxLocalId)) {
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

    // The spell and, one level down, the spell each of its cloak effects casts on whoever comes
    // near (EffectSettingData::associatedForm, RE/E/EffectSetting.h:71). The cloak effect on the
    // caster is not hostile (vanilla FireCloakFFSelf), its sub-spell is: the hostile filter runs
    // after this, so a poison cloak keeps its sub-spell and drops itself.
    void AddWithCloakSpells(RE::SpellItem* a_spell, std::vector<RE::SpellItem*>& a_out, std::size_t& a_cloakSpells)
    {
        a_out.push_back(a_spell);
        for (const auto* effect : a_spell->effects) {
            const auto* base = effect ? effect->baseEffect : nullptr;
            if (!base || !base->HasArchetype(RE::EffectSetting::Archetype::kCloak) || !base->data.associatedForm) continue;
            if (auto* sub = base->data.associatedForm->As<RE::SpellItem>()) {
                a_out.push_back(sub);
                ++a_cloakSpells;
            }
        }
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
    logger::info("Toxicology: settings: enabled {}, showIcon {}, minIntervalSeconds {}, magickaPerDose {}",
        g_settings.enabled, g_settings.showIcon, g_settings.minIntervalSeconds, g_settings.magickaPerDose);
}

// =============================================================================
// ESL forms
// =============================================================================

RE::BGSKeyword* ToxGame::MimicKeyword() { return g_mimicKeyword; }
RE::SpellItem* ToxGame::Ability() { return g_ability; }
RE::TESObjectBOOK* ToxGame::Tome() { return g_tome; }

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
    if (!g_mimicKeyword && !g_ability && !g_tome) {
        logger::warn("Toxicology: {} is not loaded - no ability, no tome, no icon keyword; the addon does nothing", kEslName);
        return;
    }
    logger::info("Toxicology: {} loaded: keyword {:08X}, ability {:08X}, tome {:08X}", kEslName,
        g_mimicKeyword ? g_mimicKeyword->GetFormID() : 0, g_ability ? g_ability->GetFormID() : 0, g_tome ? g_tome->GetFormID() : 0);
    if (!g_mimicKeyword || !g_ability || !g_tome) {
        logger::error("Toxicology: {} is loaded but a record is missing (00000000 above) - the ESL is not this version's", kEslName);
    }
}

bool ToxGame::PlayerHasAbility()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    return g_ability && player && player->HasSpell(g_ability);  // RE/A/Actor.h:620
}

// =============================================================================
// Poison spell set
// =============================================================================

void ToxGame::LoadPoisonSpells()
{
    g_poisonSpells.clear();
    // Stamped before the read: a catalog rewritten during it differs next check and is read again
    g_catalogStamp = WriteTime(kCatalogPath);
    g_catalogStampValid = true;

    const auto catalog = ReadCatalog();
    if (!catalog) return;
    std::string error;
    const auto keys = Toxicology::PoisonSpellKeys(*catalog, error);
    if (!error.empty()) {
        logger::warn("Toxicology: {}: {} - no poison spells", kCatalogPath, error);
        return;
    }
    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh) {
        logger::error("Toxicology: no TESDataHandler - no poison spells");
        return;
    }

    PluginLookup lookup;  // one per pass: the file list is converted once, not once per key
    std::vector<RE::SpellItem*> candidates;
    std::size_t resolved = 0;
    std::size_t unresolved = 0;
    std::size_t cloakSpells = 0;
    for (const auto& key : keys) {
        auto* spell = ResolveSpell(dh, lookup, key);
        if (!spell) {
            ++unresolved;
            logger::debug("Toxicology: catalog key {} is not a loaded spell", key);
            continue;
        }
        ++resolved;
        AddWithCloakSpells(spell, candidates, cloakSpells);
    }
    std::size_t notHostile = 0;
    for (auto* spell : candidates) {
        if (HasHostileEffect(spell)) {
            g_poisonSpells.insert(spell->GetFormID());
        } else {
            ++notHostile;
        }
    }
    logger::info("Toxicology: poison spells: {} catalog keys, {} resolved, {} unresolved, {} cloak sub-spells, "
        "{} without a hostile effect left out, {} kept", keys.size(), resolved, unresolved, cloakSpells, notHostile,
        g_poisonSpells.size());
}

bool ToxGame::ReloadPoisonSpellsIfChanged()
{
    const auto now = Clock::now();
    if (g_lastCatalogCheck && std::chrono::duration<double>(now - *g_lastCatalogCheck).count() < kCatalogCheckSeconds) {
        return false;
    }
    g_lastCatalogCheck = now;
    if (g_catalogStampValid && WriteTime(kCatalogPath) == g_catalogStamp) return false;
    logger::info("Toxicology: {} changed, reading it again", kCatalogPath);
    LoadPoisonSpells();
    return true;
}

bool ToxGame::IsPoisonSpell(RE::FormID a_spell)
{
    return g_poisonSpells.contains(a_spell);
}

// =============================================================================
// Tome in the shop
// =============================================================================

void ToxGame::PutTomeInShop()
{
    if (!g_tome) {
        logger::warn("Toxicology: no tome (the ESL is not loaded) - nothing put into Elgrim's Elixirs");
        return;
    }
    auto* dh = RE::TESDataHandler::GetSingleton();
    auto* chest = dh ? dh->LookupForm<RE::TESObjectCONT>(kElgrimChestId, kSkyrimEsm) : nullptr;
    if (!chest) {
        logger::error("Toxicology: Elgrim's Elixirs' chest {:06X} ({}) not found as a container - the tome is not sold",
            kElgrimChestId, kSkyrimEsm);
        return;
    }
    // RE/T/TESContainer.h:60,63; GetObjectCount first so a second call never adds a second copy
    if (chest->GetObjectCount(g_tome) > 0) {
        logger::info("Toxicology: Elgrim's Elixirs' chest already holds the tome ({}), nothing added", chest->GetObjectCount(g_tome));
        return;
    }
    const bool added = chest->AddObjectToContainer(g_tome, kTomesInShop, nullptr);
    logger::info("Toxicology: tome {} Elgrim's Elixirs' chest {:08X}: count now {}", added ? "added to" : "NOT added to",
        chest->GetFormID(), chest->GetObjectCount(g_tome));
}
