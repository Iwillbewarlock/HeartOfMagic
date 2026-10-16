#include "CopyRequest.h"

#include "Config.h"
#include "CopyRequestData.h"
#include "NifDump.h"

#include "EncodingUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <set>
#include <sstream>

namespace BlackFlame
{
    namespace
    {
        constexpr int kRequestVersion = 3;
        constexpr int kJsonIndent = 1;
        constexpr const char* kFolderName = "HeartOfMagic_BlackFlame";
        constexpr const char* kRequestName = "request.json";
        constexpr const char* kManifestName = "dump-manifest.json";
        constexpr const char* kDumpRoot = "data";

        using Clock = std::chrono::steady_clock;
        using Json = nlohmann::ordered_json;

        CopyRequestState g_state;

        std::string Utf8(std::string_view a_text)
        {
            return EncodingUtils::SanitizeToUTF8(std::string(a_text));
        }

        // A path for a log line or JSON (path::string() would throw on a name outside the ANSI code page)
        std::string PathText(const std::filesystem::path& a_path)
        {
            const auto text = a_path.u8string();
            return std::string(text.begin(), text.end());
        }

        // A Data-relative path (NormalizeModelPath's result, ANSI bytes) as a path under a_root; the path has no drive,
        // no ':' and no ".." token, so it cannot leave a_root
        std::filesystem::path UnderRoot(const std::filesystem::path& a_root, const std::string& a_dataPath)
        {
            const auto utf8 = Utf8(a_dataPath);
            return a_root / std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
        }

        struct Nif
        {
            std::string path;  // Data-relative, normalised
            NifDump::ModelBlob blob;
        };

        // Every model the request names: closure forms, PROJ muzzles, ADDNs not ours. Refused paths are counted apart.
        std::vector<std::string> ModelsToRead(const FireClosure& a_closure, std::vector<std::string>& a_refused)
        {
            std::set<std::string> paths;
            const auto add = [&](const std::string& a_model) {
                if (a_model.empty()) {
                    return;
                }
                if (const auto path = NifDump::NormalizeModelPath(a_model)) {
                    paths.insert(*path);
                } else {
                    a_refused.push_back(Utf8(a_model));
                }
            };
            for (const auto& form : a_closure.forms) {
                add(ModelPathOf(form.form));
                if (const auto* projectile = form.form->As<RE::BGSProjectile>()) {
                    add(CopyRequestData::MuzzleModelOf(*projectile));
                }
            }
            if (auto* dh = RE::TESDataHandler::GetSingleton()) {
                for (const auto* addon : dh->GetFormArray<RE::BGSAddonNode>()) {
                    if (addon && !IsOwnPluginForm(addon)) {
                        add(ModelPathOf(addon));
                    }
                }
            }
            return { paths.begin(), paths.end() };
        }

        std::string NormalizedOrRaw(const std::string& a_model)
        {
            const auto path = a_model.empty() ? std::nullopt : NifDump::NormalizeModelPath(a_model);
            return Utf8(path ? *path : a_model);
        }

        std::string ComputeFingerprint(const FireClosure& a_closure, const std::vector<Nif>& a_nifs)
        {
            std::vector<std::string> lines;
            for (const auto& form : a_closure.forms) {
                const auto* projectile = form.form->As<RE::BGSProjectile>();
                lines.push_back(std::format("F|{}|{}|{}|{}", CopyKindTag(form.kind), SourceText(form.form),
                    NormalizedOrRaw(ModelPathOf(form.form)),
                    projectile ? NormalizedOrRaw(CopyRequestData::MuzzleModelOf(*projectile)) : std::string()));
            }
            if (auto* dh = RE::TESDataHandler::GetSingleton()) {
                for (const auto* addon : dh->GetFormArray<RE::BGSAddonNode>()) {
                    if (addon && !IsOwnPluginForm(addon)) {
                        lines.push_back(std::format("A|{}|{}|{}", addon->index, SourceText(addon), NormalizedOrRaw(ModelPathOf(addon))));
                    }
                }
            }
            for (const auto& nif : a_nifs) {
                lines.push_back(std::format("N|{}|{}|{:016x}|{}", Utf8(nif.path), nif.blob.size, nif.blob.fnv64,
                    nif.blob.ok ? "ok" : "failed"));
            }
            std::ranges::sort(lines);
            std::uint64_t hash = NifDump::kFnvOffset;
            for (const auto& line : lines) {
                hash = NifDump::Fnv64(line.data(), line.size(), hash);
                hash = NifDump::Fnv64("\n", 1, hash);
            }
            return std::format("fnv64:{:016x}", hash);
        }

