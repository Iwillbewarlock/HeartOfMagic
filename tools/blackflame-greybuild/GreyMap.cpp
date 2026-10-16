// =============================================================================
// GreyMap - reads and writes HeartOfMagic_BlackFlame_greymap.json
// =============================================================================
// Shape (plugins/BlackFlame/README.md, "Grey duplicates"):
//   { "version": 1, "tint": 0.6,
//     "addons": [ { "index": 9, "grey": 9109, "model": "meshes\\mps\\...",
//                   "greyModel": "meshes\\heartofmagic\\blackflame\\mps\\...", "editorId": ..., "source": ...,
//                   "masterParticleCap": 0, "dnamFlags": 3, "greyEditorId": ..., "greyForm": ... } ],
//     "models": { "<impact / hazard NIF>": "<its grey copy>" } }
// Paths are Data-relative; the tool reads them lower case with backslashes. index, grey,
// model and greyModel are required; the rest is for the DLL and people and is carried through.

#include "GreyJson.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace GreyBuild
{
    namespace
    {
        constexpr int kMapVersion = 1;
        constexpr int kJsonIndent = 2;

        AddonEntry ReadAddon(const std::filesystem::path& a_file, const nlohmann::json& a_item, const std::string& a_where)
        {
            if (!a_item.is_object()) {
                Json::Fail(a_file, a_where + " must be an object");
            }
            AddonEntry entry;
            entry.index = Json::ReadIndex(a_file, a_item, "index", a_where);
            entry.grey = Json::ReadIndex(a_file, a_item, "grey", a_where);
            entry.model = Json::CheckedPath(a_file, Json::ReadString(a_file, a_item, "model", a_where), kMeshesRoot,
                a_where);
            entry.greyModel = Json::CheckedPath(a_file, Json::ReadString(a_file, a_item, "greyModel", a_where),
                kGreyRoot, a_where);
            entry.editorId = Json::ReadOptionalString(a_file, a_item, "editorId", a_where);
            entry.source = Json::ReadOptionalString(a_file, a_item, "source", a_where);
            entry.masterParticleCap = Json::ReadU16(a_file, a_item, "masterParticleCap", a_where, 0);
            entry.dnamFlags = Json::ReadU16(a_file, a_item, "dnamFlags", a_where, kDnamMasterParticleSystem);
            entry.greyEditorId = Json::ReadOptionalString(a_file, a_item, "greyEditorId", a_where);
            entry.greyForm = Json::ReadOptionalString(a_file, a_item, "greyForm", a_where);
            return entry;
        }

        void ReadAddons(const std::filesystem::path& a_file, const nlohmann::json& a_root, GreyMap& a_map)
        {
            const auto& addons = Json::RequireArray(a_file, a_root, "addons");
            std::set<int> greys;
            for (std::size_t i = 0; i < addons.size(); ++i) {
                const std::string where = fmt::format("addons[{}]", i);
                AddonEntry entry = ReadAddon(a_file, addons[i], where);
                if (a_map.addons.count(entry.index) != 0) {
                    Json::Fail(a_file, fmt::format("{}: index {} is listed twice", where, entry.index));
                }
                if (!greys.insert(entry.grey).second) {
                    Json::Fail(a_file, fmt::format("{}: grey index {} is listed twice", where, entry.grey));
                }
                a_map.addons.emplace(entry.index, std::move(entry));
            }
            // Two indices may share one MPS (and so one grey copy); two MPS NIFs may not write one copy
            std::map<std::string, const AddonEntry*> writers;
            for (const auto& [index, entry] : a_map.addons) {
                if (a_map.addons.count(entry.grey) != 0) {
                    Json::Fail(a_file,
                        fmt::format("grey index {} of index {} is also an original index", entry.grey, index));
                }
                const auto [at, added] = writers.emplace(entry.greyModel, &entry);
                if (!added && at->second->model != entry.model) {
                    Json::Fail(a_file, fmt::format("greyModel '{}' is written from both '{}' (index {}) and '{}' "
                                                   "(index {})",
                        entry.greyModel, at->second->model, at->second->index, entry.model, index));
                }
            }
        }

        void ReadModels(const std::filesystem::path& a_file, const nlohmann::json& a_root, GreyMap& a_map)
        {
            if (!a_root.contains("models") || !a_root["models"].is_object()) {
                Json::Fail(a_file, "'models' must be an object (impact / hazard NIF -> grey copy)");
            }
            std::set<std::string> outputs;
            for (const auto& [index, entry] : a_map.addons) {
                outputs.insert(entry.greyModel);
            }
            for (const auto& [source, grey] : a_root["models"].items()) {
                const std::string where = fmt::format("models['{}']", source);
                if (!grey.is_string()) {
                    Json::Fail(a_file, where + " must be a string");
                }
                const std::string from = Json::CheckedPath(a_file, source, kMeshesRoot, where);
                const std::string to = Json::CheckedPath(a_file, grey.get<std::string>(), kGreyRoot, where);
                if (!outputs.insert(to).second) {
                    Json::Fail(a_file, fmt::format("{}: output '{}' is written twice", where, to));
                }
                if (!a_map.models.emplace(from, to).second) {
                    Json::Fail(a_file, fmt::format("{}: listed twice", where));
                }
            }
        }
    }

    std::string NormalizeDataPath(std::string a_path)
    {
        std::replace(a_path.begin(), a_path.end(), '/', '\\');
        std::transform(a_path.begin(), a_path.end(), a_path.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return a_path;
    }

    std::string MeshDataPath(const std::string& a_path)
    {
        const std::string path = NormalizeDataPath(a_path);
        return StartsWith(path, kMeshesRoot) ? path : std::string(kMeshesRoot) + path;
    }

    std::string GreyPathFor(const std::string& a_meshDataPath)
    {
        const std::string path = MeshDataPath(a_meshDataPath);
        return std::string(kGreyRoot) + path.substr(std::char_traits<char>::length(kMeshesRoot));
    }

    GreyMap LoadMap(const std::filesystem::path& a_file)
    {
        const nlohmann::json root = Json::ReadFile(a_file);
        Json::RequireVersion(a_file, root, kMapVersion);
        GreyMap map;
        if (!root.contains("tint") || !root["tint"].is_number()) {
            Json::Fail(a_file, "'tint' must be a number (the base colour scale, default 0.6)");
        }
        map.tint = root["tint"].get<float>();
        if (!(map.tint >= kMinTint && map.tint <= kMaxTint)) {
            Json::Fail(a_file, fmt::format("'tint' = {} is outside {}..{}", map.tint, kMinTint, kMaxTint));
        }
        if (root.contains("tintAdd")) {  // v2i, optional: additive palette shaders' base colour scale
            if (!root["tintAdd"].is_number() || !(root["tintAdd"].get<float>() >= kMinTint && root["tintAdd"].get<float>() <= kMaxTint)) {
                Json::Fail(a_file, fmt::format("'tintAdd' must be a number in {}..{}", kMinTint, kMaxTint));
            }
            map.tintAdd = root["tintAdd"].get<float>();
        }
        ReadAddons(a_file, root, map);
        ReadModels(a_file, root, map);
        return map;
    }

    void WriteMap(const GreyMap& a_map, const std::filesystem::path& a_file, const std::vector<std::string>& a_comment)
    {
        nlohmann::ordered_json root;
        root["_comment"] = a_comment;
        root["version"] = kMapVersion;
        root["tint"] = std::stod(fmt::format("{:.4g}", a_map.tint));  // 0.6, not 0.6000000238 (float to double)
        root["tintAdd"] = std::stod(fmt::format("{:.4g}", a_map.tintAdd));
        root["addons"] = nlohmann::ordered_json::array();
        for (const auto& [index, entry] : a_map.addons) {
            nlohmann::ordered_json item;
            item["index"] = entry.index;
            item["grey"] = entry.grey;
            item["editorId"] = entry.editorId;
            item["source"] = entry.source;
            item["masterParticleCap"] = entry.masterParticleCap;
            item["dnamFlags"] = entry.dnamFlags;
            item["model"] = entry.model;
            item["greyModel"] = entry.greyModel;
            item["greyEditorId"] = entry.greyEditorId;
            item["greyForm"] = entry.greyForm;
            root["addons"].push_back(std::move(item));
        }
        root["models"] = nlohmann::ordered_json::object();
        for (const auto& [source, grey] : a_map.models) {
            root["models"][source] = grey;
        }
        std::filesystem::create_directories(a_file.parent_path());
        std::ofstream out(a_file, std::ios::binary | std::ios::trunc);
        out << root.dump(kJsonIndent) << "\n";
        out.close();  // the last buffer is flushed here; a full disk shows only now
        if (!out) {
            throw std::runtime_error(fmt::format("writing '{}' failed", a_file.string()));
        }
    }
}
