// =============================================================================
// GreyKeyedRequest - request.json v5's key parts, the child key strings, the object outcome (GreyKeyed.h)
// =============================================================================
// Read on top of LoadFormsRequestFile (the v4 parts, and forms[].key):
//   "contractVersion"  CopyContract::kContractVersion, else refused
//   "options"          { tint, tintAdd (numbers in 0..2), paletteMode ("derived" | "godskin"), keyPrefix } - keyPrefix must be
//                      exactly the contract's KeyPrefixString of those values and its own exe / grad / gradadd fields
//   "muzzles"          [{ path, key }]
//   "addnMps"          [{ slot, path, key[, index] }]: slot an ADDN slot's local id (0xF00-0xFCF)
//   "txst"             [{ slot, key, dodt }]: matched to the TXST form with that forms[].key
// A key is 16 lower-case hex digits. Anything malformed refuses the request (the DLL wrote it; nothing is guessed).

#include "GreyKeyed.h"

#include "GreyJson.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace GreyBuild
{
    namespace
    {
        namespace CC = BlackFlame::CopyContract;

        constexpr std::size_t kPrefixFields = 7;  // format, exe, tint, tintadd, palette, grad, gradadd
        constexpr std::size_t kMaxReasons = 3;    // fallback reasons a line names (then "and N more")

        [[nodiscard]] std::uint64_t ReadKey(const std::filesystem::path& a_file, const nlohmann::json& a_item, const std::string& a_where)
        {
            const auto text = Json::ReadString(a_file, a_item, CC::Request::kKey, a_where);
            const auto key = CC::ParseHex16(text);
            if (!key) {
                Json::Fail(a_file, fmt::format("{}: key '{}' is not 16 lower-case hex digits", a_where, text));
            }
            return *key;
        }

        [[nodiscard]] double ReadTint(const std::filesystem::path& a_file, const nlohmann::json& a_options, const char* a_name)
        {
            if (!a_options.contains(a_name) || !a_options[a_name].is_number()) {
                Json::Fail(a_file, fmt::format("options.{} must be a number", a_name));
            }
            const double value = a_options[a_name].get<double>();
            if (!std::isfinite(value) || value < kMinTint || value > kMaxTint) {
                Json::Fail(a_file, fmt::format("options.{} = {} is outside {}..{}", a_name, value, kMinTint, kMaxTint));
            }
            return value;
        }

        [[nodiscard]] std::vector<std::string> SplitFields(const std::string& a_text)
        {
            std::vector<std::string> fields;
            std::string field;
            std::istringstream in(a_text);
            while (std::getline(in, field, '|')) {
                fields.push_back(field);
            }
            return fields;
        }

        [[nodiscard]] std::optional<CC::FileHash> HashFromText(const std::string& a_text)
        {
            if (a_text == CC::kSentinelNotDumped) {
                return CC::FileHash::NotDumped();
            }
            if (a_text == CC::kSentinelMissing) {
                return CC::FileHash::Missing();
            }
            const auto value = CC::ParseHex16(a_text);
            return value ? std::optional<CC::FileHash>(CC::FileHash::Hashed(*value)) : std::nullopt;
        }

        // The prefix text: the contract's own string of the options and its exe / grad / gradadd fields, nothing else
        void ReadPrefix(const std::filesystem::path& a_file, KeyedRequest& a_request)
        {
            const auto fields = SplitFields(a_request.keyPrefix);
            const auto value = [&](std::size_t a_at) {
                const auto equals = fields[a_at].find('=');
                return equals == std::string::npos ? std::string() : fields[a_at].substr(equals + 1);
            };
            if (fields.size() != kPrefixFields) {
                Json::Fail(a_file, fmt::format("options.keyPrefix '{}' does not have the contract's {} fields", a_request.keyPrefix,
                    kPrefixFields));
            }
            CC::KeyPrefix prefix;
            const auto exe = CC::ParseHex16(value(1));
            const auto grad = HashFromText(value(5));
            const auto gradAdd = HashFromText(value(6));
            if (!exe || !grad || !gradAdd) {
                Json::Fail(a_file, fmt::format("options.keyPrefix '{}': exe, grad or gradadd is not 16 hex digits or a sentinel",
                    a_request.keyPrefix));
            }
            prefix.builderExeFnv64 = *exe;
            prefix.tint = a_request.tint;
            prefix.tintAdd = a_request.tintAdd;
            prefix.paletteMode = a_request.paletteMode == PaletteMode::kGodskin ? CC::PaletteMode::kGodskin : CC::PaletteMode::kDerived;
            prefix.gradBlackFlame = *grad;
            prefix.gradBlackFlameAdd = *gradAdd;
            const auto expected = CC::KeyPrefixString(prefix);
            if (!expected || *expected != a_request.keyPrefix) {
                Json::Fail(a_file, fmt::format("options.keyPrefix '{}' is not the contract's prefix of options.tint, tintAdd and "
                    "paletteMode ('{}')", a_request.keyPrefix, expected.value_or("refused")));
            }
            a_request.exeFnv64 = *exe;
            a_request.grad = value(5);
            a_request.gradAdd = value(6);
        }

        void ReadOptions(const std::filesystem::path& a_file, const nlohmann::json& a_root, KeyedRequest& a_request)
        {
            if (!a_root.contains(CC::Request::kContractVersion) || !a_root[CC::Request::kContractVersion].is_number_unsigned() ||
                a_root[CC::Request::kContractVersion].get<std::uint64_t>() != CC::kContractVersion) {
                Json::Fail(a_file, fmt::format("'contractVersion' must be {} (this builder's copy contract)", CC::kContractVersion));
            }
            if (!a_root.contains(CC::Request::kOptions) || !a_root[CC::Request::kOptions].is_object()) {
                Json::Fail(a_file, "'options' must be an object");
            }
            const auto& options = a_root[CC::Request::kOptions];
            a_request.tint = ReadTint(a_file, options, CC::Request::kTint);
            a_request.tintAdd = ReadTint(a_file, options, CC::Request::kTintAdd);
            const auto mode = CC::PaletteModeFromText(Json::ReadString(a_file, options, CC::Request::kPaletteMode, "options"));
            if (!mode) {
                Json::Fail(a_file, "options.paletteMode must be \"derived\" or \"godskin\"");
            }
            a_request.paletteMode = *mode == CC::PaletteMode::kGodskin ? PaletteMode::kGodskin : PaletteMode::kDerived;
            a_request.keyPrefix = Json::ReadString(a_file, options, CC::Request::kKeyPrefix, "options");
            ReadPrefix(a_file, a_request);
        }

        [[nodiscard]] std::string NifPath(const std::filesystem::path& a_file, const nlohmann::json& a_item, const std::string& a_where)
        {
            return Json::CheckedPath(a_file, MeshDataPath(Json::ReadString(a_file, a_item, CC::Request::kPath, a_where)), kMeshesRoot,
                a_where);
        }

        /// Adds a_spec unless its key is there already (the same object twice is quiet; another one with that key is warned)
        void AddObject(KeyedRequest& a_request, std::map<std::uint64_t, std::size_t>& a_byKey, KeyedSpec a_spec, const std::string& a_where)
        {
            const auto [it, added] = a_byKey.emplace(a_spec.key, a_request.objects.size());
            if (added) {
                a_request.objects.push_back(std::move(a_spec));
                return;
            }
            const auto& first = a_request.objects[it->second];
            if (first.kind != a_spec.kind || first.source != a_spec.source) {
                a_request.warnings.push_back(fmt::format("{}: key {} names another object as well ({} {}); the first is built",
                    a_where, CC::Hex16(a_spec.key), CC::ProgressKindText(first.kind), first.source));
            }
        }

        // The original ADDN index an addnMps entry binds: its "index", else the one ADDN whose model is its path
        [[nodiscard]] std::optional<int> BoundIndex(const std::filesystem::path& a_file, const nlohmann::json& a_item,
            const std::string& a_path, const KeyedRequest& a_request, const std::string& a_where, std::string& a_why)
        {
            if (a_item.contains("index")) {
                return Json::ReadIndex(a_file, a_item, "index", a_where);
            }
            std::vector<int> found;
            for (const auto& [index, addon] : a_request.base.addons->addons) {
                if (addon.model == a_path) {
                    found.push_back(index);
                }
            }
            if (found.size() == 1) {
                return found.front();
            }
            a_why = fmt::format("{}: no 'index', and {} ADDN records have the model {}: its slot is bound to no AddOnNode value",
                a_where, found.size(), a_path);
            return std::nullopt;
        }

        void ReadObjects(const std::filesystem::path& a_file, const nlohmann::json& a_root, KeyedRequest& a_request)
        {
            std::map<std::uint64_t, std::size_t> byKey;
            const auto& forms = a_request.base.forms;
            for (std::size_t i = 0; i < forms.size(); ++i) {
                if (forms[i].type == CopyType::kTextureSet || forms[i].model.empty()) {
                    continue;  // a TXST is built through txst[]; a form with no model has nothing to copy
                }
                if (forms[i].key.empty()) {
                    Json::Fail(a_file, fmt::format("forms: {} {} has a model and no key", CopyTypeTag(forms[i].type), forms[i].source));
                }
                AddObject(a_request, byKey, { CC::ProgressKind::kNif, *CC::ParseHex16(forms[i].key), forms[i].model }, "forms");
            }
            const auto& muzzles = Json::RequireArray(a_file, a_root, CC::Request::kMuzzles);
            for (std::size_t i = 0; i < muzzles.size(); ++i) {
                const std::string where = fmt::format("muzzles[{}]", i);
                AddObject(a_request, byKey, { CC::ProgressKind::kNif, ReadKey(a_file, muzzles[i], where), NifPath(a_file, muzzles[i], where) },
                    where);
            }
            const auto& mps = Json::RequireArray(a_file, a_root, CC::Request::kAddnMps);
            std::set<int> slotsTaken;
            for (std::size_t i = 0; i < mps.size(); ++i) {
                const std::string where = fmt::format("addnMps[{}]", i);
                const auto slot = Json::ReadInteger(a_file, mps[i], CC::Request::kSlot, where, 0, UINT16_MAX);
                const auto grey = CC::AddnIndexOfSlot(static_cast<std::uint32_t>(slot));
                if (!grey) {
                    Json::Fail(a_file, fmt::format("{}: slot {:X} is not an ADDN slot", where, slot));
                }
                const std::string path = NifPath(a_file, mps[i], where);
                AddObject(a_request, byKey, { CC::ProgressKind::kMps, ReadKey(a_file, mps[i], where), path }, where);
                std::string why;
                const auto index = BoundIndex(a_file, mps[i], path, a_request, where, why);
                if (!index) {
                    a_request.warnings.push_back(why);
                } else if (CC::SlotOfAddnIndex(static_cast<std::uint32_t>(*index))) {
                    a_request.warnings.push_back(fmt::format("{}: ADDN index {} is inside the slot pool's range: not bound", where, *index));
                } else if (slotsTaken.count(static_cast<int>(*grey)) != 0 || a_request.addnSlots.count(*index) != 0) {
                    a_request.warnings.push_back(fmt::format("{}: ADDN index {} or slot {:X} is bound twice: the first binding kept", where,
                        *index, slot));
                } else {
                    slotsTaken.insert(static_cast<int>(*grey));
                    a_request.addnSlots.emplace(*index, static_cast<int>(*grey));
                }
            }
            const auto& txst = Json::RequireArray(a_file, a_root, CC::Request::kTxst);
            for (std::size_t i = 0; i < txst.size(); ++i) {
                const std::string where = fmt::format("txst[{}]", i);
                KeyedSpec spec{ CC::ProgressKind::kTxst, ReadKey(a_file, txst[i], where) };
                for (std::size_t f = 0; f < forms.size() && spec.form == kNoForm; ++f) {
                    if (forms[f].type == CopyType::kTextureSet && forms[f].key == CC::Hex16(spec.key)) {
                        spec.form = f;
                    }
                }
                if (spec.form == kNoForm) {
                    a_request.warnings.push_back(fmt::format("{}: no TXST form has the key {} (its line says failed)", where,
                        CC::Hex16(spec.key)));
                }
                AddObject(a_request, byKey, std::move(spec), where);
            }
        }

        void ReadNifHashes(const nlohmann::json& a_root, KeyedRequest& a_request)
        {
            if (!a_root.contains("nifs") || !a_root["nifs"].is_array()) {
                return;
            }
            for (const auto& nif : a_root["nifs"]) {
                const bool ok = nif.is_object() && nif.contains("ok") && nif["ok"].is_boolean() && nif["ok"].get<bool>() &&
                                nif.contains("path") && nif["path"].is_string() && nif.contains("fnv64") && nif["fnv64"].is_string();
                const auto fnv = ok ? CC::ParseHex16(nif["fnv64"].get<std::string>()) : std::nullopt;
                if (fnv) {
                    a_request.nifHashes[MeshDataPath(nif["path"].get<std::string>())] = *fnv;
                }
            }
        }

        [[nodiscard]] CC::TextureRole ContractRole(TextureRole a_role) noexcept
        {
            switch (a_role) {
            case TextureRole::kDiffuse: return CC::TextureRole::kDiffuse;
            case TextureRole::kGlow: return CC::TextureRole::kGlow;
            default: return CC::TextureRole::kEffectSource;
            }
        }

        // The contract's string after its own prefix (computed for a fixed prefix), put after a_prefix
        [[nodiscard]] std::optional<std::string> AfterPrefix(const std::string& a_prefix, const std::optional<std::string>& a_contract)
        {
            const auto own = CC::KeyPrefixString(CC::KeyPrefix{});
            if (!own || !a_contract || a_contract->compare(0, own->size(), *own) != 0) {
                return std::nullopt;
            }
            return a_prefix + a_contract->substr(own->size());
        }
    }

    KeyedRequest LoadKeyedRequest(const std::filesystem::path& a_file)
    {
        KeyedRequest request;
        request.base = LoadFormsRequestFile(a_file);
        if (request.base.version != kGameRequestVersionKeyed || !request.base.addons) {
            Json::Fail(a_file, fmt::format("a version {} request: --staging (key mode) reads version {} (written by a Black Flame "
                "DLL of the single launch); --from-game --out builds an older one in the dev mode", request.base.version,
                kGameRequestVersionKeyed));
        }
        const auto root = Json::ReadFile(a_file);
        ReadOptions(a_file, root, request);
        ReadObjects(a_file, root, request);
        ReadNifHashes(root, request);
        for (auto& line : request.base.warnings) {
            request.warnings.push_back(line);
        }
        return request;
    }

    std::optional<std::string> ChildTextureKeyString(const std::string& a_prefix, TextureRole a_role, const std::string& a_sourcePath,
        std::uint64_t a_sourceFnv64)
    {
        return AfterPrefix(a_prefix, CC::TextureKeyString(CC::KeyPrefix{}, ContractRole(a_role), { a_sourcePath, a_sourceFnv64 }));
    }

    std::optional<std::string> ChildPaletteKeyString(const std::string& a_prefix, bool a_additive, const std::string& a_sourcePath,
        std::uint64_t a_sourceFnv64)
    {
        return AfterPrefix(a_prefix, CC::PaletteKeyString(CC::KeyPrefix{}, a_additive ? CC::PaletteBlend::kAdd : CC::PaletteBlend::kNormal,
                                         { a_sourcePath, a_sourceFnv64 }));
    }

    void KeyedObject::AddDep(const std::string& a_path, std::uint64_t a_fnv64)
    {
        const bool known = std::any_of(deps.begin(), deps.end(), [&](const CC::ProgressDep& a_dep) { return a_dep.path == a_path; });
        if (!known) {
            CC::ProgressDep dep;
            dep.path = a_path;
            dep.fnv64 = a_fnv64;
            deps.push_back(std::move(dep));
        }
    }

    std::pair<CC::Outcome, std::string> ClassifyObject(const KeyedObject& a_object)
    {
        if (!a_object.failed.empty()) {
            return { CC::Outcome::kFailed, a_object.failed };
        }
        if (a_object.terminal) {
            return { *a_object.terminal, a_object.terminalWhy };
        }
        if (a_object.fallbacks.empty()) {
            return { CC::Outcome::kOk, std::string() };
        }
        std::string reason;
        for (std::size_t i = 0; i < a_object.fallbacks.size() && i < kMaxReasons; ++i) {
            reason += (i == 0 ? "" : "; ") + a_object.fallbacks[i];
        }
        if (a_object.fallbacks.size() > kMaxReasons) {
            reason += fmt::format(" (and {} more)", a_object.fallbacks.size() - kMaxReasons);
        }
        return { CC::Outcome::kFallback, reason };
    }
}
