// toxicology-test - the English / Korean ESL check (toxicology-esl-compare.h) on the committed pair in
// plugins/Toxicology/data (TOXICOLOGY_DATA_DIR, set by tools/CMakeLists.txt) and on Korean copies broken in memory.
// The bytes to break are found through the parser, so the checks follow the files when they change.
// toxicology-test.cpp runs them after its own checks, with the same PASS / FAIL lines and exit code.

#include "toxicology-esl-compare.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef TOXICOLOGY_DATA_DIR
#    error "TOXICOLOGY_DATA_DIR must name plugins/Toxicology/data"
#endif

namespace
{
    using Bytes = std::vector<std::uint8_t>;
    using ToxicologyEsl::Node;

    constexpr std::uint32_t kEffectId = 0x801;
    constexpr std::uint32_t kBookId = 0x803;
    constexpr std::uint32_t kPowerId = 0x805;
    constexpr std::uint32_t kMessageOnId = 0x806;
    constexpr std::uint8_t kNotUtf8 = 0xFF;
    // 24-byte record and GRUP headers: type, size, then a record's flags or a GRUP's label
    constexpr std::size_t kHeaderSize = 24;
    constexpr std::size_t kSizeFieldOffset = 4;
    constexpr std::size_t kFlagsOrLabelOffset = 8;
    constexpr int kTooDeep = 12;

