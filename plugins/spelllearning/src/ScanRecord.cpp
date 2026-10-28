#include "Common.h"
#include "FileUtils.h"
#include "JsonText.h"
#include "PathText.h"
#include "ScanRecord.h"
#include "ScanRecordRules.h"
#include "librarian/Librarian.h"
#include "tomereach/TomeReach.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{
    constexpr const char* kCatalogFile = "Data/SKSE/Plugins/SpellLearning/spell_catalog.json";
    constexpr const char* kRecordFile = "Data/SKSE/Plugins/SpellLearning/spell_catalog_loadorder.json";
    constexpr const char* kRulesDir = "Data/SKSE/Plugins/SpellLearning/librarian";   // LibrarianInternal.h RULE_SUBDIR
    constexpr int kReadTries = 3;                                                    // a file held a moment (a virus scan)
    constexpr auto kReadRetryWait = std::chrono::milliseconds(25);
    constexpr int kRecordVersion = ScanRecordRules::kRecordVersion;
    constexpr int kRecordIndent = 2;
    constexpr std::size_t kReadChunk = 1 << 16;

    constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;

    struct FileHash
    {
        std::uintmax_t bytes = 0;
        std::string hash;   // 16 hex digits
    };

    void HashText(std::uint64_t& hash, std::string_view bytes)
    {
        for (const unsigned char c : bytes) {
            hash ^= c;
            hash *= kFnvPrime;
        }
    }

    // FNV-1a 64 of a file, read in chunks (the catalog is several MB)
    std::optional<FileHash> HashFileOnce(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return std::nullopt;
        std::uint64_t hash = kFnvOffsetBasis;
        FileHash result;
        std::string buffer(kReadChunk, '\0');
        while (file) {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto got = file.gcount();
            for (std::streamsize i = 0; i < got; ++i) {
                hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);
                hash *= kFnvPrime;
            }
            result.bytes += static_cast<std::uintmax_t>(got);
        }
        if (file.bad()) return std::nullopt;
        result.hash = std::format("{:016x}", hash);
        return result;
    }

    std::optional<FileHash> HashFile(const std::filesystem::path& path)
    {
        for (int attempt = 1; attempt <= kReadTries; ++attempt) {
            if (auto result = HashFileOnce(path)) return result;
            if (attempt < kReadTries) std::this_thread::sleep_for(kReadRetryWait);
        }
        return std::nullopt;
    }

    // What else the catalog is made from: the catalog version and the librarian's
    // rule files (names and bytes, in name order). A Heart of Magic update with
    // new rules, or a rule pack added, makes a scan of the same load order due.
    // Empty when a rule file cannot be read.
    std::string RulesFingerprint()
    {
        std::uint64_t hash = kFnvOffsetBasis;
        HashText(hash, std::to_string(Librarian::CATALOG_VERSION));
        HashText(hash, "\n");
        std::error_code ec;
        if (!std::filesystem::is_directory(kRulesDir, ec)) return std::format("{:016x}", hash);
        std::vector<std::filesystem::path> files;
        for (std::filesystem::recursive_directory_iterator it(kRulesDir, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec)) files.push_back(it->path());
        }
        if (ec) {
            logger::warn("ScanRecord: could not list {}: {}", kRulesDir, ec.message());
            return {};
        }
        std::sort(files.begin(), files.end());
        for (const auto& path : files) {
            const auto file = HashFile(path);
            if (!file) {
                logger::warn("ScanRecord: could not read {}", PathText::Utf8(path));
                return {};
            }
            HashText(hash, PathText::Utf8(path.lexically_relative(kRulesDir)));
            HashText(hash, "\n");
            HashText(hash, file->hash);
            HashText(hash, "\n");
        }
        return std::format("{:016x}", hash);
    }

    std::string TextField(const nlohmann::json& data, const char* key)
    {
        const auto found = data.find(key);
        return found != data.end() && found->is_string() ? found->get<std::string>() : std::string();
    }

    // The fingerprint the record holds and is matched by: the load order's scan
    // fingerprint with how the tome reach pass ended (empty while it runs)
    std::string RecordFingerprint(const LoadOrderFingerprint::Value& loadOrder)
    {
        return ScanRecordRules::WithTomeReach(loadOrder.scan, TomeReach::OutcomeKey());
    }

    void RemoveRecord(const char* why)
    {
        std::error_code ec;
        if (std::filesystem::remove(kRecordFile, ec)) {
            logger::info("ScanRecord: removed {} ({})", kRecordFile, why);
        } else if (ec) {
            logger::warn("ScanRecord: could not remove {} ({}): {}", kRecordFile, why, ec.message());
        }
    }
}

