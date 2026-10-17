// =============================================================================
// GreyFormsTxst - decal texture set (TXST) copies (GreyFormsTxst.h)
// =============================================================================

#include "GreyFormsTxst.h"

#include "GreyEslIo.h"
#include "GreyTexture.h"

#include <algorithm>
#include <fmt/format.h>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

namespace GreyBuild
{
    namespace
    {
        using namespace EslIo;

        // TX00..TX07: the subrecord tags in slot order (RE/B/BGSTextureSet.h textures, "TX00 - TX07")
        constexpr const char* kSlotTags[kTxstSlots] = { "TX00", "TX01", "TX02", "TX03", "TX04", "TX05", "TX06", "TX07" };

        [[nodiscard]] bool IsAscii(const std::string& a_text)
        {
            return std::all_of(a_text.begin(), a_text.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
        }

        // a_text from a_from to a_to through UTF-16; nullopt when it is not valid in a_from or loses a character in a_to
        [[nodiscard]] std::optional<std::string> Convert(const std::string& a_text, UINT a_from, UINT a_to)
        {
            if (a_text.empty() || IsAscii(a_text)) {
                return a_text;
            }
            // CP_ACP may itself be UTF-8 (Windows' "Beta: Unicode UTF-8" option, 65001), which takes no default-character flag
            a_from = a_from == CP_ACP ? ::GetACP() : a_from;
            a_to = a_to == CP_ACP ? ::GetACP() : a_to;
            if (a_from == a_to && a_from == CP_UTF8) {
                return ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_text.data(), static_cast<int>(a_text.size()),
                           nullptr, 0) > 0 ? std::optional<std::string>(a_text) : std::nullopt;
            }
            const int size = static_cast<int>(a_text.size());
            const int wide = ::MultiByteToWideChar(a_from, MB_ERR_INVALID_CHARS, a_text.data(), size, nullptr, 0);
            if (wide <= 0) {
                return std::nullopt;
            }
            std::wstring buffer(static_cast<std::size_t>(wide), L'\0');
            ::MultiByteToWideChar(a_from, MB_ERR_INVALID_CHARS, a_text.data(), size, buffer.data(), wide);
            BOOL lossy = FALSE;
            BOOL* lossyOut = a_to == CP_UTF8 ? nullptr : &lossy;  // CP_UTF8 takes no default-character flag
            const int narrow = ::WideCharToMultiByte(a_to, 0, buffer.data(), wide, nullptr, 0, nullptr, lossyOut);
            if (narrow <= 0 || lossy) {
                return std::nullopt;
            }
            std::string out(static_cast<std::size_t>(narrow), '\0');
            ::WideCharToMultiByte(a_to, 0, buffer.data(), wide, out.data(), narrow, nullptr, lossyOut);
            return lossy ? std::nullopt : std::optional<std::string>(std::move(out));
        }

        [[nodiscard]] std::string Hex(const std::string& a_bytes)
        {
            std::string out;
            for (const char c : a_bytes) {
                out += fmt::format("{:02X}", static_cast<unsigned char>(c));
            }
            return out;
        }
    }

    std::optional<std::string> Utf8ToAnsiExact(const std::string& a_text)
    {
        return Convert(a_text, CP_UTF8, CP_ACP);
    }

    std::optional<std::string> AnsiToUtf8Exact(const std::string& a_text)
    {
        return Convert(a_text, CP_ACP, CP_UTF8);
    }

    std::string Utf8ToAnsi(const std::string& a_text)
    {
        return Utf8ToAnsiExact(a_text).value_or(a_text);
    }

    const std::string& CopyRecordTexture(const CopyEntry& a_entry, std::size_t a_slot)
    {
        return a_entry.greyTextures[a_slot].empty() ? a_entry.sourceTextures[a_slot] : a_entry.greyTextures[a_slot];
    }

