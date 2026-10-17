// =============================================================================
// GreyEsl - a tiny ESL of grey ADDN records for one setup
// =============================================================================
// Skyrim SE plugin layout (form version 44, HEDR version 1.70): a TES4 record (HEDR, CNAM), then one
// top-level GRUP labelled ADDN with one record per grey index. Record header: type,
// data size, flags, FormID, version control, form version, unknown (24 bytes); GRUP
// header: "GRUP", total size, label, group type 0, stamp, unknown (24 bytes);
// subrecord: type, u16 size, data. ADDN subrecords as Skyrim.esm writes them: EDID,
// OBND (min -32768 x3, max 32767 x3), MODL, DATA (i32 index), DNAM (u16 cap, u16 flags).
// No masters: an ADDN links to nothing, so FormIDs 0x000800 up are the plugin's own.

#include "GreyEslIo.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fmt/format.h>
#include <fstream>
#include <set>
#include <stdexcept>

namespace GreyBuild
{
    namespace
    {
        using namespace EslIo;

        constexpr std::size_t kMaxLocalIdDigits = 8;       // "000800" as written; a u32 at most

        /// One ADDN record whose header starts at a_at and which must end by a_limit (its group's end).
        [[nodiscard]] EslAddon ReadAddon(const Reader& a_in, std::size_t a_at, std::size_t a_limit)
        {
            EslAddon addon;
            const auto size = a_in.Value<std::uint32_t>(a_at + 4);
            addon.formId = a_in.Value<std::uint32_t>(a_at + 12);
            std::size_t pos = a_at + kHeaderSize;
            const std::size_t end = pos + size;
            if (end > a_limit) {
                throw std::runtime_error(fmt::format("ADDN record at byte {} (size {}) runs past its group's end", a_at,
                    size));
            }
            while (pos < end) {
                if (pos + kSubHeaderSize > end) {
                    throw std::runtime_error(fmt::format("subrecord header at byte {} runs past its record's end", pos));
                }
                const std::string tag = a_in.Tag(pos);
                const auto length = a_in.Value<std::uint16_t>(pos + 4);
                const std::size_t data = pos + kSubHeaderSize;
                if (data + length > end) {
                    throw std::runtime_error(fmt::format("subrecord {} at byte {} (size {}) runs past its record's end",
                        tag, pos, length));
                }
                const auto text = [&] { return a_in.Slice(data, length > 0 ? length - 1u : 0u); };
                if (tag == "EDID") {
                    addon.editorId = text();
                } else if (tag == "MODL") {
                    addon.model = text();
                } else if (tag == "DATA") {
                    addon.index = a_in.Value<std::int32_t>(data);
                } else if (tag == "DNAM") {
                    addon.masterParticleCap = a_in.Value<std::uint16_t>(data);
                    addon.dnamFlags = a_in.Value<std::uint16_t>(data + 2);
                }
                pos = data + length;
            }
            return addon;
        }
    }

    std::string AddonRecord(const AddonEntry& a_entry, std::uint32_t a_formId)
    {
        if (!StartsWith(a_entry.greyModel, kMeshesRoot)) {
            throw std::runtime_error(fmt::format("ADDN '{}': its model '{}' is not under {}", a_entry.greyEditorId, a_entry.greyModel,
                kMeshesRoot));
        }
        std::string model = a_entry.greyModel.substr(std::char_traits<char>::length(kMeshesRoot));
        Writer body;
        body.ZString("EDID", a_entry.greyEditorId);
        body.Sub("OBND", Pack(kAddnBoundMin, kAddnBoundMin, kAddnBoundMin, kAddnBoundMax, kAddnBoundMax, kAddnBoundMax));
        body.ZString("MODL", model);
        body.Sub("DATA", Pack(static_cast<std::int32_t>(a_entry.grey)));
        body.Sub("DNAM", Pack(a_entry.masterParticleCap, a_entry.dnamFlags));
        Writer record;
        RecordHeader(record, "ADDN", body.Data().size(), 0, a_formId);
        record.Bytes(body.Data().data(), body.Data().size());
        return record.Data();
    }

    std::optional<std::uint32_t> LocalFormId(const std::string& a_form)
    {
        const auto bar = a_form.find('|');
        if (bar == std::string::npos || NormalizeDataPath(a_form.substr(0, bar)) != NormalizeDataPath(kLocalPluginName)) {
            return std::nullopt;
        }
        const std::string hex = a_form.substr(bar + 1);
        const bool isHex = !hex.empty() && hex.size() <= kMaxLocalIdDigits &&
                           hex.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
        const auto id = isHex ? static_cast<std::uint32_t>(std::stoul(hex, nullptr, 16)) : 0u;
        if (!isHex || id < kFirstLightFormId || id > kLastLightFormId) {
            throw std::runtime_error(fmt::format("'{}' names {} but its local id is not hex in {:03X}..{:03X}", a_form,
                kLocalPluginName, kFirstLightFormId, kLastLightFormId));
        }
        return id;
    }

    std::vector<std::uint32_t> EslFormIds(const std::vector<AddonEntry>& a_records)
    {
        std::vector<std::uint32_t> formIds;
        std::set<std::uint32_t> seen;
        for (const auto& record : a_records) {
            const auto id = LocalFormId(record.greyForm);
            if (!id) {
                throw std::runtime_error(fmt::format("grey record '{}' has form '{}', not one of {}",
                    record.greyEditorId, record.greyForm, kLocalPluginName));
            }
            if (!seen.insert(*id).second) {
                throw std::runtime_error(fmt::format("two grey records have the local id {:06X}", *id));
            }
            formIds.push_back(*id);
        }
        return formIds;
    }

