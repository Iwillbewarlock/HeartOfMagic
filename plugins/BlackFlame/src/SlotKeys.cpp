// =============================================================================
// SlotKeys - the bytes the single-launch keys are made of, read at kDataLoaded, and the keys (SlotKeys.h)
// =============================================================================

#include "SlotKeys.h"

#include "Config.h"
#include "NifDump.h"
#include "NifValueNodes.h"

#include <chrono>
#include <format>
#include <fstream>
#include <set>

namespace BlackFlame::SlotKeys
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        constexpr std::size_t kExePathChars = 32768;
        constexpr std::uint32_t kLightLocalMask = 0xFFF;
        constexpr std::uint32_t kFullLocalMask = 0xFFFFFF;

        long long MsSince(Clock::time_point a_start)
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - a_start).count();
        }

        std::string MuzzleOf(const RE::TESForm* a_form)
        {
            const auto* projectile = a_form ? a_form->As<RE::BGSProjectile>() : nullptr;
            return projectile ? CopyRequestData::MuzzleModelOf(*projectile) : std::string();
        }

        void ReadNifs(const FireClosure& a_closure, Inputs& a_out, std::set<std::string>& a_closurePaths)
        {
            std::set<std::string> paths;
            const auto add = [&](const std::string& a_model, bool a_closureNif) {
                if (a_model.empty()) {
                    return;
                }
                if (const auto path = NifDump::NormalizeModelPath(a_model)) {
                    paths.insert(*path);
                    if (a_closureNif) {
                        a_closurePaths.insert(*path);
                    }
                }
            };
            for (const auto& form : a_closure.forms) {
                add(ModelPathOf(form.form), true);
                add(MuzzleOf(form.form), true);
            }
            if (auto* dh = RE::TESDataHandler::GetSingleton()) {
                for (const auto* addon : dh->GetFormArray<RE::BGSAddonNode>()) {
                    if (addon && !IsOwnPluginForm(addon)) {
                        add(ModelPathOf(addon), false);
                    }
                }
            }
            const auto start = Clock::now();
            for (const auto& path : paths) {
                CopyRequestDump::Nif nif{ path, {}, {} };
                nif.blob = NifDump::ReadModel(path, &nif.bytes);
                if (!nif.blob.ok) {
                    std::vector<std::byte>().swap(nif.bytes);
                    ++a_out.nifsFailed;
                }
                a_out.nifAt.emplace(path, a_out.nifs.size());
                a_out.nifs.push_back(std::move(nif));
            }
            a_out.nifMs = MsSince(start);
        }

        void HashTextures(const FireClosure& a_closure, Inputs& a_out)
        {
            const auto start = Clock::now();
            std::vector<const std::vector<std::byte>*> bytes;
            for (const auto& nif : a_out.nifs) {
                if (nif.blob.ok) {
                    bytes.push_back(&nif.bytes);
                }
            }
            a_out.textures = CopyRequestData::PlanTextures(bytes, CopyRequestData::FormTexturePaths(a_closure));
            for (const auto& texture : a_out.textures) {
                CC::FileHash hash = CC::FileHash::Missing();
                if (texture.own) {
                    hash = texture.planned ? CC::FileHash::Hashed(texture.fnv64) : CC::FileHash::Missing();
                } else if (texture.planned) {
                    const auto blob = NifDump::ReadModel(texture.path, nullptr);  // the bytes the dump will hold
                    hash = blob.ok ? CC::FileHash::Hashed(blob.fnv64) : CC::FileHash::Missing();
                    a_out.textureBytesHashed += blob.ok ? blob.size : 0;
                } else if (texture.probe.opened) {
                    hash = CC::FileHash::NotDumped(texture.probe.totalSize);
                }
                a_out.textureHash.emplace(texture.path, hash);
            }
            a_out.textureMs = MsSince(start);
        }

        void ReadPrefix(Inputs& a_out)
        {
            a_out.builderExe = BuilderExePath();
            std::ifstream exe(a_out.builderExe, std::ios::binary);
            if (a_out.builderExe.empty() || !exe) {
                a_out.prefixWhy = "the builder exe does not open: no key this session";
                return;
            }
            std::uint64_t hash = CC::kFnvOffset;
            std::vector<char> chunk(1 << 16);
            while (exe.read(chunk.data(), static_cast<std::streamsize>(chunk.size())) || exe.gcount() > 0) {
                hash = CC::Fnv64(std::string_view(chunk.data(), static_cast<std::size_t>(exe.gcount())), hash);
            }
            a_out.builderExeFound = true;
            const auto& config = GetConfig();
            const auto tint = CC::QuantizedTint(config.tint);  // the key text and request.json carry one value
            const auto tintAdd = CC::QuantizedTint(config.tintAdd);
            if (!tint || !tintAdd) {
                a_out.prefixWhy = "a tint the contract refuses: no key this session";
                return;
            }
            CC::KeyPrefix prefix;
            prefix.builderExeFnv64 = hash;
            prefix.tint = *tint;
            prefix.tintAdd = *tintAdd;
            prefix.paletteMode = config.paletteGodskin ? CC::PaletteMode::kGodskin : CC::PaletteMode::kDerived;
            prefix.gradBlackFlame = TextureHash(a_out, DataPathRule::kOurPalette);
            prefix.gradBlackFlameAdd = TextureHash(a_out, DataPathRule::kOurPaletteAdd);
            if (!CC::KeyPrefixString(prefix)) {
                a_out.prefixWhy = "the key prefix was refused (a palette hash unset?): no key this session";
                return;
            }
            a_out.prefix = prefix;
        }

        void ReadAddns(Inputs& a_out, const std::set<std::string>& a_closurePaths)
        {
            auto* dh = RE::TESDataHandler::GetSingleton();
            if (!dh) {
                return;
            }
            for (const auto* addon : dh->GetFormArray<RE::BGSAddonNode>()) {
                const auto* file = addon ? addon->GetFile(0) : nullptr;
                if (!file || IsOwnPluginForm(addon)) {
                    continue;
                }
                const auto model = ModelPathOf(addon);
                a_out.addnRecords.push_back({ addon->index, std::string(file->GetFilename()),
                    addon->GetFormID() & (file->IsLight() ? kLightLocalMask : kFullLocalMask), model,
                    model.empty() ? CC::FileHash::Missing() : NifHash(a_out, model) });
            }
            for (const auto& path : a_closurePaths) {
                const auto it = a_out.nifAt.find(path);
                if (it == a_out.nifAt.end()) {
                    continue;
                }
                const auto& nif = a_out.nifs[it->second];
                const auto* bytes = reinterpret_cast<const unsigned char*>(nif.bytes.data());
                if (!nif.blob.ok || !CC::HasValueNodeBlock(std::string_view(reinterpret_cast<const char*>(bytes), nif.bytes.size()))) {
                    continue;
                }
                const auto read = NifValueNodes::Read(bytes, nif.bytes.size());
                if (!read.parsed) {
                    ++a_out.valueNodeUnparsed;
                    logger::warn("BlackFlame: slots: {} names BSValueNode but its header did not parse ({}): its ADDN indices are "
                                 "unknown (those AddOnNodes keep the original particles)", path, read.why);
                    continue;
                }
                for (const auto value : read.values) {
                    if (value >= 0) {
                        a_out.valueNodeIndices.insert(static_cast<std::uint32_t>(value));
                    }
                }
            }
        }
    }

    std::filesystem::path BuilderExePath()
    {
        HMODULE module = nullptr;
        if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&BuilderExePath), &module)) {
            return {};
        }
        std::wstring buffer(kExePathChars, L'\0');
        const auto length = ::GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) {
            return {};
        }
        buffer.resize(length);
        return std::filesystem::path(buffer).parent_path() / CC::kBuilderExeRelative;
    }

    Inputs Read(const FireClosure& a_closure)
    {
        Inputs out;
        std::set<std::string> closurePaths;
        ReadNifs(a_closure, out, closurePaths);
        HashTextures(a_closure, out);
        ReadPrefix(out);
        ReadAddns(out, closurePaths);
        const auto exe = out.builderExe.u8string();
        logger::info("BlackFlame: slots: key inputs: {} NIFs ({} not read) in {} ms; {} textures planned, {} MiB hashed in {} ms; "
                     "builder '{}' {}{}; {} ADDN records, {} AddOnNode indices named by the closure ({} NIF headers not parsed)",
            out.nifs.size(), out.nifsFailed, out.nifMs, out.textures.size(), out.textureBytesHashed >> 20, out.textureMs,
            std::string(exe.begin(), exe.end()), out.builderExeFound ? "read" : "NOT found",
            out.prefix ? std::format(", prefix {}", CC::KeyPrefixString(*out.prefix).value_or("?")) : ": " + out.prefixWhy,
            out.addnRecords.size(), out.valueNodeIndices.size(), out.valueNodeUnparsed);
        return out;
    }

    CC::FileHash NifHash(const Inputs& a_inputs, const std::string& a_recordModel)
    {
        const auto path = NifDump::NormalizeModelPath(a_recordModel);
        const auto it = path ? a_inputs.nifAt.find(*path) : a_inputs.nifAt.end();
        if (it == a_inputs.nifAt.end()) {
            return CC::FileHash::Missing();
        }
        const auto& blob = a_inputs.nifs[it->second].blob;
        return blob.ok ? CC::FileHash::Hashed(blob.fnv64) : CC::FileHash::Missing();
    }

    CC::FileHash TextureHash(const Inputs& a_inputs, std::string_view a_raw)
    {
        const auto path = NifDump::NormalizeTexturePath(a_raw);
        if (!path) {
            return CC::FileHash::Missing();
        }
        if (const auto it = a_inputs.textureHash.find(*path); it != a_inputs.textureHash.end()) {
            return it->second;
        }
        const auto blob = NifDump::ReadModel(*path, nullptr);  // not in the plan (one of ours): read it now
        return blob.ok ? CC::FileHash::Hashed(blob.fnv64) : CC::FileHash::Missing();
    }

    std::optional<std::uint64_t> AddnTableHash(const Inputs& a_inputs, const std::vector<CC::LedgerPair>& a_pairs)
    {
        return CC::AddnTableHash(a_inputs.addnRecords, a_pairs);
    }

    std::optional<Key> NifKey(const Inputs& a_inputs, CC::NifKind a_kind, const std::string& a_recordModel,
        const std::optional<std::uint64_t>& a_addnTableHash)
    {
        if (!a_inputs.prefix || a_recordModel.empty()) {
            return std::nullopt;
        }
        CC::NifKeyInput input;
        input.kind = a_kind;
        input.path = a_recordModel;
        input.nif = NifHash(a_inputs, a_recordModel);
        const auto path = NifDump::NormalizeModelPath(a_recordModel);
        const auto it = path ? a_inputs.nifAt.find(*path) : a_inputs.nifAt.end();
        if (it != a_inputs.nifAt.end() && a_inputs.nifs[it->second].blob.ok) {
            const auto& bytes = a_inputs.nifs[it->second].bytes;
            for (const auto& raw : NifDump::TexturePathsIn(bytes)) {
                input.textures.push_back({ raw, TextureHash(a_inputs, raw) });
            }
            if (CC::HasValueNodeBlock(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()))) {
                if (!a_addnTableHash) {
                    return std::nullopt;
                }
                input.addnTable = a_addnTableHash;
            }
        }
        const auto text = CC::NifKeyString(*a_inputs.prefix, input);
        return text ? std::optional(Key{ CC::KeyOf(*text), *text }) : std::nullopt;
    }

    std::optional<Key> TxstKey(const Inputs& a_inputs, const RE::BGSTextureSet& a_source)
    {
        if (!a_inputs.prefix) {
            return std::nullopt;
        }
        CC::TxstKeyInput input;
        for (const auto& texture : a_source.textures) {  // TX00..TX07, the record's bytes
            const char* text = texture.textureName.c_str();
            input.slotsRaw.emplace_back(text ? text : "");
        }
        input.tx00 = TextureHash(a_inputs, input.slotsRaw[CC::kTxstDiffuseSlot]);
        input.tx03 = TextureHash(a_inputs, input.slotsRaw[CC::kTxstGlowSlot]);
        input.dodt = a_source.decalData != nullptr;
        const auto text = CC::TxstKeyString(*a_inputs.prefix, input);
        return text ? std::optional(Key{ CC::KeyOf(*text), *text }) : std::nullopt;
    }
}
