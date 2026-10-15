#pragma once

#include "Common.h"

#include <cstdint>
#include <string>
#include <vector>

// =============================================================================
// GREY MAP - Data/SKSE/Plugins/HeartOfMagic_BlackFlame_greymap.json
// =============================================================================
// The grey duplicates of shared-particle fire meshes (README, "Grey duplicates"):
// "models" maps an impact / hazard NIF to its grey copy, "addons" an original ADDN
// index to its grey ADDN record. One file name only: tools/blackflame-greybuild
// --request writes a full map under the same name, so a generated map installed
// in a later mod replaces the shipped one through the VFS.
// Read once at kDataLoaded and never changed or freed afterwards: the engine keeps
// the const char* a model lookup returns (BSTempEffectParticle hands it to an async
// clone task), so the strings live for the whole session.

namespace BlackFlame
{
    struct GreyAddon
    {
        std::uint32_t index = 0;
        std::uint32_t grey = 0;
        std::string greyModel;  // Data-relative, with meshes\ (for the existence check)
        std::string greyForm;   // "Plugin.esp|00XXXX"
    };

    /// kDataLoaded, once: reads and validates the map (bad entries logged and skipped)
    void LoadGreyMap();

    /// Any thread after kDataLoaded: the grey copy of a model path as TESModel::GetModel gives it
    /// (case and slashes ignored, a leading "meshes\" ignored), in the same form (no "meshes\"), or null
    [[nodiscard]] const char* GreyModelFor(const char* a_model);

    [[nodiscard]] std::size_t GreyModelCount() noexcept;
    [[nodiscard]] const std::vector<GreyAddon>& GreyAddons();

    /// The grey models with their "meshes\" path, for the existence check
    [[nodiscard]] std::vector<std::string> GreyModelFiles();
}
