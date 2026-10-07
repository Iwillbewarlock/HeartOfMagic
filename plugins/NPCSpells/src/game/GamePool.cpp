#include "game/GamePool.h"

#include "tomereach/TomeReachRecords.h"

#include <charconv>
#include <optional>
#include <string>
#include <unordered_map>

namespace
{
    constexpr std::string_view kHexPrefix = "0x";
    constexpr std::uint32_t kMaxLocalId = 0xFFFFFF;       // a full plugin's local id: 24 bits
    constexpr std::uint32_t kMaxLightLocalId = 0xFFF;     // a light plugin's: 12 bits
    constexpr int kHexBase = 16;

    struct PidParts
    {
        std::string plugin;
        std::uint32_t local = 0;
    };

    // "Plugin.esp|0x00XXXX" -> (plugin, local); nullopt for anything else (counted as unresolved, never thrown)
    std::optional<PidParts> SplitPersistentId(std::string_view pid)
    {
        const auto bar = pid.find('|');
        if (bar == std::string_view::npos || bar == 0) {
            return std::nullopt;
        }
        auto hex = pid.substr(bar + 1);
        if (hex.size() >= kHexPrefix.size() && (hex.starts_with("0x") || hex.starts_with("0X"))) {
            hex.remove_prefix(kHexPrefix.size());
        }
        PidParts out{ std::string(pid.substr(0, bar)), 0 };
        const auto [end, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), out.local, kHexBase);
        if (hex.empty() || ec != std::errc{} || end != hex.data() + hex.size() || out.local > kMaxLocalId) {
            return std::nullopt;
        }
        return out;
    }

    // RE/A/ActorValues.h:26-30: kAlteration..kRestoration are 18..22, in NpcSpells::School order
    std::optional<NpcSpells::School> SchoolFromSkill(RE::ActorValue skill)
    {
        const auto first = static_cast<int>(RE::ActorValue::kAlteration);
        const auto value = static_cast<int>(skill);
        if (value < first || value > static_cast<int>(RE::ActorValue::kRestoration)) {
            return std::nullopt;
        }
        return static_cast<NpcSpells::School>(value - first);
    }
    static_assert(static_cast<int>(RE::ActorValue::kRestoration) - static_cast<int>(RE::ActorValue::kAlteration) + 1 ==
                  static_cast<int>(NpcSpells::kSchoolCount));
}

RE::TESForm* NpcSpellsGame::LookupLocalForm(PluginLookup& lookup, const std::string& plugin, std::uint32_t local)
{
    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh) {
        return nullptr;
    }
    // SpellScannerFormId.cpp FindPlugin: raw-byte lookup, then the UTF-8 names of non-ASCII files
    const RE::TESFile* file = lookup.Find(
        plugin,
        [dh](const std::string& name) { return dh->LookupModByName(name); },
        [dh](auto&& visit) {
            for (const auto* f : dh->files) {
                if (f) {
                    visit(f->fileName, f);
                }
            }
        });
    // LookupFormID (src/RE/T/TESDataHandler.cpp:125-141) adds the local id to the file's index bits unmasked:
    // a local id too wide for the file would land in another plugin's FormID range
    if (!file || local > (file->IsLight() ? kMaxLightLocalId : kMaxLocalId)) {  // RE/T/TESFile.h:63
        return nullptr;
    }
    // RE/T/TESDataHandler.h:62 with the file's own (raw) name; null for a plugin that is present but not loaded
    return dh->LookupForm(local, file->GetFilename());
}

NpcSpellsGame::ResolvedPool NpcSpellsGame::ResolvePool(const nlohmann::json& tree, const nlohmann::json& catalog,
    const NpcSpells::Rules& rules, const TomeReach::ConfigText& distr, NpcSpells::PoolStats& stats)
{
    ResolvedPool out;
    // Editor ids are not in the catalog: SPID lines naming the spell by FormID are found, editor-id lines are not
    const NpcSpells::DistributedFn distributed = [&distr](const std::string& pid) {
        const auto parts = SplitPersistentId(pid);
        return parts && distr.Names(TomeReach::LowerAscii(parts->plugin), parts->local, "");
    };
    try {
        out.facts = NpcSpells::BuildPool(tree, catalog, rules, distributed, stats);
    } catch (const nlohmann::json::exception& e) {
        // A catalog entry of the wrong shape (a null school, elements that are not strings): stop the addon cleanly
        logger::error("NPCSpells: spell_catalog.json has a malformed entry ({}); nothing changed", e.what());
        stats = {};
        return {};
    }

    PluginLookup lookup;
    std::unordered_map<std::string, RE::SpellItem*> resolved;  // persistentId -> form, filled inside the EngineSchoolFn
    NpcSpells::ApplyEngineSchool(
        out.facts,
        [&](const std::string& pid) -> std::optional<NpcSpells::School> {
            const auto parts = SplitPersistentId(pid);
            if (!parts) {
                return std::nullopt;
            }
            auto* form = LookupLocalForm(lookup, parts->plugin, parts->local);
            auto* spell = form ? form->As<RE::SpellItem>() : nullptr;  // RE/T/TESForm.h:259
            // RE/S/SpellItem.h:67, :81
            if (!spell || spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell) {
                return std::nullopt;
            }
            const auto school = SchoolFromSkill(spell->GetAssociatedSkill());
            if (school) {
                resolved[pid] = spell;
            }
            return school;
        },
        stats);
    out.forms.reserve(out.facts.size());
    for (const auto& f : out.facts) {
        out.forms.push_back(resolved.at(f.persistentId));  // parallel to facts
    }
    return out;
}