    std::string RecordTexturePath(const std::string& a_dataPath)
    {
        const std::size_t root = std::char_traits<char>::length(kTexturesRoot);
        return StartsWith(a_dataPath, kTexturesRoot) ? a_dataPath.substr(root) : a_dataPath;
    }

    bool ReadTextureSetParts(const std::filesystem::path& a_file, const nlohmann::json& a_item, FormSource& a_form,
        const std::string& a_where, std::string& a_why)
    {
        // Codex 12: the raw "model" first (the generic model reader would turn a bad one into "" with a warning)
        if (a_item.contains("model") && (!a_item["model"].is_string() || !a_item["model"].get<std::string>().empty())) {
            Json::Fail(a_file, a_where + ": a TXST has no model ('model' must be empty or absent)");
        }
        if (!a_item.contains("textures") || !a_item["textures"].is_array() || a_item["textures"].size() != kTxstSlots) {
            Json::Fail(a_file, fmt::format("{}: a TXST needs 'textures', {} strings (TX00..TX07, \"\" for an empty slot)",
                a_where, kTxstSlots));
        }
        if (!a_item.contains("hasDecal") || !a_item["hasDecal"].is_boolean()) {
            Json::Fail(a_file, a_where + ": a TXST needs 'hasDecal' (true when the source has DODT)");
        }
        a_form.hasDecal = a_item["hasDecal"].get<bool>();
        for (std::size_t i = 0; i < kTxstSlots; ++i) {
            const auto& slot = a_item["textures"][i];
            if (!slot.is_string()) {
                Json::Fail(a_file, fmt::format("{}: textures[{}] must be a string", a_where, i));
            }
            const auto text = Utf8ToAnsiExact(slot.get<std::string>());
            if (!text) {
                a_why = fmt::format("{} {}: '{}' does not convert to this system's code page without loss: no copy of this "
                    "texture set (its decal stays the source's)", a_form.source, kSlotTags[i], slot.get<std::string>());
                return false;
            }
            a_form.textures[i] = *text;
        }
        return true;
    }

    std::string TextureSetRecordBody(const CopyEntry& a_entry, const FormSource& a_source, const std::string& a_dnam)
    {
        Writer body;
        body.ZString("EDID", a_entry.editorId);
        const auto& b = a_source.bounds;
        body.Sub("OBND", Pack(b[0], b[1], b[2], b[3], b[4], b[5]));
        for (std::size_t i = 0; i < kTxstSlots; ++i) {
            if (const auto& text = CopyRecordTexture(a_entry, i); !text.empty()) {
                body.ZString(kSlotTags[i], text);
            }
        }
        if (a_entry.hasDecal) {
            body.Sub("DODT", std::string(kDodtSize, '\0'));  // the stub: the DLL assigns the live source's decal data
        }
        body.Sub("DNAM", a_dnam);
        return body.Data();
    }

    bool ReadTextureSetSubrecord(const std::string& a_tag, const std::string& a_bytes, EslForm& a_form)
    {
        if (a_tag == "DODT") {
            if (a_form.hasDodt) {
                return false;
            }
            a_form.hasDodt = true;
            a_form.dodt = a_bytes;
            return true;
        }
        for (std::size_t i = 0; i < kTxstSlots; ++i) {
            if (a_tag != kSlotTags[i]) {
                continue;
            }
            const std::uint32_t bit = 1u << i;
            if ((a_form.textureMask & bit) != 0 || a_bytes.empty() || a_bytes.back() != '\0') {
                return false;  // twice, or not a terminated string
            }
            a_form.textureMask |= bit;
            a_form.textures[i] = a_bytes.substr(0, a_bytes.size() - 1);
            return true;
        }
        return false;
    }