    std::vector<std::string> VerifyAddonEsl(const std::vector<AddonEntry>& a_records,
        const std::filesystem::path& a_file)
    {
        std::vector<std::string> problems;
        std::uint32_t flags = 0;
        std::vector<EslAddon> back;
        try {
            back = ReadAddonEsl(a_file, flags);
        } catch (const std::exception& e) {
            return { fmt::format("cannot read it back: {}", e.what()) };
        }
        if ((flags & kEslFlag) == 0) {
            problems.push_back(fmt::format("header flags 0x{:X} lack the light plugin flag 0x{:X}", flags, kEslFlag));
        }
        if (back.size() != a_records.size()) {
            problems.push_back(fmt::format("{} ADDN record(s) read back, {} written", back.size(), a_records.size()));
            return problems;
        }
        const std::vector<std::uint32_t> formIds = EslFormIds(a_records);
        for (std::size_t i = 0; i < back.size(); ++i) {
            const AddonEntry& want = a_records[i];
            const EslAddon& got = back[i];
            const std::string model = want.greyModel.substr(std::char_traits<char>::length(kMeshesRoot));
            if (got.formId != formIds[i] || got.editorId != want.greyEditorId || got.model != model ||
                got.index != want.grey || got.masterParticleCap != want.masterParticleCap ||
                got.dnamFlags != want.dnamFlags) {
                problems.push_back(fmt::format(
                    "record {}: read {:06X} '{}' '{}' index {} DNAM {}/{}, written {:06X} '{}' '{}' index {} DNAM {}/{}",
                    i, got.formId, got.editorId, got.model, got.index, got.masterParticleCap, got.dnamFlags,
                    formIds[i], want.greyEditorId, model, want.grey, want.masterParticleCap, want.dnamFlags));
            }
        }
        return problems;
    }

    void WriteAddonEsl(const std::vector<AddonEntry>& a_records, const std::filesystem::path& a_file)
    {
        if (a_records.size() > kLastLightFormId - kFirstLightFormId + 1) {
            throw std::runtime_error(fmt::format("an ESL holds 0 to 2048 records, not {}", a_records.size()));
        }
        const std::vector<std::uint32_t> formIds = EslFormIds(a_records);
        std::string records;
        for (std::size_t i = 0; i < a_records.size(); ++i) {
            records += AddonRecord(a_records[i], formIds[i]);
        }

        Writer header;
        // records and groups; an empty plugin (a --forms run that needs no local record) has neither
        const auto count = static_cast<std::int32_t>(a_records.empty() ? 0 : a_records.size() + 1);
        const std::uint32_t nextObjectId = formIds.empty() ? kFirstLightFormId : *std::max_element(formIds.begin(), formIds.end()) + 1;
        header.Sub("HEDR", Pack(kHedrVersion, count, nextObjectId));
        header.ZString("CNAM", kAuthor);

        Writer file;
        RecordHeader(file, "TES4", header.Data().size(), kEslFlag, 0);
        file.Bytes(header.Data().data(), header.Data().size());
        if (!a_records.empty()) {
            TopGroup(file, "ADDN", records);
        }

        std::filesystem::create_directories(a_file.parent_path());
        std::ofstream out(a_file, std::ios::binary | std::ios::trunc);
        out.write(file.Data().data(), static_cast<std::streamsize>(file.Data().size()));
        out.close();  // the last buffer is flushed here; a full disk shows only now
        if (!out) {
            throw std::runtime_error(fmt::format("writing '{}' failed", a_file.string()));
        }
    }

    std::vector<EslAddon> ReadAddonEsl(const std::filesystem::path& a_file, std::uint32_t& a_headerFlags)
    {
        std::ifstream in(a_file, std::ios::binary);
        if (!in) {
            throw std::runtime_error(fmt::format("cannot open '{}'", a_file.string()));
        }
        const Reader reader{ std::string{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() } };
        if (reader.Tag(0) != "TES4") {
            throw std::runtime_error(fmt::format("'{}' is not a plugin (no TES4 record)", a_file.string()));
        }
        a_headerFlags = reader.Value<std::uint32_t>(8);
        std::vector<EslAddon> addons;
        std::size_t pos = kHeaderSize + reader.Value<std::uint32_t>(4);
        if (pos > reader.Size()) {
            throw std::runtime_error(fmt::format("'{}': the TES4 record runs past the end of the file", a_file.string()));
        }
        while (pos < reader.Size()) {
            if (reader.Tag(pos) != "GRUP") {
                throw std::runtime_error(fmt::format("'{}': expected a GRUP at byte {}", a_file.string(), pos));
            }
            const auto groupSize = reader.Value<std::uint32_t>(pos + 4);  // header included
            const std::size_t groupEnd = pos + groupSize;
            if (groupSize < kHeaderSize || groupEnd > reader.Size()) {
                throw std::runtime_error(fmt::format("'{}': GRUP at byte {} has size {} (file {} bytes)",
                    a_file.string(), pos, groupSize, reader.Size()));
            }
            const bool isAddn = reader.Tag(pos + 8) == "ADDN";
            std::size_t at = pos + kHeaderSize;
            while (isAddn && at < groupEnd) {
                if (reader.Tag(at) != "ADDN") {
                    throw std::runtime_error(fmt::format("'{}': expected an ADDN record at byte {}", a_file.string(), at));
                }
                try {
                    addons.push_back(ReadAddon(reader, at, groupEnd));
                } catch (const std::runtime_error& e) {
                    throw std::runtime_error(fmt::format("'{}': {}", a_file.string(), e.what()));
                }
                at += kHeaderSize + reader.Value<std::uint32_t>(at + 4);
            }
            pos = groupEnd;
        }
        return addons;
    }
}
