#include "CopyRequest.h"

#include "Config.h"
#include "CopyRequestData.h"
#include "CopyRequestDump.h"
#include "FormCopies.h"
#include "NifDump.h"

#include "EncodingUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <set>

namespace BlackFlame
{
    namespace
    {
        constexpr int kRequestVersion = 3;
        constexpr int kJsonIndent = 1;
        constexpr const char* kFolderName = "HeartOfMagic_BlackFlame";
        constexpr const char* kRequestName = "request.json";
        using namespace CopyRequestDump;
        constexpr const char* kTextureListError = "error";  // T13: the manifest's texture fingerprint when the listing threw
        using CopyRequestData::Texture;
        using CopyRequestData::kMiB;

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

        constexpr std::size_t kAddonSample = 5;

        // Every model the request names: closure forms, PROJ muzzles, ADDNs not ours. Refused paths are counted apart;
        // a_formPaths gets the form and muzzle models (a NIF failure there fails T0-request, an ADDN-only one is INFO)
        // a_basePaths (H1): the form and muzzle models a base-game form names (never counted as absent)
        std::vector<std::string> ModelsToRead(const FireClosure& a_closure, std::vector<std::string>& a_refused,
            std::set<std::string>& a_formPaths, std::set<std::string>& a_basePaths)
        {
            std::set<std::string> paths;
            const auto add = [&](const std::string& a_model, bool a_base) {
                if (a_model.empty()) {
                    return;
                }
                if (const auto path = NifDump::NormalizeModelPath(a_model)) {
                    paths.insert(*path);
                    if (a_base) {
                        a_basePaths.insert(*path);
                    }
                } else {
                    a_refused.push_back(Utf8(a_model));
                }
            };
            for (const auto& form : a_closure.forms) {
                const bool base = IsBaseGameForm(form.form);
                add(ModelPathOf(form.form), base);
                if (const auto* projectile = form.form->As<RE::BGSProjectile>()) {
                    add(CopyRequestData::MuzzleModelOf(*projectile), base);
                }
            }
            a_formPaths = paths;
            if (auto* dh = RE::TESDataHandler::GetSingleton()) {
                for (const auto* addon : dh->GetFormArray<RE::BGSAddonNode>()) {
                    if (addon && !IsOwnPluginForm(addon)) {
                        add(ModelPathOf(addon), false);
                    }
                }
            }
            return { paths.begin(), paths.end() };
        }

        // H1: a form's or muzzle's NIF that does not open at all (no mod or archive has it) is counted apart as absent
        // (INFO), unless a base-game form names it; one that opened but could not be read stays a failure
        void CountFailure(const std::string& a_path, const std::set<std::string>& a_formPaths,
            const std::set<std::string>& a_basePaths, bool a_opened)
        {
            ++g_state.nifsFailed;
            if (a_formPaths.contains(a_path)) {
                if (!a_opened && !a_basePaths.contains(a_path)) {
                    ++g_state.nifsAbsentForms;
                    if (g_state.absentFormSample.size() < kAddonSample) {
                        g_state.absentFormSample.push_back(Utf8(a_path));
                    }
                    return;
                }
                ++g_state.nifsFailedForms;
                return;
            }
            ++g_state.nifsFailedAddons;
            if (g_state.failedAddonSample.size() < kAddonSample) {
                g_state.failedAddonSample.push_back(Utf8(a_path));
            }
        }

        // H1 guard: when no NIF opened at all the cause is ours (a path bug, the file system), so nothing is "absent"
        void FoldAbsentWhenNothingOpened()
        {
            if (g_state.nifsAbsentForms == 0 || g_state.nifs > g_state.nifsFailed) {
                return;
            }
            logger::warn("BlackFlame: copy request: no NIF opened at all - the {} absent form NIF(s) count as failures",
                g_state.nifsAbsentForms);
            g_state.nifsFailedForms += g_state.nifsAbsentForms;
            g_state.nifsAbsentForms = 0;
            g_state.absentFormSample.clear();
        }

        std::string NormalizedOrRaw(const std::string& a_model)
        {
            const auto path = a_model.empty() ? std::nullopt : NifDump::NormalizeModelPath(a_model);
            return Utf8(path ? *path : a_model);
        }