        Json RequestJson(const FireClosure& a_closure, const std::vector<Nif>& a_nifs, const std::string& a_fingerprint)
        {
            Json root;
            root["version"] = kRequestVersion;
            root["game"] = REL::Module::get().version().string();
            root["fingerprint"] = a_fingerprint;
            root["copiesPlugin"] = "HeartOfMagic_BlackFlame_Copies.esp";
            root["forms"] = Json::array();
            std::set<std::string> impactModels;
            for (const auto& form : a_closure.forms) {
                auto* spell = RE::TESForm::LookupByID(form.viaSpell);
                Json item = CopyRequestData::FormJson(form.form, form.kind);
                item["viaSpell"] = SourceText(spell);
                item["costliest"] = form.reachedByCostliest;
                root["forms"].push_back(std::move(item));
                if (form.kind == CopyKind::kImpact || form.kind == CopyKind::kHazard) {
                    if (const auto model = ModelPathOf(form.form); !model.empty()) {
                        impactModels.insert(Utf8(model));
                    }
                }
            }
            root["excluded"] = Json::array();
            for (const auto& excluded : a_closure.excluded) {
                root["excluded"].push_back({ { "source", SourceText(excluded.form) },
                    { "type", std::string(RE::FormTypeToString(excluded.form->GetFormType())) }, { "reason", excluded.reason },
                    { "viaSpell", SourceText(RE::TESForm::LookupByID(excluded.viaSpell)) } });
            }
            root["addons"] = CopyRequestData::AddonsJson();
            root["models"] = Json(impactModels);
            root["nifs"] = Json::array();
            for (const auto& nif : a_nifs) {
                Json item{ { "path", Utf8(nif.path) }, { "size", nif.blob.size }, { "fnv64", std::format("{:016x}", nif.blob.fnv64) },
                    { "ok", nif.blob.ok } };
                if (!nif.blob.ok) {
                    item["error"] = nif.blob.error;
                }
                root["nifs"].push_back(std::move(item));
            }
            return root;
        }

        // a_text into a_file through a temporary file and a rename (no half-written request is ever read)
        void WriteWhole(const std::filesystem::path& a_file, const std::string& a_text)
        {
            auto temp = a_file;
            temp += L".tmp";
            {
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                out << a_text;
                out.close();
                if (!out) {
                    throw std::runtime_error(std::format("writing {} failed", PathText(temp)));
                }
            }
            std::filesystem::rename(temp, a_file);  // MSVC: MoveFileExW with MOVEFILE_REPLACE_EXISTING
        }

        std::string ManifestFingerprint(const std::filesystem::path& a_folder)
        {
            std::ifstream in(a_folder / kManifestName, std::ios::binary);
            if (!in) {
                return {};
            }
            std::stringstream text;
            text << in.rdbuf();
            const auto root = nlohmann::json::parse(text.str(), nullptr, false);
            return root.is_object() && root.contains("fingerprint") && root["fingerprint"].is_string() ?
                       root["fingerprint"].get<std::string>() :
                       std::string();
        }

        // Every NIF read again (bytes this time) and written under data\; a failed one's stale copy is removed
        std::string Dump(const std::filesystem::path& a_folder, const std::vector<Nif>& a_nifs, const std::string& a_fingerprint)
        {
            const auto root = a_folder / kDumpRoot;
            int written = 0;
            int failed = 0;
            for (const auto& nif : a_nifs) {
                const auto file = UnderRoot(root, nif.path);
                std::error_code ec;
                if (!nif.blob.ok) {
                    std::filesystem::remove(file, ec);
                    continue;
                }
                std::vector<std::byte> bytes;
                const auto again = NifDump::ReadModel(nif.path, &bytes);
                if (!again.ok || again.fnv64 != nif.blob.fnv64) {
                    ++failed;
                    std::filesystem::remove(file, ec);
                    logger::warn("BlackFlame: copy request: {} changed or failed between two reads ({}): not dumped",
                        Utf8(nif.path), again.ok ? "other bytes" : again.error);
                    continue;
                }
                std::filesystem::create_directories(file.parent_path(), ec);
                std::ofstream out(file, std::ios::binary | std::ios::trunc);
                out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                out.close();
                if (!out) {
                    ++failed;
                    logger::warn("BlackFlame: copy request: writing the dump of {} failed", Utf8(nif.path));
                    continue;
                }
                ++written;
            }
            if (failed == 0) {
                WriteWhole(a_folder / kManifestName, Json{ { "fingerprint", a_fingerprint } }.dump(kJsonIndent) + "\n");
            }
            return failed == 0 ? std::format("written ({} NIFs)", written) :
                                 std::format("written with {} failure(s) ({} NIFs; the manifest not updated, so the next start "
                                             "dumps again)", failed, written);
        }
    }

