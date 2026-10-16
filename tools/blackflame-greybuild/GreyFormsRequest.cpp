// =============================================================================
// GreyFormsRequest - the forms request, the copies map and stable copy ids
// =============================================================================
// Request (version 2; type ARTO with "data": { "artType": 0..2 } since the ARTO spike):
//   { "version": 2,
//     "forms": [ { "source": "Skyrim.esm|10FBED", "type": "PROJ", "editorId": "MAG_FireBallProjectile01",
//                  "model": "Magic\\FireballProjectile01.nif", "bounds": [x1, y1, z1, x2, y2, z2],
//                  "data": { "projectileType": "Missile", "speed": 2500, ... } } ] }
// Copies map (version 1, written by the builder, read back on the next run for stable ids):
//   { "version": 1, "plugin": "HeartOfMagic_BlackFlame_Copies.esp",
//     "copies": [ { "source": "skyrim.esm|10FBED", "type": "PROJ", "copy": "000800", "editorId": ...,
//                   "sourceEditorId": ..., "sourceModel": ..., "greyModel": ... } ],
//     "retired": [ { "source": ..., "copy": "000805" } ] }

#include "GreyForms.h"
#include "GreyJson.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <fstream>
#include <set>

namespace GreyBuild
{
    namespace
    {
        constexpr std::uint32_t kMaxLocalId = 0xFFFFFF;
        constexpr std::size_t kMaxIdDigits = 6;
        constexpr int kJsonIndent = 2;
        constexpr int kHexBase = 16;
        constexpr std::size_t kBoundCount = 6;

        [[nodiscard]] CopyType ParseType(const std::filesystem::path& a_file, const std::string& a_tag,
            const std::string& a_where)
        {
            for (const auto type : kCopyTypes) {
                if (a_tag == CopyTypeTag(type)) {
                    return type;
                }
            }
            Json::Fail(a_file, fmt::format("{}: type '{}' is not PROJ, HAZD, EXPL, IPCT or ARTO", a_where, a_tag));
        }

        // projectileType: a name (Missile, Lobber, Beam, Flame, Cone, Barrier, Arrow) or the raw u16 bit
        [[nodiscard]] double ProjectileTypeValue(const std::filesystem::path& a_file, const nlohmann::json& a_value,
            const std::string& a_where)
        {
            static const std::map<std::string, int> kTypes{ { "Missile", 0x1 }, { "Lobber", 0x2 }, { "Beam", 0x4 },
                { "Flame", 0x8 }, { "Cone", 0x10 }, { "Barrier", 0x20 }, { "Arrow", 0x40 } };
            if (a_value.is_string()) {
                const auto it = kTypes.find(a_value.get<std::string>());
                if (it == kTypes.end()) {
                    Json::Fail(a_file, fmt::format("{}: projectileType '{}' is not a projectile type name", a_where,
                        a_value.get<std::string>()));
                }
                return it->second;
            }
            if (!a_value.is_number_integer()) {
                Json::Fail(a_file, a_where + ": projectileType must be a name or an integer");
            }
            return a_value.get<double>();
        }

        [[nodiscard]] std::map<std::string, double> ReadData(const std::filesystem::path& a_file,
            const nlohmann::json& a_item, CopyType a_type, const std::string& a_where)
        {
            std::map<std::string, double> data;
            if (!a_item.contains("data")) {
                return data;
            }
            const auto& object = a_item["data"];
            if (!object.is_object()) {
                Json::Fail(a_file, a_where + ": 'data' must be an object");
            }
            const auto keys = CopyFieldKeys(a_type);
            for (const auto& [key, value] : object.items()) {
                if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
                    Json::Fail(a_file, fmt::format("{}: data key '{}' is not a {} field", a_where, key, CopyTypeTag(a_type)));
                }
                if (key == "projectileType") {
                    data[key] = ProjectileTypeValue(a_file, value, a_where);
                } else if (value.is_number()) {
                    data[key] = value.get<double>();
                } else {
                    Json::Fail(a_file, fmt::format("{}: data '{}' must be a number", a_where, key));
                }
            }
            return data;
        }

