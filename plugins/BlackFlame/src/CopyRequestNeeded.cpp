// =============================================================================
// CopyRequestNeeded - the dump files the builder will read this start (CopyRequestNeeded.h)
// =============================================================================

#include "CopyRequestNeeded.h"

#include "BuildProgress.h"
#include "BuildProgressTrim.h"
#include "CopyContractEfsh.h"
#include "NifDump.h"
#include "SlotAssign.h"

#include <fstream>
#include <iterator>
#include <map>
#include <unordered_map>

namespace BlackFlame::CopyRequestNeeded
{
    namespace
    {
        namespace CC = CopyContract;

        // A model's NIF path and every texture it names, as the dump plan normalises them (CopyRequestData PlanTextures)
        class NifFiles
        {
        public:
            explicit NifFiles(const SlotKeys::Inputs& a_inputs) : m_inputs(a_inputs) {}

            std::vector<std::string> Of(const std::string& a_recordModel)
            {
                const auto path = NifDump::NormalizeModelPath(a_recordModel);
                if (!path) {
                    return {};
                }
                if (const auto it = m_cache.find(*path); it != m_cache.end()) {
                    return it->second;
                }
                std::vector<std::string> files{ *path };
                if (const auto at = m_inputs.nifAt.find(*path); at != m_inputs.nifAt.end()) {
                    const auto texts = NifDump::TexturePathsIn(m_inputs.nifs[at->second].bytes);
                    const auto textures = CC::TexturePlanTiers({ texts });
                    files.insert(files.end(), textures.begin(), textures.end());
                }
                return m_cache.emplace(*path, std::move(files)).first->second;
            }

        private:
            const SlotKeys::Inputs& m_inputs;
            std::unordered_map<std::string, std::vector<std::string>> m_cache;
        };

        std::set<std::uint64_t> NotFire()
        {
            const auto file = BuildProgress::StagingFolder() / CC::kNotFireFile;
            std::ifstream in(file, std::ios::binary);
            if (!in) {
                return {};
            }
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            return DiskTrimRules::NotFireKeys(text);
        }

        std::vector<DiskTrimRules::NeedObject> Objects(const SlotKeys::Inputs& a_inputs)
        {
            std::vector<DiskTrimRules::NeedObject> out;
            NifFiles nifs(a_inputs);
            for (const auto id : SlotAssign::Assigned()) {
                const auto& slot = SlotAssign::At(id);
                if (!slot.usable || !slot.source) {
                    continue;  // not in the request (CopyRequestV5 Forms and SlotObjects' rule)
                }
                if (slot.modelKey) {
                    out.push_back({ slot.modelKey->key, false, nifs.Of(slot.sourceModel) });
                }
                if (slot.muzzleKey) {
                    out.push_back({ slot.muzzleKey->key, false, nifs.Of(slot.sourceMuzzle) });
                }
                if (slot.txstKey) {
                    std::vector<std::string> texts;
                    if (const auto* set = slot.source->As<RE::BGSTextureSet>()) {
                        for (const auto& texture : set->textures) {
                            const char* text = texture.textureName.c_str();
                            texts.emplace_back(text ? text : "");
                        }
                    }
                    out.push_back({ slot.txstKey->key, false, CC::TexturePlanTiers({ texts }) });
                }
            }
            for (const auto& palette : a_inputs.efsh.palettes) {
                if (palette.key) {
                    out.push_back({ *palette.key, false, { palette.path } });
                }
            }
            for (const auto& base : a_inputs.efshBase.palettes) {
                if (base.key) {
                    out.push_back({ *base.key, true, { base.path } });
                }
            }
            return out;
        }
    }

    DiskTrimRules::Needed Compute(const SlotKeys::Inputs& a_inputs, bool a_startRead, bool a_listWritten)
    {
        if (!a_startRead || !a_listWritten) {
            DiskTrimRules::Needed all;
            all.all = true;
            return all;
        }
        std::vector<std::string> always;
        for (const auto& texture : a_inputs.textures) {
            if (texture.own) {
                always.push_back(texture.path);
            }
        }
        return DiskTrimRules::NeededFiles(Objects(a_inputs), &BuildProgress::LastLine, NotFire(), BuildProgress::Published(),
            &BuildProgress::StagedSize, always);
    }
}
