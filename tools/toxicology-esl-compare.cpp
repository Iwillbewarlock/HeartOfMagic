// The English / Korean ESL rule of toxicology-esl-compare.h. Format: uncompressed TES4 plugin, 24-byte record and
// GRUP headers (type, size, then 16 more bytes), 6-byte subrecord headers (type, u16 size), little endian.
// A record's size counts its data only; a GRUP's size counts its own header too.

#include "toxicology-esl-compare.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace ToxicologyEsl
{
    namespace
    {
        constexpr std::size_t kHeaderSize = 24;
        constexpr std::size_t kSizeFieldOffset = 4;
        constexpr std::size_t kSizeFieldEnd = 8;
        constexpr std::size_t kFlagsOffset = 8;
        constexpr std::size_t kFormIdOffset = 12;
        constexpr std::size_t kSubrecordHeaderSize = 6;
        constexpr std::size_t kSubrecordSizeOffset = 4;
        constexpr int kMaxGroupDepth = 8;  // the game nests at most a few levels (top, block, sub-block, cell children)
        constexpr std::uint32_t kCompressedFlag = 0x00040000;
        constexpr std::uint32_t kLocalIdMask = 0x00FFFFFF;
        constexpr int kMasterIndexShift = 24;
        constexpr std::uint32_t kMaxMasters = 0xFF;  // index 0xFF is the file itself when it has 255 masters

        // The subrecords that hold text and may differ (ESL.md "Korean ESL"): local FormID and type
        struct TextSubrecord
        {
            std::uint32_t localId;
            std::string_view type;
        };

        constexpr std::array<TextSubrecord, 10> kTextSubrecords{ {
            { 0x801, "FULL" },
            { 0x801, "DNAM" },
            { 0x802, "FULL" },
            { 0x803, "FULL" },
            { 0x803, "DESC" },
            { 0x804, "FULL" },
            { 0x804, "DNAM" },
            { 0x805, "FULL" },
            { 0x806, "FULL" },
            { 0x807, "FULL" },
        } };

        std::uint32_t ReadU32(std::span<const std::uint8_t> a_bytes, std::size_t a_at)
        {
            std::uint32_t value = 0;
            for (std::size_t i = 0; i < 4; ++i) value |= std::uint32_t(a_bytes[a_at + i]) << (8 * i);
            return value;
        }

        std::uint16_t ReadU16(std::span<const std::uint8_t> a_bytes, std::size_t a_at)
        {
            return std::uint16_t(a_bytes[a_at] | (a_bytes[a_at + 1] << 8));
        }

        std::string ReadType(std::span<const std::uint8_t> a_bytes, std::size_t a_at)
        {
            return std::string(reinterpret_cast<const char*>(a_bytes.data() + a_at), 4);
        }

        std::string Hex(std::uint64_t a_value)
        {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "0x%llX", static_cast<unsigned long long>(a_value));
            return buf;
        }

        std::string HexU32(std::uint32_t a_value)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "0x%08X", a_value);
            return buf;
        }

        struct Parser
        {
            std::span<const std::uint8_t> bytes;
            Plugin& out;

            std::string Record(std::size_t a_at, std::size_t a_end)
            {
                if (a_end - a_at < kHeaderSize) return "record header cut short at " + Hex(a_at);
                const auto type = ReadType(bytes, a_at);
                const auto dataSize = ReadU32(bytes, a_at + kSizeFieldOffset);
                const auto flags = ReadU32(bytes, a_at + kFlagsOffset);
                const auto formId = ReadU32(bytes, a_at + kFormIdOffset);
                const auto where = type + " " + HexU32(formId) + " at " + Hex(a_at);
                // Only the file header is a TES4, and its FormID is 0, so it can never be taken for one of the text records
                if ((type == "TES4") != (a_at == 0)) return where + ": a TES4 record belongs only at the start of the file";
                if (type == "TES4" && formId != 0) return where + ": the TES4 header's FormID must be 0";
                if (flags & kCompressedFlag) return where + " is compressed (flag 0x00040000); this check reads uncompressed records only";
                if (dataSize > a_end - a_at - kHeaderSize) return where + " has data size " + Hex(dataSize) + ", past the end of its group or file (record sizes do not add up)";

                Node node{ Node::Kind::kRecord, type, formId, a_at, {} };
                const std::size_t end = a_at + kHeaderSize + dataSize;
                std::size_t at = a_at + kHeaderSize;
                while (at < end) {
                    if (end - at < kSubrecordHeaderSize) return where + ": subrecord header cut short at " + Hex(at) + " (record sizes do not add up)";
                    const auto subType = ReadType(bytes, at);
                    const std::size_t subSize = ReadU16(bytes, at + kSubrecordSizeOffset);
                    if (subType == "XXXX") return where + ": XXXX subrecord at " + Hex(at) + "; this check does not read oversized subrecords";
                    if (subSize > end - at - kSubrecordHeaderSize) return where + ": " + subType + " at " + Hex(at) + " has size " + Hex(subSize) + ", past the end of the record (record sizes do not add up)";
                    node.subrecords.push_back({ subType, at + kSubrecordHeaderSize, subSize });
                    at += kSubrecordHeaderSize + subSize;
                }
                if (a_at == 0) {  // the file header: its masters fix which FormIDs are the file's own
                    for (const auto& sub : node.subrecords) out.masterCount += sub.type == "MAST";
                    if (out.masterCount > kMaxMasters) return where + ": " + std::to_string(out.masterCount) + " masters, more than a FormID's index byte can name";
                }
                out.nodes.push_back(std::move(node));
                return {};
            }

            std::string Group(std::size_t a_at, std::size_t a_end, int a_depth)
            {
                if (a_depth > kMaxGroupDepth) return "GRUP at " + Hex(a_at) + " nested deeper than " + std::to_string(kMaxGroupDepth) + " levels";
                if (a_end - a_at < kHeaderSize) return "GRUP header cut short at " + Hex(a_at);
                const auto size = ReadU32(bytes, a_at + kSizeFieldOffset);
                if (size < kHeaderSize || size > a_end - a_at) return "GRUP at " + Hex(a_at) + " has size " + Hex(size) + ", which does not fit its parent (group sizes do not add up)";
                out.nodes.push_back({ Node::Kind::kGroupBegin, "GRUP", 0, a_at, {} });
                const std::size_t end = a_at + size;
                std::size_t at = a_at + kHeaderSize;
                while (at < end) {
                    if (end - at < kHeaderSize) return "GRUP at " + Hex(a_at) + ": " + std::to_string(end - at) + " bytes left over at " + Hex(at) + " (group sizes do not add up)";
                    const bool isGroup = ReadType(bytes, at) == "GRUP";
                    const std::size_t next = isGroup ? at + ReadU32(bytes, at + kSizeFieldOffset) : at + kHeaderSize + ReadU32(bytes, at + kSizeFieldOffset);
                    if (auto error = isGroup ? Group(at, end, a_depth + 1) : Record(at, end); !error.empty()) return error;
                    at = next;
                }
                out.nodes.push_back({ Node::Kind::kGroupEnd, "GRUP", 0, a_at, {} });
                return {};
            }
        };

        bool SameHeader(std::span<const std::uint8_t> a_en, std::size_t a_enAt, std::span<const std::uint8_t> a_ko, std::size_t a_koAt)
        {
            // Everything but the size field, which follows from the content and is checked by Parse
            return std::memcmp(a_en.data() + a_enAt, a_ko.data() + a_koAt, kSizeFieldOffset) == 0 &&
                   std::memcmp(a_en.data() + a_enAt + kSizeFieldEnd, a_ko.data() + a_koAt + kSizeFieldEnd, kHeaderSize - kSizeFieldEnd) == 0;
        }

        const TextSubrecord* FindText(std::uint32_t a_formId, std::uint32_t a_masterCount, std::string_view a_type)
        {
            if ((a_formId >> kMasterIndexShift) != a_masterCount) return nullptr;  // not one of the file's own records
            for (const auto& text : kTextSubrecords) {
                if (text.localId == (a_formId & kLocalIdMask) && text.type == a_type) return &text;
            }
            return nullptr;
        }

        // "" when the Korean text is UTF-8 ending in exactly one NUL with no other NUL
        std::string CheckKoreanText(std::span<const std::uint8_t> a_data)
        {
            if (a_data.empty() || a_data.back() != 0) return "does not end in a NUL";
            const auto text = a_data.first(a_data.size() - 1);
            if (const auto nul = std::ranges::find(text, std::uint8_t(0)); nul != text.end()) {
                return "has a NUL at byte " + std::to_string(nul - text.begin()) + " before its end";
            }
            if (!IsValidUtf8(text)) return "is not valid UTF-8";
            return {};
        }

        void CompareRecord(std::span<const std::uint8_t> a_en, const Node& a_enNode, std::span<const std::uint8_t> a_ko, const Node& a_koNode,
            std::uint32_t a_masterCount, std::size_t& a_sameText, std::vector<std::string>& a_problems)
        {
            const auto record = "record " + a_enNode.type + " " + HexU32(a_enNode.formId);
            if (!SameHeader(a_en, a_enNode.offset, a_ko, a_koNode.offset)) {
                a_problems.push_back(record + ": header differs (flags, version or timestamp; English at " + Hex(a_enNode.offset) + ", Korean at " + Hex(a_koNode.offset) + ")");
            }
            if (a_enNode.subrecords.size() != a_koNode.subrecords.size()) {
                a_problems.push_back(record + ": " + std::to_string(a_enNode.subrecords.size()) + " subrecords in English, " + std::to_string(a_koNode.subrecords.size()) + " in Korean");
                return;
            }
            for (std::size_t j = 0; j < a_enNode.subrecords.size(); ++j) {
                const auto& en = a_enNode.subrecords[j];
                const auto& ko = a_koNode.subrecords[j];
                const auto sub = record + " subrecord " + std::to_string(j) + " " + en.type;
                if (en.type != ko.type) {
                    a_problems.push_back(record + " subrecord " + std::to_string(j) + ": " + en.type + " in English, " + ko.type + " in Korean (order or set differs)");
                    return;
                }
                if (FindText(a_enNode.formId, a_masterCount, en.type)) {
                    const auto koText = a_ko.subspan(ko.dataOffset, ko.size);
                    if (auto bad = CheckKoreanText(koText); !bad.empty()) a_problems.push_back(sub + ": the Korean text " + bad);
                    a_sameText += std::ranges::equal(a_en.subspan(en.dataOffset, en.size), koText);
                    continue;
                }
                if (en.size != ko.size) {
                    a_problems.push_back(sub + ": " + std::to_string(en.size) + " bytes in English, " + std::to_string(ko.size) + " in Korean (not one of the text subrecords)");
                    continue;
                }
                const auto enData = a_en.subspan(en.dataOffset, en.size);
                const auto koData = a_ko.subspan(ko.dataOffset, ko.size);
                const auto [enAt, koAt] = std::ranges::mismatch(enData, koData);
                if (enAt != enData.end()) {
                    const auto i = static_cast<std::size_t>(enAt - enData.begin());
                    a_problems.push_back(sub + ": byte " + std::to_string(i) + " is " + Hex(*enAt) + " in English, " + Hex(*koAt) + " in Korean (not one of the text subrecords)");
                }
            }
        }

        // Each text subrecord must be in each file exactly once, so a text subrecord the English file dropped,
        // or one of them moved to another record, is a failure rather than nothing to compare
        void CountTextSubrecords(const Plugin& a_plugin, std::string_view a_file, std::vector<std::string>& a_problems)
        {
            for (const auto& text : kTextSubrecords) {
                const auto formId = OwnFormId(a_plugin, text.localId);
                std::size_t count = 0;
                for (const auto& node : a_plugin.nodes) {
                    if (node.kind != Node::Kind::kRecord || node.formId != formId) continue;
                    count += std::ranges::count_if(node.subrecords, [&](const Subrecord& a_sub) { return a_sub.type == text.type; });
                }
                if (count != 1) {
                    a_problems.push_back(std::string(a_file) + ": " + std::string(text.type) + " of " + HexU32(formId) + " found " + std::to_string(count) + " times, expected once");
                }
            }
        }
    }

    bool IsValidUtf8(std::span<const std::uint8_t> a_bytes)
    {
        std::size_t i = 0;
        while (i < a_bytes.size()) {
            const std::uint8_t lead = a_bytes[i];
            std::size_t length = 0;
            std::uint32_t code = 0;
            std::uint32_t minimum = 0;
            if (lead < 0x80) {
                ++i;
                continue;
            } else if ((lead & 0xE0) == 0xC0) {
                length = 2, code = lead & 0x1F, minimum = 0x80;
            } else if ((lead & 0xF0) == 0xE0) {
                length = 3, code = lead & 0x0F, minimum = 0x800;
            } else if ((lead & 0xF8) == 0xF0) {
                length = 4, code = lead & 0x07, minimum = 0x10000;
            } else {
                return false;  // a continuation byte with no lead, or 0xF8-0xFF
            }
            if (a_bytes.size() - i < length) return false;
            for (std::size_t k = 1; k < length; ++k) {
                const std::uint8_t next = a_bytes[i + k];
                if ((next & 0xC0) != 0x80) return false;
                code = (code << 6) | (next & 0x3F);
            }
            if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return false;
            i += length;
        }
        return true;
    }

    std::uint32_t OwnFormId(const Plugin& a_plugin, std::uint32_t a_localId)
    {
        return (a_plugin.masterCount << kMasterIndexShift) | a_localId;
    }

    std::string Parse(std::span<const std::uint8_t> a_bytes, Plugin& a_out)
    {
        a_out = {};
        Parser parser{ a_bytes, a_out };
        if (a_bytes.size() < kHeaderSize || ReadType(a_bytes, 0) != "TES4") return "does not start with a TES4 record";
        if (auto error = parser.Record(0, a_bytes.size()); !error.empty()) return error;
        std::size_t at = kHeaderSize + ReadU32(a_bytes, kSizeFieldOffset);
        while (at < a_bytes.size()) {
            if (a_bytes.size() - at < kHeaderSize || ReadType(a_bytes, at) != "GRUP") return "something other than a GRUP at " + Hex(at) + " after the TES4 record";
            const std::size_t next = at + ReadU32(a_bytes, at + kSizeFieldOffset);
            if (auto error = parser.Group(at, a_bytes.size(), 1); !error.empty()) return error;
            at = next;
        }
        return {};
    }

    std::vector<std::string> CompareToxicologyEsl(std::span<const std::uint8_t> a_english, std::span<const std::uint8_t> a_korean)
    {
        std::vector<std::string> problems;
        Plugin en;
        Plugin ko;
        if (auto error = Parse(a_english, en); !error.empty()) problems.push_back("English: " + error);
        if (auto error = Parse(a_korean, ko); !error.empty()) problems.push_back("Korean: " + error);
        if (!problems.empty()) return problems;

        CountTextSubrecords(en, "English", problems);
        CountTextSubrecords(ko, "Korean", problems);

        std::size_t sameText = 0;
        const std::size_t shared = std::min(en.nodes.size(), ko.nodes.size());
        for (std::size_t i = 0; i < shared; ++i) {
            const auto& enNode = en.nodes[i];
            const auto& koNode = ko.nodes[i];
            if (enNode.kind != koNode.kind || enNode.type != koNode.type || enNode.formId != koNode.formId) {
                const auto describe = [](const Node& a_node) {
                    if (a_node.kind == Node::Kind::kGroupEnd) return "the end of the GRUP at " + Hex(a_node.offset);
                    if (a_node.kind == Node::Kind::kGroupBegin) return "a GRUP at " + Hex(a_node.offset);
                    return "record " + a_node.type + " " + HexU32(a_node.formId) + " at " + Hex(a_node.offset);
                };
                problems.push_back("structure differs: English has " + describe(enNode) + ", Korean has " + describe(koNode));
                return problems;  // past this point the two files no longer line up
            }
            if (enNode.kind == Node::Kind::kGroupBegin && !SameHeader(a_english, enNode.offset, a_korean, koNode.offset)) {
                problems.push_back("GRUP header differs (label, type or timestamp; English at " + Hex(enNode.offset) + ", Korean at " + Hex(koNode.offset) + ")");
            }
            if (enNode.kind == Node::Kind::kRecord) CompareRecord(a_english, enNode, a_korean, koNode, en.masterCount, sameText, problems);
        }
        if (en.nodes.size() != ko.nodes.size()) {
            problems.push_back("structure differs: " + std::to_string(en.nodes.size()) + " records and group bounds in English, " + std::to_string(ko.nodes.size()) + " in Korean");
        }
        // The rule lets the text subrecords differ, but all of them the same as in English means the English file was copied over
        // the Korean one (a merge resolved the wrong way), which would ship English text in the Korean zip
        if (sameText == kTextSubrecords.size()) {
            problems.push_back("all " + std::to_string(kTextSubrecords.size()) + " text subrecords are the same as in the English file: the Korean file holds the English text");
        }
        return problems;
    }
}
