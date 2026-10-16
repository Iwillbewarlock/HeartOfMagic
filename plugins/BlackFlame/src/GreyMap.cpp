#include "GreyMap.h"

#include "DataPathRule.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <memory>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace BlackFlame
{
    namespace
    {
        constexpr const char* kGreyMapPath = "Data/SKSE/Plugins/HeartOfMagic_BlackFlame_greymap.json";
        constexpr int kMapVersion = 1;
        constexpr std::string_view kMeshesPrefix = "meshes\\";
        constexpr std::string_view kGreyRoot = "heartofmagic\\blackflame\\";  // where every grey copy lives
        constexpr std::size_t kMaxPathLength = 260;

        struct Map
        {
            std::unordered_map<std::string, std::string> models;  // normalised original -> grey (no meshes\)
            std::vector<std::string> modelFiles;                  // grey with meshes\ (existence check)
            std::vector<GreyAddon> addons;
        };

        // Built once at kDataLoaded and never changed or freed (see the header)
        Map& TheMap()
        {
            static auto* map = std::make_unique<Map>().release();
            return *map;
        }

        // DataPathRule::LowerSlashes (the builder's key rule, a DBCS trail byte kept), a leading "meshes\" dropped
        std::string Normalise(std::string_view a_path)
        {
            std::string out = DataPathRule::LowerSlashes(a_path);
            if (out.starts_with(kMeshesPrefix)) {
                out.erase(0, kMeshesPrefix.size());
            }
            return out;
        }

        // A path the map may name: relative, .nif, no "..", not too long
        bool IsSafeNif(const std::string& a_normalised)
        {
            return !a_normalised.empty() && a_normalised.size() < kMaxPathLength && a_normalised.ends_with(".nif") &&
                   a_normalised.find("..") == std::string::npos && a_normalised.find(':') == std::string::npos &&
                   a_normalised.front() != '\\';
        }

        void ReadModels(const nlohmann::json& a_root, Map& a_map)
        {
            const auto it = a_root.find("models");
            if (it == a_root.end() || !it->is_object()) {
                logger::warn("BlackFlame: grey map: no \"models\" object - no impact or hazard model is swapped");
                return;
            }
            for (const auto& [source, grey] : it->items()) {
                const auto from = Normalise(source);
                const auto to = grey.is_string() ? Normalise(grey.get_ref<const std::string&>()) : std::string{};
                if (!IsSafeNif(from) || !IsSafeNif(to) || !to.starts_with(kGreyRoot)) {
                    logger::warn("BlackFlame: grey map: models entry \"{}\" -> {} skipped (a .nif under meshes\\, the grey "
                                 "copy under meshes\\{}, is expected)", source, grey.dump(), kGreyRoot);
                    continue;
                }
                a_map.modelFiles.push_back(std::string(kMeshesPrefix) + to);
                a_map.models.emplace(from, to);
            }
        }

        void ReadAddons(const nlohmann::json& a_root, Map& a_map)
        {
            const auto it = a_root.find("addons");
            if (it == a_root.end() || !it->is_array()) {
                return;
            }
            for (const auto& item : *it) {
                if (!item.is_object() || !item.contains("index") || !item.contains("grey") || !item["index"].is_number_unsigned() ||
                    !item["grey"].is_number_unsigned() || !item.contains("greyForm") || !item["greyForm"].is_string() ||
                    !item.contains("greyModel") || !item["greyModel"].is_string()) {
                    logger::warn("BlackFlame: grey map: addons entry {} skipped (index, grey, greyModel, greyForm expected)",
                        item.dump());
                    continue;
                }
                const auto greyModel = Normalise(item["greyModel"].get_ref<const std::string&>());
                if (!IsSafeNif(greyModel)) {
                    logger::warn("BlackFlame: grey map: addons entry {} skipped (bad greyModel)", item.dump());
                    continue;
                }
                a_map.addons.push_back({ item["index"].get<std::uint32_t>(), item["grey"].get<std::uint32_t>(),
                    std::string(kMeshesPrefix) + greyModel, item["greyForm"].get<std::string>() });
            }
        }
    }

    void LoadGreyMap()
    {
        static bool loaded = false;
        if (loaded) {
            return;  // once: lookups hand out pointers into this storage
        }
        loaded = true;
        std::ifstream file(kGreyMapPath, std::ios::binary);
        if (!file) {
            logger::warn("BlackFlame: grey map '{}' not found - impact and hazard swaps are off", kGreyMapPath);
            return;
        }
        std::stringstream text;
        text << file.rdbuf();
        const auto root = nlohmann::json::parse(text.str(), nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            logger::warn("BlackFlame: grey map '{}' is not a JSON object - swaps are off", kGreyMapPath);
            return;
        }
        const auto version = root.find("version");
        if (version == root.end() || !version->is_number_integer() || version->get<int>() != kMapVersion) {
            logger::warn("BlackFlame: grey map '{}' is not version {} - swaps are off", kGreyMapPath, kMapVersion);
            return;
        }
        auto& map = TheMap();
        ReadModels(root, map);
        ReadAddons(root, map);
        logger::info("BlackFlame: grey map: {} models, {} grey addon nodes", map.models.size(), map.addons.size());
    }

    const char* GreyModelFor(const char* a_model)
    {
        if (!a_model || !*a_model) {
            return nullptr;
        }
        const auto& models = TheMap().models;
        const auto it = models.find(Normalise(a_model));
        return it != models.end() ? it->second.c_str() : nullptr;
    }

    std::size_t GreyModelCount() noexcept
    {
        return TheMap().models.size();
    }

    const std::vector<GreyAddon>& GreyAddons()
    {
        return TheMap().addons;
    }

    std::vector<std::string> GreyModelFiles()
    {
        return TheMap().modelFiles;
    }
}
