#pragma once

// =============================================================================
// SLOT TXST PATHS - a TXST slot's eight record strings from its txst object (no game types)
// =============================================================================
// The builder's txst line names its texture copies as deps only (CopyContractProgress.h: no slot per dep). Each dep is
// matched to TX00 (role diffuse) or TX03 (role glow) by the key the builder gave that copy: the contract's
// TextureKeyString over the key prefix, the slot's texture (normalised under textures\) and that file's FNV-1a 64 - the
// same inputs the builder's KeyedTextures hashes (GreyKeyedTextures.cpp, P1). A colour slot no dep matches keeps the
// source's bytes (the builder left it: missing, grey already, not ASCII, failed); every other slot keeps the source's.
// A record string is Data-relative under textures\ without that prefix (TESTexture::GetDefaultPath is "Textures\\",
// RE/T/TESTexture.h), so a dep's "textures\" is dropped.
// Refused (nullopt + why): no deps (a fallback with both colour slots kept publishes nothing), an ADDN dep, a dep that
// is no texture copy path, a dep that matches no colour slot. blackflame-cosave-check tests it.

#include "CopyContractKeys.h"
#include "DataPathRule.h"
#include "SlotLedgerModel.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace BlackFlame::SlotTxstPaths
{
    namespace CC = CopyContract;

    inline constexpr std::size_t kSlots = CC::kTxstSlots;
    using Strings = std::array<std::string, kSlots>;

    struct ColourSlot
    {
        std::size_t slot;
        CC::TextureRole role;
    };
    inline constexpr ColourSlot kColourSlots[] = { { CC::kTxstDiffuseSlot, CC::TextureRole::kDiffuse },
        { CC::kTxstGlowSlot, CC::TextureRole::kGlow } };

    /// The key16 the builder gives a_raw's copy in a_role, or nullopt (empty slot, a path Normalize refuses, no hash)
    [[nodiscard]] inline std::optional<std::string> CopyKey16(const CC::KeyPrefix& a_prefix, CC::TextureRole a_role,
        const std::string& a_raw, const CC::FileHash& a_hash)
    {
        if (a_raw.empty() || a_hash.state != CC::FileState::kHashed) {
            return std::nullopt;
        }
        const auto path = DataPathRule::Normalize(a_raw, DataPathRule::kTexturesRoot);
        if (!path) {
            return std::nullopt;
        }
        const auto text = CC::TextureKeyString(a_prefix, a_role, { *path, a_hash.fnv64 });
        return text ? std::optional<std::string>(CC::Key16Of(*text)) : std::nullopt;
    }

    /// a_raw: the source's TX00..TX07 record bytes; a_tx00 / a_tx03: those textures' hashes (SlotKeys::TextureHash)
    [[nodiscard]] inline std::optional<Strings> RecordStrings(const CC::KeyPrefix& a_prefix, const Strings& a_raw,
        const CC::FileHash& a_tx00, const CC::FileHash& a_tx03, const std::vector<SlotLedgerModel::Dep>& a_deps, std::string& a_why)
    {
        if (a_deps.empty()) {
            a_why = "no colour slot got a copy (the builder kept both): the decal keeps the source";
            return std::nullopt;
        }
        Strings out = a_raw;
        std::vector<bool> used(a_deps.size(), false);
        for (const auto& colour : kColourSlots) {
            const auto key16 = CopyKey16(a_prefix, colour.role, a_raw[colour.slot], colour.slot == CC::kTxstDiffuseSlot ? a_tx00 : a_tx03);
            if (!key16) {
                continue;
            }
            for (std::size_t i = 0; i < a_deps.size(); ++i) {
                const auto& dep = a_deps[i];
                const auto parts = dep.isAddn ? std::nullopt : DataPathRule::ParseCopyPath(dep.path);
                if (used[i] || !parts || parts->palette || parts->root != DataPathRule::kTexturesRoot || parts->key16 != *key16) {
                    continue;
                }
                used[i] = true;
                out[colour.slot] = dep.path.substr(DataPathRule::kTexturesRoot.size());
                break;
            }
        }
        for (std::size_t i = 0; i < a_deps.size(); ++i) {
            if (!used[i]) {
                const auto& dep = a_deps[i];
                a_why = dep.isAddn ? "an ADDN dep on a txst object" :
                                     "the dep '" + dep.path + "' is the copy of neither TX00 nor TX03 (keys from this session's textures)";
                return std::nullopt;
            }
        }
        return out;
    }
}
