#include "FileProbeRun.h"

#include "NifDump.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>

// The dev file visibility probe's source files (FileVisibilityProbe.h): a projectile NIF and a DDS it names, read through
// the game's file system (the bytes every test writes or serves), and C-loading's in-flight target

namespace BlackFlame::FileProbe
{
    namespace
    {
        constexpr std::size_t kMaxSourceBytes = 8u * 1024u * 1024u;  // a larger NIF or DDS is not a quick probe file
        constexpr std::size_t kMaxProjectilesTried = 48;             // per pass (fire models first, then any)
        constexpr const char* kPaletteTexture = "textures\\heartofmagic\\blackflame\\gradblackflame.dds";  // ships with the addon
        constexpr std::size_t kDdsHeaderEnd = 128;  // "DDS " + the 124-byte header
        constexpr std::size_t kDdsHeightAt = 12;    // after the 4-byte magic: dwSize, dwFlags, dwHeight, dwWidth
        constexpr std::size_t kDdsWidthAt = 16;

        bool ReadSource(const std::string& a_dataPath, Bytes& a_bytes)
        {
            a_bytes.clear();
            const auto blob = NifDump::ReadModel(a_dataPath, &a_bytes);
            return blob.ok && !a_bytes.empty() && a_bytes.size() <= kMaxSourceBytes;
        }
    }

    std::optional<std::pair<std::uint32_t, std::uint32_t>> DdsSize(const Bytes& a_bytes)
    {
        if (a_bytes.size() < kDdsHeaderEnd || std::memcmp(a_bytes.data(), "DDS ", 4) != 0) {
            return std::nullopt;
        }
        std::uint32_t height = 0;
        std::uint32_t width = 0;
        std::memcpy(&height, a_bytes.data() + kDdsHeightAt, sizeof(height));
        std::memcpy(&width, a_bytes.data() + kDdsWidthAt, sizeof(width));
        if (width == 0 || height == 0) {
            return std::nullopt;
        }
        return std::pair{ width, height };
    }

    // A projectile's NIF that reads through the game's file system, and a DDS it names that reads too
    bool PickSources(Run& a_run)
    {
        auto* handler = RE::TESDataHandler::GetSingleton();
        if (!handler) {
            return false;
        }
        std::string fallbackModel;
        Bytes fallbackNif;
        for (const bool firePass : { true, false }) {
            std::size_t tried = 0;
            for (auto* projectile : handler->GetFormArray<RE::BGSProjectile>()) {
                const char* model = projectile ? static_cast<const RE::TESModel*>(projectile)->GetModel() : nullptr;
                if (!model || !*model) {
                    continue;
                }
                std::string lower(model);
                std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if ((lower.find("fire") != std::string::npos) != firePass || ++tried > kMaxProjectilesTried) {
                    continue;
                }
                const auto dataPath = NifDump::NormalizeModelPath(model);
                Bytes nif;
                if (!dataPath || !ReadSource(*dataPath, nif)) {
                    continue;
                }
                if (fallbackModel.empty()) {
                    fallbackModel = model;
                    fallbackNif = nif;
                }
                for (const auto& slot : NifDump::TexturePathsIn(nif)) {
                    const auto texture = NifDump::NormalizeTexturePath(slot);
                    Bytes dds;
                    if (!texture || !ReadSource(*texture, dds) || !DdsSize(dds)) {
                        continue;
                    }
                    a_run.sourceModel = model;
                    a_run.sourceTexture = *texture;
                    for (const auto& other : NifDump::TexturePathsIn(nif)) {
                        if (const auto path = NifDump::NormalizeTexturePath(other); path && *path != *texture) {
                            a_run.otherTextures.push_back(*path);
                        }
                    }
                    a_run.nif = std::make_shared<const Bytes>(std::move(nif));
                    a_run.dds = std::make_shared<const Bytes>(std::move(dds));
                    return true;
                }
            }
        }
        Bytes palette;
        if (fallbackModel.empty() || !ReadSource(kPaletteTexture, palette) || !DdsSize(palette)) {
            return false;
        }
        a_run.sourceModel = fallbackModel;
        a_run.sourceTexture = kPaletteTexture;
        a_run.nif = std::make_shared<const Bytes>(std::move(fallbackNif));
        a_run.dds = std::make_shared<const Bytes>(std::move(palette));
        return true;
    }

