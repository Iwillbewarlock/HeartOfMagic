#pragma once

// =============================================================================
// NIF VALUE NODES - the BSValueNode values (AddOnNode indices) of a NIF, from its header's block table (no NIF parser)
// =============================================================================
// Single launch needs to know which ADDN records a closure NIF names, to give only those an ADDN slot: the sample request
// of this load order lists 328 ADDN records not ours against a pool of 208, while the builder's run named 97 indices.
// A BSValueNode is a NiNode followed by `int Value` and `byte Value Node Flags` (nif.xml), so its value is the int32 five
// bytes before its block's end - and a 20.2.0.7 NIF header lists every block's type and size. Read here:
//   header line ("Gamebryo File Format, Version 20.2.0.7\n"), version u32 (0x14020007 only), endian u8 (1),
//   user version u32, block count u32, the Bethesda stream header (bs version u32, author ExportString, an unknown u32
//   when bs version > 130, process script ExportString when bs version < 131, export script ExportString, max filepath
//   ExportString when bs version >= 103), block type count u16 and SizedStrings, block type index u16 per block (bit 15
//   masked), block size u32 per block, string count u32, max string length u32, SizedStrings, group count u32 and u32s;
//   then the blocks, back to back by their sizes, which must stay inside the file.
// ExportString: u8 length then that many bytes; SizedString: u32 length then bytes. Every length is checked against what
// is left. Any other version, a cut or inconsistent header gives parsed = false (the caller treats the NIF's ADDN indices
// as unknown). C++17, no game types: blackflame-cosave-check tests it (--self-test, --nif-values <file>).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace BlackFlame::NifValueNodes
{
    inline constexpr std::string_view kHeaderPrefix = "Gamebryo File Format, Version ";
    inline constexpr std::size_t kMaxHeaderLine = 128;
    inline constexpr std::uint32_t kVersion20207 = 0x14020007;
    inline constexpr std::uint8_t kLittleEndian = 1;
    inline constexpr std::uint32_t kBsHasUnknownInt = 130;     // bs version > 130: an unknown u32 after the author
    inline constexpr std::uint32_t kBsHasProcessScript = 131;  // bs version < 131: a process script string
    inline constexpr std::uint32_t kBsHasMaxFilepath = 103;    // bs version >= 103: a max filepath string
    inline constexpr std::uint16_t kBlockTypeMask = 0x7FFF;    // bit 15 of a block type index is a flag (PhysX)
    inline constexpr std::string_view kValueNodeType = "BSValueNode";
    inline constexpr std::size_t kValueTail = sizeof(std::int32_t) + sizeof(std::uint8_t);  // Value, Value Node Flags
    inline constexpr std::uint32_t kMaxBlocks = 1u << 20;      // more blocks than this is not a NIF we read

    struct Result
    {
        bool parsed = false;
        std::string why;                   // why not parsed
        std::uint32_t blocks = 0;
        std::uint32_t valueNodes = 0;      // BSValueNode blocks
        std::vector<std::int32_t> values;  // their values, in block order
    };

    namespace Detail
    {
        class Cursor
        {
        public:
            Cursor(const unsigned char* a_bytes, std::size_t a_size) : m_bytes(a_bytes), m_size(a_size) {}

            template <class T>
            [[nodiscard]] bool Read(T& a_out)
            {
                if (m_size - m_at < sizeof(T)) {
                    return false;
                }
                std::memcpy(&a_out, m_bytes + m_at, sizeof(T));
                m_at += sizeof(T);
                return true;
            }

            [[nodiscard]] bool Skip(std::uint64_t a_count)
            {
                if (m_size - m_at < a_count) {
                    return false;
                }
                m_at += static_cast<std::size_t>(a_count);
                return true;
            }

            [[nodiscard]] bool Text(std::uint64_t a_count, std::string& a_out)
            {
                if (m_size - m_at < a_count) {
                    return false;
                }
                a_out.assign(reinterpret_cast<const char*>(m_bytes + m_at), static_cast<std::size_t>(a_count));
                m_at += static_cast<std::size_t>(a_count);
                return true;
            }

            [[nodiscard]] bool ExportString()
            {
                std::uint8_t length = 0;
                return Read(length) && Skip(length);
            }

            [[nodiscard]] bool SizedString(std::string* a_out = nullptr)
            {
                std::uint32_t length = 0;
                if (!Read(length)) {
                    return false;
                }
                std::string ignored;
                return Text(length, a_out ? *a_out : ignored);
            }

            [[nodiscard]] std::size_t At() const noexcept { return m_at; }
            [[nodiscard]] std::size_t Left() const noexcept { return m_size - m_at; }
            void Set(std::size_t a_at) noexcept { m_at = a_at; }

        private:
            const unsigned char* m_bytes;
            std::size_t m_size;
            std::size_t m_at = 0;
        };
    }

    [[nodiscard]] inline Result Read(const unsigned char* a_bytes, std::size_t a_size)
    {
        Result out;
        const auto fail = [&](const char* a_why) {
            out.parsed = false;
            out.why = a_why;
            out.values.clear();
            return out;
        };
        if (!a_bytes || a_size < kHeaderPrefix.size()) {
            return fail("shorter than a NIF header line");
        }
        const std::string_view text(reinterpret_cast<const char*>(a_bytes), a_size < kMaxHeaderLine ? a_size : kMaxHeaderLine);
        const auto newline = text.find('\n');
        if (text.substr(0, kHeaderPrefix.size()) != kHeaderPrefix || newline == std::string_view::npos) {
            return fail("no Gamebryo header line");
        }
        Detail::Cursor in(a_bytes, a_size);
        in.Set(newline + 1);
        std::uint32_t version = 0;
        std::uint8_t endian = 0;
        std::uint32_t userVersion = 0;
        std::uint32_t blockCount = 0;
        if (!in.Read(version) || !in.Read(endian) || !in.Read(userVersion) || !in.Read(blockCount)) {
            return fail("the header is cut short");
        }
        if (version != kVersion20207) {
            return fail("not a 20.2.0.7 NIF");
        }
        if (endian != kLittleEndian) {
            return fail("not little-endian");
        }
        if (blockCount > kMaxBlocks) {
            return fail("an implausible block count");
        }
        std::uint32_t bsVersion = 0;
        if (!in.Read(bsVersion) || !in.ExportString()) {
            return fail("the Bethesda header is cut short");
        }
        std::uint32_t unknown = 0;
        if ((bsVersion > kBsHasUnknownInt && !in.Read(unknown)) || (bsVersion < kBsHasProcessScript && !in.ExportString()) ||
            !in.ExportString() || (bsVersion >= kBsHasMaxFilepath && !in.ExportString())) {
            return fail("the Bethesda header is cut short");
        }
        std::uint16_t typeCount = 0;
        if (!in.Read(typeCount)) {
            return fail("no block type count");
        }
        std::vector<bool> isValueNode(typeCount, false);
        for (std::uint16_t i = 0; i < typeCount; ++i) {
            std::string name;
            if (!in.SizedString(&name)) {
                return fail("the block type list is cut short");
            }
            isValueNode[i] = name == kValueNodeType;
        }
        if (in.Left() / (sizeof(std::uint16_t) + sizeof(std::uint32_t)) < blockCount) {
            return fail("the block index and size lists are cut short");
        }
        std::vector<std::uint16_t> types(blockCount);
        for (auto& type : types) {
            (void)in.Read(type);
            type = static_cast<std::uint16_t>(type & kBlockTypeMask);
            if (type >= typeCount) {
                return fail("a block type index past the type list");
            }
        }
        std::vector<std::uint32_t> sizes(blockCount);
        for (auto& size : sizes) {
            (void)in.Read(size);
        }
        std::uint32_t stringCount = 0;
        std::uint32_t maxString = 0;
        if (!in.Read(stringCount) || !in.Read(maxString)) {
            return fail("no string table");
        }
        for (std::uint32_t i = 0; i < stringCount; ++i) {
            if (!in.SizedString()) {
                return fail("the string table is cut short");
            }
        }
        std::uint32_t groupCount = 0;
        if (!in.Read(groupCount) || !in.Skip(static_cast<std::uint64_t>(groupCount) * sizeof(std::uint32_t))) {
            return fail("the group list is cut short");
        }
        for (std::uint32_t b = 0; b < blockCount; ++b) {
            const std::size_t start = in.At();
            if (!in.Skip(sizes[b])) {
                return fail("a block runs past the end of the file");
            }
            if (!isValueNode[types[b]]) {
                continue;
            }
            if (sizes[b] < kValueTail) {
                return fail("a BSValueNode block shorter than its value");
            }
            std::int32_t value = 0;
            std::memcpy(&value, a_bytes + start + sizes[b] - kValueTail, sizeof(value));
            out.values.push_back(value);
            ++out.valueNodes;
        }
        out.blocks = blockCount;
        out.parsed = true;
        return out;
    }
}
