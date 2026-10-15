#include "FormCopies.h"

#include "PluginNames.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace BlackFlame
{
    namespace
    {
        constexpr int kMapVersion = 1;
        constexpr std::uint32_t kMaxLocalId = 0xFFFFFF;
        constexpr std::uint32_t kMaxLightLocalId = 0xFFF;
        constexpr int kHexBase = 16;

        struct Entry
        {
            std::string sourceText;  // "plugin|localId" as the map gives it
            RE::FormType type = RE::FormType::None;
            RE::TESForm* source = nullptr;
            RE::TESForm* copy = nullptr;
            bool ok = false;
            std::string detail;
        };

        struct Copies
        {
            std::unordered_map<RE::FormID, RE::TESForm*> bySource;
            std::unordered_map<RE::FormID, const RE::TESForm*> byCopy;
            std::vector<Entry> entries;
            CopyCounts counts;
        };

        // Built once on the kDataLoaded thread and never changed afterwards (hooks read it from any thread)
        Copies& TheCopies()
        {
            static auto* copies = std::make_unique<Copies>().release();
            return *copies;
        }

        RE::FormType TypeOf(std::string_view a_tag)
        {
            if (a_tag == "PROJ") {
                return RE::FormType::Projectile;
            }
            if (a_tag == "EXPL") {
                return RE::FormType::Explosion;
            }
            if (a_tag == "IPCT") {
                return RE::FormType::Impact;
            }
            if (a_tag == "HAZD") {
                return RE::FormType::Hazard;
            }
            return RE::FormType::None;
        }

        bool ParseHex(std::string_view a_text, std::uint32_t& a_out)
        {
            if (a_text.starts_with("0x") || a_text.starts_with("0X")) {
                a_text.remove_prefix(2);
            }
            const auto [end, ec] = std::from_chars(a_text.data(), a_text.data() + a_text.size(), a_out, kHexBase);
            return !a_text.empty() && ec == std::errc{} && end == a_text.data() + a_text.size() && a_out <= kMaxLocalId;
        }

        // A form by its plugin's file name (non-ASCII names through PluginNames) and local id; the local id
        // must fit the file (a light plugin's 12 bits), or LookupForm would land in another plugin's range
        RE::TESForm* LookupLocal(RE::TESDataHandler* a_dh, PluginNames::Lookup<const RE::TESFile*>& a_lookup,
            const std::string& a_plugin, std::uint32_t a_local)
        {
            const RE::TESFile* file = a_lookup.Find(
                a_plugin, [a_dh](const std::string& name) { return a_dh->LookupModByName(name); },
                [a_dh](auto&& visit) {
                    for (const auto* f : a_dh->files) {
                        if (f) {
                            visit(f->fileName, f);
                        }
                    }
                });
            if (!file || a_local > (file->IsLight() ? kMaxLightLocalId : kMaxLocalId)) {
                return nullptr;
            }
            return a_dh->LookupForm(a_local, file->GetFilename());
        }

        Entry ResolveEntry(RE::TESDataHandler* a_dh, PluginNames::Lookup<const RE::TESFile*>& a_lookup,
            const nlohmann::json& a_item)
        {
            Entry entry;
            if (!a_item.is_object() || !a_item.contains("source") || !a_item["source"].is_string() ||
                !a_item.contains("type") || !a_item["type"].is_string() || !a_item.contains("copy") ||
                !a_item["copy"].is_string()) {
                entry.detail = std::format("bad entry {} (source, type, copy expected)", a_item.dump());
                return entry;
            }
            entry.sourceText = a_item["source"].get<std::string>();
            entry.type = TypeOf(a_item["type"].get<std::string>());
            const auto bar = entry.sourceText.find('|');
            std::uint32_t sourceLocal = 0;
            std::uint32_t copyLocal = 0;
            if (entry.type == RE::FormType::None || bar == std::string::npos || bar == 0 ||
                !ParseHex(std::string_view(entry.sourceText).substr(bar + 1), sourceLocal) ||
                !ParseHex(a_item["copy"].get<std::string>(), copyLocal)) {
                entry.detail = std::format("bad entry {} (type PROJ/EXPL/IPCT/HAZD, hex ids)", a_item.dump());
                return entry;
            }
            entry.source = LookupLocal(a_dh, a_lookup, entry.sourceText.substr(0, bar), sourceLocal);
            entry.copy = LookupLocal(a_dh, a_lookup, kCopiesPluginName, copyLocal);
            if (!entry.source || !entry.copy) {
                entry.detail = std::format("{} not found ({} is the source in the load order? is {} enabled?)",
                    !entry.source ? "source" : "copy", entry.sourceText, kCopiesPluginName);
                return entry;
            }
            if (entry.source->GetFormType() != entry.type || entry.copy->GetFormType() != entry.type) {
                entry.detail = std::format("type mismatch: map {}, source {:08X} type {}, copy {:08X} type {}",
                    static_cast<int>(entry.type), entry.source->GetFormID(), static_cast<int>(entry.source->GetFormType()),
                    entry.copy->GetFormID(), static_cast<int>(entry.copy->GetFormType()));
                return entry;
            }
            if (entry.source == entry.copy) {
                entry.detail = "the source is in the copies plugin itself";
                return entry;
            }
            entry.ok = true;
            return entry;
        }

        void Count(CopyCounts& a_counts, RE::FormType a_type)
        {
            switch (a_type) {
            case RE::FormType::Projectile:
                ++a_counts.projectiles;
                break;
            case RE::FormType::Explosion:
                ++a_counts.explosions;
                break;
            case RE::FormType::Impact:
                ++a_counts.impacts;
                break;
            default:
                ++a_counts.hazards;
                break;
            }
        }

        bool ReadMap(nlohmann::json& a_root)
        {
            std::ifstream file(kCopiesMapPath, std::ios::binary);
            if (!file) {
                logger::info("BlackFlame: copies: no map at '{}' - no v2 copies (every fire form stays orange; run "
                             "blackflame-greybuild --forms)", kCopiesMapPath);
                return false;
            }
            std::stringstream text;
            text << file.rdbuf();
            a_root = nlohmann::json::parse(text.str(), nullptr, false);
            if (a_root.is_discarded() || !a_root.is_object() || !a_root.contains("version") ||
                a_root["version"] != kMapVersion || !a_root.contains("copies") || !a_root["copies"].is_array()) {
                logger::warn("BlackFlame: copies: '{}' is not a version {} copies map - no v2 copies", kCopiesMapPath,
                    kMapVersion);
                return false;
            }
            return true;
        }
    }

    void LoadAndFillCopies()
    {
        static bool loaded = false;
        if (loaded) {
            return;  // kDataLoaded only: a re-fill would be a compare-and-fix, never a rebuild
        }
        loaded = true;
        auto& copies = TheCopies();
        nlohmann::json root;
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh || !ReadMap(root)) {
            return;
        }
        copies.counts.mapRead = true;
        copies.counts.mapEntries = root["copies"].size();
        PluginNames::Lookup<const RE::TESFile*> lookup;
        // Pass 1: resolve and fill data members; failures stay out of the lookup
        for (const auto& item : root["copies"]) {
            Entry entry = ResolveEntry(dh, lookup, item);
            if (entry.ok && !CopyFill::FillData(entry.copy, entry.source)) {
                entry.ok = false;
                entry.detail = "the fill refused the type";
            }
            if (entry.ok && copies.bySource.contains(entry.source->GetFormID())) {
                entry.ok = false;
                entry.detail = "a second copy of the same source";
            }
            if (entry.ok) {
                copies.bySource.emplace(entry.source->GetFormID(), entry.copy);
                copies.byCopy.emplace(entry.copy->GetFormID(), entry.source);
                Count(copies.counts, entry.type);
            } else {
                ++copies.counts.failed;
                logger::warn("BlackFlame: copies: {} left out: {}", entry.sourceText.empty() ? item.dump() : entry.sourceText,
                    entry.detail);
            }
            copies.entries.push_back(std::move(entry));
        }
        // Pass 2: links that have a copy point at it (all copies exist now)
        for (const auto& entry : copies.entries) {
            if (entry.ok) {
                copies.counts.linksToCopies += static_cast<std::size_t>(CopyFill::RewireLinks(entry.copy));
            }
        }
        const auto& c = copies.counts;
        logger::info("BlackFlame: copies: {} map entries; filled PROJ {}, EXPL {}, IPCT {}, HAZD {}; {} left out; {} links "
                     "pointed at copies", c.mapEntries, c.projectiles, c.explosions, c.impacts, c.hazards, c.failed,
            c.linksToCopies);
    }

    RE::TESForm* CopyOf(const RE::TESForm* a_source) noexcept
    {
        if (!a_source) {
            return nullptr;
        }
        const auto& map = TheCopies().bySource;
        const auto it = map.find(a_source->GetFormID());
        return it != map.end() ? it->second : nullptr;
    }

    const RE::TESForm* SourceOf(const RE::TESForm* a_form) noexcept
    {
        if (!a_form) {
            return nullptr;
        }
        const auto& map = TheCopies().byCopy;
        const auto it = map.find(a_form->GetFormID());
        return it != map.end() ? it->second : nullptr;
    }

    CopyCounts GetCopyCounts() noexcept
    {
        return TheCopies().counts;
    }

    std::vector<CopyCheckLine> CheckCopies()
    {
        std::vector<CopyCheckLine> lines;
        const auto& copies = TheCopies();
        const auto& c = copies.counts;
        lines.push_back({ c.mapRead && c.failed == 0 && c.mapEntries > 0,
            std::format("copies map {}: {} entries, filled PROJ {} EXPL {} IPCT {} HAZD {}, {} left out, {} links to copies",
                c.mapRead ? "read" : "NOT read", c.mapEntries, c.projectiles, c.explosions, c.impacts, c.hazards, c.failed,
                c.linksToCopies) });
        for (const auto& entry : copies.entries) {
            if (!entry.ok) {
                lines.push_back({ false, std::format("{}: left out: {}", entry.sourceText, entry.detail) });
                continue;
            }
            const auto diff = CopyFill::Compare(entry.copy, entry.source);
            lines.push_back({ diff.empty(), std::format("{} {:08X} -> copy {:08X}: {}", entry.sourceText,
                                                entry.source->GetFormID(), entry.copy->GetFormID(),
                                                diff.empty() ? "fields equal the live source, links to copies where one exists" : diff) });
        }
        return lines;
    }
}
