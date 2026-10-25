#pragma once

#include "Common.h"

namespace BlackFlame::HitPath::Detail
{
    // Installed with CorpseGrey's two checked sites, on 1.6.1170 and 1.7.104 only. Update and Pop refill the instance's
    // membrane colours from the shared record; their chained vfuncs grey those values again after the original returns.
    [[nodiscard]] bool InstallCorpseColours();
    void GreyCorpseMembrane(RE::ShaderReferenceEffect* a_sre, const char* a_point);
    // CorpseGrey's attach decides once per shader record whether it is fire (CorpseColourRules.h FireKeys, or a grey copy);
    // Update and Pop grey only a marked SRE whose shader was decided fire. Records are never freed: their address is a key.
    void NoteCorpseFireShader(const RE::TESEffectShader* a_shader, bool a_fire);
    [[nodiscard]] bool IsCorpseFireShader(const RE::TESEffectShader* a_shader);
}