    // FP6: C-loading's in-flight load must be a DDS the probe never asked for: a slot of ANOTHER projectile's NIF that is not
    // a slot of the source NIF and that opens through the game's file system
    void PickInFlightTarget(Run& a_run)
    {
        auto* handler = RE::TESDataHandler::GetSingleton();
        if (!handler) {
            return;
        }
        std::size_t tried = 0;
        for (auto* projectile : handler->GetFormArray<RE::BGSProjectile>()) {
            const char* model = projectile ? static_cast<const RE::TESModel*>(projectile)->GetModel() : nullptr;
            if (!model || !*model || a_run.sourceModel == model || ++tried > kMaxProjectilesTried) {
                continue;
            }
            const auto dataPath = NifDump::NormalizeModelPath(model);
            Bytes nif;
            if (!dataPath || !ReadSource(*dataPath, nif)) {
                continue;
            }
            for (const auto& slot : NifDump::TexturePathsIn(nif)) {
                const auto texture = NifDump::NormalizeTexturePath(slot);
                if (!texture || *texture == a_run.sourceTexture || std::ranges::find(a_run.otherTextures, *texture) != a_run.otherTextures.end() ||
                    !NifDump::ProbeModel(*texture).opened) {
                    continue;
                }
                a_run.inFlightTarget = *texture;
                return;
            }
        }
    }

    // Test E's overwrite files: another projectile NIF of a different byte size from the source (eNif, with its own record
    // path to Demand for the new content's root name) and a DDS of different dimensions (eDds); and, when found, a NIF of the
    // source's exact byte size with different content (eSameNif) and a DDS of the source's dimensions (eSameDds) for E-same
    void PickOverwrite(Run& a_run)
    {
        auto* handler = RE::TESDataHandler::GetSingleton();
        if (!handler || !a_run.nif || !a_run.dds) {
            return;
        }
        const auto sourceDds = DdsSize(*a_run.dds);
        std::size_t tried = 0;
        for (auto* projectile : handler->GetFormArray<RE::BGSProjectile>()) {
            const char* model = projectile ? static_cast<const RE::TESModel*>(projectile)->GetModel() : nullptr;
            if (!model || !*model || a_run.sourceModel == model || ++tried > kMaxProjectilesTried) {
                continue;
            }
            const auto dataPath = NifDump::NormalizeModelPath(model);
            Bytes nif;
            if (!dataPath || !ReadSource(*dataPath, nif) || nif == *a_run.nif) {
                continue;
            }
            if (!a_run.eNif && nif.size() != a_run.nif->size()) {
                a_run.eNifRecord = model;  // its own record path: Demanded for the new content's root name
                a_run.eNif = std::make_shared<const Bytes>(nif);
            } else if (!a_run.eSameNif && nif.size() == a_run.nif->size()) {
                a_run.eSameNifRecord = model;
                a_run.eSameNif = std::make_shared<const Bytes>(nif);
            }
            for (const auto& slot : NifDump::TexturePathsIn(nif)) {
                const auto texture = NifDump::NormalizeTexturePath(slot);
                Bytes dds;
                if (!texture || !ReadSource(*texture, dds) || dds == *a_run.dds) {
                    continue;
                }
                const auto dims = DdsSize(dds);
                if (!dims) {
                    continue;
                }
                if (!a_run.eDds && (!sourceDds || *dims != *sourceDds)) {
                    a_run.eDdsWidth = dims->first;
                    a_run.eDdsHeight = dims->second;
                    a_run.eDds = std::make_shared<const Bytes>(std::move(dds));
                } else if (!a_run.eSameDds && sourceDds && *dims == *sourceDds) {
                    a_run.eSameDds = std::make_shared<const Bytes>(std::move(dds));
                }
            }
            if (a_run.eNif && a_run.eDds && a_run.eSameNif && a_run.eSameDds) {
                return;
            }
        }
    }
}
