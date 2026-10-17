#pragma once

// =============================================================================
// COPY CONTRACT PROGRESS - the builder's progress.jsonl
// =============================================================================
// One JSON object per line, ASCII (paths are copy paths, which are ASCII), ended by '\n'; the builder appends one line
// per finished object, flushed after its file was renamed into place. Fields, in the order the writer puts them:
//   {"v":1,"kind":"nif|dds|mps|txst","key":"<16hex>","path":"<staging-relative>","bytes":N,"fnv64":"<16hex>",
//    "outcome":"ok|fallback|failed|too_large|not_dumped","reason":"...","deps":[{"path":"...","fnv64":"..."},{"addn":40012}]}
// path, bytes and fnv64 are written only for a line with a file (path not empty).
// The parser takes a line only when:
//   - it is at most kMaxProgressLine bytes, one JSON object with no key twice (at any depth);
//   - v is the unsigned number 1; kind, key (16 lower-case hex) and outcome are known values; path and reason, when
//     present, are strings;
//   - path, when not empty, is a copy path (DataPathRule::ParseCopyPath: ASCII, ...\copies\<key16>\copy.<ext>) under
//     meshes\ with ext nif for nif and mps, under textures\ with ext dds for dds, whose key16 folder is the line's key;
//     a txst line has no path (its files are its deps);
//   - an ok or fallback line of nif, dds or mps has its path, bytes > 0 and fnv64 != 0 (FNV-1a 64 of any real file);
//   - every dep is exactly one of {path, fnv64} (path a copy path, fnv64 != 0) or {addn} (an ADDN slot index,
//     40000-40207, on nif lines only); an ok or fallback txst line has at least one dep, all under textures\.
// Unknown extra fields are ignored. Shipped files (our two palettes) are never deps: they are always present.
// A last line without '\n' is a write cut short and is ignored (cutLastLine; whitespace after the last '\n' is not a
// cut line). Before appending, the builder drops such a tail (ProgressValidPrefix), so a cut line never fuses with the
// next one; the compaction at its start writes the kept lines to a temporary file and renames it over (v3.3 #3). A bad
// line is reported with its number and the lines around it still count (append-only: one bad line must not drop the
// finished objects beside it). CRLF and blank lines are tolerated.

