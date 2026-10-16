// =============================================================================
// GreyFormsRequest - the forms request, the copies map and stable copy ids
// =============================================================================
// Request version 3 (written by the DLL at kDataLoaded, plugins/BlackFlame/src/CopyRequest.h): the forms as below plus
// "muzzleModel" (PROJ), "viaSpell", "costliest"; and "fingerprint", "game", "excluded" [{ source, type, reason }],
// "addons" and "models" (the --request shape, ours marked "own"), "nifs" [{ path, size, fnv64, ok }].
// Request (version 2; type ARTO with "data": { "artType": 0..2 } since the ARTO spike):
//   { "version": 2,
//     "forms": [ { "source": "Skyrim.esm|10FBED", "type": "PROJ", "editorId": "MAG_FireBallProjectile01",
//                  "model": "Magic\\FireballProjectile01.nif", "bounds": [x1, y1, z1, x2, y2, z2],
//                  "data": { "projectileType": "Missile", "speed": 2500, ... } } ] }
// Copies map (version 2, written by the builder, read back on the next run for stable ids; version 1 is still read):
//   { "version": 2, "requestFingerprint": "fnv64:..", "copiesPlugin": "HeartOfMagic_BlackFlame_Copies.esp",
//     "copies": [ ... as below, plus "sourceMuzzleModel", "greyMuzzleModel", "note" ], "retired": [...], "excluded": [...] }
// version 1: { "version": 1, "plugin": "HeartOfMagic_BlackFlame_Copies.esp",
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

        std::vector<ExcludedForm> ReadExcluded(const std::filesystem::path& a_file, const nlohmann::json& a_root)
        {
            std::vector<ExcludedForm> out;
            if (!a_root.contains("excluded")) {
                return out;
            }
            const auto& list = Json::RequireArray(a_file, a_root, "excluded");
            for (std::size_t i = 0; i < list.size(); ++i) {
                const std::string where = fmt::format("excluded[{}]", i);
                out.push_back({ Json::ReadOptionalString(a_file, list[i], "source", where),
                    Json::ReadOptionalString(a_file, list[i], "type", where),
                    Json::ReadOptionalString(a_file, list[i], "reason", where) });
            }
            return out;
        }

        // v3: the game's parts of the request (addons and models, fingerprint, excluded, the NIFs it could not read)
        void ReadGameParts(const std::filesystem::path& a_file, const nlohmann::json& a_root, FormsRequest& a_request)
        {
            a_request.addons = ParseRequest(a_file, a_root, &a_request.warnings);
            a_request.fingerprint = Json::ReadString(a_file, a_root, "fingerprint", "the request");
            a_request.fingerprintSizes = Json::ReadOptionalString(a_file, a_root, "fingerprintSizes", "the request");
            a_request.game = Json::ReadOptionalString(a_file, a_root, "game", "the request");
            a_request.excluded = ReadExcluded(a_file, a_root);
            if (a_root.contains("nifs")) {
                const auto& nifs = Json::RequireArray(a_file, a_root, "nifs");
                for (std::size_t i = 0; i < nifs.size(); ++i) {
                    const std::string where = fmt::format("nifs[{}]", i);
                    const bool ok = nifs[i].is_object() && nifs[i].contains("ok") && nifs[i]["ok"].is_boolean() &&
                                    nifs[i]["ok"].get<bool>();
                    if (!ok) {
                        a_request.failedNifs.push_back(NormalizeDataPath(Json::ReadString(a_file, nifs[i], "path", where)));
                    }
                }
            }
            // T1: the textures the game dumped next to the NIFs ([{ path, size, ok, error }]); one left out keeps its reason
            if (a_root.contains("textures")) {
                a_request.hasTextures = true;
                const auto& textures = Json::RequireArray(a_file, a_root, "textures");
                for (std::size_t i = 0; i < textures.size(); ++i) {
                    const std::string where = fmt::format("textures[{}]", i);
                    const bool ok = textures[i].is_object() && textures[i].contains("ok") && textures[i]["ok"].is_boolean() &&
                                    textures[i]["ok"].get<bool>();
                    if (!ok) {
                        const auto path = TextureDataPath(Json::ReadString(a_file, textures[i], "path", where));
                        if (path) {
                            a_request.texturesNotDumped[*path] = Json::ReadOptionalString(a_file, textures[i], "error", where);
                        }
                    }
                }
            }
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
            const bool hex = !a_text.empty() && a_text.size() <= kMaxIdDigits &&
                             a_text.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
            const auto id = hex ? static_cast<std::uint32_t>(std::stoul(a_text, nullptr, kHexBase)) : 0u;  // 6 hex digits
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
        return LoadFormsRequestFile(a_file).forms;
    }

    FormsRequest LoadFormsRequestFile(const std::filesystem::path& a_file)
    {
        const nlohmann::json root = Json::ReadFile(a_file);
        FormsRequest request;
        if (!root.contains("version") || (root["version"] != kFormsRequestVersion && root["version"] != kGameRequestVersion)) {
            Json::Fail(a_file, fmt::format("'version' must be {} (a hand-made forms request) or {} (the request the game "
                "writes)", kFormsRequestVersion, kGameRequestVersion));
        }
        request.version = root["version"].get<int>();
        const bool fromGame = request.version == kGameRequestVersion;
        const auto& forms = Json::RequireArray(a_file, root, "forms");
        std::vector<FormSource>& out = request.forms;
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
            const auto readModel = [&](const char* a_key) {
                const std::string model = Json::ReadOptionalString(a_file, item, a_key, where);
                if (model.empty()) {
                    return std::string();
                }
                try {
                    return Json::CheckedPath(a_file, MeshDataPath(model), kMeshesRoot, where);
                } catch (const std::runtime_error& e) {
                    if (!fromGame) {
                        throw;
                    }
                    request.warnings.push_back(fmt::format("{} {}: {} - no grey copy of it", form.source, a_key, e.what()));
                    return std::string();
                }
            };
            form.model = readModel("model");
            if (form.type == CopyType::kProjectile) {
                form.muzzleModel = readModel("muzzleModel");
            }
            if (item.contains("bounds") && !CopyHasBounds(form.type)) {
                Json::Fail(a_file, fmt::format("{}: a {} record has no object bounds", where, CopyTypeTag(form.type)));
            }
            form.bounds = ReadBounds(a_file, item, where);
            form.data = ReadData(a_file, item, form.type, where);
            out.push_back(std::move(form));
        }
        if (out.empty()) {
            Json::Fail(a_file, fromGame ? "'forms' is empty: the game found no fire spell forms to copy" : "'forms' is empty");
        }
        if (fromGame) {
            ReadGameParts(a_file, root, request);
        }
        return request;
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
        map.excluded = ReadExcluded(a_file, root);
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
        root["_comment"] = { "Generated by blackflame-greybuild (plugins/BlackFlame/README.md, \"Black Flame v2\").",
            "Source form -> its copy in the copies plugin. Read back by the next run: ids stay stable, retired ids are "
            "never given to another source (unless --reclaim-retired). requestFingerprint: the DLL compares it with the "
            "load order at every start." };
        root["requestFingerprint"] = a_map.requestFingerprint;
        root["requestFingerprintSizes"] = a_map.requestFingerprintSizes;
        root["copiesPlugin"] = kCopiesPluginName;
        root["copies"] = nlohmann::ordered_json::array();
        for (const auto& entry : a_map.entries) {
            root["copies"].push_back({ { "source", entry.source }, { "type", CopyTypeTag(entry.type) },
                { "copy", fmt::format("{:06X}", entry.localId) }, { "editorId", entry.editorId },
                { "sourceEditorId", entry.sourceEditorId }, { "sourceModel", entry.sourceModel },
                { "greyModel", entry.greyModel }, { "sourceMuzzleModel", entry.sourceMuzzleModel },
                { "greyMuzzleModel", entry.greyMuzzleModel }, { "note", entry.note } });
        }
        root["retired"] = nlohmann::ordered_json::array();
        for (const auto& [source, id] : a_map.retired) {
            root["retired"].push_back({ { "source", source }, { "copy", fmt::format("{:06X}", id) } });
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
            requested.insert(NormalizeSource(form.source));
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
            out.entries.push_back(std::move(entry));
        }
        for (const auto& [source, id] : known) {
            if (requested.count(source) == 0 && reclaimed.count(source) == 0) {
                out.retired[source] = id;
            }
        }
        return out;
    }
}
