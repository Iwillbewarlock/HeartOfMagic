#pragma once

// The Toxicology addon's English and Korean ESL (plugins/Toxicology/data/HeartOfMagic_Toxicology.esp and
// data/ko/HeartOfMagic_Toxicology.esp, ESL.md section "Korean ESL") must be the same file except for five
// text subrecords. CompareToxicologyEsl checks that rule on the two files' bytes; toxicology-esl-check runs it
// before the Korean zip is made, toxicology-test runs it on the committed pair and on broken copies.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ToxicologyEsl
{
    // One subrecord of a parsed record: its type and where its data sits in the file
    struct Subrecord
    {
        std::string type;
        std::size_t dataOffset = 0;
        std::size_t size = 0;
    };

    // A parsed plugin in file order: GRUP begin, record, GRUP end. offset is where the node's 24-byte header
    // starts (for kGroupEnd, where the group's header started).
    struct Node
    {
        enum class Kind
        {
            kGroupBegin,
            kGroupEnd,
            kRecord
        };

        Kind kind = Kind::kRecord;
        std::string type;  // the record type, or "GRUP"
        std::uint32_t formId = 0;
        std::size_t offset = 0;
        std::vector<Subrecord> subrecords;
    };

    struct Plugin
    {
        std::vector<Node> nodes;
        std::uint32_t masterCount = 0;
    };

    // Parses an uncompressed TES4 plugin and checks that every record and group size adds up.
    // Returns "" on success, otherwise what is wrong and at which offset; a compressed record or an XXXX
    // subrecord is refused rather than parsed.
    std::string Parse(std::span<const std::uint8_t> a_bytes, Plugin& a_out);

    // The stored FormID of the file's own record a_localId (master index = the file's master count)
    std::uint32_t OwnFormId(const Plugin& a_plugin, std::uint32_t a_localId);

    // Every problem found, empty when the Korean file matches the English one under the rule above.
    // Each line names the record (FormID and type), the subrecord and what differs.
    std::vector<std::string> CompareToxicologyEsl(std::span<const std::uint8_t> a_english, std::span<const std::uint8_t> a_korean);

    // Strict UTF-8: no overlong forms, no surrogates, nothing above U+10FFFF, no stray continuation bytes
    bool IsValidUtf8(std::span<const std::uint8_t> a_bytes);
}