        [[nodiscard]] std::array<std::int16_t, 6> ReadBounds(const std::filesystem::path& a_file,
            const nlohmann::json& a_item, const std::string& a_where)
        {
            std::array<std::int16_t, 6> bounds{};
            if (!a_item.contains("bounds")) {
                return bounds;
            }
            const auto& list = a_item["bounds"];
            if (!list.is_array() || list.size() != kBoundCount) {
                Json::Fail(a_file, a_where + ": 'bounds' must be 6 integers");
            }
            for (std::size_t i = 0; i < kBoundCount; ++i) {
                if (!list[i].is_number_integer() || list[i].get<long long>() < SHRT_MIN ||
                    list[i].get<long long>() > SHRT_MAX) {
                    Json::Fail(a_file, a_where + ": 'bounds' must be 6 integers in -32768..32767");
                }
                bounds[i] = static_cast<std::int16_t>(list[i].get<int>());
            }
            return bounds;
        }

        [[nodiscard]] std::uint32_t ParseCopyId(const std::filesystem::path& a_file, const std::string& a_text,
            const std::string& a_where)
        {
            const bool hex = !a_text.empty() && a_text.size() <= kMaxIdDigits &&
                             a_text.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
            const auto id = hex ? static_cast<std::uint32_t>(std::stoul(a_text, nullptr, kHexBase)) : 0u;
            if (!hex || id < kFirstLightFormId || id > kLastLightFormId) {
                Json::Fail(a_file, fmt::format("{}: copy id '{}' is not hex in {:03X}..{:03X}", a_where, a_text,
                    kFirstLightFormId, kLastLightFormId));
            }
            return id;
        }
    }

    std::string NormalizeSource(const std::string& a_source)
    {
        const auto bar = a_source.find('|');
        if (bar == std::string::npos || bar == 0) {
            throw std::runtime_error(fmt::format("source '{}' is not \"Plugin|localId\"", a_source));
        }
        std::string plugin = a_source.substr(0, bar);
        std::string id = a_source.substr(bar + 1);
        if (id.rfind("0x", 0) == 0 || id.rfind("0X", 0) == 0) {
            id.erase(0, 2);
        }
        const bool hex = !id.empty() && id.size() <= kMaxIdDigits &&
                         id.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
        if (!hex || std::stoul(id, nullptr, kHexBase) > kMaxLocalId) {
            throw std::runtime_error(fmt::format("source '{}': the local id must be up to 6 hex digits", a_source));
        }
        std::transform(plugin.begin(), plugin.end(), plugin.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return fmt::format("{}|{:06X}", plugin, std::stoul(id, nullptr, kHexBase));
    }

    std::string CopyModelFor(const std::string& a_meshDataPath)
    {
        const std::string path = MeshDataPath(a_meshDataPath);
        return std::string(kCopyRoot) + path.substr(std::char_traits<char>::length(kMeshesRoot));
    }

    std::vector<FormSource> LoadFormsRequest(const std::filesystem::path& a_file)
    {
        const nlohmann::json root = Json::ReadFile(a_file);
        Json::RequireVersion(a_file, root, kFormsRequestVersion);
        const auto& forms = Json::RequireArray(a_file, root, "forms");
        std::vector<FormSource> out;
        std::set<std::string> seen;
        for (std::size_t i = 0; i < forms.size(); ++i) {
            const std::string where = fmt::format("forms[{}]", i);
            const auto& item = forms[i];
            if (!item.is_object()) {
                Json::Fail(a_file, where + " must be an object");
            }
            FormSource form;
            try {
                form.source = NormalizeSource(Json::ReadString(a_file, item, "source", where));
            } catch (const std::invalid_argument&) {
                Json::Fail(a_file, where + ": bad source");
            } catch (const std::out_of_range&) {
                Json::Fail(a_file, where + ": bad source");
            }
            if (!seen.insert(form.source).second) {
                Json::Fail(a_file, fmt::format("{}: source {} is listed twice", where, form.source));
            }
            form.type = ParseType(a_file, Json::ReadString(a_file, item, "type", where), where);
            form.editorId = Json::ReadOptionalString(a_file, item, "editorId", where);
            const std::string model = Json::ReadOptionalString(a_file, item, "model", where);
            if (!model.empty()) {
                form.model = Json::CheckedPath(a_file, MeshDataPath(model), kMeshesRoot, where);
            }
            if (item.contains("bounds") && !CopyHasBounds(form.type)) {
                Json::Fail(a_file, fmt::format("{}: a {} record has no object bounds", where, CopyTypeTag(form.type)));
            }
            form.bounds = ReadBounds(a_file, item, where);
            form.data = ReadData(a_file, item, form.type, where);
            out.push_back(std::move(form));
        }
        if (out.empty()) {
            Json::Fail(a_file, "'forms' is empty");
        }
        return out;
    }

    CopiesMap LoadCopiesMap(const std::filesystem::path& a_file)
    {
        const nlohmann::json root = Json::ReadFile(a_file);
        Json::RequireVersion(a_file, root, kCopiesMapVersion);
        CopiesMap map;
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
            }
        }
        return map;
    }

    void WriteCopiesMap(const CopiesMap& a_map, const std::filesystem::path& a_file)
    {
        nlohmann::ordered_json root;
        root["version"] = kCopiesMapVersion;
        root["_comment"] = { "Generated by blackflame-greybuild --forms (plugins/BlackFlame/README.md, \"Black Flame v2\").",
            "Source form -> its copy in the copies plugin. Read back by the next run: ids stay stable, retired ids are "
            "never given to another source." };
        root["plugin"] = kCopiesPluginName;
        root["copies"] = nlohmann::ordered_json::array();
        for (const auto& entry : a_map.entries) {
            root["copies"].push_back({ { "source", entry.source }, { "type", CopyTypeTag(entry.type) },
                { "copy", fmt::format("{:06X}", entry.localId) }, { "editorId", entry.editorId },
                { "sourceEditorId", entry.sourceEditorId }, { "sourceModel", entry.sourceModel },
                { "greyModel", entry.greyModel } });
        }
        root["retired"] = nlohmann::ordered_json::array();
        for (const auto& [source, id] : a_map.retired) {
            root["retired"].push_back({ { "source", source }, { "copy", fmt::format("{:06X}", id) } });
        }
        std::filesystem::create_directories(a_file.parent_path());
        std::ofstream out(a_file, std::ios::binary | std::ios::trunc);
        out << root.dump(kJsonIndent) << "\n";
        out.close();
        if (!out) {
            throw std::runtime_error(fmt::format("writing '{}' failed", a_file.string()));
        }
    }

    CopiesMap AssignCopyIds(const std::vector<FormSource>& a_sources, const CopiesMap& a_previous)
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
            } else {
                if (highest >= kLastLightFormId) {
                    throw std::runtime_error(fmt::format("no copy id left above {:03X} for {} (an ESL holds 0x800..0xFFF)",
                        highest, source));
                }
                entry.localId = ++highest;
            }
            entry.editorId = fmt::format("{}{}_{}", kCopyEditorIdPrefix, form.editorId.empty() ? "Form" : form.editorId,
                source.substr(source.find('|') + 1));
            entry.sourceEditorId = form.editorId;
            entry.sourceModel = form.model;
            entry.greyModel = form.model.empty() ? std::string() : CopyModelFor(form.model);
            out.entries.push_back(std::move(entry));
        }
        for (const auto& [source, id] : known) {
            if (requested.count(source) == 0) {
                out.retired[source] = id;
            }
        }
        return out;
    }
}
