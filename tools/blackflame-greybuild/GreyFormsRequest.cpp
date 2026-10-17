// =============================================================================
// GreyFormsRequest - the forms request (the copies map and stable ids: GreyFormsMap.cpp)
// =============================================================================
// Request version 3 (written by the DLL at kDataLoaded, plugins/BlackFlame/src/CopyRequest.h): the forms as below plus
// "muzzleModel" (PROJ), "viaSpell", "costliest"; and "fingerprint", "game", "excluded" [{ source, type, reason }],
// "addons" and "models" (the --request shape, ours marked "own"), "nifs" [{ path, size, fnv64, ok }], "textures"
// [{ path, size, ok, error, own }] (our two palettes marked own), "decalTextureSets" true (TXST forms walked;
// GreyFormsTxst.h for a TXST form's "textures" and "hasDecal").
// Request (version 2; type ARTO with "data": { "artType": 0..2 } since the ARTO spike):
//   { "version": 2,
//     "forms": [ { "source": "Skyrim.esm|10FBED", "type": "PROJ", "editorId": "MAG_FireBallProjectile01",
//                  "model": "Magic\\FireballProjectile01.nif", "bounds": [x1, y1, z1, x2, y2, z2],
//                  "data": { "projectileType": "Missile", "speed": 2500, ... } } ] }
// The copies map: GreyFormsMap.cpp.

#include "GreyForms.h"
#include "GreyFormsTxst.h"
#include "GreyJson.h"
#include "GreyTexture.h"

#include "../../plugins/BlackFlame/src/DataPathRule.h"

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
        constexpr std::size_t kBoundCount = 6;

        [[nodiscard]] CopyType ParseType(const std::filesystem::path& a_file, const std::string& a_tag,
            const std::string& a_where)
        {
            if (const auto type = CopyTypeFromTag(a_tag)) {
                return *type;
            }
            Json::Fail(a_file, fmt::format("{}: type '{}' is not PROJ, HAZD, EXPL, IPCT, ARTO or TXST", a_where, a_tag));
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

        // v3: the game's parts of the request (addons and models, fingerprint, excluded, the NIFs it could not read)
        void ReadGameParts(const std::filesystem::path& a_file, const nlohmann::json& a_root, FormsRequest& a_request)
        {
            a_request.addons = ParseRequest(a_file, a_root, &a_request.warnings);
            a_request.fingerprint = Json::ReadString(a_file, a_root, "fingerprint", "the request");
            a_request.fingerprintSizes = Json::ReadOptionalString(a_file, a_root, "fingerprintSizes", "the request");
            a_request.game = Json::ReadOptionalString(a_file, a_root, "game", "the request");
            a_request.excluded = ReadExcludedForms(a_file, a_root);
            a_request.decalTextureSets = a_root.contains("decalTextureSets") && a_root["decalTextureSets"].is_boolean() &&
                                         a_root["decalTextureSets"].get<bool>();
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
                    // reviewer 7, Codex 9: our fixed palettes listed and dumped, each on its own (ok entries too)
                    if (const auto path = TextureDataPath(Utf8ToAnsi(Json::ReadString(a_file, textures[i], "path", where)));
                        path && (*path == BlackFlame::DataPathRule::kOurPalette || *path == BlackFlame::DataPathRule::kOurPaletteAdd)) {
                        a_request.palettesListed.insert(*path);
                        if (ok) {
                            a_request.palettesDumped.insert(*path);
                        }
                    }
                    if (!ok) {
                        const auto path = TextureDataPath(Utf8ToAnsi(Json::ReadString(a_file, textures[i], "path", where)));
                        if (path) {
                            a_request.texturesNotDumped[*path] = Json::ReadOptionalString(a_file, textures[i], "error", where);
                        }
                    }
                }
            }
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
        const bool hex = !id.empty() && id.size() <= kMaxLocalIdDigits &&
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
            if (form.type == CopyType::kTextureSet) {
                if (std::string why; !ReadTextureSetParts(a_file, item, form, where, why)) {
                    if (!fromGame) {
                        Json::Fail(a_file, where + ": " + why);
                    }
                    request.warnings.push_back(why);
                    request.leftOut.insert(form.source);  // AA3: BuildForms refuses to retire a copy --out still has
                    continue;  // left out: its decal stays the source's
                }
            }
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
            form.model = TraitsOf(form.type).model ? readModel("model") : std::string();  // TXST: no MODL
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

    std::vector<ExcludedForm> ReadExcludedForms(const std::filesystem::path& a_file, const nlohmann::json& a_root)
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
}
