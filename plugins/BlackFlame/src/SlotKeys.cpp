// =============================================================================
// SlotKeys - the bytes the single-launch keys are made of, read at kDataLoaded, and the keys (SlotKeys.h)
// =============================================================================

#include "SlotKeys.h"

#include "Config.h"
#include "CopyContractIdentity.h"
#include "CopyRequestV5.h"
#include "CorpsePalettes.h"
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

        // Contract 6: the palettes the effect shaders use (the SRE takes NAM8 / NAM9 only with these flags: design v2 C10, C11).
        // Contract 8: the base textures whose RGB those shaders draw (EfshBaseRoles: ICON fill, ICO2 particle base)
        void ReadEfshPalettes(Inputs& a_out)
        {
            auto* dh = RE::TESDataHandler::GetSingleton();
            if (!dh) {
                return;
            }
            std::vector<CC::EfshPaletteSource> sources;
            std::vector<CC::EfshPaletteSource> bases;
            for (const auto* shader : dh->GetFormArray<RE::TESEffectShader>()) {
                if (!shader || IsOwnPluginForm(shader)) {
                    continue;
                }
                ++a_out.efshShaders;
                const auto text = [](const RE::TESTexture& a_texture) {
                    const char* name = a_texture.textureName.c_str();
                    return std::string(name ? name : "");
                };
                const auto flags = CorpsePalettes::FlagsOf(shader->data);
                const auto roles = CC::EfshPaletteRoles(flags);
                if (CC::EfshRoleHas(roles, CC::EfshRole::kParticle)) {
                    sources.push_back({ text(shader->particlePaletteTexture), CC::EfshRole::kParticle });
                }
                if (CC::EfshRoleHas(roles, CC::EfshRole::kMembrane)) {
                    sources.push_back({ text(shader->membranePaletteTexture), CC::EfshRole::kMembrane });
                }
                const auto baseRoles = CC::EfshBaseRoles(flags);
                if (CC::EfshRoleHas(baseRoles, CC::EfshRole::kParticle)) {
                    bases.push_back({ text(shader->particleShaderTexture), CC::EfshRole::kParticle });
                }
                if (CC::EfshRoleHas(baseRoles, CC::EfshRole::kMembrane)) {
                    bases.push_back({ text(shader->fillTexture), CC::EfshRole::kMembrane });
                }
            }
            a_out.efsh = CC::MergeEfshPalettes(sources);
            a_out.efshBase = CC::MergeEfshPalettes(bases);
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
            std::vector<std::string> palettes;
            for (const auto& palette : a_out.efsh.palettes) {
                palettes.push_back(palette.path);
            }
            // contract 8: the bases the last request keyed first in their tier (CopyRequestData.h PlanTextures: stable eviction)
            const auto lastKeyed = CopyRequestV5::LastKeyedBases();
            std::vector<std::string> kept;
            std::vector<std::string> fresh;
            std::set<std::string> basePaths;
            for (const auto& base : a_out.efshBase.palettes) {
                (lastKeyed.contains(base.path) ? kept : fresh).push_back(base.path);
                basePaths.insert(base.path);
            }
            a_out.efshBaseKept = kept.size();
            a_out.textures = CopyRequestData::PlanTextures(bytes, CopyRequestData::FormTexturePaths(a_closure), palettes, kept, fresh);
            Clock::duration baseRead{};
            for (const auto& texture : a_out.textures) {
                CC::FileHash hash = CC::FileHash::Missing();
                if (texture.own && !texture.planned) {
                    continue;  // review agy 15 #3: not in the map, so TextureHash reads our own file itself (never "missing")
                }
                if (texture.own) {
                    hash = CC::FileHash::Hashed(texture.fnv64);
                } else if (texture.planned) {
                    const auto read = Clock::now();
                    const auto blob = NifDump::ReadModel(texture.path, nullptr);  // the bytes the dump will hold
                    hash = blob.ok ? CC::FileHash::Hashed(blob.fnv64) : CC::FileHash::Missing();
                    a_out.textureBytesHashed += blob.ok ? blob.size : 0;
                    baseRead += basePaths.contains(texture.path) ? Clock::now() - read : Clock::duration::zero();
                } else if (texture.probe.opened) {
                    hash = CC::FileHash::NotDumped(texture.probe.totalSize);
                }
                a_out.textureHash.emplace(texture.path, hash);
            }
            a_out.baseTextureMs = std::chrono::duration_cast<std::chrono::milliseconds>(baseRead).count();
            a_out.textureMs = MsSince(start);
        }

        // The builder's output identity (its RCDATA resource, CopyContractIdentity.h), checked against this DLL's
        // kBuilderOutputVersion; false with prefixWhy and builderIdentityWhy set when it does not read, is malformed or is of
        // another version (builderKind tells the two apart for the player's line). A dev build is taken, with a warning
        bool ReadBuilderIdentity(Inputs& a_out)
        {
            const auto resource = CC::ReadIdentityResource(a_out.builderExe);
            const auto fault = resource.text ? CC::BuilderIdentityFault(*resource.text) : std::optional<std::string>(resource.why);
            a_out.builderKind = BuilderControl::ClassifyIdentity(true, resource.text, resource.noResource);
            if (fault || !BuilderControl::IdentityTaken(a_out.builderKind)) {
                a_out.builderIdentityWhy = fault.value_or("has an output identity this DLL does not take");
                a_out.prefixWhy = "the builder exe " + a_out.builderIdentityWhy + ": no key this session";
                return false;
            }
            a_out.builderIdentity = *resource.text;
            if (a_out.builderKind == BuilderControl::BuilderIdentityKind::kDev) {
                logger::warn("BlackFlame: slots: the builder is a dev build (output identity {}): its copies use dev keys",
                    a_out.builderIdentity);
            }
            return true;
        }

        void ReadPrefix(Inputs& a_out)
        {
            a_out.builderExe = BuilderExePath();
            if (a_out.builderExe.empty() || !std::ifstream(a_out.builderExe, std::ios::binary)) {
                a_out.prefixWhy = "the builder exe does not open: no key this session";
                return;
            }
            a_out.builderExeFound = true;
            if (!ReadBuilderIdentity(a_out)) {
                return;
            }
            const auto& config = GetConfig();
            const auto tint = CC::QuantizedTint(config.tint);  // the key text and request.json carry one value
            const auto tintAdd = CC::QuantizedTint(config.tintAdd);
            if (!tint || !tintAdd) {
                a_out.prefixWhy = "a tint the contract refuses: no key this session";
                return;
            }
            CC::KeyPrefix prefix;
            prefix.outputIdentity = a_out.builderIdentity;  // verbatim: the builder rebuilds the prefix from its compiled copy
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
            // The closure's NIFs, then (review: the transitive part) the MPS NIFs of the ADDN records they name, until no new
            // index appears: an MPS naming another ADDN gets that one a slot too
            std::vector<std::string> queue(a_closurePaths.begin(), a_closurePaths.end());
            std::set<std::string> scanned;
            const auto& nodes = dh->addonNodes;
            while (!queue.empty()) {
                const auto path = std::move(queue.back());
                queue.pop_back();
                const auto it = scanned.insert(path).second ? a_out.nifAt.find(path) : a_out.nifAt.end();
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
                    if (value < 0 || !a_out.valueNodeIndices.insert(static_cast<std::uint32_t>(value)).second) {
                        continue;
                    }
                    const auto index = static_cast<std::uint32_t>(value);
                    const auto* addon = index < nodes.free_idx() ? nodes[static_cast<std::uint16_t>(index)] : nullptr;
                    if (const auto model = addon ? NifDump::NormalizeModelPath(ModelPathOf(addon)) : std::nullopt) {
                        queue.push_back(*model);
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
        ReadEfshPalettes(out);
        HashTextures(a_closure, out);
        ReadPrefix(out);
        ReadAddns(out, closurePaths);
        std::unordered_map<std::string_view, const CopyRequestData::Texture*> planned;
        for (const auto& texture : out.textures) {
            planned.emplace(texture.path, &texture);
        }
        struct EfshCounts
        {
            std::size_t hashed = 0;
            std::size_t unread = 0;
            std::size_t notDumped = 0;
            std::uint64_t bytes = 0;  // the hashed ones' sizes (read whole above)
        };
        const auto hashAll = [&](std::vector<CC::EfshPalette>& a_entries) {
            EfshCounts counts;
            for (auto& entry : a_entries) {
                entry.hash = TextureHash(out, entry.path);  // in the plan: its state there
                counts.hashed += entry.hash.state == CC::FileState::kHashed ? 1 : 0;
                counts.notDumped += entry.hash.state == CC::FileState::kNotDumped ? 1 : 0;
                // review round 1: planned but not read is "missing" in the array (no key) yet "unread" in textures[]: counted apart
                const auto it = planned.find(entry.path);
                counts.unread += it != planned.end() && TextureStateOf(out, *it->second) == CC::Request::TextureState::kUnread ? 1 : 0;
                counts.bytes += it != planned.end() && it->second->planned ? it->second->probe.totalSize : 0;
            }
            return counts;
        };
        const auto palettes = hashAll(out.efsh.palettes);
        const auto bases = hashAll(out.efshBase.palettes);
        if (out.prefix) {
            CC::KeyEfshPalettes(out.efsh.palettes, *out.prefix);
            CC::KeyEfshBases(out.efshBase.palettes, *out.prefix);
        }
        logger::info("BlackFlame: slots: effect shader palettes: {} shaders, {} palettes ({} in the dump{}; {} planned but not read: "
                     "missing here, unread in textures[]); left out: {} paths refused, {} not plain ASCII, {} of ours",
            out.efshShaders, out.efsh.palettes.size(), palettes.hashed, out.prefix ? ", keyed" : ", no key this session", palettes.unread,
            out.efsh.refused, out.efsh.nonAscii, out.efsh.own);
        logger::info("BlackFlame: slots: effect shader base textures (contract 8: non-palette membrane fill and particle base): {} "
                     "paths ({} keyed by the last request, planned first), {} in the dump, {} MiB read and hashed in {} ms{}; {} "
                     "outside the dump caps ({} MiB for bases); {} planned but not read; left out: {} paths refused, {} not plain "
                     "ASCII, {} of ours",
            out.efshBase.palettes.size(), out.efshBaseKept, bases.hashed, bases.bytes >> 20, out.baseTextureMs,
            out.prefix ? ", keyed" : ", no key this session", bases.notDumped, CopyRequestData::kMaxBaseDumpBytes >> 20, bases.unread,
            out.efshBase.refused, out.efshBase.nonAscii, out.efshBase.own);
        const auto exe = out.builderExe.u8string();
        logger::info("BlackFlame: slots: key inputs: {} NIFs ({} not read) in {} ms; {} textures planned, {} MiB hashed in {} ms; "
                     "builder '{}' {}{}; {} ADDN records, {} AddOnNode indices named by the closure ({} NIF headers not parsed)",
            out.nifs.size(), out.nifsFailed, out.nifMs, out.textures.size(), out.textureBytesHashed >> 20, out.textureMs,
            std::string(exe.begin(), exe.end()),
            !out.builderExeFound          ? std::string("NOT found") :
            !out.builderIdentityWhy.empty() ? "found, but it " + out.builderIdentityWhy :
                                              std::format("found, output identity {} (this DLL's output version {})",
                                                  out.builderIdentity, CC::kBuilderOutputVersion),
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

    CC::Request::TextureState TextureStateOf(const Inputs& a_inputs, const CopyRequestData::Texture& a_texture)
    {
        using State = CC::Request::TextureState;
        if (a_texture.planned) {
            const auto it = a_inputs.textureHash.find(a_texture.path);
            const bool hashed = it != a_inputs.textureHash.end() && it->second.state == CC::FileState::kHashed;
            return hashed && !a_texture.readFailed ? State::kOk : State::kUnread;
        }
        return a_texture.probe.opened ? State::kNotDumped : State::kMissing;
    }

    std::optional<std::uint64_t> AddnTableHash(const Inputs& a_inputs, const std::vector<CC::LedgerPair>& a_pairs)
    {
        return CC::AddnTableHash(a_inputs.addnRecords, a_pairs);
    }

    std::optional<Key> NifKey(const Inputs& a_inputs, CC::NifKind a_kind, const std::string& a_recordModel,
        const std::optional<std::uint64_t>& a_addnTableHash, std::optional<double> a_tintAddHands,
        double a_handMarbleOpacity, double a_handMarbleScale)
    {
        if (!a_inputs.prefix || a_recordModel.empty()) {
            return std::nullopt;
        }
        CC::NifKeyInput input;
        input.kind = a_kind;
        input.tintAddHands = a_tintAddHands;
        input.handMarbleOpacity = a_handMarbleOpacity;
        input.handMarbleScale = a_handMarbleScale;
        input.path = a_recordModel;
        input.nif = NifHash(a_inputs, a_recordModel);
        const auto path = NifDump::NormalizeModelPath(a_recordModel);
        const auto it = path ? a_inputs.nifAt.find(*path) : a_inputs.nifAt.end();
        if (it != a_inputs.nifAt.end() && !a_inputs.nifs[it->second].blob.ok) {
            return std::nullopt;  // review agy 15 #2: a NIF that is there but was not read is no "missing" NIF (and no key)
        }
        if (it != a_inputs.nifAt.end()) {
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

    CC::TxstKeyInput TxstInput(const Inputs& a_inputs, const RE::BGSTextureSet& a_source)
    {
        CC::TxstKeyInput input;
        for (const auto& texture : a_source.textures) {  // TX00..TX07, the record's bytes
            const char* text = texture.textureName.c_str();
            input.slotsRaw.emplace_back(text ? text : "");
        }
        input.tx00 = TextureHash(a_inputs, input.slotsRaw[CC::kTxstDiffuseSlot]);
        input.tx03 = TextureHash(a_inputs, input.slotsRaw[CC::kTxstGlowSlot]);
        input.dodt = a_source.decalData != nullptr;
        return input;
    }

    std::optional<Key> TxstKey(const Inputs& a_inputs, const RE::BGSTextureSet& a_source)
    {
        if (!a_inputs.prefix) {
            return std::nullopt;
        }
        const auto text = CC::TxstKeyString(*a_inputs.prefix, TxstInput(a_inputs, a_source));
        return text ? std::optional(Key{ CC::KeyOf(*text), *text }) : std::nullopt;
    }
}