#include "CopyContract.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace BlackFlame::CopyContract
{
    inline constexpr std::uint64_t kProgressVersion = 1;
    inline constexpr std::size_t kMaxProgressLine = 64 * 1024;  // a line longer than this is no line of ours
    inline constexpr std::string_view kNifExtension = "nif";
    inline constexpr std::string_view kDdsExtension = "dds";

    enum class ProgressKind : std::uint8_t
    {
        kNif,
        kDds,
        kMps,
        kTxst,
    };

    // What became of one object. ok and fallback are published; failed is tried again at the next run; too_large and
    // not_dumped are terminal for that key (the member leaves its group, links to it go to the source). A new key - new
    // inputs or a new cap - tries again.
    enum class Outcome : std::uint8_t
    {
        kOk,
        kFallback,   // deterministic fallback, logged "FALLBACK <reason>"; the same inputs always give the same bytes
        kFailed,     // I/O, bad_alloc, killed or crashed builder, a parse failure: retryable
        kTooLarge,   // the admission estimate is over the job cap even alone: terminal
        kNotDumped,  // a texture outside the dump caps: terminal
    };

    // DLL-side, not a progress outcome (v3.3 #1): a key whose copy fails to parse in kParseFailuresTerminal sessions
    // becomes terminal "parse" in the ledger and leaves its group; one failure stays retryable. For P2's ledger.
    inline constexpr std::string_view kLedgerParseState = "parse";
    inline constexpr int kParseFailuresTerminal = 2;

    /// "nif", "dds", "mps", "txst"; "" for a value no kind has (the writer refuses it)
    [[nodiscard]] constexpr std::string_view ProgressKindText(ProgressKind a_kind) noexcept
    {
        switch (a_kind) {
            case ProgressKind::kNif: return "nif";
            case ProgressKind::kDds: return "dds";
            case ProgressKind::kMps: return "mps";
            case ProgressKind::kTxst: return "txst";
        }
        return "";
    }

    [[nodiscard]] constexpr std::optional<ProgressKind> ProgressKindFromText(std::string_view a_text) noexcept
    {
        for (const auto kind : { ProgressKind::kNif, ProgressKind::kDds, ProgressKind::kMps, ProgressKind::kTxst }) {
            if (!a_text.empty() && ProgressKindText(kind) == a_text) {
                return kind;
            }
        }
        return std::nullopt;
    }

    /// The outcome's text; "" for a value no outcome has (the writer refuses it)
    [[nodiscard]] constexpr std::string_view OutcomeText(Outcome a_outcome) noexcept
    {
        switch (a_outcome) {
            case Outcome::kOk: return "ok";
            case Outcome::kFallback: return "fallback";
            case Outcome::kFailed: return "failed";
            case Outcome::kTooLarge: return "too_large";
            case Outcome::kNotDumped: return "not_dumped";
        }
        return "";
    }

    [[nodiscard]] constexpr std::optional<Outcome> OutcomeFromText(std::string_view a_text) noexcept
    {
        for (const auto outcome : { Outcome::kOk, Outcome::kFallback, Outcome::kFailed, Outcome::kTooLarge, Outcome::kNotDumped }) {
            if (!a_text.empty() && OutcomeText(outcome) == a_text) {
                return outcome;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr bool IsPublishable(Outcome a_outcome) noexcept
    {
        return a_outcome == Outcome::kOk || a_outcome == Outcome::kFallback;
    }

    [[nodiscard]] constexpr bool IsTerminal(Outcome a_outcome) noexcept
    {
        return a_outcome == Outcome::kTooLarge || a_outcome == Outcome::kNotDumped;
    }

    struct ProgressDep
    {
        bool isAddn = false;
        std::uint32_t addn = 0;    // isAddn: the ADDN slot index the NIF names
        std::string path;          // else: a copy path the object needs
        std::uint64_t fnv64 = 0;

        [[nodiscard]] bool operator==(const ProgressDep& a_other) const
        {
            return isAddn == a_other.isAddn && addn == a_other.addn && path == a_other.path && fnv64 == a_other.fnv64;
        }
    };

    struct ProgressLine
    {
        ProgressKind kind = ProgressKind::kNif;
        std::uint64_t key = 0;
        std::string path;  // staging-relative (= Data-relative) copy path; "" for txst or an object with no file
        std::uint64_t bytes = 0;
        std::uint64_t fnv64 = 0;
        Outcome outcome = Outcome::kOk;
        std::string reason;
        std::vector<ProgressDep> deps;

        [[nodiscard]] bool operator==(const ProgressLine& a_other) const
        {
            return kind == a_other.kind && key == a_other.key && path == a_other.path && bytes == a_other.bytes &&
                   fnv64 == a_other.fnv64 && outcome == a_other.outcome && reason == a_other.reason && deps == a_other.deps;
        }
    };

    struct ProgressLineResult
    {
        std::optional<ProgressLine> line;
        std::string error;  // why it was refused ("" when line is set)
    };

    struct ProgressError
    {
        std::size_t lineNumber = 0;  // 1-based
        std::string error;
    };

    struct ProgressFile
    {
        std::vector<ProgressLine> lines;
        std::vector<ProgressError> errors;
        bool cutLastLine = false;  // the text ended inside a line (ignored)
    };

    namespace Detail
    {
        [[nodiscard]] inline std::optional<std::uint64_t> HexField(const nlohmann::json& a_object, const char* a_name)
        {
            const auto it = a_object.find(a_name);
            if (it == a_object.end() || !it->is_string()) {
                return std::nullopt;
            }
            return ParseHex16(it->get_ref<const std::string&>());
        }

        /// The parsed object, or a discarded value for text that is no JSON or names a key twice in one object
        [[nodiscard]] inline nlohmann::json ParseUniqueKeys(std::string_view a_text)
        {
            std::vector<std::set<std::string>> keys;  // one set per open object
            bool duplicate = false;
            const nlohmann::json::parser_callback_t callback = [&](int, nlohmann::json::parse_event_t a_event, nlohmann::json& a_parsed) {
                if (a_event == nlohmann::json::parse_event_t::object_start) {
                    keys.emplace_back();
                } else if (a_event == nlohmann::json::parse_event_t::object_end && !keys.empty()) {
                    keys.pop_back();
                } else if (a_event == nlohmann::json::parse_event_t::key && !keys.empty() && a_parsed.is_string() &&
                           !keys.back().insert(a_parsed.get<std::string>()).second) {
                    duplicate = true;
                }
                return true;
            };
            auto root = nlohmann::json::parse(a_text.begin(), a_text.end(), callback, false);
            return duplicate ? nlohmann::json(nlohmann::json::value_t::discarded) : root;
        }

        [[nodiscard]] inline std::string DepError(const nlohmann::json& a_dep, ProgressKind a_kind)
        {
            if (!a_dep.is_object()) {
                return "a dep is not an object";
            }
            const bool hasAddn = a_dep.contains("addn");
            const bool hasPath = a_dep.contains("path") || a_dep.contains("fnv64");
            if (hasAddn == hasPath) {
                return "a dep must be exactly one of {addn} and {path, fnv64}";
            }
            if (hasAddn) {
                const auto& addn = a_dep.at("addn");
                const auto value = addn.is_number_unsigned() ? addn.get<std::uint64_t>() : 0;
                if (value >= kAddnIndexEnd || !SlotOfAddnIndex(static_cast<std::uint32_t>(value))) {
                    return "a dep's addn is not an ADDN slot index (40000-40207)";
                }
                return a_kind == ProgressKind::kNif ? "" : "an addn dep on a line that is not nif";
            }
            const auto path = a_dep.find("path");
            if (path == a_dep.end() || !path->is_string() || !DataPathRule::ParseCopyPath(path->get_ref<const std::string&>())) {
                return "a dep's path is not a copy path";
            }
            const auto fnv = HexField(a_dep, "fnv64");
            if (!fnv || *fnv == 0) {
                return "a dep's fnv64 is not 16 lower-case hex digits of a real file";
            }
            return "";
        }

        [[nodiscard]] inline std::string PathError(const ProgressLine& a_line)
        {
            if (a_line.kind == ProgressKind::kTxst) {
                return a_line.path.empty() ? "" : "a txst line has no path";
            }
            if (a_line.path.empty()) {
                return "";
            }
            const auto parts = DataPathRule::ParseCopyPath(a_line.path);
            const bool dds = a_line.kind == ProgressKind::kDds;
            if (!parts || parts->root != (dds ? DataPathRule::kTexturesRoot : DataPathRule::kMeshesRoot) ||
                parts->extension != (dds ? kDdsExtension : kNifExtension)) {
                return "path is not a copy path of the kind's root and extension";
            }
            return parts->key16 == Hex16(a_line.key) ? "" : "path's key folder is not the line's key";
        }
    }

    /// One line (without its '\n')
    [[nodiscard]] inline ProgressLineResult ParseProgressLine(std::string_view a_text)
    {
        if (a_text.size() > kMaxProgressLine) {
            return { std::nullopt, "longer than a progress line can be" };
        }
        const auto root = Detail::ParseUniqueKeys(a_text);
        if (root.is_discarded() || !root.is_object()) {
            return { std::nullopt, "not a JSON object (or a key given twice)" };
        }
        const auto v = root.find("v");
        if (v == root.end() || !v->is_number_unsigned() || v->get<std::uint64_t>() != kProgressVersion) {
            return { std::nullopt, "unknown v (only 1 is read)" };
        }
        ProgressLine line;
        const auto text = [&](const char* a_name, std::string& a_out) {  // false: present but no string
            const auto it = root.find(a_name);
            if (it == root.end()) {
                return true;
            }
            if (!it->is_string()) {
                return false;
            }
            a_out = it->get<std::string>();
            return true;
        };
        std::string kind;
        std::string outcome;
        if (!text("kind", kind) || !text("outcome", outcome) || !text("path", line.path) || !text("reason", line.reason)) {
            return { std::nullopt, "kind, outcome, path or reason is not a string" };
        }
        const auto key = Detail::HexField(root, "key");
        if (!ProgressKindFromText(kind)) {
            return { std::nullopt, "unknown kind" };
        }
        if (!OutcomeFromText(outcome)) {
            return { std::nullopt, "unknown outcome" };
        }
        if (!key) {
            return { std::nullopt, "key is not 16 lower-case hex digits" };
        }
        line.kind = *ProgressKindFromText(kind);
        line.outcome = *OutcomeFromText(outcome);
        line.key = *key;
        const bool hasBytes = root.contains("bytes");
        const bool hasFnv = root.contains("fnv64");
        if (hasBytes) {
            if (!root.at("bytes").is_number_unsigned()) {
                return { std::nullopt, "bytes is not an unsigned integer" };
            }
            line.bytes = root.at("bytes").get<std::uint64_t>();
        }
        if (hasFnv) {
            const auto fnv = Detail::HexField(root, "fnv64");
            if (!fnv) {
                return { std::nullopt, "fnv64 is not 16 lower-case hex digits" };
            }
            line.fnv64 = *fnv;
        }
        if (auto why = Detail::PathError(line); !why.empty()) {
            return { std::nullopt, std::move(why) };
        }
        const bool publishable = IsPublishable(line.outcome);
        if (publishable && line.kind != ProgressKind::kTxst && (line.path.empty() || line.bytes == 0 || line.fnv64 == 0)) {
            return { std::nullopt, "an ok or fallback file line needs path, bytes > 0 and a real fnv64" };
        }
        if (const auto deps = root.find("deps"); deps != root.end()) {
            if (!deps->is_array()) {
                return { std::nullopt, "deps is not an array" };
            }
            for (const auto& dep : *deps) {
                if (auto why = Detail::DepError(dep, line.kind); !why.empty()) {
                    return { std::nullopt, std::move(why) };
                }
                ProgressDep out;
                if (dep.contains("addn")) {
                    out.isAddn = true;
                    out.addn = static_cast<std::uint32_t>(dep.at("addn").get<std::uint64_t>());
                } else {
                    out.path = dep.at("path").get<std::string>();
                    out.fnv64 = *Detail::HexField(dep, "fnv64");
                }
                line.deps.push_back(std::move(out));
            }
        }
        if (line.kind == ProgressKind::kTxst && publishable) {
            const bool texturesOnly = std::all_of(line.deps.begin(), line.deps.end(), [](const ProgressDep& a_dep) {
                return !a_dep.isAddn && DataPathRule::StartsWith(a_dep.path, DataPathRule::kTexturesRoot);
            });
            if (line.deps.empty() || !texturesOnly) {
                return { std::nullopt, "an ok or fallback txst line needs at least one dep, all textures" };
            }
        }
        return { std::move(line), "" };
    }

    /// The line as the builder appends it, '\n' included; nullopt for an unknown enum value, a string that is not UTF-8,
    /// or a line that would not parse back field for field (ParseProgressLine)
    [[nodiscard]] inline std::optional<std::string> WriteProgressLine(const ProgressLine& a_line)
    {
        const auto kind = ProgressKindText(a_line.kind);
        const auto outcome = OutcomeText(a_line.outcome);
        if (kind.empty() || outcome.empty()) {
            return std::nullopt;
        }
        nlohmann::ordered_json root;
        root["v"] = kProgressVersion;
        root["kind"] = std::string(kind);
        root["key"] = Hex16(a_line.key);
        if (!a_line.path.empty()) {
            root["path"] = a_line.path;
            root["bytes"] = a_line.bytes;
            root["fnv64"] = Hex16(a_line.fnv64);
        }
        root["outcome"] = std::string(outcome);
        root["reason"] = a_line.reason;
        root["deps"] = nlohmann::ordered_json::array();
        for (const auto& dep : a_line.deps) {
            if (dep.isAddn) {
                root["deps"].push_back({ { "addn", dep.addn } });
            } else {
                root["deps"].push_back({ { "path", dep.path }, { "fnv64", Hex16(dep.fnv64) } });
            }
        }
        std::string text;
        try {
            text = root.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::strict);
        } catch (const nlohmann::ordered_json::exception&) {
            return std::nullopt;  // a string that is not UTF-8
        }
        const auto back = ParseProgressLine(text);
        auto expected = a_line;
        if (expected.path.empty()) {
            expected.bytes = 0;
            expected.fnv64 = 0;
        }
        if (!back.line || !(*back.line == expected)) {
            return std::nullopt;
        }
        text.push_back('\n');
        return text;
    }

    /// How much of a_text the builder keeps before appending: up to and including its last '\n' (a cut last line is
    /// dropped, so the next line never fuses with it)
    [[nodiscard]] inline std::size_t ProgressValidPrefix(std::string_view a_text) noexcept
    {
        const auto end = a_text.rfind('\n');
        return end == std::string_view::npos ? 0 : end + 1;
    }

    /// The whole progress.jsonl text
    [[nodiscard]] inline ProgressFile ParseProgress(std::string_view a_text)
    {
        constexpr std::string_view kBlank = " \t\r";
        ProgressFile file;
        std::size_t lineNumber = 0;
        while (!a_text.empty()) {
            const auto end = a_text.find('\n');
            ++lineNumber;
            if (end == std::string_view::npos) {
                file.cutLastLine = a_text.find_first_not_of(kBlank) != std::string_view::npos;  // a write cut short: ignored
                break;
            }
            const std::string_view text = a_text.substr(0, end);
            a_text.remove_prefix(end + 1);
            if (text.find_first_not_of(kBlank) == std::string_view::npos) {
                continue;
            }
            auto result = ParseProgressLine(text);
            if (result.line) {
                file.lines.push_back(std::move(*result.line));
            } else {
                file.errors.push_back({ lineNumber, std::move(result.error) });
            }
        }
        return file;
    }
}
