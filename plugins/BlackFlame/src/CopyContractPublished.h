#pragma once

// =============================================================================
// COPY CONTRACT PUBLISHED - staging\published.tsv: the copies Data holds byte for byte (disk trim, 2026-10-26)
// =============================================================================
// Written whole by the DLL at kDataLoaded (BuildProgress::ReadAtStart), before the builder is launched: one line per copy
// file a progress line (or one of its copy deps) names whose Data file has exactly the line's size and fnv64 this start.
// The DLL then deletes those files' staging duplicates (BuildProgressTrim.h), and the builder's reuse check (GreyProgress.h
// StagedMatches) takes a listed file as present, so a published object is not built again. Text, ASCII (copy paths are):
//   # blackflame published v1 <token: 16 lower-case hex, not 0>\n
//   <copy path>\t<bytes>\t<fnv64 16 lower-case hex>\n   (one per file, path order)
// The token is new each start and also written into request.json (kPublishedToken): the builder takes the list only when
// the two match, so a list an earlier start left behind (its removal failed: a lock, a read-only file) never vouches for
// this start's Data (review Codex). The parser takes the file only with that first line; a line is taken only when its path is a copy path
// (DataPathRule::ParseCopyPath), bytes > 0 and fnv64 a non-zero Hex16; a path listed twice with other values is dropped
// (neither is trusted); a last line without '\n' is a cut write and is dropped. A file that is absent lists nothing.

#include "CopyContract.h"
#include "DataPathRule.h"

#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace BlackFlame::CopyContract
{
    inline constexpr const char* kPublishedFile = "published.tsv";  // in the staging folder
    inline constexpr std::string_view kPublishedHeader = "# blackflame published v1 ";  // then the token
    inline constexpr const char* kPublishedToken = "publishedList";  // request.json, top level: the list's token (Hex16)
    // The builder's memory of the effect shader bases that are not fire ("<key16> <tag>" a line, its own tag rule:
    // GreyKeyedBases.cpp). The DLL reads only the keys (DiskTrimRules::NotFireKeys): such a base needs no source in the dump
    inline constexpr const char* kNotFireFile = "efshbase-notfire.txt";

    struct PublishedFile
    {
        std::uint64_t bytes = 0;
        std::uint64_t fnv64 = 0;

        [[nodiscard]] bool operator==(const PublishedFile& a_other) const { return bytes == a_other.bytes && fnv64 == a_other.fnv64; }
        [[nodiscard]] bool operator!=(const PublishedFile& a_other) const { return !(*this == a_other); }  // C++17 (the tools)
    };

    using PublishedList = std::map<std::string, PublishedFile>;  // copy path -> its Data bytes

    /// The whole file's text; nullopt when an entry would not read back (not a copy path, bytes 0, fnv64 0) or a_token is 0
    [[nodiscard]] inline std::optional<std::string> WritePublished(const PublishedList& a_list, std::uint64_t a_token)
    {
        if (a_token == 0) {
            return std::nullopt;
        }
        std::string out(kPublishedHeader);
        out += Hex16(a_token);
        out += '\n';
        for (const auto& [path, file] : a_list) {
            if (!DataPathRule::ParseCopyPath(path) || file.bytes == 0 || file.fnv64 == 0) {
                return std::nullopt;
            }
            out += path;
            out += '\t';
            out += std::to_string(file.bytes);
            out += '\t';
            out += Hex16(file.fnv64);
            out += '\n';
        }
        return out;
    }

    struct PublishedParse
    {
        PublishedList list;
        std::size_t refused = 0;  // lines not taken (bad fields, a path listed twice with other values, a cut last line)
        bool headerOk = false;    // false: nothing is taken
        std::uint64_t token = 0;  // the header's token (headerOk): the builder compares it with the request's
    };

    [[nodiscard]] inline PublishedParse ParsePublished(std::string_view a_text)
    {
        PublishedParse out;
        const auto firstEnd = a_text.find('\n');
        auto header = a_text.substr(0, firstEnd);
        if (!header.empty() && header.back() == '\r') {
            header.remove_suffix(1);
        }
        const bool prefixed = header.size() > kPublishedHeader.size() && header.substr(0, kPublishedHeader.size()) == kPublishedHeader;
        const auto token = prefixed ? ParseHex16(header.substr(kPublishedHeader.size())) : std::nullopt;
        if (firstEnd == std::string_view::npos || !token || *token == 0) {
            return out;
        }
        out.headerOk = true;
        out.token = *token;
        std::set<std::string> conflicted;
        std::size_t at = firstEnd + 1;
        while (at < a_text.size()) {
            const auto end = a_text.find('\n', at);
            if (end == std::string_view::npos) {
                ++out.refused;  // a cut last line
                break;
            }
            auto line = a_text.substr(at, end - at);
            at = end + 1;
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            if (line.empty()) {
                continue;
            }
            const auto tab1 = line.find('\t');
            const auto tab2 = tab1 == std::string_view::npos ? std::string_view::npos : line.find('\t', tab1 + 1);
            if (tab2 == std::string_view::npos || line.find('\t', tab2 + 1) != std::string_view::npos) {
                ++out.refused;
                continue;
            }
            const auto path = line.substr(0, tab1);
            const auto sizeText = line.substr(tab1 + 1, tab2 - tab1 - 1);
            std::uint64_t bytes = 0;
            const auto parsed = std::from_chars(sizeText.data(), sizeText.data() + sizeText.size(), bytes);
            const auto fnv = ParseHex16(line.substr(tab2 + 1));
            if (!DataPathRule::ParseCopyPath(path) || sizeText.empty() || parsed.ec != std::errc{} ||
                parsed.ptr != sizeText.data() + sizeText.size() || bytes == 0 || !fnv || *fnv == 0) {
                ++out.refused;
                continue;
            }
            const std::string key(path);
            const PublishedFile file{ bytes, *fnv };
            if (conflicted.count(key) != 0) {
                ++out.refused;
                continue;
            }
            const auto [it, added] = out.list.try_emplace(key, file);
            if (!added && !(it->second == file)) {
                out.list.erase(it);
                conflicted.insert(key);
                out.refused += 2;
            }
        }
        return out;
    }

    /// a_list holds a_path with a_fnv64 (and a_bytes when given)
    [[nodiscard]] inline bool PublishedHolds(const PublishedList& a_list, std::string_view a_path, std::uint64_t a_fnv64,
        std::optional<std::uint64_t> a_bytes = std::nullopt)
    {
        const auto it = a_list.find(std::string(a_path));
        return it != a_list.end() && it->second.fnv64 == a_fnv64 && (!a_bytes || it->second.bytes == *a_bytes);
    }
}