        // a_sizesOnly: the NIF lines without the byte hash (path, size, ok) - the cheap form C3's freshness check can make
        // by opening each stream without reading it
        std::string ComputeFingerprint(const FireClosure& a_closure, const std::vector<Nif>& a_nifs, bool a_sizesOnly)
        {
            std::vector<std::string> lines;
            for (const auto& form : a_closure.forms) {
                const auto* projectile = form.form->As<RE::BGSProjectile>();
                lines.push_back(std::format("F|{}|{}|{}|{}", CopyKindTag(form.kind), SourceText(form.form),
                    CopyRequestData::FingerprintModelOf(form.form),  // TXST: its slots and DODT presence
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
                lines.push_back(a_sizesOnly ? std::format("N|{}|{}|{}", Utf8(nif.path), nif.blob.totalSize, nif.blob.opened ? "opened" : "missing") :
                                              std::format("N|{}|{}|{:016x}|{}", Utf8(nif.path), nif.blob.size, nif.blob.fnv64,
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

        Json RequestJson(const FireClosure& a_closure, const std::vector<Nif>& a_nifs, const std::vector<Texture>* a_textures)
        {
            Json root;
            root["version"] = kRequestVersion;
            root["game"] = REL::Module::get().version().string();
            root["fingerprint"] = g_state.fingerprint;
            root["fingerprintSizes"] = g_state.sizeFingerprint;
            root["copiesPlugin"] = "HeartOfMagic_BlackFlame_Copies.esp";
            root["decalTextureSets"] = true;  // TXST forms walked (the builder refuses an older request over TXST copies)
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
            if (a_textures) {  // T1; T13: none when the listing threw, so the builder takes its no-list path
                root["textures"] = CopyRequestData::TexturesJson(*a_textures);
                root["textureCaps"] = { { "perTexture", CopyRequestData::kMaxTextureBytes }, { "total", CopyRequestData::kMaxTextureDumpBytes } };
            }
            return root;
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
        if (!GetConfig().writeRequest) {
            // J7 / C3: players' default - no NIF byte is read; each stream is only opened for its size (the size-only
            // fingerprint the freshness check compares with the map's)
            try {
                std::vector<std::string> refused;
                std::set<std::string> formPaths;
                std::set<std::string> basePaths;
                const auto probeStart = Clock::now();
                for (const auto& path : ModelsToRead(a_closure, refused, formPaths, basePaths)) {
                    nifs.push_back({ path, NifDump::ProbeModel(path), {} });
                    if (!nifs.back().blob.opened) {
                        CountFailure(path, formPaths, basePaths, false);
                    }
                }
                g_state.readMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - probeStart).count();
                g_state.nifs = nifs.size();
                FoldAbsentWhenNothingOpened();
                g_state.sizeFingerprint = ComputeFingerprint(a_closure, nifs, true);
            } catch (const std::exception& e) {
                logger::error("BlackFlame: copy request: the size fingerprint failed: {}", e.what());
            }
            logger::info("BlackFlame: copy request: off (writeRequest false): {} closure forms, {} excluded, from {} fire spells; "
                         "{} NIFs opened for their size ({} missing) in {} ms, size fingerprint {}", g_state.forms, g_state.excluded,
                a_closure.spells, g_state.nifs, g_state.nifsFailed, g_state.readMs, g_state.sizeFingerprint);
            g_state.dumpState = "off";
            return;
        }
        try {
            std::vector<std::string> refused;
            std::set<std::string> formPaths;
            std::set<std::string> basePaths;
            const auto readStart = Clock::now();
            for (const auto& path : ModelsToRead(a_closure, refused, formPaths, basePaths)) {
                Nif nif{ path, {}, {} };
                nif.blob = NifDump::ReadModel(path, &nif.bytes);
                if (!nif.blob.ok) {
                    std::vector<std::byte>().swap(nif.bytes);
                }
                if (!nif.blob.ok) {
                    CountFailure(path, formPaths, basePaths, nif.blob.opened);
                }
                nifs.push_back(std::move(nif));
            }
            g_state.readMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - readStart).count();
            g_state.nifs = nifs.size();
            FoldAbsentWhenNothingOpened();
            for (const auto& model : refused) {
                logger::warn("BlackFlame: copy request: model path '{}' refused (a drive, ':' or '..'): not read", model);
            }
            g_state.fingerprint = ComputeFingerprint(a_closure, nifs, false);
            g_state.sizeFingerprint = ComputeFingerprint(a_closure, nifs, true);
            g_state.computed = true;
        } catch (const std::exception& e) {
            logger::error("BlackFlame: copy request: the fingerprint failed: {}", e.what());
        }
        // T1: the textures the read NIFs name, opened for their size (no byte read unless the dump is written)
        std::vector<Texture> textures;
        std::string textureFingerprint;
        bool texturesListed = false;  // T13: a listing that threw leaves "textures" out of the request (the builder's old path)
        long long textureMs = 0;      // listing and opening the textures
        long long dumpMs = 0;         // the dump step (NIFs and textures written, or the unchanged check)
        try {
            const auto textureStart = Clock::now();
            std::vector<const std::vector<std::byte>*> nifBytes;
            for (const auto& nif : nifs) {
                if (nif.blob.ok) {
                    nifBytes.push_back(&nif.bytes);
                }
            }
            textures = CopyRequestData::PlanTextures(nifBytes, CopyRequestData::FormTexturePaths(a_closure));
            textureFingerprint = CopyRequestData::TextureFingerprint(textures);
            texturesListed = true;
            textureMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - textureStart).count();
        } catch (const std::exception& e) {
            textures.clear();
            textureFingerprint = kTextureListError;  // never equal to a real fingerprint: the next start tries again
            logger::error("BlackFlame: copy request: listing the NIFs' textures failed: {}", e.what());
        }
        const auto folder = RequestFolder();
        g_state.folder = folder.value_or(std::filesystem::path());
        if (g_state.computed && folder) {
            // The dump before the request: a texture the dump could not read is named in the request with its error
            try {
                std::filesystem::create_directories(*folder);
                const auto dumpStart = Clock::now();
                const auto manifest = CopyRequestData::ReadJsonObject(*folder / kManifestName);
                const auto readFailed = CopyRequestData::JsonStringSet(manifest, kTextureReadFailuresKey);
                const bool unchanged = CopyRequestData::JsonString(manifest, kFingerprintKey) == g_state.fingerprint &&
                                       CopyRequestData::JsonString(manifest, kTextureFingerprintKey) == textureFingerprint &&
                                       DumpIntact(*folder, nifs, textures, readFailed);
                g_state.dumpState = unchanged ? RetryReadFailures(*folder, textures, readFailed, g_state.fingerprint, textureFingerprint) :
                                                Dump(*folder, nifs, textures, g_state.fingerprint, textureFingerprint);
                dumpMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - dumpStart).count();
            } catch (const std::exception& e) {
                g_state.dumpState = std::format("failed: {}", e.what());
                logger::error("BlackFlame: copy request: the NIF dump failed: {}", e.what());
            }
            try {
                WriteWhole(*folder / kRequestName,
                    RequestJson(a_closure, nifs, texturesListed ? &textures : nullptr).dump(kJsonIndent) + "\n");
                g_state.requestWritten = true;
            } catch (const std::exception& e) {
                logger::error("BlackFlame: copy request: writing {} failed: {}", kRequestName, e.what());
            }
            std::size_t planned = 0;
            std::uint64_t plannedBytes = 0;
            for (const auto& texture : textures) {
                if (texture.planned) {
                    ++planned;
                    plannedBytes += texture.probe.totalSize;
                }
            }
            logger::info("BlackFlame: copy request: textures: {} named by the NIFs, {} in the dump ({} MiB), {} left out; listed "
                         "and opened in {} ms; the dump step (NIFs and textures, dump {}) {} ms", textures.size(), planned,
                plannedBytes / kMiB, textures.size() - planned, textureMs, g_state.dumpState, dumpMs);
        } else if (!folder) {
            logger::warn("BlackFlame: copy request: no SKSE log folder - no request written");
        }
        g_state.totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        std::array<int, kCopyKindCount> perKind{};
        for (const auto& form : a_closure.forms) {
            ++perKind[static_cast<std::size_t>(form.kind)];
        }
        logger::info("BlackFlame: copy request: {} forms (PROJ {}, EXPL {}, IPCT {}, HAZD {}, ARTO {}, TXST {}) from {} fire spells and {} bound "
                     "fire weapon seeds ({} bound-arrow AMMO), {} excluded, "
                     "{} NIFs ({} failed) read in {} ms, fingerprint {}, request {}, dump {}, {} ms in all; folder {}",
            g_state.forms, perKind[0], perKind[1], perKind[2], perKind[3], perKind[4], perKind[5], a_closure.spells, a_closure.boundSeeds,
            a_closure.boundArrowAmmo, g_state.excluded,
            g_state.nifs, g_state.nifsFailed, g_state.readMs, g_state.computed ? g_state.fingerprint : std::string("NOT computed"),
            g_state.requestWritten ? "written" : "NOT written",
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
