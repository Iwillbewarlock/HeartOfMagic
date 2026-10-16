#include "FormCopies.h"

#include "TrackedMutex.h"

#include "FireClosure.h"
#include "NifDump.h"

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
        constexpr int kMapVersionOld = 1;  // read: stale by definition (the C3 freshness check)
        constexpr int kMapVersion = 2;
        constexpr std::string_view kMeshesPrefix = "meshes\\";
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
            bool sourceMissing = false;
            bool copyMissing = false;
            std::string detail;
        };

        struct Copies
        {
            std::unordered_map<RE::FormID, RE::TESForm*> bySource;
            std::unordered_map<RE::FormID, const RE::TESForm*> byCopy;
            std::vector<Entry> entries;
            CopyCounts counts;
            std::unordered_map<RE::FormID, std::string> muzzles;  // copy -> expected muzzle model (node-stable strings)
        };

        TrackedMutex g_refreshMutex;
        std::vector<std::string> g_refreshNotes;  // under g_refreshMutex

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
            if (a_tag == "ARTO") {
                return RE::FormType::ArtObject;
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
                entry.detail = std::format("bad entry {} (type PROJ/EXPL/IPCT/HAZD/ARTO, hex ids)", a_item.dump());
                return entry;
            }
            entry.source = LookupLocal(a_dh, a_lookup, entry.sourceText.substr(0, bar), sourceLocal);
            entry.copy = LookupLocal(a_dh, a_lookup, kCopiesPluginName, copyLocal);
            entry.sourceMissing = !entry.source;
            entry.copyMissing = !entry.copy;
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
            // W4: a source in the copies plugin itself (or the copy named as its own source) is never filled from
            const auto* sourceFile = entry.source->GetFile(0);
            const bool inCopiesPlugin = sourceFile && _stricmp(std::string(sourceFile->GetFilename()).c_str(), kCopiesPluginName) == 0;
            if (entry.source == entry.copy || inCopiesPlugin) {
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
            case RE::FormType::Hazard:
                ++a_counts.hazards;
                break;
            case RE::FormType::ArtObject:
                ++a_counts.arts;
                break;
            default:
                break;  // TypeOf admits no other type
            }
        }

        // J5: the copy's own model opens through the game's file system (as GreyCheck opens the grey models); a copy whose
        // NIF is missing is left out, so the engine keeps the original form and its runtime fallback. Empty model: fine.
        std::string ModelProblem(const RE::TESForm* a_copy)
        {
            const auto model = ModelPathOf(a_copy);
            if (model.empty()) {
                return {};
            }
            const auto path = NifDump::NormalizeModelPath(model);
            if (!path) {
                return std::format("the copy's model path '{}' is refused", model);
            }
            RE::BSResourceNiBinaryStream stream(path->c_str());
            return stream.good() ? std::string() : std::format("the copy's model {} does not open (is the builder's output mod "
                                                               "enabled with its meshes?)", *path);
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
                (a_root["version"] != kMapVersion && a_root["version"] != kMapVersionOld) || !a_root.contains("copies") ||
                !a_root["copies"].is_array()) {
                logger::warn("BlackFlame: copies: '{}' is not a version {} or {} copies map - no v2 copies", kCopiesMapPath,
                    kMapVersionOld, kMapVersion);
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
        copies.counts.mapVersion = root["version"].get<int>();
        const auto text = [&](const char* a_key) {
            return root.contains(a_key) && root[a_key].is_string() ? root[a_key].get<std::string>() : std::string();
        };
        copies.counts.requestFingerprint = text("requestFingerprint");
        copies.counts.requestFingerprintSizes = text("requestFingerprintSizes");
        PluginNames::Lookup<const RE::TESFile*> lookup;
        // Pass 1: resolve and fill data members; failures stay out of the lookup
        for (const auto& item : root["copies"]) {
            Entry entry = ResolveEntry(dh, lookup, item);
            copies.counts.sourceMissing += entry.sourceMissing ? 1 : 0;
            copies.counts.copyMissing += entry.copyMissing ? 1 : 0;
            // C3: a PROJ copy's muzzle is the map's grey copy when the builder made one (before the fill reads it)
            if (entry.copy && entry.type == RE::FormType::Projectile && item.contains("greyMuzzleModel") &&
                item["greyMuzzleModel"].is_string() && !item["greyMuzzleModel"].get<std::string>().empty()) {
                std::string muzzle = item["greyMuzzleModel"].get<std::string>();
                if (muzzle.size() > kMeshesPrefix.size() && _strnicmp(muzzle.c_str(), kMeshesPrefix.data(), kMeshesPrefix.size()) == 0) {
                    muzzle.erase(0, kMeshesPrefix.size());  // a record's model string leaves out the meshes folder
                }
                copies.muzzles.emplace(entry.copy->GetFormID(), std::move(muzzle));
            }
            // Checked before the fill: a second entry must not overwrite a copy another source already filled,
            // and the two maps stay one-to-one (the first entry wins, the second is logged and left out)
            if (entry.ok && copies.bySource.contains(entry.source->GetFormID())) {
                entry.ok = false;
                entry.detail = "a second copy of the same source";
            }
            if (entry.ok && (copies.byCopy.contains(entry.copy->GetFormID()) || copies.bySource.contains(entry.copy->GetFormID()) ||
                                copies.byCopy.contains(entry.source->GetFormID()))) {
                entry.ok = false;
                entry.detail = "the copy form already belongs to another entry (or a copy is named as a source)";
            }
            if (const auto problem = entry.ok ? ModelProblem(entry.copy) : std::string(); !problem.empty()) {
                entry.ok = false;
                entry.detail = problem;
            }
            if (const auto refusal = entry.ok ? CopyFill::Refusal(entry.source) : std::string(); !refusal.empty()) {
                entry.ok = false;
                entry.detail = refusal;
            }
            if (entry.ok && !CopyFill::FillData(entry.copy, entry.source)) {
                entry.ok = false;
                entry.detail = "the fill refused the type";
            }
            if (entry.ok) {
                const bool greyless = item.contains("greyModel") && item["greyModel"].is_string() &&
                                      item["greyModel"].get<std::string>().empty() && item.contains("sourceModel") &&
                                      item["sourceModel"].is_string() && !item["sourceModel"].get<std::string>().empty();
                copies.counts.greyless += greyless ? 1 : 0;
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
        // C5: a copy that differs right after its own fill is a fill fault, not a later change
        for (const auto& entry : copies.entries) {
            if (const auto diff = entry.ok ? CopyFill::Compare(entry.copy, entry.source) : std::string(); !diff.empty()) {
                logger::warn("BlackFlame: copies: {} {:08X} -> {:08X} differs right after the fill: {}", entry.sourceText,
                    entry.source->GetFormID(), entry.copy->GetFormID(), diff);
            }
        }
        const auto& c = copies.counts;
        logger::info("BlackFlame: copies: {} map entries; filled PROJ {}, EXPL {}, IPCT {}, HAZD {}, ARTO {}; {} left out; {} "
                     "links pointed at copies", c.mapEntries, c.projectiles, c.explosions, c.impacts, c.hazards, c.arts, c.failed,
            c.linksToCopies);
    }

    void RefreshCopies(const char* a_reason)
    {
        std::vector<std::string> notes;
        for (const auto& entry : TheCopies().entries) {
            if (!entry.ok) {
                continue;
            }
            const auto before = CopyFill::Compare(entry.copy, entry.source);
            if (before.empty()) {
                continue;
            }
            (void)CopyFill::Refill(entry.copy, entry.source);  // D3: no window where a copy points at an original
            const auto after = CopyFill::Compare(entry.copy, entry.source);
            notes.push_back(std::format("{} {:08X} -> copy {:08X} differed at the {} ({}); filled again: {}", entry.sourceText,
                entry.source->GetFormID(), entry.copy->GetFormID(), a_reason, before, after.empty() ? "now equal" : after));
            logger::warn("BlackFlame: copies: {}", notes.back());
        }
        logger::info("BlackFlame: copies: refresh at the {}: {} cop{} differed from the live source", a_reason, notes.size(),
            notes.size() == 1 ? "y" : "ies");
        std::lock_guard lock(g_refreshMutex);
        g_refreshNotes = std::move(notes);
    }

    std::vector<std::string> CopyRefreshNotes()
    {
        std::lock_guard lock(g_refreshMutex);
        return g_refreshNotes;
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

    const char* ExpectedMuzzleFor(const RE::TESForm* a_copy) noexcept
    {
        if (!a_copy) {
            return nullptr;
        }
        const auto& map = TheCopies().muzzles;
        const auto it = map.find(a_copy->GetFormID());
        return it != map.end() ? it->second.c_str() : nullptr;
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
            std::format("copies map {}: {} entries, filled PROJ {} EXPL {} IPCT {} HAZD {} ARTO {}, {} left out, {} links to "
                "copies", c.mapRead ? "read" : "NOT read", c.mapEntries, c.projectiles, c.explosions, c.impacts, c.hazards,
                c.arts, c.failed, c.linksToCopies) });
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
