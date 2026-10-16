#pragma once

#include "Common.h"

#include "FireClosure.h"

#include <nlohmann/json.hpp>

#include <string>

// =============================================================================
// COPY REQUEST DATA - one form or ADDN as the request writes it (CopyRequest.h)
// =============================================================================
// The scalar keys are the builder's (tools/blackflame-greybuild GreyFormsEsl.cpp Fields): the values only make a
// valid placeholder record - the DLL fills every copy from the live form at kDataLoaded.

namespace BlackFlame::CopyRequestData
{
    /// The projectile's muzzle flash model as its record stores it, or empty
    [[nodiscard]] std::string MuzzleModelOf(const RE::BGSProjectile& a_projectile);

    /// { source, type, editorId, model, [muzzleModel], [bounds], data }
    [[nodiscard]] nlohmann::ordered_json FormJson(const RE::TESForm* a_form, CopyKind a_kind);

    /// Every ADDN record: { index, model, editorId, source, masterParticleCap, dnamFlags, own }
    [[nodiscard]] nlohmann::ordered_json AddonsJson();
}
