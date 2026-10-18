#pragma once

// =============================================================================
// SLOT TXST - a decal texture set slot's runtime write (config slotTxst; CopyGroups opens it like any member)
// =============================================================================
// New code: the copies-map route never wrote a texture set at runtime (FormCopiesFillTexture.cpp: the builder baked the
// grey paths into its TXST records), so what follows is the design's open point (v2 §1), checked by the log:
//   strings  SlotTxstPaths::RecordStrings: TX00 / TX03 point at the builder's copies, the other slots keep the source's
//   write    BGSTextureSet::textures[i].textureName (TESTexture, 0x040) and textureFileIDs[i] (BSResource::ID, 0x0CC;
//            RE/B/BGSTextureSet.h). SetTexturePath (BSTextureSet vfunc 27) is not called. A slot whose string is the
//            source's takes the source's own textureFileID; a copy slot's is made with BSResource::ID::GenerateFromPath
//            (RE/I/ID.h, 68635 / 69979) by the rule the engine's own IDs follow:
//   rule     once a session, from every assigned TXST slot's source: for each non-empty slot, GenerateFromPath of
//            "textures\" + name and of name are compared with the stored ID (all 12 bytes). The rule is the one that
//            matches every slot, or "zero" when every stored ID is zero; none: no TXST slot is written this session
//            (logged with the first slot's stored and computed IDs).
// Windows: W1 (kDataLoaded thread) and W2 (main-thread drains), as the other members; DDS deps are registered like every
// copy file (RegisterObject), though a DDS loads without it (probe run 3).

#include "Common.h"

#include "SlotAssign.h"
#include "SlotLedgerModel.h"
#include "SlotTxstPaths.h"

#include <optional>
#include <string>

namespace BlackFlame::SlotTxst
{
    /// The slot's eight record strings for a_object (its deps matched to TX00 / TX03), or nullopt with a_why
    [[nodiscard]] std::optional<SlotTxstPaths::Strings> StringsFor(const SlotAssign::Slot& a_slot, const SlotLedgerModel::Object& a_object,
        std::string& a_why);

    /// The textureFileIDs rule can be used this session (made once, from the assigned TXST slots' sources); a_why when not
    [[nodiscard]] bool RuleUsable(std::string& a_why);

    /// The rule as one line for the log and the self-test
    [[nodiscard]] std::string RuleText();

    /// Writes a_strings into the slot's form (names and IDs) and keeps them in a_slot.publishedTextures; false with a_why
    [[nodiscard]] bool Write(SlotAssign::Slot& a_slot, const SlotTxstPaths::Strings& a_strings, std::string& a_why);

    /// The ID the slot's form should hold for a_string at slot a_index (the source's own when the string is the source's)
    [[nodiscard]] std::optional<RE::BSResource::ID> ExpectedId(const SlotAssign::Slot& a_slot, std::size_t a_index, const std::string& a_string);
}
