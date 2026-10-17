#pragma once

// =============================================================================
// COPY CONTRACT PROGRESS - the builder's progress.jsonl (included by CopyContract.h)
// =============================================================================
// One JSON object per line, UTF-8, ended by '\n'; the builder appends one line per finished object, flushed after its
// file was renamed into place. Fields, in the order the writer puts them:
//   {"v":1,"kind":"nif|dds|mps|txst","key":"<16hex>","path":"<staging-relative>","bytes":N,"fnv64":"<16hex>",
//    "outcome":"ok|fallback|failed|too_large|not_dumped","reason":"...","deps":[{"path":"...","fnv64":"..."},{"addn":40012}]}
// The parser takes a line only when:
//   - v is 1 (any other v, or none, is refused); kind, key (16 lower-case hex) and outcome are known values;
//   - path, when not empty, is a copy path (DataPathRule::ParseCopyPath) under meshes\ for nif and mps, textures\ for dds,
//     whose key16 folder is the line's key; a txst line has no path (its files are its deps);
//   - an ok or fallback line of nif, dds or mps has its path, bytes and fnv64;
//   - every dep is {path, fnv64} with a Data-relative path Normalize leaves unchanged (meshes\ or textures\), or {addn}
//     with an ADDN slot index (40000-40207).
// Unknown extra fields are ignored. The JSON path is UTF-8; the root, copies folder and key16 are ASCII. A last line
// without '\n' is a write cut short and is ignored (cutLastLine); a bad line is reported with its number and the lines
// around it still count (the file is append-only: one bad line must not drop the finished objects beside it). CRLF and
// blank lines are tolerated.

