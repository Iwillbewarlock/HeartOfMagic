#pragma once

// =============================================================================
// DISK TRIM RULES - which work files are still needed (pure: blackflame-cosave-check tests them), 2026-10-26
// =============================================================================
// Two work folders held a second copy of everything (docs/DISK-TRIM-DESIGN.md):
//   staging  the builder's outputs; after a publish each one is a byte-for-byte duplicate of its Data file. The DLL lists
//            the files Data holds exactly (staging\published.tsv, CopyContractPublished.h), the builder counts a listed
//            file as staged, and the DLL deletes the staging duplicate (BuildProgressTrim.h)
//   data     the dump of the source NIFs and textures. The builder reads a source only for an object it builds, so the dump
//            keeps only the sources of objects that are not done (Needed below); the request is unchanged (a texture left
//            out stays listed as dumped, so a missed mapping makes the builder record the object failed - never a silent
//            fallback - and the next start dumps everything)
// "Done" mirrors the builder's reuse check (GreyProgress.h Reusable): an ok or fallback line whose own file and every copy
// dep are listed in published.tsv with the line's bytes or are in staging with its size; a not_dumped line. too_large is
// never done (a larger job memory cap tries again); failed is never done, and any failed line makes the whole dump stay.

#include "CopyContractProgress.h"
#include "CopyContractPublished.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace BlackFlame::DiskTrimRules
{
    namespace CC = CopyContract;

    /// A staged file's size, nullopt when it is not there
    using StagedSize = std::function<std::optional<std::uint64_t>(const std::string& a_path)>;

    [[nodiscard]] inline bool FilePresent(const CC::PublishedList& a_published, const StagedSize& a_staged, const std::string& a_path,
        std::uint64_t a_fnv64, std::optional<std::uint64_t> a_bytes)
    {
        if (CC::PublishedHolds(a_published, a_path, a_fnv64, a_bytes)) {
            return true;
        }
        const auto size = a_staged(a_path);
        return size && (!a_bytes || *size == *a_bytes);
    }

    /// a_line's object will not be built again by the builder (see the header)
    [[nodiscard]] inline bool LineDone(const CC::ProgressLine& a_line, const CC::PublishedList& a_published, const StagedSize& a_staged)
    {
        if (a_line.outcome == CC::Outcome::kNotDumped) {
            return true;
        }
        if (!CC::IsPublishable(a_line.outcome)) {
            return false;  // failed, too_large
        }
        if (!a_line.path.empty() && !FilePresent(a_published, a_staged, a_line.path, a_line.fnv64, a_line.bytes)) {
            return false;
        }
        for (const auto& dep : a_line.deps) {
            if (!dep.isAddn && !FilePresent(a_published, a_staged, dep.path, dep.fnv64, std::nullopt)) {
                return false;
            }
        }
        return true;
    }

    /// One object the request asks the builder for, and the dump files it reads when it builds it
    struct NeedObject
    {
        std::uint64_t key = 0;
        bool efshBase = false;            // judged "not fire" by an earlier run (no line, the builder's not-fire file) is done
        std::vector<std::string> files;   // Data-relative dump paths (NIF, textures)
    };

    struct Needed
    {
        bool all = false;               // keep the whole dump (a failed line)
        std::set<std::string> files;    // otherwise: these and nothing else
        std::size_t pending = 0;        // objects not done
        std::size_t failed = 0;         // objects whose last line is failed
    };

    /// a_lastLine: a key's last progress line (as read at the start), or nullopt. a_always: files every run reads (our palettes)
    [[nodiscard]] inline Needed NeededFiles(const std::vector<NeedObject>& a_objects,
        const std::function<std::optional<CC::ProgressLine>(std::uint64_t)>& a_lastLine, const std::set<std::uint64_t>& a_notFire,
        const CC::PublishedList& a_published, const StagedSize& a_staged, const std::vector<std::string>& a_always)
    {
        Needed out;
        out.files.insert(a_always.begin(), a_always.end());
        for (const auto& object : a_objects) {
            const auto line = a_lastLine(object.key);
            bool done = false;
            if (line) {
                out.failed += line->outcome == CC::Outcome::kFailed ? 1 : 0;
                done = LineDone(*line, a_published, a_staged);
            } else {
                done = object.efshBase && a_notFire.count(object.key) != 0;
            }
            if (!done) {
                ++out.pending;
                out.files.insert(object.files.begin(), object.files.end());
            }
        }
        out.all = out.failed > 0;
        return out;
    }

    /// The keys of the builder's not-fire file (CopyContract kNotFireFile, "<key16> <tag>" a line). The tag (the builder's
    /// fire test version) is not checked here: a base the builder judges again under a new tag finds no source, is failed,
    /// and the next start keeps the whole dump
    [[nodiscard]] inline std::set<std::uint64_t> NotFireKeys(std::string_view a_text)
    {
        std::set<std::uint64_t> out;
        std::size_t at = 0;
        while (at < a_text.size()) {
            auto end = a_text.find('\n', at);
            if (end == std::string_view::npos) {
                break;  // a cut last line (the builder appends without a lock)
            }
            const auto line = a_text.substr(at, end - at);
            at = end + 1;
            const auto space = line.find(' ');
            if (space == std::string_view::npos) {
                continue;
            }
            if (const auto key = CC::ParseHex16(line.substr(0, space))) {
                out.insert(*key);
            }
        }
        return out;
    }

    /// A staged copy may be deleted: listed (Data holds it exactly) and not one of our copies a source names as its
    /// texture (the builder reads those only from staging, GreyTexStats.cpp)
    [[nodiscard]] inline bool MayTrim(const std::string& a_path, const CC::PublishedList& a_published,
        const std::set<std::string>& a_namedBySources)
    {
        return a_published.count(a_path) != 0 && a_namedBySources.count(a_path) == 0;
    }
}
