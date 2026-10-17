// =============================================================================
// GreyFormsTextureSets - the texture step and the risk rule of decal texture set copies (GreyFormsTxst.h)
// =============================================================================
// TX00 (diffuse) takes the run's --tint, as a lit mesh's diffuse does (a decal is drawn on a lit surface; how the decal
// shader uses each slot is not checked against the game); TX03 (glow / detail) is a brightness mask at tint 1. Both go
// through the NIFs' TextureJob, so a texture a NIF names too is recoloured once per tint. Every other slot (normal, masks,
// environment) keeps the source's text. A slot whose texture is missing, already grey at tint 1 or fails keeps the
// source's text as well; DECAL_COLOUR then says when that leaves a coloured decal.

#include "GreyFormsTxst.h"

#include "GreyRisk.h"
#include "GreyTexture.h"

#include <algorithm>
#include <fmt/format.h>
#include <iostream>

namespace GreyBuild
{
    namespace
    {
        constexpr const char* kRuleDecalColour = "DECAL_COLOUR";
        constexpr const char* kRuleUnread = "UNREAD";
        constexpr const char* kRuleMissing = "MISSING";
        constexpr const char* kRuleDecalNotJudged = "DECAL_NOT_JUDGED";

        struct ColourSlot
        {
            std::size_t slot;
            TextureRole role;
            const char* name;
        };
        constexpr ColourSlot kColourSlots[] = { { kTxstSlotDiffuse, TextureRole::kDiffuse, "TX00" },
            { kTxstSlotGlow, TextureRole::kGlow, "TX03" } };

        void AddFinding(RiskJob& a_risk, RiskLevel a_level, const char* a_rule, const CopyEntry& a_entry, std::string a_detail)
        {
            RiskFinding finding;
            finding.level = a_level;
            finding.rule = a_rule;
            finding.nif = fmt::format("TXST {} ({})", a_entry.editorId, a_entry.source);
            finding.detail = std::move(a_detail);
            ++a_risk.perRule[a_rule];
            a_risk.findings.push_back(std::move(finding));
        }
    }

    void StageTextureSets(CopiesMap& a_copies, TextureJob* a_job, RiskJob& a_risk)
    {
        for (auto& entry : a_copies.entries) {
            if (entry.type != CopyType::kTextureSet) {
                continue;
            }
            entry.greyTextures = {};
            int moved = 0;
            if (a_job) {
                for (const auto& colour : kColourSlots) {
                    std::string slot = entry.sourceTextures[colour.slot];
                    if (slot.empty()) {
                        continue;
                    }
                    if (!IsAscii(slot)) {
                        // AB1: its dump file's name depends on a code page conversion of these bytes: left as the source's
                        std::cout << fmt::format("NOTE  TXST {} {}: '{}' is not plain ASCII: not recoloured (the slot keeps the "
                                                 "source's bytes)\n", entry.source, colour.name, entry.sourceTextureNames[colour.slot]);
                        ++a_job->totals.decalSlots;
                        continue;
                    }
                    const std::string before = slot;
                    const int repointed = a_job->totals.repointed;
                    ++a_job->totals.decalSlots;
                    RetargetTextureSlot(slot, colour.role, *a_job, fmt::format("TXST {} {}", entry.source, colour.name));
                    if (a_job->totals.repointed != repointed && slot != before) {
                        entry.greyTextures[colour.slot] = RecordTexturePath(slot);
                        ++a_job->totals.decalRepointed;
                        ++moved;
                    }
                }
            }
            if (moved == 0) {
                const std::string why = a_job ? "no colour slot got a grey copy" : "texture copies off";
                entry.note = fmt::format("{}: the record names the source's textures (the DLL still greys its DODT colour)", why);
            }
            RiskCheckTextureSet(entry, a_risk);
        }
    }

    void RiskCheckTextureSet(const CopyEntry& a_entry, RiskJob& a_risk)
    {
        for (const auto& colour : kColourSlots) {
            const std::string& text = CopyRecordTexture(a_entry, colour.slot);
            if (text.empty()) {
                continue;
            }
            if (!IsAscii(text)) {
                AddFinding(a_risk, RiskLevel::kWarn, kRuleDecalNotJudged, a_entry, fmt::format("{} '{}' is not plain ASCII: not "
                    "recoloured and not judged", colour.name, a_entry.sourceTextureNames[colour.slot]));
                continue;
            }
            if (!a_risk.textures) {
                AddFinding(a_risk, RiskLevel::kUnread, kRuleUnread, a_entry, fmt::format("{} {}: no texture source in this run",
                    colour.name, text));
                continue;
            }
            const auto look = a_risk.textures->Look(text);
            if (look.status != TextureRead::kOk || !look.stats) {
                const bool missing = look.status == TextureRead::kMissing;
                AddFinding(a_risk, missing ? RiskLevel::kMissing : RiskLevel::kUnread, missing ? kRuleMissing : kRuleUnread, a_entry,
                    fmt::format("{} {}: {}", colour.name, look.path.empty() ? text : look.path, look.why));
                continue;
            }
            if (IsColouredTexture(*look.stats)) {
                AddFinding(a_risk, RiskLevel::kFail, kRuleDecalColour, a_entry, fmt::format("{} {} is still coloured (mean "
                    "saturation {:.2f}, coloured share {:.0f}%): the player's decal shows its colour", colour.name, look.path,
                    look.stats->meanSat, look.stats->colouredFrac * 100.0));
            }
        }
    }
}