    std::optional<Bytes> Load(const char* a_relative)
    {
        const std::string path = std::string(TOXICOLOGY_DATA_DIR) + "/" + a_relative;
        std::ifstream file(path, std::ios::binary);
        if (!file) return std::nullopt;
        return Bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    struct Pair
    {
        Bytes english;
        Bytes korean;
    };

    // The committed pair, or "" filled with why it could not be read
    std::optional<Pair> LoadPair(std::string& a_error)
    {
        auto english = Load("HeartOfMagic_Toxicology.esp");
        auto korean = Load("ko/HeartOfMagic_Toxicology.esp");
        if (!english || !korean) {
            a_error = std::string("cannot read the ESL pair under ") + TOXICOLOGY_DATA_DIR;
            return std::nullopt;
        }
        return Pair{ std::move(*english), std::move(*korean) };
    }

    std::string Join(const std::vector<std::string>& a_lines)
    {
        std::string out;
        for (const auto& line : a_lines) out += (out.empty() ? "" : " | ") + line;
        return out;
    }

    struct Found
    {
        std::size_t recordOffset;
        std::size_t dataOffset;
        std::size_t size;
    };

    // The record header and the data of a_type in the file's own record a_localId
    std::optional<Found> FindSubrecord(const Bytes& a_bytes, std::uint32_t a_localId, std::string_view a_type)
    {
        ToxicologyEsl::Plugin plugin;
        if (!ToxicologyEsl::Parse(a_bytes, plugin).empty()) return std::nullopt;
        const std::uint32_t formId = ToxicologyEsl::OwnFormId(plugin, a_localId);
        for (const auto& node : plugin.nodes) {
            if (node.kind != Node::Kind::kRecord || node.formId != formId) continue;
            for (const auto& sub : node.subrecords) {
                if (sub.type == a_type) return Found{ node.offset, sub.dataOffset, sub.size };
            }
        }
        return std::nullopt;
    }

    // "" when comparing the two gives a problem line that holds every one of a_want
    // (so a failure for some other reason does not count)
    std::string ExpectProblem(const Bytes& a_english, const Bytes& a_korean, const std::vector<std::string_view>& a_want)
    {
        const auto problems = ToxicologyEsl::CompareToxicologyEsl(a_english, a_korean);
        for (const auto& problem : problems) {
            bool all = true;
            for (const auto want : a_want) all = all && problem.find(want) != std::string::npos;
            if (all) return {};
        }
        std::string wanted;
        for (const auto want : a_want) wanted += "\"" + std::string(want) + "\" ";
        return "no problem line with " + wanted + "; got: " + (problems.empty() ? std::string("none") : Join(problems));
    }

    using ByteChange = std::uint8_t (*)(std::uint8_t);

    // Changes the first data byte of a_type in the Korean file's record a_localId, then wants a_want
    std::string ExpectFailure(std::uint32_t a_localId, std::string_view a_type, ByteChange a_change, const std::vector<std::string_view>& a_want)
    {
        std::string error;
        auto pair = LoadPair(error);
        if (!pair) return error;
        const auto sub = FindSubrecord(pair->korean, a_localId, a_type);
        if (!sub || sub->size == 0) return "no " + std::string(a_type) + " with data in the Korean file's record " + std::to_string(a_localId);
        auto& byte = pair->korean[sub->dataOffset];
        byte = a_change(byte);
        return ExpectProblem(pair->english, pair->korean, a_want);
    }

    std::string EslCommittedPairMatches()
    {
        std::string error;
        const auto pair = LoadPair(error);
        if (!pair) return error;
        return Join(ToxicologyEsl::CompareToxicologyEsl(pair->english, pair->korean));
    }

    // A non-text byte: the first byte of the book's DATA (its flags) changed in the Korean copy only
    std::string EslNonTextByteFails()
    {
        return ExpectFailure(kBookId, "DATA", [](std::uint8_t a_b) { return std::uint8_t(a_b ^ 0x01); },
            { "BOOK 0x01000803", "DATA", "byte 0 is", "not one of the text subrecords" });
    }

    std::string EslFullNotUtf8Fails()
    {
        return ExpectFailure(kEffectId, "FULL", [](std::uint8_t) { return kNotUtf8; }, { "MGEF 0x01000801", "FULL", "not valid UTF-8" });
    }

    // A text subrecord added in 1.1: the message's FULL no longer UTF-8
    std::string EslMessageFullNotUtf8Fails()
    {
        return ExpectFailure(kMessageOnId, "FULL", [](std::uint8_t) { return kNotUtf8; }, { "MESG 0x01000806", "FULL", "not valid UTF-8" });
    }

    // A non-text subrecord of a 1.1 record: the power's SPIT (its base cost) changed in the Korean copy only
    std::string EslPowerSpitByteFails()
    {
        return ExpectFailure(kPowerId, "SPIT", [](std::uint8_t a_b) { return std::uint8_t(a_b ^ 0x01); },
            { "SPEL 0x01000805", "SPIT", "byte 0 is", "not one of the text subrecords" });
    }

    std::string EslTextInnerNulFails()
    {
        return ExpectFailure(kBookId, "DESC", [](std::uint8_t) { return std::uint8_t(0); }, { "BOOK 0x01000803", "DESC", "has a NUL at byte 0" });
    }

    // A record size that no longer adds up: the Korean MGEF's data size changed by one
    std::string EslRecordSizeMismatchFails()
    {
        std::string error;
        auto pair = LoadPair(error);
        if (!pair) return error;
        const auto sub = FindSubrecord(pair->korean, kEffectId, "EDID");
        if (!sub) return "no EDID in the Korean file's MGEF";
        pair->korean[sub->recordOffset + kSizeFieldOffset] ^= 0x01;
        const auto problems = ToxicologyEsl::CompareToxicologyEsl(pair->english, pair->korean);
        for (const auto& problem : problems) {
            if (problem.starts_with("Korean: ") && problem.find("do not add up") != std::string::npos) return {};
        }
        return "no size problem reported; got: " + (problems.empty() ? std::string("none") : Join(problems));
    }

    // A record header byte outside the size field: a flag bit of the Korean MGEF
    std::string EslRecordHeaderFails()
    {
        std::string error;
        auto pair = LoadPair(error);
        if (!pair) return error;
        const auto sub = FindSubrecord(pair->korean, kEffectId, "EDID");
        if (!sub) return "no EDID in the Korean file's MGEF";
        pair->korean[sub->recordOffset + kFlagsOrLabelOffset] ^= 0x02;
        return ExpectProblem(pair->english, pair->korean, { "MGEF 0x01000801: header differs" });
    }

    // A GRUP header byte outside the size field: the first group's label
    std::string EslGroupHeaderFails()
    {
        std::string error;
        auto pair = LoadPair(error);
        if (!pair) return error;
        ToxicologyEsl::Plugin plugin;
        if (auto parseError = ToxicologyEsl::Parse(pair->korean, plugin); !parseError.empty()) return parseError;
        const auto group = std::ranges::find_if(plugin.nodes, [](const Node& a_node) { return a_node.kind == Node::Kind::kGroupBegin; });
        if (group == plugin.nodes.end()) return "no GRUP in the Korean file";
        pair->korean[group->offset + kFlagsOrLabelOffset] ^= 0x01;
        return ExpectProblem(pair->english, pair->korean, { "GRUP header differs" });
    }

    // One byte after the last group
    std::string EslTrailingByteFails()
    {
        std::string error;
        auto pair = LoadPair(error);
        if (!pair) return error;
        pair->korean.push_back(0);
        return ExpectProblem(pair->english, pair->korean, { "Korean: ", "something other than a GRUP" });
    }

    // The English file copied over the Korean one: within the rule's letter, but it would ship English text
    std::string EslKoreanIsEnglishCopyFails()
    {
        std::string error;
        const auto pair = LoadPair(error);
        if (!pair) return error;
        return ExpectProblem(pair->english, pair->english, { "holds the English text" });
    }

    // GRUPs nested deeper than any plugin needs: refused with a message, not a stack overflow
    std::string EslDeepNestingFails()
    {
        Bytes bytes(kHeaderSize * (kTooDeep + 1), 0);
        const auto put = [&](std::size_t a_at, std::string_view a_type, std::uint32_t a_size) {
            std::ranges::copy(a_type, bytes.begin() + a_at);
            for (std::size_t i = 0; i < 4; ++i) bytes[a_at + kSizeFieldOffset + i] = std::uint8_t(a_size >> (8 * i));
        };
        put(0, "TES4", 0);
        for (int level = 0; level < kTooDeep; ++level) {
            put(kHeaderSize * (level + 1), "GRUP", static_cast<std::uint32_t>(kHeaderSize * (kTooDeep - level)));
        }
        return ExpectProblem(bytes, bytes, { "English: ", "nested deeper than" });
    }

    std::string Utf8Strictness()
    {
        struct Case
        {
            Bytes bytes;
            bool valid;
            const char* what;
        };
        const std::vector<Case> cases{
            { { 0xEB, 0x8F, 0x85 }, true, "a Hangul syllable" },
            { { 'a', 0xF0, 0x9F, 0x98, 0x80 }, true, "a 4-byte code point" },
            { { 0xC0, 0x80 }, false, "an overlong NUL" },
            { { 0xE0, 0x80, 0xAF }, false, "an overlong 3-byte form" },
            { { 0xED, 0xA0, 0x80 }, false, "a surrogate" },
            { { 0xF4, 0x90, 0x80, 0x80 }, false, "above U+10FFFF" },
            { { 0x80 }, false, "a stray continuation byte" },
            { { 0xEB, 0x8F }, false, "a cut-short sequence" },
        };
        for (const auto& c : cases) {
            if (ToxicologyEsl::IsValidUtf8(c.bytes) != c.valid) return std::string(c.what) + (c.valid ? " refused" : " accepted");
        }
        return {};
    }
}

std::vector<std::pair<const char*, std::string (*)()>> EslChecks()
{
    return {
        { "EslCommittedPairMatches", EslCommittedPairMatches },
        { "EslNonTextByteFails", EslNonTextByteFails },
        { "EslFullNotUtf8Fails", EslFullNotUtf8Fails },
        { "EslMessageFullNotUtf8Fails", EslMessageFullNotUtf8Fails },
        { "EslPowerSpitByteFails", EslPowerSpitByteFails },
        { "EslTextInnerNulFails", EslTextInnerNulFails },
        { "EslRecordSizeMismatchFails", EslRecordSizeMismatchFails },
        { "EslRecordHeaderFails", EslRecordHeaderFails },
        { "EslGroupHeaderFails", EslGroupHeaderFails },
        { "EslTrailingByteFails", EslTrailingByteFails },
        { "EslKoreanIsEnglishCopyFails", EslKoreanIsEnglishCopyFails },
        { "EslDeepNestingFails", EslDeepNestingFails },
        { "Utf8Strictness", Utf8Strictness },
    };
}
