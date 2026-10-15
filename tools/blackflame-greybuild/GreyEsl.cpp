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

#include "GreyBuild.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fmt/format.h>
#include <fstream>
#include <stdexcept>

namespace GreyBuild
{
    namespace
    {
        constexpr std::uint16_t kFormVersion = 44;         // Skyrim SE
        constexpr std::size_t kHeaderSize = 24;            // record and GRUP headers
        constexpr std::size_t kSubHeaderSize = 6;          // subrecord type + u16 size
        constexpr std::int16_t kBoundMin = -32768;         // OBND as Skyrim.esm's MPS ADDN records
        constexpr std::int16_t kBoundMax = 32767;
        constexpr const char* kAuthor = "Heart of Magic blackflame-greybuild";

        class Writer
        {
        public:
            void Bytes(const void* a_data, std::size_t a_size)
            {
                const auto* bytes = static_cast<const char*>(a_data);
                m_data.insert(m_data.end(), bytes, bytes + a_size);
            }
            template <class T>
            void Value(T a_value)
            {
                Bytes(&a_value, sizeof(T));
            }
            void Tag(const char* a_tag) { Bytes(a_tag, 4); }
            void Sub(const char* a_tag, const std::string& a_data)
            {
                if (a_data.size() > UINT16_MAX) {
                    throw std::runtime_error(fmt::format("subrecord {} is too long", a_tag));
                }
                Tag(a_tag);
                Value(static_cast<std::uint16_t>(a_data.size()));
                Bytes(a_data.data(), a_data.size());
            }
            void ZString(const char* a_tag, const std::string& a_text) { Sub(a_tag, a_text + '\0'); }
            const std::string& Data() const { return m_data; }

        private:
            std::string m_data;
        };

        template <class... T>
        [[nodiscard]] std::string Pack(T... a_values)
        {
            Writer writer;
            (writer.Value(a_values), ...);
            return writer.Data();
        }

        void RecordHeader(Writer& a_out, const char* a_type, std::size_t a_dataSize, std::uint32_t a_flags,
            std::uint32_t a_formId)
        {
            a_out.Tag(a_type);
            a_out.Value(static_cast<std::uint32_t>(a_dataSize));
            a_out.Value(a_flags);
            a_out.Value(a_formId);
            a_out.Value(std::uint32_t{ 0 });  // version control
            a_out.Value(kFormVersion);
            a_out.Value(std::uint16_t{ 0 });
        }

        [[nodiscard]] std::string AddonRecord(const AddonEntry& a_entry, std::uint32_t a_formId)
        {
            std::string model = a_entry.greyModel.substr(std::char_traits<char>::length(kMeshesRoot));
            Writer body;
            body.ZString("EDID", a_entry.greyEditorId);
            body.Sub("OBND", Pack(kBoundMin, kBoundMin, kBoundMin, kBoundMax, kBoundMax, kBoundMax));
            body.ZString("MODL", model);
            body.Sub("DATA", Pack(static_cast<std::int32_t>(a_entry.grey)));
            body.Sub("DNAM", Pack(a_entry.masterParticleCap, a_entry.dnamFlags));
            Writer record;
            RecordHeader(record, "ADDN", body.Data().size(), 0, a_formId);
            record.Bytes(body.Data().data(), body.Data().size());
            return record.Data();
        }

        class Reader
        {
        public:
            explicit Reader(std::string a_data) : m_data(std::move(a_data)) {}

            template <class T>
            T Value(std::size_t a_at) const
            {
                if (a_at + sizeof(T) > m_data.size()) {
                    throw std::runtime_error("plugin ends inside a record");
                }
                T value{};
                std::memcpy(&value, m_data.data() + a_at, sizeof(T));
                return value;
            }
            std::string Tag(std::size_t a_at) const
            {
                if (a_at + 4 > m_data.size()) {
                    throw std::runtime_error("plugin ends inside a tag");
                }
                return m_data.substr(a_at, 4);
            }
            std::string Slice(std::size_t a_at, std::size_t a_size) const { return m_data.substr(a_at, a_size); }
            std::size_t Size() const { return m_data.size(); }

        private:
            std::string m_data;
        };

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

    std::vector<std::uint32_t> EslFormIds(const std::vector<AddonEntry>& a_records)
    {
        std::vector<std::uint32_t> formIds;
        for (std::size_t i = 0; i < a_records.size(); ++i) {
            formIds.push_back(kFirstLightFormId + static_cast<std::uint32_t>(i));
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
        if (a_records.empty() || a_records.size() > kLastLightFormId - kFirstLightFormId + 1) {
            throw std::runtime_error(fmt::format("an ESL holds 1 to 2048 records, not {}", a_records.size()));
        }
        const std::vector<std::uint32_t> formIds = EslFormIds(a_records);
        std::string records;
        for (std::size_t i = 0; i < a_records.size(); ++i) {
            records += AddonRecord(a_records[i], formIds[i]);
        }

        Writer header;
        const auto count = static_cast<std::int32_t>(a_records.size() + 1);  // records and groups
        const std::uint32_t nextObjectId = *std::max_element(formIds.begin(), formIds.end()) + 1;
        header.Sub("HEDR", Pack(kHedrVersion, count, nextObjectId));
        header.ZString("CNAM", kAuthor);

        Writer file;
        RecordHeader(file, "TES4", header.Data().size(), kEslFlag, 0);
        file.Bytes(header.Data().data(), header.Data().size());
        file.Tag("GRUP");
        file.Value(static_cast<std::uint32_t>(kHeaderSize + records.size()));
        file.Tag("ADDN");
        file.Value(std::int32_t{ 0 });   // group type: top level
        file.Value(std::uint32_t{ 0 });  // stamp and version control
        file.Value(std::uint32_t{ 0 });
        file.Bytes(records.data(), records.size());

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