#include "CopyContract.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BlackFlame::CopyContract
{
    inline constexpr std::int64_t kProgressVersion = 1;

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
    // becomes terminal "parse" in the ledger and leaves its group; one failure stays retryable
    inline constexpr std::string_view kLedgerParseState = "parse";
    inline constexpr int kParseFailuresTerminal = 2;

    [[nodiscard]] inline constexpr std::string_view ProgressKindText(ProgressKind a_kind) noexcept
    {
        switch (a_kind) {
            case ProgressKind::kDds: return "dds";
            case ProgressKind::kMps: return "mps";
            case ProgressKind::kTxst: return "txst";
            case ProgressKind::kNif: break;
        }
        return "nif";
    }

    [[nodiscard]] inline constexpr std::optional<ProgressKind> ProgressKindFromText(std::string_view a_text) noexcept
    {
        for (const auto kind : { ProgressKind::kNif, ProgressKind::kDds, ProgressKind::kMps, ProgressKind::kTxst }) {
            if (ProgressKindText(kind) == a_text) {
                return kind;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] inline constexpr std::string_view OutcomeText(Outcome a_outcome) noexcept
    {
        switch (a_outcome) {
            case Outcome::kFallback: return "fallback";
            case Outcome::kFailed: return "failed";
            case Outcome::kTooLarge: return "too_large";
            case Outcome::kNotDumped: return "not_dumped";
            case Outcome::kOk: break;
        }
        return "ok";
    }

    [[nodiscard]] inline constexpr std::optional<Outcome> OutcomeFromText(std::string_view a_text) noexcept
    {
        for (const auto outcome : { Outcome::kOk, Outcome::kFallback, Outcome::kFailed, Outcome::kTooLarge, Outcome::kNotDumped }) {
            if (OutcomeText(outcome) == a_text) {
                return outcome;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] inline constexpr bool IsPublishable(Outcome a_outcome) noexcept
    {
        return a_outcome == Outcome::kOk || a_outcome == Outcome::kFallback;
    }

    [[nodiscard]] inline constexpr bool IsTerminal(Outcome a_outcome) noexcept
    {
        return a_outcome == Outcome::kTooLarge || a_outcome == Outcome::kNotDumped;
    }

    struct ProgressDep
    {
        bool isAddn = false;
        std::uint32_t addn = 0;    // isAddn: the ADDN slot index the NIF names
        std::string path;          // else: a Data-relative file the object needs
        std::uint64_t fnv64 = 0;
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

        [[nodiscard]] inline std::string DepError(const nlohmann::json& a_dep)
        {
            if (!a_dep.is_object()) {
                return "a dep is not an object";
            }
            if (const auto addn = a_dep.find("addn"); addn != a_dep.end()) {
                const auto value = addn->is_number_unsigned() ? addn->get<std::uint64_t>() : 0;
                if (value >= kAddnIndexEnd || !SlotOfAddnIndex(static_cast<std::uint32_t>(value))) {
                    return "a dep's addn is not an ADDN slot index (40000-40207)";
                }
                return "";
            }
            const auto path = a_dep.find("path");
            if (path == a_dep.end() || !path->is_string()) {
                return "a dep has neither addn nor a path";
            }
            const auto& text = path->get_ref<const std::string&>();
            const auto root = DataPathRule::StartsWith(text, DataPathRule::kMeshesRoot) ? DataPathRule::kMeshesRoot :
                                                                                          DataPathRule::kTexturesRoot;
            const auto normalized = DataPathRule::Normalize(text, root);
            if (!normalized || *normalized != text) {
                return "a dep's path is not a normalised Data-relative path under meshes\\ or textures\\";
            }
            if (!HexField(a_dep, "fnv64")) {
                return "a dep's fnv64 is not 16 lower-case hex digits";
            }
            return "";
        }
    }

    /// One line (without its '\n'); a trailing '\r' is dropped
    [[nodiscard]] inline ProgressLineResult ParseProgressLine(std::string_view a_text)
    {
        if (!a_text.empty() && a_text.back() == '\r') {
            a_text.remove_suffix(1);
        }
        const auto root = nlohmann::json::parse(a_text.begin(), a_text.end(), nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            return { std::nullopt, "not a JSON object" };
        }
        const auto v = root.find("v");
        if (v == root.end() || !v->is_number_integer() || v->get<std::int64_t>() != kProgressVersion) {
            return { std::nullopt, "unknown v (only 1 is read)" };
        }
        const auto text = [&](const char* a_name) -> std::optional<std::string> {
            const auto it = root.find(a_name);
            return it != root.end() && it->is_string() ? std::optional<std::string>(it->get<std::string>()) : std::nullopt;
        };
        ProgressLine line;
        const auto kind = text("kind");
        const auto outcome = text("outcome");
        const auto key = Detail::HexField(root, "key");
        if (!kind || !ProgressKindFromText(*kind)) {
            return { std::nullopt, "unknown kind" };
        }
        if (!outcome || !OutcomeFromText(*outcome)) {
            return { std::nullopt, "unknown outcome" };
        }
        if (!key) {
            return { std::nullopt, "key is not 16 lower-case hex digits" };
        }
        line.kind = *ProgressKindFromText(*kind);
        line.outcome = *OutcomeFromText(*outcome);
        line.key = *key;
        line.path = text("path").value_or("");
        line.reason = text("reason").value_or("");
        if (const auto bytes = root.find("bytes"); bytes != root.end()) {
            if (!bytes->is_number_unsigned()) {
                return { std::nullopt, "bytes is not an unsigned integer" };
            }
            line.bytes = bytes->get<std::uint64_t>();
        }
        const bool hasFnv = root.find("fnv64") != root.end();
        if (hasFnv) {
            const auto fnv = Detail::HexField(root, "fnv64");
            if (!fnv) {
                return { std::nullopt, "fnv64 is not 16 lower-case hex digits" };
            }
            line.fnv64 = *fnv;
        }
        if (line.kind == ProgressKind::kTxst) {
            if (!line.path.empty()) {
                return { std::nullopt, "a txst line has no path" };
            }
        } else if (!line.path.empty()) {
            const auto parts = DataPathRule::ParseCopyPath(line.path);
            const auto expectedRoot = line.kind == ProgressKind::kDds ? DataPathRule::kTexturesRoot : DataPathRule::kMeshesRoot;
            if (!parts || parts->root != expectedRoot) {
                return { std::nullopt, "path is not a copy path under the kind's root" };
            }
            if (parts->key16 != Hex16(line.key)) {
                return { std::nullopt, "path's key folder is not the line's key" };
            }
        }
        if (IsPublishable(line.outcome) && line.kind != ProgressKind::kTxst &&
            (line.path.empty() || !hasFnv || root.find("bytes") == root.end())) {
            return { std::nullopt, "an ok or fallback file line needs path, bytes and fnv64" };
        }
        if (const auto deps = root.find("deps"); deps != root.end()) {
            if (!deps->is_array()) {
                return { std::nullopt, "deps is not an array" };
            }
            for (const auto& dep : *deps) {
                if (auto why = Detail::DepError(dep); !why.empty()) {
                    return { std::nullopt, std::move(why) };
                }
                ProgressDep out;
                if (const auto addn = dep.find("addn"); addn != dep.end()) {
                    out.isAddn = true;
                    out.addn = static_cast<std::uint32_t>(addn->get<std::uint64_t>());
                } else {
                    out.path = dep.at("path").get<std::string>();
                    out.fnv64 = *Detail::HexField(dep, "fnv64");
                }
                line.deps.push_back(std::move(out));
            }
        }
        return { std::move(line), "" };
    }

    /// The line as the builder appends it, '\n' included; nullopt when it would not parse back (ParseProgressLine) or a
    /// string is not UTF-8
    [[nodiscard]] inline std::optional<std::string> WriteProgressLine(const ProgressLine& a_line)
    {
        nlohmann::ordered_json root;
        root["v"] = kProgressVersion;
        root["kind"] = std::string(ProgressKindText(a_line.kind));
        root["key"] = Hex16(a_line.key);
        root["path"] = a_line.path;
        root["bytes"] = a_line.bytes;
        root["fnv64"] = Hex16(a_line.fnv64);
        root["outcome"] = std::string(OutcomeText(a_line.outcome));
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
        if (!ParseProgressLine(text).line) {
            return std::nullopt;
        }
        text.push_back('\n');
        return text;
    }

    /// The whole progress.jsonl text
    [[nodiscard]] inline ProgressFile ParseProgress(std::string_view a_text)
    {
        ProgressFile file;
        std::size_t lineNumber = 0;
        while (!a_text.empty()) {
            const auto end = a_text.find('\n');
            ++lineNumber;
            if (end == std::string_view::npos) {
                file.cutLastLine = true;  // a write cut short: ignored
                break;
            }
            std::string_view text = a_text.substr(0, end);
            a_text.remove_prefix(end + 1);
            if (text.find_first_not_of(" \t\r") == std::string_view::npos) {
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