namespace ScanRecord
{
    void CatalogWritten(bool fullScan, const LoadOrderFingerprint::Value* loadOrder, std::string_view incomplete) noexcept
    {
        try {
            const std::string fingerprint = loadOrder ? RecordFingerprint(*loadOrder) : std::string();
            const auto decision = ScanRecordRules::DecideRecord(fullScan,
                loadOrder ? std::string_view(loadOrder->hash) : std::string_view(), fingerprint, incomplete);
            if (decision.action != ScanRecordRules::RecordAction::kWrite) {
                RemoveRecord(decision.why.c_str());
                return;
            }
            const auto catalog = HashFile(kCatalogFile);
            if (!catalog) {
                RemoveRecord("the catalog just written could not be read back");
                return;
            }
            nlohmann::json data;
            data["version"] = kRecordVersion;
            data["loadOrder"] = loadOrder->hash;
            data["scan"] = fingerprint;
            data["tomeReach"] = TomeReach::OutcomeKey();
            data["plugins"] = loadOrder->plugins;
            data["spells"] = loadOrder->spells;
            data["catalogBytes"] = catalog->bytes;
            data["catalogHash"] = catalog->hash;
            data["rules"] = RulesFingerprint();
            if (!FileUtils::WriteAtomically(kRecordFile, JsonText::Dump(data, kRecordIndent), false)) {
                logger::warn("ScanRecord: could not write {} - the next game start scans again", kRecordFile);
                RemoveRecord("its write failed");
                return;
            }
            logger::info("ScanRecord: catalog made under load order {} (scan fingerprint {}, {} plugins, {} spell forms)",
                loadOrder->hash, fingerprint, loadOrder->plugins, loadOrder->spells);
        } catch (const std::exception& e) {
            logger::error("ScanRecord: the record was not kept: {}", e.what());
        } catch (...) {
            logger::error("ScanRecord: the record was not kept (unknown error)");
        }
    }

    void Forget(const char* why) noexcept
    {
        try {
            RemoveRecord(why);
        } catch (...) {
            logger::error("ScanRecord: the record could not be removed");
        }
    }

    bool CatalogMatches(const LoadOrderFingerprint::Value& now) noexcept
    {
        try {
            const std::string fingerprint = RecordFingerprint(now);
            if (fingerprint.empty()) return false;
            std::ifstream file(kRecordFile, std::ios::binary);
            if (!file.is_open()) return false;
            const auto data = nlohmann::json::parse(file, nullptr, false);
            if (data.is_discarded() || !data.is_object()) {
                logger::warn("ScanRecord: {} could not be read - the catalog's load order is unknown", kRecordFile);
                return false;
            }
            if (!ScanRecordRules::RecordFits(data, fingerprint)) {
                const auto version = data.find("version");
                if (version == data.end() || !version->is_number_integer() || version->get<long long>() != kRecordVersion) {
                    logger::info("ScanRecord: {} is from another version - the catalog's load order is unknown", kRecordFile);
                }
                return false;
            }
            const auto catalog = HashFile(kCatalogFile);
            if (!catalog) return false;
            const auto bytes = data.find("catalogBytes");
            if (TextField(data, "catalogHash") != catalog->hash || bytes == data.end() || !bytes->is_number_unsigned() ||
                bytes->get<std::uintmax_t>() != catalog->bytes) {
                return false;
            }
            const std::string rules = TextField(data, "rules");
            if (rules.empty() || rules != RulesFingerprint()) {
                logger::info("ScanRecord: the librarian's rules changed since the catalog was made");
                return false;
            }
            return true;
        } catch (const std::exception& e) {
            logger::warn("ScanRecord: could not compare the record: {}", e.what());
        } catch (...) {
            logger::warn("ScanRecord: could not compare the record (unknown error)");
        }
        return false;
    }
}
