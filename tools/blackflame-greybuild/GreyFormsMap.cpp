// =============================================================================
// GreyFormsMap - the copies map and stable copy ids (GreyForms.h)
// =============================================================================
// Copies map (version 2, written by the builder, read back on the next run for stable ids; version 1 is still read):
//   { "version": 2, "requestFingerprint": "fnv64:..", "copiesPlugin": "HeartOfMagic_BlackFlame_Copies.esp",
//     "copies": [ { "source": "skyrim.esm|10FBED", "type": "PROJ", "copy": "000800", "editorId", "sourceEditorId",
//                   "sourceModel", "greyModel", "sourceMuzzleModel", "greyMuzzleModel", "note",
//                   TXST: "sourceTextures", "greyTextures", "hasDecal" (GreyFormsTxst.h) } ],
//     "retired": [ { "source": ..., "copy": "000805", "type": "PROJ" } ], "excluded": [...] }
// Codex 8: an id stays with its source AND type: a source that comes back as another type, active or retired, is refused
// before anything is written (a save holding the old copy would read it as a form of the other type). Retired entries of
// maps written before TXST copies have no "type" and match any.

#include "GreyForms.h"
#include "GreyFormsTxst.h"
#include "GreyJson.h"

#include <algorithm>
#include <fstream>
#include <set>

namespace GreyBuild
{
    namespace
    {
        constexpr int kJsonIndent = 2;

        [[nodiscard]] CopyType ParseType(const std::filesystem::path& a_file, const std::string& a_tag, const std::string& a_where)
        {
            if (const auto type = CopyTypeFromTag(a_tag)) {
                return *type;
            }
            Json::Fail(a_file, fmt::format("{}: type '{}' is not PROJ, HAZD, EXPL, IPCT, ARTO or TXST", a_where, a_tag));
        }

        std::string OutOfIds(const std::vector<FormSource>& a_sources, const CopiesMap& a_previous, bool a_reclaim)
        {
            std::map<std::string, int> byType;
            for (const auto& form : a_sources) {
                ++byType[CopyTypeTag(form.type)];
            }
            std::string types;
            for (const auto& [tag, count] : byType) {
                types += fmt::format("{}{} {}", types.empty() ? "" : ", ", tag, count);
            }
            return fmt::format("no copy id left: {} forms requested ({}), {} copies in the earlier map, {} retired ids; an ESL "
                "holds 2048 (0x800..0xFFF). Nothing was written. {}", a_sources.size(), types, a_previous.entries.size(),
                a_previous.retired.size(), a_reclaim ? "Even with --reclaim-retired the ids do not fit: the load order has more "
                "fire forms than one ESL can copy." : "Run again with --reclaim-retired to give retired ids to new forms - only "
                "for a new save: a save holding a projectile or hazard of a retired copy would read it as the new form.");
        }

        [[nodiscard]] std::uint32_t ParseCopyId(const std::filesystem::path& a_file, const std::string& a_text,
            const std::string& a_where)
        {
            const bool hex = !a_text.empty() && a_text.size() <= kMaxLocalIdDigits &&
                             a_text.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
            const auto id = hex ? static_cast<std::uint32_t>(std::stoul(a_text, nullptr, kHexBase)) : 0u;  // 6 hex digits
            if (!hex || id < kFirstLightFormId || id > kLastLightFormId) {
                Json::Fail(a_file, fmt::format("{}: copy id '{}' is not hex in {:03X}..{:03X}", a_where, a_text,
                    kFirstLightFormId, kLastLightFormId));
            }
            return id;
        }

        // Codex 8: the previous type of a_source (active or retired), when the map knows it
        [[nodiscard]] std::optional<CopyType> PreviousType(const CopiesMap& a_previous, const std::string& a_source)
        {
            for (const auto& entry : a_previous.entries) {
                if (entry.source == a_source) {
                    return entry.type;
                }
            }
            const auto it = a_previous.retiredTypes.find(a_source);
            return it != a_previous.retiredTypes.end() ? std::optional<CopyType>(it->second) : std::nullopt;
        }
    }

