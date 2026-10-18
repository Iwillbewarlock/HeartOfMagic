#pragma once

// =============================================================================
// SLOT TXST - a decal texture set slot's runtime write (config slotTxst; CopyGroups opens it like any member)
// =============================================================================
// New code: the copies-map route never wrote a texture set at runtime (FormCopiesFillTexture.cpp: the builder baked the
// grey paths into its TXST records), so what follows is the design's open point (v2 §1), checked by the log:
//   strings  SlotTxstPaths::RecordStrings: TX00 / TX03 point at the builder's copies, the other slots keep the source's
//   write    BGSTextureSet::textures[i].textureName (TESTexture, 0x040) and textureFileIDs[i] (BSResource::ID, 0x0CC;
//            RE/B/BGSTextureSet.h). SetTexturePath (BSTextureSet vfunc 27) is not called. A slot whose string is the
//            source's takes the source's own textureFileID when that is zero, the rule's ID of the string, or Normalize
//            changes the string's shape (SlotTxstRule::NormalizeKeepsShape), else the rule's (a stale ID naming another
//            file, run 3's 'black.dds' TX03); an empty slot takes zero; a copy slot's is made
//            with BSResource::ID::GenerateFromPath (RE/I/ID.h, 68635 / 69979) by the rule the engine's own IDs follow:
//   rule     once a session, from every assigned TXST slot's source, slot by slot (SlotTxstRule.h): a non-empty slot whose
//            stored ID is non-zero and equals GenerateFromPath of Normalize(name, "textures\") votes "prefixed", of name
//            "bare"; a slot both forms match (a rooted name) does not vote; any other slot is unexplained (zero: a
//            missing file; another file) and does not vote. The rule is the form with votes when the other has none;
//            none (both or neither): no TXST slot is written this session. The line groups the unexplained slots by name.
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

    /// W1: the strings of a set published in an earlier session, from what the publish stored (SlotTxstPaths::RestoredStrings:
    /// the source's key fields now and the deps, not this session's key prefix); a ledger without them: StringsFor
    [[nodiscard]] std::optional<SlotTxstPaths::Strings> RestoredFor(const SlotAssign::Slot& a_slot, const SlotLedgerModel::Object& a_object,
        std::string& a_why);

    /// The rule's name as the ledger stores it with a published set ("prefixed", "bare", "none"; an older ledger may hold "zero")
    [[nodiscard]] std::string RuleName();

    /// The textureFileIDs rule can be used this session (made once, from the assigned TXST slots' sources); a_why when not
    [[nodiscard]] bool RuleUsable(std::string& a_why);

    /// The rule as one line for the log and the self-test
    [[nodiscard]] std::string RuleText();

    /// Writes a_strings into the slot's form (names and IDs) and keeps them in a_slot.publishedTextures; false with a_why
    [[nodiscard]] bool Write(SlotAssign::Slot& a_slot, const SlotTxstPaths::Strings& a_strings, std::string& a_why);

    /// The ID the slot's form should hold for a_string at slot a_index: zero for an empty string; for the source's string its
    /// stored ID unless that names another file and the string hashes as it is (SlotTxstRule::KeepsStored), then the
    /// rule's; for any other string the rule's (nullopt when it cannot be made)
    [[nodiscard]] std::optional<RE::BSResource::ID> ExpectedId(const SlotAssign::Slot& a_slot, std::size_t a_index, const std::string& a_string);
}
