#pragma once

#include "Common.h"

namespace BlackFlame::HitPath::Detail
{
    // Installed with CorpseGrey's two checked sites, on 1.6.1170 only. Update and Pop refill the instance's membrane
    // colours from the shared record; their chained vfuncs grey those values again after the original returns.
    [[nodiscard]] bool InstallCorpseColours();
    void GreyCorpseMembrane(RE::ShaderReferenceEffect* a_sre, const char* a_point);
}