    CopiesMap LoadCopiesMap(const std::filesystem::path& a_file)
    {
        const nlohmann::json root = Json::ReadFile(a_file);
        if (!root.contains("version") || (root["version"] != 1 && root["version"] != kCopiesMapVersion)) {
            Json::Fail(a_file, fmt::format("'version' must be 1 or {}", kCopiesMapVersion));
        }
        CopiesMap map;
        map.requestFingerprint = Json::ReadOptionalString(a_file, root, "requestFingerprint", "the map");
        map.requestFingerprintSizes = Json::ReadOptionalString(a_file, root, "requestFingerprintSizes", "the map");
        map.excluded = ReadExcludedForms(a_file, root);
        std::set<std::uint32_t> ids;
        std::set<std::string> sources;  // V16: a source once, across copies and retired
        const auto& copies = Json::RequireArray(a_file, root, "copies");
        for (std::size_t i = 0; i < copies.size(); ++i) {
            const std::string where = fmt::format("copies[{}]", i);
            CopyEntry entry;
            entry.source = NormalizeSource(Json::ReadString(a_file, copies[i], "source", where));
            entry.type = ParseType(a_file, Json::ReadString(a_file, copies[i], "type", where), where);
            entry.localId = ParseCopyId(a_file, Json::ReadString(a_file, copies[i], "copy", where), where);
            entry.editorId = Json::ReadOptionalString(a_file, copies[i], "editorId", where);
            entry.sourceEditorId = Json::ReadOptionalString(a_file, copies[i], "sourceEditorId", where);
            entry.sourceModel = Json::ReadOptionalString(a_file, copies[i], "sourceModel", where);
            entry.greyModel = Json::ReadOptionalString(a_file, copies[i], "greyModel", where);
            entry.sourceMuzzleModel = Json::ReadOptionalString(a_file, copies[i], "sourceMuzzleModel", where);
            entry.greyMuzzleModel = Json::ReadOptionalString(a_file, copies[i], "greyMuzzleModel", where);
            entry.note = Json::ReadOptionalString(a_file, copies[i], "note", where);
            if (entry.type == CopyType::kTextureSet) {
                ReadTextureSetMapParts(a_file, copies[i], entry, where);
            }
            if (!ids.insert(entry.localId).second) {
                Json::Fail(a_file, fmt::format("{}: copy id {:06X} is used twice", where, entry.localId));
            }
            if (!sources.insert(entry.source).second) {
                Json::Fail(a_file, fmt::format("{}: source {} is listed twice", where, entry.source));
            }
            map.entries.push_back(std::move(entry));
        }
        if (root.contains("retired")) {
            const auto& retired = Json::RequireArray(a_file, root, "retired");
            for (std::size_t i = 0; i < retired.size(); ++i) {
                const std::string where = fmt::format("retired[{}]", i);
                const auto id = ParseCopyId(a_file, Json::ReadString(a_file, retired[i], "copy", where), where);
                if (!ids.insert(id).second) {
                    Json::Fail(a_file, fmt::format("{}: copy id {:06X} is used twice", where, id));
                }
                const auto source = NormalizeSource(Json::ReadString(a_file, retired[i], "source", where));
                if (!sources.insert(source).second) {
                    Json::Fail(a_file, fmt::format("{}: source {} is listed twice (copies and retired)", where, source));
                }
                map.retired[source] = id;
                if (const auto type = Json::ReadOptionalString(a_file, retired[i], "type", where); !type.empty()) {
                    map.retiredTypes[source] = ParseType(a_file, type, where);
                }
            }
        }
        return map;
    }

    void WriteCopiesMap(const CopiesMap& a_map, const std::filesystem::path& a_file)
    {
        nlohmann::ordered_json root;
        root["version"] = kCopiesMapVersion;
        root["_comment"] = { "Generated by blackflame-greybuild (plugins/BlackFlame/README.md, \"Black Flame v2\").",
            "Source form -> its copy in the copies plugin. Read back by the next run: ids stay stable, retired ids are "
            "never given to another source (unless --reclaim-retired). requestFingerprint: the DLL compares it with the "
            "load order at every start." };
        root["requestFingerprint"] = a_map.requestFingerprint;
        root["requestFingerprintSizes"] = a_map.requestFingerprintSizes;
        root["copiesPlugin"] = kCopiesPluginName;
        root["copies"] = nlohmann::ordered_json::array();
        for (const auto& entry : a_map.entries) {
            nlohmann::ordered_json item{ { "source", entry.source }, { "type", CopyTypeTag(entry.type) },
                { "copy", fmt::format("{:06X}", entry.localId) }, { "editorId", entry.editorId },
                { "sourceEditorId", entry.sourceEditorId }, { "sourceModel", entry.sourceModel },
                { "greyModel", entry.greyModel }, { "sourceMuzzleModel", entry.sourceMuzzleModel },
                { "greyMuzzleModel", entry.greyMuzzleModel }, { "note", entry.note } };
            if (entry.type == CopyType::kTextureSet) {
                WriteTextureSetMapParts(item, entry);
            }
            root["copies"].push_back(std::move(item));
        }
        root["retired"] = nlohmann::ordered_json::array();
        for (const auto& [source, id] : a_map.retired) {
            nlohmann::ordered_json item{ { "source", source }, { "copy", fmt::format("{:06X}", id) } };
            if (const auto it = a_map.retiredTypes.find(source); it != a_map.retiredTypes.end()) {
                item["type"] = CopyTypeTag(it->second);
            }
            root["retired"].push_back(std::move(item));
        }
        root["excluded"] = nlohmann::ordered_json::array();
        for (const auto& excluded : a_map.excluded) {
            root["excluded"].push_back({ { "source", excluded.source }, { "type", excluded.type }, { "reason", excluded.reason } });
        }
        // v2i: the palette kinds of the grey NIFs (information only: no reader takes it)
        root["shaders"] = { { "tint", std::stod(fmt::format("{:.4g}", a_map.tint)) },
            { "tintAdd", std::stod(fmt::format("{:.4g}", a_map.tintAdd)) }, { "effectShaders", a_map.shaders.effectShaders },
            { "paletteNormal", a_map.shaders.paletted }, { "paletteNormalNoAlpha", a_map.shaders.paletteNoAlpha },
            { "paletteAdditive", a_map.shaders.palettedAdd }, { "left", a_map.shaders.paletteLeft } };
        std::filesystem::create_directories(a_file.parent_path());
        std::ofstream out(a_file, std::ios::binary | std::ios::trunc);
        out << root.dump(kJsonIndent) << "\n";
        out.close();
        if (!out) {
            throw std::runtime_error(fmt::format("writing '{}' failed", a_file.string()));
        }
    }

