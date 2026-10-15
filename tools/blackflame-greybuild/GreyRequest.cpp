// =============================================================================
// GreyRequest - what one setup needs: read the request, plan the grey indices
// =============================================================================
// Request shape (plugins/BlackFlame/README.md, "Grey duplicates for any setup"):
//   { "version": 1,
//     "addons": [ { "index": 9, "model": "MPS\\MPSFireBoltImpact01.nif", "editorId": "MPSFireBoltImpact01",
//                   "source": "Skyrim.esm|02B95B", "masterParticleCap": 0, "dnamFlags": 3 }, ... ],
//     "models": [ "Effects\\ImpactEffects\\FireballImpact01.nif", ... ] }
// "addons" is the load order's winning ADDN record per index (all of them, so the plan
// knows which indices are taken); "models" the fire impact / hazard NIFs. Model paths may
// leave out "meshes\" as record Model fields do.

#include "GreyJson.h"

#include <NifFile.hpp>

#include <algorithm>
#include <set>

namespace GreyBuild
{
    namespace
    {
        constexpr int kRequestVersion = 1;
        constexpr const char* kGreyEditorIdPrefix = "HoMBF_GreyAddon_";

        struct ValueNode
        {
            std::string name;
            int value = 0;
        };

        [[nodiscard]] std::vector<ValueNode> ReadValueNodes(const std::filesystem::path& a_file)
        {
            nifly::NifFile nif;
            LoadNif(nif, a_file);
            std::vector<ValueNode> nodes;
            const auto& hdr = nif.GetHeader();
            for (std::uint32_t id = 0; id < hdr.GetNumBlocks(); ++id) {
                if (const auto* node = hdr.GetBlock<nifly::BSValueNode>(id)) {
                    nodes.push_back({ node->name.get(), node->value });
                }
            }
            return nodes;
        }

        /// Grey index for an original one (see PlanFromRequest); marks it used.
        void Allocate(const Request& a_request, AddonEntry& a_entry, std::set<int>& a_used, int& a_nextFree,
            std::vector<AddonEntry>& a_newRecords)
        {
            const int preferred = kGreyIndexBase + a_entry.index;
            const auto existing = a_request.addons.find(preferred);
            if (existing != a_request.addons.end() && existing->second.model == a_entry.greyModel) {
                a_entry.grey = preferred;  // a shipped grey record already names this copy
                a_entry.greyEditorId = existing->second.editorId;
                a_entry.greyForm = existing->second.source;
                return;
            }
            if (a_used.count(preferred) == 0) {
                a_entry.grey = preferred;
            } else {
                while (a_used.count(a_nextFree) != 0) {
                    ++a_nextFree;
                }
                a_entry.grey = a_nextFree;
            }
            a_used.insert(a_entry.grey);
            const auto formId = kFirstLightFormId + static_cast<std::uint32_t>(a_newRecords.size());
            if (formId > kLastLightFormId) {
                throw std::runtime_error("more grey ADDN records than an ESL can hold (2048)");
            }
            a_entry.greyEditorId = kGreyEditorIdPrefix +
                                   (a_entry.editorId.empty() ? std::to_string(a_entry.index) : a_entry.editorId);
            a_entry.greyForm = fmt::format("{}|{:06X}", kLocalPluginName, formId);
            a_newRecords.push_back(a_entry);
        }
    }

    Request LoadRequest(const std::filesystem::path& a_file)
    {
        const nlohmann::json root = Json::ReadFile(a_file);
        Json::RequireVersion(a_file, root, kRequestVersion);
        Request request;
        const auto& addons = Json::RequireArray(a_file, root, "addons");
        for (std::size_t i = 0; i < addons.size(); ++i) {
            const std::string where = fmt::format("addons[{}]", i);
            const auto& item = addons[i];
            if (!item.is_object()) {
                Json::Fail(a_file, where + " must be an object");
            }
            RequestAddon addon;
            addon.index = Json::ReadIndex(a_file, item, "index", where);
            addon.model = Json::CheckedPath(a_file, MeshDataPath(Json::ReadString(a_file, item, "model", where)),
                kMeshesRoot, where);
            addon.editorId = Json::ReadOptionalString(a_file, item, "editorId", where);
            addon.source = Json::ReadOptionalString(a_file, item, "source", where);
            addon.masterParticleCap = Json::ReadU16(a_file, item, "masterParticleCap", where, 0);
            addon.dnamFlags = Json::ReadU16(a_file, item, "dnamFlags", where, kDnamMasterParticleSystem);
            if (!request.addons.emplace(addon.index, addon).second) {
                Json::Fail(a_file, fmt::format("{}: index {} is listed twice (list the winning ADDN per index)", where,
                    addon.index));
            }
        }
        const auto& models = Json::RequireArray(a_file, root, "models");
        std::set<std::string> seen;
        for (std::size_t i = 0; i < models.size(); ++i) {
            const std::string where = fmt::format("models[{}]", i);
            if (!models[i].is_string()) {
                Json::Fail(a_file, where + " must be a string");
            }
            const std::string path =
                Json::CheckedPath(a_file, MeshDataPath(models[i].get<std::string>()), kMeshesRoot, where);
            if (seen.insert(path).second) {
                request.models.push_back(path);
            }
        }
        return request;
    }

    Plan PlanFromRequest(const Request& a_request, const std::vector<std::filesystem::path>& a_inputs, float a_tint)
    {
        Plan plan;
        plan.map.tint = a_tint;
        std::set<int> needed;
        for (const auto& model : a_request.models) {
            const auto source = FindSource(a_inputs, model);
            if (!source) {
                plan.skipped.push_back(model + ": in no --in folder");
                continue;
            }
            const auto nodes = ReadValueNodes(*source);
            if (nodes.empty()) {
                plan.skipped.push_back(model + ": no AddOnNodes (nothing shared to replace)");
                continue;
            }
            bool known = true;
            for (const auto& node : nodes) {
                if (a_request.addons.count(node.value) == 0) {
                    plan.problems.push_back(fmt::format("{} ({}): AddOnNode '{}' value {} has no ADDN in the request",
                        model, source->string(), node.name, node.value));
                    known = false;
                } else {
                    needed.insert(node.value);
                }
            }
            if (known) {
                plan.map.models.emplace(model, GreyPathFor(model));
            }
        }

        std::set<int> used;
        for (const auto& [index, addon] : a_request.addons) {
            used.insert(index);
        }
        int nextFree = used.empty() ? kGreyIndexBase : std::max(kGreyIndexBase, *used.rbegin() + 1);
        for (const int index : needed) {
            const RequestAddon& addon = a_request.addons.at(index);
            AddonEntry entry;
            entry.index = index;
            entry.model = addon.model;
            entry.greyModel = GreyPathFor(addon.model);
            entry.editorId = addon.editorId;
            entry.source = addon.source;
            entry.masterParticleCap = addon.masterParticleCap;
            entry.dnamFlags = addon.dnamFlags;
            Allocate(a_request, entry, used, nextFree, plan.newRecords);
            plan.map.addons.emplace(index, std::move(entry));
        }
        return plan;
    }
}