    std::vector<std::string> VerifyTextureSetRecord(const EslForm& a_back, const CopyEntry& a_entry, const FormSource& a_source,
        const std::string& a_dnam)
    {
        std::vector<std::string> problems;
        const auto add = [&](const std::string& a_what) {
            problems.push_back(fmt::format("{:06X} {}: {}", a_entry.localId, a_entry.editorId, a_what));
        };
        if (a_back.type != CopyTypeTag(CopyType::kTextureSet) || a_back.editorId != a_entry.editorId) {
            add(fmt::format("read back {} '{}'", a_back.type, a_back.editorId));
        }
        if (!a_back.hasBounds || a_back.bounds != a_source.bounds) {
            add("bounds differ");
        }
        for (std::size_t i = 0; i < kTxstSlots; ++i) {
            const auto& text = CopyRecordTexture(a_entry, i);
            const bool present = (a_back.textureMask & (1u << i)) != 0;
            if (present != !text.empty() || a_back.textures[i] != text) {
                add(fmt::format("{} read back '{}', written '{}'", kSlotTags[i], a_back.textures[i], text));
            }
        }
        if (a_back.hasDodt != a_entry.hasDecal || (a_back.hasDodt && a_back.dodt != std::string(kDodtSize, '\0'))) {
            add(fmt::format("DODT {} ({} bytes {}), the source {}", a_back.hasDodt ? "present" : "absent", a_back.dodt.size(),
                Hex(a_back.dodt), a_entry.hasDecal ? "has one" : "has none"));
        }
        if (!a_back.hasDnam || a_back.dnam != a_dnam) {
            add(fmt::format("DNAM {} bytes {}, written {}", a_back.dnam.size(), Hex(a_back.dnam), Hex(a_dnam)));
        }
        if (a_back.hasModel || a_back.hasData || a_back.hasSoundLevel || !a_back.otherSubrecords.empty()) {
            add(fmt::format("MODL {}, DATA {}, VNAM {}, other subrecords {} (none expected)", a_back.hasModel, a_back.hasData,
                a_back.hasSoundLevel, a_back.otherSubrecords.size()));
        }
        return problems;
    }

    void WriteTextureSetMapParts(nlohmann::ordered_json& a_item, const CopyEntry& a_entry)
    {
        nlohmann::ordered_json source = nlohmann::ordered_json::array();
        nlohmann::ordered_json grey = nlohmann::ordered_json::array();
        for (std::size_t i = 0; i < kTxstSlots; ++i) {
            // reviewer 1: JSON holds UTF-8 (a code page byte would make dump() throw); every slot converted the way in
            source.push_back(AnsiToUtf8Exact(a_entry.sourceTextures[i]).value_or(std::string()));
            grey.push_back(AnsiToUtf8Exact(a_entry.greyTextures[i]).value_or(std::string()));
        }
        a_item["sourceTextures"] = std::move(source);
        a_item["greyTextures"] = std::move(grey);
        a_item["hasDecal"] = a_entry.hasDecal;
    }

    void ReadTextureSetMapParts(const std::filesystem::path& a_file, const nlohmann::json& a_item, CopyEntry& a_entry,
        const std::string& a_where)
    {
        for (const char* key : { "sourceTextures", "greyTextures" }) {
            if (!a_item.contains(key)) {
                continue;  // ids only matter for a later run; a map without them still keeps them stable
            }
            const auto& list = a_item[key];
            if (!list.is_array() || list.size() != kTxstSlots) {
                Json::Fail(a_file, fmt::format("{}: '{}' must be {} strings", a_where, key, kTxstSlots));
            }
            auto& out = std::string_view(key) == "sourceTextures" ? a_entry.sourceTextures : a_entry.greyTextures;
            for (std::size_t i = 0; i < kTxstSlots; ++i) {
                if (!list[i].is_string()) {
                    Json::Fail(a_file, fmt::format("{}: {}[{}] must be a string", a_where, key, i));
                }
                out[i] = Utf8ToAnsi(list[i].get<std::string>());
            }
        }
        a_entry.hasDecal = a_item.contains("hasDecal") && a_item["hasDecal"].is_boolean() && a_item["hasDecal"].get<bool>();
    }
}