    std::optional<std::filesystem::path> RequestFolder()
    {
        auto dir = SKSE::log::log_directory();
        if (!dir) {
            return std::nullopt;
        }
        return *dir / kFolderName;
    }

    const CopyRequestState& GetCopyRequestState() noexcept
    {
        return g_state;
    }

    void PrepareCopyRequest(const FireClosure& a_closure)
    {
        const auto start = Clock::now();
        g_state = {};
        g_state.forms = a_closure.forms.size();
        g_state.excluded = a_closure.excluded.size();
        std::vector<Nif> nifs;
        try {
            std::vector<std::string> refused;
            const auto readStart = Clock::now();
            for (const auto& path : ModelsToRead(a_closure, refused)) {
                nifs.push_back({ path, NifDump::ReadModel(path, nullptr) });
                g_state.nifsFailed += nifs.back().blob.ok ? 0 : 1;
            }
            g_state.readMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - readStart).count();
            g_state.nifs = nifs.size();
            for (const auto& model : refused) {
                logger::warn("BlackFlame: copy request: model path '{}' refused (a drive, ':' or '..'): not read", model);
            }
            g_state.fingerprint = ComputeFingerprint(a_closure, nifs);
            g_state.computed = true;
        } catch (const std::exception& e) {
            logger::error("BlackFlame: copy request: the fingerprint failed: {}", e.what());
        }
        const auto folder = RequestFolder();
        g_state.folder = folder.value_or(std::filesystem::path());
        if (GetConfig().writeRequest && g_state.computed && folder) {
            try {
                std::filesystem::create_directories(*folder);
                WriteWhole(*folder / kRequestName, RequestJson(a_closure, nifs, g_state.fingerprint).dump(kJsonIndent) + "\n");
                g_state.requestWritten = true;
            } catch (const std::exception& e) {
                logger::error("BlackFlame: copy request: writing {} failed: {}", kRequestName, e.what());
            }
            try {
                g_state.dumpState = ManifestFingerprint(*folder) == g_state.fingerprint ? std::string("unchanged") :
                                                                                         Dump(*folder, nifs, g_state.fingerprint);
            } catch (const std::exception& e) {
                g_state.dumpState = std::format("failed: {}", e.what());
                logger::error("BlackFlame: copy request: the NIF dump failed: {}", e.what());
            }
        } else if (!folder) {
            logger::warn("BlackFlame: copy request: no SKSE log folder - no request written");
        }
        g_state.totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        std::array<int, 5> perKind{};
        for (const auto& form : a_closure.forms) {
            ++perKind[static_cast<std::size_t>(form.kind)];
        }
        logger::info("BlackFlame: copy request: {} forms (PROJ {}, EXPL {}, IPCT {}, HAZD {}, ARTO {}) from {} fire spells, {} excluded, "
                     "{} NIFs ({} failed) read in {} ms, fingerprint {}, request {}, dump {}, {} ms in all; folder {}",
            g_state.forms, perKind[0], perKind[1], perKind[2], perKind[3], perKind[4], a_closure.spells, g_state.excluded,
            g_state.nifs, g_state.nifsFailed, g_state.readMs, g_state.computed ? g_state.fingerprint : std::string("NOT computed"),
            g_state.requestWritten ? "written" : (GetConfig().writeRequest ? "NOT written" : "off (writeRequest false)"),
            g_state.dumpState, g_state.totalMs, PathText(g_state.folder));
        constexpr int kMaxFailureLines = 20;
        int lines = 0;
        for (const auto& nif : nifs) {
            if (!nif.blob.ok && ++lines <= kMaxFailureLines) {
                logger::warn("BlackFlame: copy request: NIF {} not read: {}", Utf8(nif.path), nif.blob.error);
            }
        }
    }
}
