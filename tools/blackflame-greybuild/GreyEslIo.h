#pragma once

// =============================================================================
// GreyEslIo - byte writer and reader shared by the generated ESLs
// =============================================================================
// Skyrim SE plugin layout: record header = type, data size, flags, FormID, version
// control, form version, unknown (24 bytes); GRUP header = "GRUP", total size,
// label, group type, stamp, unknown (24 bytes); subrecord = type, u16 size, data.

#include "GreyBuild.h"

#include <cstring>
#include <fmt/format.h>
#include <stdexcept>
#include <string>

namespace GreyBuild::EslIo
{
    inline constexpr std::uint16_t kFormVersion = 44;  // Skyrim SE
    inline constexpr std::size_t kHeaderSize = 24;     // record and GRUP headers
    inline constexpr std::size_t kSubHeaderSize = 6;   // subrecord type + u16 size
    inline constexpr const char* kAuthor = "Heart of Magic blackflame-greybuild";

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

    inline void RecordHeader(Writer& a_out, const char* a_type, std::size_t a_dataSize, std::uint32_t a_flags,
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

    /// A top-level GRUP holding a_records (already serialised records)
    inline void TopGroup(Writer& a_out, const char* a_label, const std::string& a_records)
    {
        a_out.Tag("GRUP");
        a_out.Value(static_cast<std::uint32_t>(kHeaderSize + a_records.size()));
        a_out.Tag(a_label);
        a_out.Value(std::int32_t{ 0 });   // group type: top level
        a_out.Value(std::uint32_t{ 0 });  // stamp and version control
        a_out.Value(std::uint32_t{ 0 });
        a_out.Bytes(a_records.data(), a_records.size());
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
}
