#pragma once

// =============================================================================
// GreyFormsTxst - decal texture set (TXST) copies: request parts, the record, its read-back, the map parts
// =============================================================================
// The DLL's closure reaches a TXST through a copied PROJ's decal (BGSProjectileData::decalData) or a copied IPCT's two
// texture sets (BGSImpactData::decalTextureSet / decalTextureSet2). The request gives its eight slots (TX00..TX07, as the
// record stores them, UTF-8), "hasDecal" (the source has DODT), its DNAM flags and bounds. The copy record:
//   EDID HoMBF_C_TXST_<id>, OBND, TX00..TX07 (each slot the source has: its grey copy for TX00 / TX03 when one was made,
//   else the source's text), DODT (36 zero bytes, only when the source has DODT), DNAM u16 flags.
// The DODT bytes are a stub: the DLL assigns the live source's decal data (colour greyed) at kDataLoaded, so a stub it
// cannot allocate later is there exactly when the source has one (RE/B/BGSTextureSet.h: decalData is a pointer).
// Text: the request is UTF-8 (the DLL's SanitizeToUTF8 of the code page's bytes); the record and the texture lookups use
// the system code page's bytes (Utf8ToAnsiExact), the map UTF-8 again (AnsiToUtf8Exact). A slot that does not convert
// without loss refuses its form (a hand-made request) or leaves it out with a warning (--from-game).

#include "GreyForms.h"
#include "GreyJson.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace GreyBuild
{
    struct RiskJob;
    struct TextureJob;

    /// a_text from UTF-8 to the system code page; the text itself when ASCII; nullopt when it is not UTF-8 or does not
    /// convert without loss
    [[nodiscard]] std::optional<std::string> Utf8ToAnsiExact(const std::string& a_text);

    /// a_text from UTF-8 to a_codePage (Utf8ToAnsiExact with CP_ACP): no best-fit character, read back to the same text
    [[nodiscard]] std::optional<std::string> Utf8ToCodePageExact(const std::string& a_text, unsigned int a_codePage);

    /// a_text from the system code page to UTF-8; the text itself when ASCII; nullopt when it does not convert
    [[nodiscard]] std::optional<std::string> AnsiToUtf8Exact(const std::string& a_text);

    /// U11: a_text to the code page, or unchanged when it does not convert without loss (texture list keys)
    [[nodiscard]] std::string Utf8ToAnsi(const std::string& a_text);

    /// A TXST form's own request parts ("textures", "hasDecal"; "model" must be empty). False with a_why when a slot
    /// cannot be used (not UTF-8, lossy in the code page, U+FFFD); throws (Json::Fail) on a malformed field.
    [[nodiscard]] bool ReadTextureSetParts(const std::filesystem::path& a_file, const nlohmann::json& a_item, FormSource& a_form,
        const std::string& a_where, std::string& a_why);

    /// The record text of a Data-relative texture path (TextureDataPath / TextureCopyPathFor): without "textures\"
    [[nodiscard]] std::string RecordTexturePath(const std::string& a_dataPath);

    /// The TXST record's subrecords (EDID .. DNAM), a_dnam the scalar bytes (DataBytes)
    [[nodiscard]] std::string TextureSetRecordBody(const CopyEntry& a_entry, const FormSource& a_source, const std::string& a_dnam);

    /// TX00..TX07 and DODT into a_form; false for any other tag, or one seen twice
    bool ReadTextureSetSubrecord(const std::string& a_tag, const std::string& a_bytes, EslForm& a_form);

    /// What a read-back TXST record differs in from what was written (empty: it matches)
    [[nodiscard]] std::vector<std::string> VerifyTextureSetRecord(const EslForm& a_back, const CopyEntry& a_entry,
        const FormSource& a_source, const std::string& a_dnam);

    /// The map entry's TXST parts: "sourceTextures", "greyTextures" (UTF-8), "hasDecal"
    void WriteTextureSetMapParts(nlohmann::ordered_json& a_item, const CopyEntry& a_entry);
    void ReadTextureSetMapParts(const std::filesystem::path& a_file, const nlohmann::json& a_item, CopyEntry& a_entry,
        const std::string& a_where);

    /// The texture step of every TXST copy (GreyFormsTextureSets.cpp): TX00 (diffuse, --tint) and TX03 (glow, tint 1)
    /// through a_job (null: textures off), the grey copies into a_copies' greyTextures, and the risk check's decal rule
    /// (DECAL_COLOUR, GreyRisk.h) on what each copy record names
    void StageTextureSets(CopiesMap& a_copies, TextureJob* a_job, RiskJob& a_risk);

    /// The decal rule alone (GreyFormsTextureSets.cpp): TX00 and TX03 as the copy record names them, judged by the texture
    /// statistics' coloured rule; FAIL DECAL_COLOUR, UNREAD, MISSING
    void RiskCheckTextureSet(const CopyEntry& a_entry, RiskJob& a_risk);
}