    std::string CopyRecordModel(const CopyEntry& a_entry)
    {
        const std::string& model = a_entry.greyModel.empty() ? a_entry.sourceModel : a_entry.greyModel;
        return StartsWith(model, kMeshesRoot) ? model.substr(std::char_traits<char>::length(kMeshesRoot)) : model;
    }

    CopiesMap AssignCopyIds(const std::vector<FormSource>& a_sources, const CopiesMap& a_previous, bool a_reclaimRetired)
    {
        std::map<std::string, std::uint32_t> known = a_previous.retired;
        std::uint32_t highest = kFirstLightFormId - 1;
        for (const auto& entry : a_previous.entries) {
            known[entry.source] = entry.localId;
        }
        // V16: from every id the previous map holds (entries and retired alike), not from the merged source map
        for (const auto& entry : a_previous.entries) {
            highest = std::max(highest, entry.localId);
        }
        for (const auto& [source, id] : a_previous.retired) {
            highest = std::max(highest, id);
        }
        CopiesMap out;
        std::set<std::string> requested;
        // --reclaim-retired: retired ids lowest first, once the ids above every used one run out
        std::map<std::uint32_t, std::string> reclaimable;
        if (a_reclaimRetired) {
            for (const auto& [source, id] : a_previous.retired) {
                reclaimable.emplace(id, source);
            }
        }
        std::set<std::string> reclaimed;
        for (const auto& form : a_sources) {
            const std::string source = NormalizeSource(form.source);
            requested.insert(source);
            // Codex 8: a source keeps its type with its id
            if (const auto before = PreviousType(a_previous, source); before && *before != form.type) {
                throw std::runtime_error(fmt::format("source {} was copied as {} and is now requested as {}: its copy id would "
                    "change type (a save holding the old copy would read it as the new one). Nothing was written; build into a "
                    "new --out for a new save.", source, CopyTypeTag(*before), CopyTypeTag(form.type)));
            }
        }
        for (auto it = reclaimable.begin(); it != reclaimable.end();) {
            it = requested.count(it->second) != 0 ? reclaimable.erase(it) : std::next(it);  // requested again: keeps its id
        }
        requested.clear();
        for (const auto& form : a_sources) {
            const std::string source = NormalizeSource(form.source);
            if (!requested.insert(source).second) {
                throw std::runtime_error(fmt::format("source {} is listed twice", source));
            }
            CopyEntry entry;
            entry.source = source;
            entry.type = form.type;
            if (const auto it = known.find(source); it != known.end()) {
                entry.localId = it->second;
            } else if (highest < kLastLightFormId) {
                entry.localId = ++highest;
            } else if (!reclaimable.empty()) {
                entry.localId = reclaimable.begin()->first;
                reclaimed.insert(reclaimable.begin()->second);
                reclaimable.erase(reclaimable.begin());
            } else {
                throw std::runtime_error(OutOfIds(a_sources, a_previous, a_reclaimRetired));
            }
            entry.editorId = fmt::format("{}{}_{:03X}", kCopyEditorIdPrefix, CopyTypeTag(form.type), entry.localId);
            entry.sourceEditorId = form.editorId;
            entry.sourceModel = form.model;
            entry.greyModel = form.model.empty() ? std::string() : CopyModelFor(form.model);
            entry.sourceMuzzleModel = form.muzzleModel;
            entry.greyMuzzleModel = form.muzzleModel.empty() ? std::string() : CopyModelFor(form.muzzleModel);
            entry.sourceTextures = form.textures;  // TXST (empty otherwise); greyTextures come from the texture step
            entry.hasDecal = form.hasDecal;
            out.entries.push_back(std::move(entry));
        }
        for (const auto& [source, id] : known) {
            if (requested.count(source) == 0 && reclaimed.count(source) == 0) {
                out.retired[source] = id;
                if (const auto type = PreviousType(a_previous, source)) {
                    out.retiredTypes[source] = *type;
                }
            }
        }
        return out;
    }
}
